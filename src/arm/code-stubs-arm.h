// Copyright 2010 the V8 project authors. All rights reserved.
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

#ifndef V8_ARM_CODE_STUBS_ARM_H_
#define V8_ARM_CODE_STUBS_ARM_H_

#include "ic-inl.h"

namespace v8 {
namespace internal {


// Compute a transcendental math function natively, or call the
// TranscendentalCache runtime function.
class TranscendentalCacheStub: public CodeStub {
 public:
  enum ArgumentType {
    TAGGED = 0 << TranscendentalCache::kTranscendentalTypeBits,
    UNTAGGED = 1 << TranscendentalCache::kTranscendentalTypeBits
  };

  TranscendentalCacheStub(TranscendentalCache::Type type,
                          ArgumentType argument_type)
      : type_(type), argument_type_(argument_type) { }
  void Generate(MacroAssembler* masm);
 private:
  TranscendentalCache::Type type_;
  ArgumentType argument_type_;
  void GenerateCallCFunction(MacroAssembler* masm, Register scratch);

  Major MajorKey() { return TranscendentalCache; }
  int MinorKey() { return type_ | argument_type_; }
  Runtime::FunctionId RuntimeFunction();
};


class ToBooleanStub: public CodeStub {
 public:
  explicit ToBooleanStub(Register tos) : tos_(tos) { }

  void Generate(MacroAssembler* masm);

 private:
  Register tos_;
  Major MajorKey() { return ToBoolean; }
  int MinorKey() { return tos_.code(); }
};


class TypeRecordingBinaryOpStub: public CodeStub {
 public:
  TypeRecordingBinaryOpStub(Token::Value op, OverwriteMode mode)
      : op_(op),
        mode_(mode),
        operands_type_(TRBinaryOpIC::UNINITIALIZED),
        result_type_(TRBinaryOpIC::UNINITIALIZED),
        name_(NULL) {
    use_vfp3_ = CpuFeatures::IsSupported(VFP3);
    ASSERT(OpBits::is_valid(Token::NUM_TOKENS));
  }

  TypeRecordingBinaryOpStub(
      int key,
      TRBinaryOpIC::TypeInfo operands_type,
      TRBinaryOpIC::TypeInfo result_type = TRBinaryOpIC::UNINITIALIZED)
      : op_(OpBits::decode(key)),
        mode_(ModeBits::decode(key)),
        use_vfp3_(VFP3Bits::decode(key)),
        operands_type_(operands_type),
        result_type_(result_type),
        name_(NULL) { }

 private:
  enum SmiCodeGenerateHeapNumberResults {
    ALLOW_HEAPNUMBER_RESULTS,
    NO_HEAPNUMBER_RESULTS
  };

  Token::Value op_;
  OverwriteMode mode_;
  bool use_vfp3_;

  // Operand type information determined at runtime.
  TRBinaryOpIC::TypeInfo operands_type_;
  TRBinaryOpIC::TypeInfo result_type_;

  char* name_;

  const char* GetName();

#ifdef DEBUG
  void Print() {
    PrintF("TypeRecordingBinaryOpStub %d (op %s), "
           "(mode %d, runtime_type_info %s)\n",
           MinorKey(),
           Token::String(op_),
           static_cast<int>(mode_),
           TRBinaryOpIC::GetName(operands_type_));
  }
#endif

  // Minor key encoding in 16 bits RRRTTTVOOOOOOOMM.
  class ModeBits: public BitField<OverwriteMode, 0, 2> {};
  class OpBits: public BitField<Token::Value, 2, 7> {};
  class VFP3Bits: public BitField<bool, 9, 1> {};
  class OperandTypeInfoBits: public BitField<TRBinaryOpIC::TypeInfo, 10, 3> {};
  class ResultTypeInfoBits: public BitField<TRBinaryOpIC::TypeInfo, 13, 3> {};

  Major MajorKey() { return TypeRecordingBinaryOp; }
  int MinorKey() {
    return OpBits::encode(op_)
           | ModeBits::encode(mode_)
           | VFP3Bits::encode(use_vfp3_)
           | OperandTypeInfoBits::encode(operands_type_)
           | ResultTypeInfoBits::encode(result_type_);
  }

  void Generate(MacroAssembler* masm);
  void GenerateGeneric(MacroAssembler* masm);
  void GenerateSmiSmiOperation(MacroAssembler* masm);
  void GenerateFPOperation(MacroAssembler* masm,
                           bool smi_operands,
                           Label* not_numbers,
                           Label* gc_required);
  void GenerateSmiCode(MacroAssembler* masm,
                       Label* use_runtime,
                       Label* gc_required,
                       SmiCodeGenerateHeapNumberResults heapnumber_results);
  void GenerateLoadArguments(MacroAssembler* masm);
  void GenerateReturn(MacroAssembler* masm);
  void GenerateUninitializedStub(MacroAssembler* masm);
  void GenerateSmiStub(MacroAssembler* masm);
  void GenerateInt32Stub(MacroAssembler* masm);
  void GenerateHeapNumberStub(MacroAssembler* masm);
  void GenerateOddballStub(MacroAssembler* masm);
  void GenerateStringStub(MacroAssembler* masm);
  void GenerateGenericStub(MacroAssembler* masm);
  void GenerateAddStrings(MacroAssembler* masm);
  void GenerateCallRuntime(MacroAssembler* masm);

  void GenerateHeapResultAllocation(MacroAssembler* masm,
                                    Register result,
                                    Register heap_number_map,
                                    Register scratch1,
                                    Register scratch2,
                                    Label* gc_required);
  void GenerateRegisterArgsPush(MacroAssembler* masm);
  void GenerateTypeTransition(MacroAssembler* masm);
  void GenerateTypeTransitionWithSavedArgs(MacroAssembler* masm);

  virtual int GetCodeKind() { return Code::TYPE_RECORDING_BINARY_OP_IC; }

  virtual InlineCacheState GetICState() {
    return TRBinaryOpIC::ToState(operands_type_);
  }

  virtual void FinishCode(Code* code) {
    code->set_type_recording_binary_op_type(operands_type_);
    code->set_type_recording_binary_op_result_type(result_type_);
  }

  friend class CodeGenerator;
};


// Flag that indicates how to generate code for the stub StringAddStub.
enum StringAddFlags {
  NO_STRING_ADD_FLAGS = 0,
  // Omit left string check in stub (left is definitely a string).
  NO_STRING_CHECK_LEFT_IN_STUB = 1 << 0,
  // Omit right string check in stub (right is definitely a string).
  NO_STRING_CHECK_RIGHT_IN_STUB = 1 << 1,
  // Omit both string checks in stub.
  NO_STRING_CHECK_IN_STUB =
      NO_STRING_CHECK_LEFT_IN_STUB | NO_STRING_CHECK_RIGHT_IN_STUB
};


class StringAddStub: public CodeStub {
 public:
  explicit StringAddStub(StringAddFlags flags) : flags_(flags) {}

 private:
  Major MajorKey() { return StringAdd; }
  int MinorKey() { return flags_; }

  void Generate(MacroAssembler* masm);

  void GenerateConvertArgument(MacroAssembler* masm,
                               int stack_offset,
                               Register arg,
                               Register scratch1,
                               Register scratch2,
                               Register scratch3,
                               Register scratch4,
                               Label* slow);

  const StringAddFlags flags_;
};


class SubStringStub: public CodeStub {
 public:
  SubStringStub() {}

 private:
  Major MajorKey() { return SubString; }
  int MinorKey() { return 0; }

  void Generate(MacroAssembler* masm);
};



class StringCompareStub: public CodeStub {
 public:
  StringCompareStub() { }

  // Compare two flat ASCII strings and returns result in r0.
  // Does not use the stack.
  static void GenerateCompareFlatAsciiStrings(MacroAssembler* masm,
                                              Register left,
                                              Register right,
                                              Register scratch1,
                                              Register scratch2,
                                              Register scratch3,
                                              Register scratch4);

 private:
  Major MajorKey() { return StringCompare; }
  int MinorKey() { return 0; }

  void Generate(MacroAssembler* masm);
};


// This stub can convert a signed int32 to a heap number (double).  It does
// not work for int32s that are in Smi range!  No GC occurs during this stub
// so you don't have to set up the frame.
class WriteInt32ToHeapNumberStub : public CodeStub {
 public:
  WriteInt32ToHeapNumberStub(Register the_int,
                             Register the_heap_number,
                             Register scratch)
      : the_int_(the_int),
        the_heap_number_(the_heap_number),
        scratch_(scratch) { }

 private:
  Register the_int_;
  Register the_heap_number_;
  Register scratch_;

  // Minor key encoding in 16 bits.
  class IntRegisterBits: public BitField<int, 0, 4> {};
  class HeapNumberRegisterBits: public BitField<int, 4, 4> {};
  class ScratchRegisterBits: public BitField<int, 8, 4> {};

  Major MajorKey() { return WriteInt32ToHeapNumber; }
  int MinorKey() {
    // Encode the parameters in a unique 16 bit value.
    return IntRegisterBits::encode(the_int_.code())
           | HeapNumberRegisterBits::encode(the_heap_number_.code())
           | ScratchRegisterBits::encode(scratch_.code());
  }

  void Generate(MacroAssembler* masm);

  const char* GetName() { return "WriteInt32ToHeapNumberStub"; }

#ifdef DEBUG
  void Print() { PrintF("WriteInt32ToHeapNumberStub\n"); }
#endif
};


class NumberToStringStub: public CodeStub {
 public:
  NumberToStringStub() { }

  // Generate code to do a lookup in the number string cache. If the number in
  // the register object is found in the cache the generated code falls through
  // with the result in the result register. The object and the result register
  // can be the same. If the number is not found in the cache the code jumps to
  // the label not_found with only the content of register object unchanged.
  static void GenerateLookupNumberStringCache(MacroAssembler* masm,
                                              Register object,
                                              Register result,
                                              Register scratch1,
                                              Register scratch2,
                                              Register scratch3,
                                              bool object_is_smi,
                                              Label* not_found);

 private:
  Major MajorKey() { return NumberToString; }
  int MinorKey() { return 0; }

  void Generate(MacroAssembler* masm);

  const char* GetName() { return "NumberToStringStub"; }
};


// Enter C code from generated RegExp code in a way that allows
// the C code to fix the return address in case of a GC.
// Currently only needed on ARM.
class RegExpCEntryStub: public CodeStub {
 public:
  RegExpCEntryStub() {}
  virtual ~RegExpCEntryStub() {}
  void Generate(MacroAssembler* masm);

 private:
  Major MajorKey() { return RegExpCEntry; }
  int MinorKey() { return 0; }

  bool NeedsImmovableCode() { return true; }

  const char* GetName() { return "RegExpCEntryStub"; }
};


// Trampoline stub to call into native code. To call safely into native code
// in the presence of compacting GC (which can move code objects) we need to
// keep the code which called into native pinned in the memory. Currently the
// simplest approach is to generate such stub early enough so it can never be
// moved by GC
class DirectCEntryStub: public CodeStub {
 public:
  DirectCEntryStub() {}
  void Generate(MacroAssembler* masm);
  void GenerateCall(MacroAssembler* masm, ExternalReference function);
  void GenerateCall(MacroAssembler* masm, Register target);

 private:
  Major MajorKey() { return DirectCEntry; }
  int MinorKey() { return 0; }

  bool NeedsImmovableCode() { return true; }

  const char* GetName() { return "DirectCEntryStub"; }
};


} }  // namespace v8::internal

#endif  // V8_ARM_CODE_STUBS_ARM_H_
