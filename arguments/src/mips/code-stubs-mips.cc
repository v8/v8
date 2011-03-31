// Copyright 2011 the V8 project authors. All rights reserved.
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

#if defined(V8_TARGET_ARCH_MIPS)

#include "bootstrapper.h"
#include "code-stubs.h"
#include "codegen-inl.h"
#include "regexp-macro-assembler.h"

namespace v8 {
namespace internal {


#define __ ACCESS_MASM(masm)


void ToNumberStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void FastNewClosureStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void FastNewContextStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void FastCloneShallowArrayStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


// Takes a Smi and converts to an IEEE 64 bit floating point value in two
// registers.  The format is 1 sign bit, 11 exponent bits (biased 1023) and
// 52 fraction bits (20 in the first word, 32 in the second).  Zeros is a
// scratch register.  Destroys the source register.  No GC occurs during this
// stub so you don't have to set up the frame.
class ConvertToDoubleStub : public CodeStub {
 public:
  ConvertToDoubleStub(Register result_reg_1,
                      Register result_reg_2,
                      Register source_reg,
                      Register scratch_reg)
      : result1_(result_reg_1),
        result2_(result_reg_2),
        source_(source_reg),
        zeros_(scratch_reg) { }

 private:
  Register result1_;
  Register result2_;
  Register source_;
  Register zeros_;

  // Minor key encoding in 16 bits.
  class ModeBits: public BitField<OverwriteMode, 0, 2> {};
  class OpBits: public BitField<Token::Value, 2, 14> {};

  Major MajorKey() { return ConvertToDouble; }
  int MinorKey() {
    // Encode the parameters in a unique 16 bit value.
    return  result1_.code() +
           (result2_.code() << 4) +
           (source_.code() << 8) +
           (zeros_.code() << 12);
  }

  void Generate(MacroAssembler* masm);

  const char* GetName() { return "ConvertToDoubleStub"; }

#ifdef DEBUG
  void Print() { PrintF("ConvertToDoubleStub\n"); }
#endif
};


void ConvertToDoubleStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


class FloatingPointHelper : public AllStatic {
 public:

  enum Destination {
    kFPURegisters,
    kCoreRegisters
  };


  // Loads smis from a0 and a1 (right and left in binary operations) into
  // floating point registers. Depending on the destination the values ends up
  // either f14 and f12 or in a2/a3 and a0/a1 respectively. If the destination
  // is floating point registers FPU must be supported. If core registers are
  // requested when FPU is supported f12 and f14 will be scratched.
  static void LoadSmis(MacroAssembler* masm,
                       Destination destination,
                       Register scratch1,
                       Register scratch2);

  // Loads objects from a0 and a1 (right and left in binary operations) into
  // floating point registers. Depending on the destination the values ends up
  // either f14 and f12 or in a2/a3 and a0/a1 respectively. If the destination
  // is floating point registers FPU must be supported. If core registers are
  // requested when FPU is supported f12 and f14 will still be scratched. If
  // either a0 or a1 is not a number (not smi and not heap number object) the
  // not_number label is jumped to with a0 and a1 intact.
  static void LoadOperands(MacroAssembler* masm,
                           FloatingPointHelper::Destination destination,
                           Register heap_number_map,
                           Register scratch1,
                           Register scratch2,
                           Label* not_number);
  // Loads the number from object into dst as a 32-bit integer if possible. If
  // the object is not a 32-bit integer control continues at the label
  // not_int32. If FPU is supported double_scratch is used but not scratch2.
  static void LoadNumberAsInteger(MacroAssembler* masm,
                                  Register object,
                                  Register dst,
                                  Register heap_number_map,
                                  Register scratch1,
                                  Register scratch2,
                                  FPURegister double_scratch,
                                  Label* not_int32);
 private:
  static void LoadNumber(MacroAssembler* masm,
                         FloatingPointHelper::Destination destination,
                         Register object,
                         FPURegister dst,
                         Register dst1,
                         Register dst2,
                         Register heap_number_map,
                         Register scratch1,
                         Register scratch2,
                         Label* not_number);
};


void FloatingPointHelper::LoadSmis(MacroAssembler* masm,
                                   FloatingPointHelper::Destination destination,
                                   Register scratch1,
                                   Register scratch2) {
  UNIMPLEMENTED_MIPS();
}


void FloatingPointHelper::LoadOperands(
    MacroAssembler* masm,
    FloatingPointHelper::Destination destination,
    Register heap_number_map,
    Register scratch1,
    Register scratch2,
    Label* slow) {
  UNIMPLEMENTED_MIPS();
}


void FloatingPointHelper::LoadNumber(MacroAssembler* masm,
                                     Destination destination,
                                     Register object,
                                     FPURegister dst,
                                     Register dst1,
                                     Register dst2,
                                     Register heap_number_map,
                                     Register scratch1,
                                     Register scratch2,
                                     Label* not_number) {
  UNIMPLEMENTED_MIPS();
}


void FloatingPointHelper::LoadNumberAsInteger(MacroAssembler* masm,
                                              Register object,
                                              Register dst,
                                              Register heap_number_map,
                                              Register scratch1,
                                              Register scratch2,
                                              FPURegister double_scratch,
                                              Label* not_int32) {
  UNIMPLEMENTED_MIPS();
}


// See comment for class, this does NOT work for int32's that are in Smi range.
void WriteInt32ToHeapNumberStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void EmitNanCheck(MacroAssembler* masm, Condition cc) {
  UNIMPLEMENTED_MIPS();
}


void NumberToStringStub::GenerateLookupNumberStringCache(MacroAssembler* masm,
                                                         Register object,
                                                         Register result,
                                                         Register scratch1,
                                                         Register scratch2,
                                                         Register scratch3,
                                                         bool object_is_smi,
                                                         Label* not_found) {
  UNIMPLEMENTED_MIPS();
}


void NumberToStringStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


// On entry lhs_ (lhs) and rhs_ (rhs) are the things to be compared.
// On exit, v0 is 0, positive, or negative (smi) to indicate the result
// of the comparison.
void CompareStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


// This stub does not handle the inlined cases (Smis, Booleans, undefined).
// The stub returns zero for false, and a non-zero value for true.
void ToBooleanStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


// We fall into this code if the operands were Smis, but the result was
// not (eg. overflow).  We branch into this code (to the not_smi label) if
// the operands were not both Smi.  The operands are in lhs and rhs.
// To call the C-implemented binary fp operation routines we need to end up
// with the double precision floating point operands in a0 and a1 (for the
// value in a1) and a2 and a3 (for the value in a0).
void GenericBinaryOpStub::HandleBinaryOpSlowCases(MacroAssembler* masm,
                                    Label* not_smi,
                                    Register lhs,
                                    Register rhs,
                                    const Builtins::JavaScript& builtin) {
  UNIMPLEMENTED_MIPS();
}


// For bitwise ops where the inputs are not both Smis we here try to determine
// whether both inputs are either Smis or at least heap numbers that can be
// represented by a 32 bit signed value.  We truncate towards zero as required
// by the ES spec.  If this is the case we do the bitwise op and see if the
// result is a Smi.  If so, great, otherwise we try to find a heap number to
// write the answer into (either by allocating or by overwriting).
// On entry the operands are in lhs (x) and rhs (y). (Result = x op y).
// On exit the result is in v0.
void GenericBinaryOpStub::HandleNonSmiBitwiseOp(MacroAssembler* masm,
                                                Register lhs,
                                                Register rhs) {
  UNIMPLEMENTED_MIPS();
}


void GenericBinaryOpStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void GenericBinaryOpStub::GenerateTypeTransition(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


Handle<Code> GetBinaryOpStub(int key, BinaryOpIC::TypeInfo type_info) {
  GenericBinaryOpStub stub(key, type_info);
  return stub.GetCode();
}


Handle<Code> GetTypeRecordingBinaryOpStub(int key,
    TRBinaryOpIC::TypeInfo type_info,
    TRBinaryOpIC::TypeInfo result_type_info) {
  TypeRecordingBinaryOpStub stub(key, type_info, result_type_info);
  return stub.GetCode();
}


void TypeRecordingBinaryOpStub::GenerateTypeTransition(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void TypeRecordingBinaryOpStub::GenerateTypeTransitionWithSavedArgs(
    MacroAssembler* masm) {
  UNIMPLEMENTED();
}


void TypeRecordingBinaryOpStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


const char* TypeRecordingBinaryOpStub::GetName() {
  UNIMPLEMENTED_MIPS();
  return name_;
}



void TypeRecordingBinaryOpStub::GenerateSmiSmiOperation(
    MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void TypeRecordingBinaryOpStub::GenerateFPOperation(MacroAssembler* masm,
                                                    bool smi_operands,
                                                    Label* not_numbers,
                                                    Label* gc_required) {
  UNIMPLEMENTED_MIPS();
}


// Generate the smi code. If the operation on smis are successful this return is
// generated. If the result is not a smi and heap number allocation is not
// requested the code falls through. If number allocation is requested but a
// heap number cannot be allocated the code jumps to the lable gc_required.
void TypeRecordingBinaryOpStub::GenerateSmiCode(MacroAssembler* masm,
    Label* gc_required,
    SmiCodeGenerateHeapNumberResults allow_heapnumber_results) {
  UNIMPLEMENTED_MIPS();
}


void TypeRecordingBinaryOpStub::GenerateSmiStub(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void TypeRecordingBinaryOpStub::GenerateStringStub(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void TypeRecordingBinaryOpStub::GenerateInt32Stub(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void TypeRecordingBinaryOpStub::GenerateHeapNumberStub(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void TypeRecordingBinaryOpStub::GenerateGeneric(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void TypeRecordingBinaryOpStub::GenerateAddStrings(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void TypeRecordingBinaryOpStub::GenerateCallRuntime(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void TypeRecordingBinaryOpStub::GenerateHeapResultAllocation(
    MacroAssembler* masm,
    Register result,
    Register heap_number_map,
    Register scratch1,
    Register scratch2,
    Label* gc_required) {
  UNIMPLEMENTED_MIPS();
}


void TypeRecordingBinaryOpStub::GenerateRegisterArgsPush(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}



void TranscendentalCacheStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


Runtime::FunctionId TranscendentalCacheStub::RuntimeFunction() {
  UNIMPLEMENTED_MIPS();
  return Runtime::kAbort;
}


void StackCheckStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void GenericUnaryOpStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


bool CEntryStub::NeedsImmovableCode() {
  return true;
}


void CEntryStub::GenerateThrowTOS(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void CEntryStub::GenerateThrowUncatchable(MacroAssembler* masm,
                                          UncatchableExceptionType type) {
  UNIMPLEMENTED_MIPS();
}


void CEntryStub::GenerateCore(MacroAssembler* masm,
                              Label* throw_normal_exception,
                              Label* throw_termination_exception,
                              Label* throw_out_of_memory_exception,
                              bool do_gc,
                              bool always_allocate) {
  UNIMPLEMENTED_MIPS();
}


void CEntryStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void JSEntryStub::GenerateBody(MacroAssembler* masm, bool is_construct) {
  UNIMPLEMENTED_MIPS();
}


// Uses registers a0 to t0. Expected input is
// object in a0 (or at sp+1*kPointerSize) and function in
// a1 (or at sp), depending on whether or not
// args_in_registers() is true.
void InstanceofStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void ArgumentsAccessStub::GenerateReadElement(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void ArgumentsAccessStub::GenerateNewObject(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void RegExpExecStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void RegExpConstructResultStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void CallFunctionStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


// Unfortunately you have to run without snapshots to see most of these
// names in the profile since most compare stubs end up in the snapshot.
const char* CompareStub::GetName() {
  UNIMPLEMENTED_MIPS();
  return name_;
}


int CompareStub::MinorKey() {
  UNIMPLEMENTED_MIPS();
  return 0;
}


// StringCharCodeAtGenerator

void StringCharCodeAtGenerator::GenerateFast(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void StringCharCodeAtGenerator::GenerateSlow(
    MacroAssembler* masm, const RuntimeCallHelper& call_helper) {
  UNIMPLEMENTED_MIPS();
}


// -------------------------------------------------------------------------
// StringCharFromCodeGenerator

void StringCharFromCodeGenerator::GenerateFast(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void StringCharFromCodeGenerator::GenerateSlow(
    MacroAssembler* masm, const RuntimeCallHelper& call_helper) {
  UNIMPLEMENTED_MIPS();
}


// -------------------------------------------------------------------------
// StringCharAtGenerator

void StringCharAtGenerator::GenerateFast(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void StringCharAtGenerator::GenerateSlow(
    MacroAssembler* masm, const RuntimeCallHelper& call_helper) {
  UNIMPLEMENTED_MIPS();
}


class StringHelper : public AllStatic {
 public:
  // Generate code for copying characters using a simple loop. This should only
  // be used in places where the number of characters is small and the
  // additional setup and checking in GenerateCopyCharactersLong adds too much
  // overhead. Copying of overlapping regions is not supported.
  // Dest register ends at the position after the last character written.
  static void GenerateCopyCharacters(MacroAssembler* masm,
                                     Register dest,
                                     Register src,
                                     Register count,
                                     Register scratch,
                                     bool ascii);

  // Generate code for copying a large number of characters. This function
  // is allowed to spend extra time setting up conditions to make copying
  // faster. Copying of overlapping regions is not supported.
  // Dest register ends at the position after the last character written.
  static void GenerateCopyCharactersLong(MacroAssembler* masm,
                                         Register dest,
                                         Register src,
                                         Register count,
                                         Register scratch1,
                                         Register scratch2,
                                         Register scratch3,
                                         Register scratch4,
                                         Register scratch5,
                                         int flags);


  // Probe the symbol table for a two character string. If the string is
  // not found by probing a jump to the label not_found is performed. This jump
  // does not guarantee that the string is not in the symbol table. If the
  // string is found the code falls through with the string in register r0.
  // Contents of both c1 and c2 registers are modified. At the exit c1 is
  // guaranteed to contain halfword with low and high bytes equal to
  // initial contents of c1 and c2 respectively.
  static void GenerateTwoCharacterSymbolTableProbe(MacroAssembler* masm,
                                                   Register c1,
                                                   Register c2,
                                                   Register scratch1,
                                                   Register scratch2,
                                                   Register scratch3,
                                                   Register scratch4,
                                                   Register scratch5,
                                                   Label* not_found);

  // Generate string hash.
  static void GenerateHashInit(MacroAssembler* masm,
                               Register hash,
                               Register character);

  static void GenerateHashAddCharacter(MacroAssembler* masm,
                                       Register hash,
                                       Register character);

  static void GenerateHashGetHash(MacroAssembler* masm,
                                  Register hash);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(StringHelper);
};


void StringHelper::GenerateCopyCharacters(MacroAssembler* masm,
                                          Register dest,
                                          Register src,
                                          Register count,
                                          Register scratch,
                                          bool ascii) {
  UNIMPLEMENTED_MIPS();
}


enum CopyCharactersFlags {
  COPY_ASCII = 1,
  DEST_ALWAYS_ALIGNED = 2
};


void StringHelper::GenerateCopyCharactersLong(MacroAssembler* masm,
                                              Register dest,
                                              Register src,
                                              Register count,
                                              Register scratch1,
                                              Register scratch2,
                                              Register scratch3,
                                              Register scratch4,
                                              Register scratch5,
                                              int flags) {
  UNIMPLEMENTED_MIPS();
}


void StringHelper::GenerateTwoCharacterSymbolTableProbe(MacroAssembler* masm,
                                                        Register c1,
                                                        Register c2,
                                                        Register scratch1,
                                                        Register scratch2,
                                                        Register scratch3,
                                                        Register scratch4,
                                                        Register scratch5,
                                                        Label* not_found) {
  UNIMPLEMENTED_MIPS();
}


void StringHelper::GenerateHashInit(MacroAssembler* masm,
                                      Register hash,
                                      Register character) {
  UNIMPLEMENTED_MIPS();
}


void StringHelper::GenerateHashAddCharacter(MacroAssembler* masm,
                                              Register hash,
                                              Register character) {
  UNIMPLEMENTED_MIPS();
}


void StringHelper::GenerateHashGetHash(MacroAssembler* masm,
                                         Register hash) {
  UNIMPLEMENTED_MIPS();
}


void SubStringStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void StringCompareStub::GenerateCompareFlatAsciiStrings(MacroAssembler* masm,
                                                        Register right,
                                                        Register left,
                                                        Register scratch1,
                                                        Register scratch2,
                                                        Register scratch3,
                                                        Register scratch4) {
  UNIMPLEMENTED_MIPS();
}


void StringCompareStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void StringAddStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void ICCompareStub::GenerateSmis(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void ICCompareStub::GenerateHeapNumbers(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void ICCompareStub::GenerateObjects(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void ICCompareStub::GenerateMiss(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void GenerateFastPixelArrayLoad(MacroAssembler* masm,
                                Register receiver,
                                Register key,
                                Register elements_map,
                                Register elements,
                                Register scratch1,
                                Register scratch2,
                                Register result,
                                Label* not_pixel_array,
                                Label* key_not_smi,
                                Label* out_of_range) {
  UNIMPLEMENTED_MIPS();
}


#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_MIPS

