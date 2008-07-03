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
  // If the V8NameConverter is used for well known code, so we can "safely"
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

static const int kOutBufferSize = 1024;
static const int kRelocInfoPosition = 57;

static int DecodeIt(FILE* f,
                    const V8NameConverter& converter,
                    byte* begin,
                    byte* end) {
  ExternalReferenceEncoder ref_encoder;
  char decode_buffer[128];
  char out_buffer[kOutBufferSize];
  const int sob = sizeof out_buffer;
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
      OS::SNPrintF(decode_buffer, sizeof(decode_buffer), "%s", "constant");
      constants--;
      pc += 4;
    } else {
      int instruction_bits = *(reinterpret_cast<int*>(pc));
      if ((instruction_bits & 0xfff00000) == 0x03000000) {
        OS::SNPrintF(decode_buffer, sizeof(decode_buffer),
                     "%s", "constant pool begin");
        constants = instruction_bits & 0x0000ffff;
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

    int outp = 0;  // pointer into out_buffer, implements append operation.

    // Comments.
    for (int i = 0; i < comments.length(); i++) {
      outp += OS::SNPrintF(out_buffer + outp, sob - outp,
                           "                  %s\n", comments[i]);
    }

    // Write out comments, resets outp so that we can format the next line.
    if (outp > 0) {
      DumpBuffer(f, out_buffer);
      outp = 0;
    }

    // Instruction address and instruction offset.
    outp += OS::SNPrintF(out_buffer + outp, sob - outp,
                         "%p  %4d  ", prev_pc, prev_pc - begin);

    // Instruction bytes.
    ASSERT(pc - prev_pc == 4);
    outp += OS::SNPrintF(out_buffer + outp,
                         sob - outp,
                         "%08x",
                         *reinterpret_cast<intptr_t*>(prev_pc));

    for (int i = 6 - (pc - prev_pc); i >= 0; i--) {
      outp += OS::SNPrintF(out_buffer + outp, sob - outp, "  ");
    }
    outp += OS::SNPrintF(out_buffer + outp, sob - outp, " %s", decode_buffer);

    // Print all the reloc info for this instruction which are not comments.
    for (int i = 0; i < pcs.length(); i++) {
      // Put together the reloc info
      RelocInfo relocinfo(pcs[i], rmodes[i], datas[i]);

      // Indent the printing of the reloc info.
      if (i == 0) {
        // The first reloc info is printed after the disassembled instruction.
        for (int p = outp; p < kRelocInfoPosition; p++) {
          outp += OS::SNPrintF(out_buffer + outp, sob - outp, " ");
        }
      } else {
        // Additional reloc infos are printed on separate lines.
        outp += OS::SNPrintF(out_buffer + outp, sob - outp, "\n");
        for (int p = 0; p < kRelocInfoPosition; p++) {
          outp += OS::SNPrintF(out_buffer + outp, sob - outp, " ");
        }
      }

      if (is_position(relocinfo.rmode())) {
        outp += OS::SNPrintF(out_buffer + outp,
                             sob - outp,
                             "    ;; debug: statement %d",
                             relocinfo.data());
      } else if (relocinfo.rmode() == embedded_object) {
        HeapStringAllocator allocator;
        StringStream accumulator(&allocator);
        relocinfo.target_object()->ShortPrint(&accumulator);
        SmartPointer<char> obj_name = accumulator.ToCString();
        outp += OS::SNPrintF(out_buffer + outp, sob - outp,
                             "    ;; object: %s",
                             *obj_name);
      } else if (relocinfo.rmode() == external_reference) {
        const char* reference_name =
            ref_encoder.NameOfAddress(*relocinfo.target_reference_address());
        outp += OS::SNPrintF(out_buffer + outp, sob - outp,
                            "    ;; external reference (%s)",
                            reference_name);
      } else if (relocinfo.rmode() == code_target) {
        outp +=
            OS::SNPrintF(out_buffer + outp, sob - outp,
                         "    ;; code target (%s)",
                         converter.NameOfAddress(relocinfo.target_address()));
      } else {
        outp += OS::SNPrintF(out_buffer + outp, sob - outp,
                             "    ;; %s%s",
#if defined(DEBUG)
                             RelocInfo::RelocModeName(relocinfo.rmode()),
#else
                             "reloc_info",
#endif
                             "");
      }
    }
    outp += OS::SNPrintF(out_buffer + outp, sob - outp, "\n");

    if (outp > 0) {
      ASSERT(outp < kOutBufferSize);
      DumpBuffer(f, out_buffer);
      outp = 0;
    }
  }

  delete it;
  return pc - begin;
}


int Disassembler::Decode(FILE* f, byte* begin, byte* end) {
  V8NameConverter defaultConverter(NULL);
  return DecodeIt(f, defaultConverter, begin, end);
}


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
