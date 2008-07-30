// Copyright 2006-2008 Google Inc. All Rights Reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "code-stubs.h"
#include "debug.h"
#include "disasm.h"
#include "disassembler.h"
#include "macro-assembler.h"
#include "serialize.h"
#include "string-stream.h"

namespace v8 { namespace internal {

#ifdef ENABLE_DISASSEMBLER

void Disassembler::Dump(FILE* f, byte* begin, byte* end) {
  for (byte* pc = begin; pc < end; pc++) {
    if (f == NULL) {
      PrintF("%p  %4d  %02x\n", pc, pc - begin, *pc);
    } else {
      fprintf(f, "%p  %4d  %02x\n", pc, pc - begin, *pc);
    }
  }
}


class V8NameConverter: public disasm::NameConverter {
 public:
  explicit V8NameConverter(Code* code) : code_(code) {}
  virtual const char* NameOfAddress(byte* pc) const;
  virtual const char* NameInCode(byte* addr) const;
  Code* code() const { return code_; }
 private:
  Code* code_;
};


const char* V8NameConverter::NameOfAddress(byte* pc) const {
  static char buffer[128];

  const char* name = Builtins::Lookup(pc);
  if (name != NULL) {
    OS::SNPrintF(buffer, sizeof buffer, "%s  (%p)", name, pc);
    return buffer;
  }

  if (code_ != NULL) {
    int offs = pc - code_->instruction_start();
    // print as code offset, if it seems reasonable
    if (0 <= offs && offs < code_->instruction_size()) {
      OS::SNPrintF(buffer, sizeof buffer, "%d  (%p)", offs, pc);
      return buffer;
    }
  }

  return disasm::NameConverter::NameOfAddress(pc);
}


const char* V8NameConverter::NameInCode(byte* addr) const {
  // The V8NameConverter is used for well known code, so we can "safely"
  // dereference pointers in generated code.
  return (code_ != NULL) ? reinterpret_cast<const char*>(addr) : "";
}


static void DumpBuffer(FILE* f, char* buff) {
  if (f == NULL) {
    PrintF("%s", buff);
  } else {
    fprintf(f, "%s", buff);
  }
}

static const int kOutBufferSize = 256 + String::kMaxShortPrintLength;
static const int kRelocInfoPosition = 57;

static int DecodeIt(FILE* f,
                    const V8NameConverter& converter,
                    byte* begin,
                    byte* end) {
  NoHandleAllocation ha;
  AssertNoAllocation no_alloc;
  ExternalReferenceEncoder ref_encoder;

  char decode_buffer[128];
  char out_buffer[kOutBufferSize];
  byte* pc = begin;
  disasm::Disassembler d(converter);
  RelocIterator* it = NULL;
  if (converter.code() != NULL) {
    it = new RelocIterator(converter.code());
  } else {
    // No relocation information when printing code stubs.
  }
  int constants = -1;  // no constants being decoded at the start

  while (pc < end) {
    // First decode instruction so that we know its length.
    byte* prev_pc = pc;
    if (constants > 0) {
      OS::SNPrintF(decode_buffer,
                   sizeof(decode_buffer),
                   "%08x       constant",
                   *reinterpret_cast<int32_t*>(pc));
      constants--;
      pc += 4;
    } else {
      int num_const = d.ConstantPoolSizeAt(pc);
      if (num_const >= 0) {
        OS::SNPrintF(decode_buffer,
                     sizeof(decode_buffer),
                     "%08x       constant pool begin",
                     *reinterpret_cast<int32_t*>(pc));
        constants = num_const;
        pc += 4;
      } else {
        decode_buffer[0] = '\0';
        pc += d.InstructionDecode(decode_buffer, sizeof decode_buffer, pc);
      }
    }

    // Collect RelocInfo for this instruction (prev_pc .. pc-1)
    List<const char*> comments(4);
    List<byte*> pcs(1);
    List<RelocMode> rmodes(1);
    List<intptr_t> datas(1);
    if (it != NULL) {
      while (!it->done() && it->rinfo()->pc() < pc) {
        if (is_comment(it->rinfo()->rmode())) {
          // For comments just collect the text.
          comments.Add(reinterpret_cast<const char*>(it->rinfo()->data()));
        } else {
          // For other reloc info collect all data.
          pcs.Add(it->rinfo()->pc());
          rmodes.Add(it->rinfo()->rmode());
          datas.Add(it->rinfo()->data());
        }
        it->next();
      }
    }

    StringBuilder out(out_buffer, sizeof(out_buffer));

    // Comments.
    for (int i = 0; i < comments.length(); i++) {
      out.AddFormatted("                  %s\n", comments[i]);
    }

    // Write out comments, resets outp so that we can format the next line.
    DumpBuffer(f, out.Finalize());
    out.Reset();

    // Instruction address and instruction offset.
    out.AddFormatted("%p  %4d  ", prev_pc, prev_pc - begin);

    // Instruction.
    out.AddFormatted("%s", decode_buffer);

    // Print all the reloc info for this instruction which are not comments.
    for (int i = 0; i < pcs.length(); i++) {
      // Put together the reloc info
      RelocInfo relocinfo(pcs[i], rmodes[i], datas[i]);

      // Indent the printing of the reloc info.
      if (i == 0) {
        // The first reloc info is printed after the disassembled instruction.
        out.AddPadding(' ', kRelocInfoPosition - out.position());
      } else {
        // Additional reloc infos are printed on separate lines.
        out.AddFormatted("\n");
        out.AddPadding(' ', kRelocInfoPosition);
      }

      if (is_position(relocinfo.rmode())) {
        out.AddFormatted("    ;; debug: statement %d", relocinfo.data());
      } else if (relocinfo.rmode() == embedded_object) {
        HeapStringAllocator allocator;
        StringStream accumulator(&allocator);
        relocinfo.target_object()->ShortPrint(&accumulator);
        SmartPointer<char> obj_name = accumulator.ToCString();
        out.AddFormatted("    ;; object: %s", *obj_name);
      } else if (relocinfo.rmode() == external_reference) {
        const char* reference_name =
            ref_encoder.NameOfAddress(*relocinfo.target_reference_address());
        out.AddFormatted("    ;; external reference (%s)", reference_name);
      } else {
        out.AddFormatted("    ;; %s",
                         RelocInfo::RelocModeName(relocinfo.rmode()));
        if (is_code_target(relocinfo.rmode())) {
          Code* code = Debug::GetCodeTarget(relocinfo.target_address());
          Code::Kind kind = code->kind();
          if (kind == Code::STUB) {
            // Reverse lookup required as the minor key cannot be retrieved
            // from the code object.
            Object* obj = Heap::code_stubs()->SlowReverseLookup(code);
            if (obj != Heap::undefined_value()) {
              ASSERT(obj->IsSmi());
              // Get the STUB key and extract major and minor key.
              uint32_t key = Smi::cast(obj)->value();
              CodeStub::Major major_key = code->major_key();
              uint32_t minor_key = CodeStub::MinorKeyFromKey(key);
              ASSERT(major_key == CodeStub::MajorKeyFromKey(key));
              out.AddFormatted(" (%s, %s, ",
                               Code::Kind2String(kind),
                               CodeStub::MajorName(code->major_key()));
              switch (code->major_key()) {
                case CodeStub::CallFunction:
                  out.AddFormatted("argc = %d)", minor_key);
                  break;
                case CodeStub::Runtime: {
                  Runtime::FunctionId id =
                      static_cast<Runtime::FunctionId>(minor_key);
                  out.AddFormatted("%s)", Runtime::FunctionForId(id)->name);
                  break;
                }
                default:
                  out.AddFormatted("minor: %d)", minor_key);
              }
            }
          } else {
            out.AddFormatted(" (%s)", Code::Kind2String(kind));
          }
        }
      }
    }
    out.AddString("\n");
    DumpBuffer(f, out.Finalize());
    out.Reset();
  }

  delete it;
  return pc - begin;
}


int Disassembler::Decode(FILE* f, byte* begin, byte* end) {
  V8NameConverter defaultConverter(NULL);
  return DecodeIt(f, defaultConverter, begin, end);
}


// Called by Code::CodePrint.
void Disassembler::Decode(FILE* f, Code* code) {
  byte* begin = Code::cast(code)->instruction_start();
  byte* end = begin + Code::cast(code)->instruction_size();
  V8NameConverter v8NameConverter(code);
  DecodeIt(f, v8NameConverter, begin, end);
}

#else  // ENABLE_DISASSEMBLER

void Disassembler::Dump(FILE* f, byte* begin, byte* end) {}
int Disassembler::Decode(FILE* f, byte* begin, byte* end) { return 0; }
void Disassembler::Decode(FILE* f, Code* code) {}

#endif  // ENABLE_DISASSEMBLER

} }  // namespace v8::internal
