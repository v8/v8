// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_ARM64

#include "src/codegen.h"
#include "src/ic/ic.h"
#include "src/ic/ic-compiler.h"
#include "src/ic/stub-cache.h"

namespace v8 {
namespace internal {


#define __ ACCESS_MASM(masm)

// Helper function used from LoadIC GenerateNormal.
//
// elements: Property dictionary. It is not clobbered if a jump to the miss
//           label is done.
// name:     Property name. It is not clobbered if a jump to the miss label is
//           done
// result:   Register for the result. It is only updated if a jump to the miss
//           label is not done.
// The scratch registers need to be different from elements, name and result.
// The generated code assumes that the receiver has slow properties,
// is not a global object and does not have interceptors.
static void GenerateDictionaryLoad(MacroAssembler* masm, Label* miss,
                                   Register elements, Register name,
                                   Register result, Register scratch1,
                                   Register scratch2) {
  DCHECK(!AreAliased(elements, name, scratch1, scratch2));
  DCHECK(!AreAliased(result, scratch1, scratch2));

  Label done;

  // Probe the dictionary.
  NameDictionaryLookupStub::GeneratePositiveLookup(masm, miss, &done, elements,
                                                   name, scratch1, scratch2);

  // If probing finds an entry check that the value is a normal property.
  __ Bind(&done);

  static const int kElementsStartOffset =
      NameDictionary::kHeaderSize +
      NameDictionary::kElementsStartIndex * kPointerSize;
  static const int kDetailsOffset = kElementsStartOffset + 2 * kPointerSize;
  __ Ldr(scratch1, FieldMemOperand(scratch2, kDetailsOffset));
  __ Tst(scratch1, Smi::FromInt(PropertyDetails::TypeField::kMask));
  __ B(ne, miss);

  // Get the value at the masked, scaled index and return.
  __ Ldr(result,
         FieldMemOperand(scratch2, kElementsStartOffset + 1 * kPointerSize));
}


// Helper function used from StoreIC::GenerateNormal.
//
// elements: Property dictionary. It is not clobbered if a jump to the miss
//           label is done.
// name:     Property name. It is not clobbered if a jump to the miss label is
//           done
// value:    The value to store (never clobbered).
//
// The generated code assumes that the receiver has slow properties,
// is not a global object and does not have interceptors.
static void GenerateDictionaryStore(MacroAssembler* masm, Label* miss,
                                    Register elements, Register name,
                                    Register value, Register scratch1,
                                    Register scratch2) {
  DCHECK(!AreAliased(elements, name, value, scratch1, scratch2));

  Label done;

  // Probe the dictionary.
  NameDictionaryLookupStub::GeneratePositiveLookup(masm, miss, &done, elements,
                                                   name, scratch1, scratch2);

  // If probing finds an entry in the dictionary check that the value
  // is a normal property that is not read only.
  __ Bind(&done);

  static const int kElementsStartOffset =
      NameDictionary::kHeaderSize +
      NameDictionary::kElementsStartIndex * kPointerSize;
  static const int kDetailsOffset = kElementsStartOffset + 2 * kPointerSize;
  static const int kTypeAndReadOnlyMask =
      PropertyDetails::TypeField::kMask |
      PropertyDetails::AttributesField::encode(READ_ONLY);
  __ Ldrsw(scratch1, UntagSmiFieldMemOperand(scratch2, kDetailsOffset));
  __ Tst(scratch1, kTypeAndReadOnlyMask);
  __ B(ne, miss);

  // Store the value at the masked, scaled index and return.
  static const int kValueOffset = kElementsStartOffset + kPointerSize;
  __ Add(scratch2, scratch2, kValueOffset - kHeapObjectTag);
  __ Str(value, MemOperand(scratch2));

  // Update the write barrier. Make sure not to clobber the value.
  __ Mov(scratch1, value);
  __ RecordWrite(elements, scratch2, scratch1, kLRHasNotBeenSaved,
                 kDontSaveFPRegs);
}

void LoadIC::GenerateNormal(MacroAssembler* masm) {
  Register dictionary = x0;
  DCHECK(!dictionary.is(LoadDescriptor::ReceiverRegister()));
  DCHECK(!dictionary.is(LoadDescriptor::NameRegister()));
  Label slow;

  __ Ldr(dictionary, FieldMemOperand(LoadDescriptor::ReceiverRegister(),
                                     JSObject::kPropertiesOffset));
  GenerateDictionaryLoad(masm, &slow, dictionary,
                         LoadDescriptor::NameRegister(), x0, x3, x4);
  __ Ret();

  // Dictionary load failed, go slow (but don't miss).
  __ Bind(&slow);
  GenerateRuntimeGetProperty(masm);
}


void LoadIC::GenerateMiss(MacroAssembler* masm) {
  // The return address is in lr.
  Isolate* isolate = masm->isolate();
  ASM_LOCATION("LoadIC::GenerateMiss");

  DCHECK(!AreAliased(x4, x5, LoadWithVectorDescriptor::SlotRegister(),
                     LoadWithVectorDescriptor::VectorRegister()));
  __ IncrementCounter(isolate->counters()->ic_load_miss(), 1, x4, x5);

  // Perform tail call to the entry.
  __ Push(LoadWithVectorDescriptor::ReceiverRegister(),
          LoadWithVectorDescriptor::NameRegister(),
          LoadWithVectorDescriptor::SlotRegister(),
          LoadWithVectorDescriptor::VectorRegister());
  __ TailCallRuntime(Runtime::kLoadIC_Miss);
}

void LoadIC::GenerateRuntimeGetProperty(MacroAssembler* masm) {
  // The return address is in lr.
  __ Push(LoadDescriptor::ReceiverRegister(), LoadDescriptor::NameRegister());

  // Do tail-call to runtime routine.
  __ TailCallRuntime(Runtime::kGetProperty);
}


void KeyedLoadIC::GenerateMiss(MacroAssembler* masm) {
  // The return address is in lr.
  Isolate* isolate = masm->isolate();

  DCHECK(!AreAliased(x10, x11, LoadWithVectorDescriptor::SlotRegister(),
                     LoadWithVectorDescriptor::VectorRegister()));
  __ IncrementCounter(isolate->counters()->ic_keyed_load_miss(), 1, x10, x11);

  __ Push(LoadWithVectorDescriptor::ReceiverRegister(),
          LoadWithVectorDescriptor::NameRegister(),
          LoadWithVectorDescriptor::SlotRegister(),
          LoadWithVectorDescriptor::VectorRegister());

  // Perform tail call to the entry.
  __ TailCallRuntime(Runtime::kKeyedLoadIC_Miss);
}

void KeyedLoadIC::GenerateRuntimeGetProperty(MacroAssembler* masm) {
  // The return address is in lr.
  __ Push(LoadDescriptor::ReceiverRegister(), LoadDescriptor::NameRegister());

  // Do tail-call to runtime routine.
  __ TailCallRuntime(Runtime::kKeyedGetProperty);
}

static void StoreIC_PushArgs(MacroAssembler* masm) {
  __ Push(StoreWithVectorDescriptor::ValueRegister(),
          StoreWithVectorDescriptor::SlotRegister(),
          StoreWithVectorDescriptor::VectorRegister(),
          StoreWithVectorDescriptor::ReceiverRegister(),
          StoreWithVectorDescriptor::NameRegister());
}


void KeyedStoreIC::GenerateMiss(MacroAssembler* masm) {
  ASM_LOCATION("KeyedStoreIC::GenerateMiss");
  StoreIC_PushArgs(masm);
  __ TailCallRuntime(Runtime::kKeyedStoreIC_Miss);
}

void KeyedStoreIC::GenerateSlow(MacroAssembler* masm) {
  ASM_LOCATION("KeyedStoreIC::GenerateSlow");
  StoreIC_PushArgs(masm);

  // The slow case calls into the runtime to complete the store without causing
  // an IC miss that would otherwise cause a transition to the generic stub.
  __ TailCallRuntime(Runtime::kKeyedStoreIC_Slow);
}

void StoreIC::GenerateMiss(MacroAssembler* masm) {
  StoreIC_PushArgs(masm);

  // Tail call to the entry.
  __ TailCallRuntime(Runtime::kStoreIC_Miss);
}


void StoreIC::GenerateNormal(MacroAssembler* masm) {
  Label miss;
  Register value = StoreDescriptor::ValueRegister();
  Register receiver = StoreDescriptor::ReceiverRegister();
  Register name = StoreDescriptor::NameRegister();
  Register dictionary = x5;
  DCHECK(!AreAliased(value, receiver, name,
                     StoreWithVectorDescriptor::SlotRegister(),
                     StoreWithVectorDescriptor::VectorRegister(), x5, x6, x7));

  __ Ldr(dictionary, FieldMemOperand(receiver, JSObject::kPropertiesOffset));

  GenerateDictionaryStore(masm, &miss, dictionary, name, value, x6, x7);
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->ic_store_normal_hit(), 1, x6, x7);
  __ Ret();

  // Cache miss: Jump to runtime.
  __ Bind(&miss);
  __ IncrementCounter(counters->ic_store_normal_miss(), 1, x6, x7);
  GenerateMiss(masm);
}


Condition CompareIC::ComputeCondition(Token::Value op) {
  switch (op) {
    case Token::EQ_STRICT:
    case Token::EQ:
      return eq;
    case Token::LT:
      return lt;
    case Token::GT:
      return gt;
    case Token::LTE:
      return le;
    case Token::GTE:
      return ge;
    default:
      UNREACHABLE();
      return al;
  }
}


bool CompareIC::HasInlinedSmiCode(Address address) {
  // The address of the instruction following the call.
  Address info_address = Assembler::return_address_from_call_start(address);

  InstructionSequence* patch_info = InstructionSequence::At(info_address);
  return patch_info->IsInlineData();
}


// Activate a SMI fast-path by patching the instructions generated by
// JumpPatchSite::EmitJumpIf(Not)Smi(), using the information encoded by
// JumpPatchSite::EmitPatchInfo().
void PatchInlinedSmiCode(Isolate* isolate, Address address,
                         InlinedSmiCheck check) {
  // The patch information is encoded in the instruction stream using
  // instructions which have no side effects, so we can safely execute them.
  // The patch information is encoded directly after the call to the helper
  // function which is requesting this patch operation.
  Address info_address = Assembler::return_address_from_call_start(address);
  InlineSmiCheckInfo info(info_address);

  // Check and decode the patch information instruction.
  if (!info.HasSmiCheck()) {
    return;
  }

  if (FLAG_trace_ic) {
    PrintF("[  Patching ic at %p, marker=%p, SMI check=%p\n",
           static_cast<void*>(address), static_cast<void*>(info_address),
           static_cast<void*>(info.SmiCheck()));
  }

  // Patch and activate code generated by JumpPatchSite::EmitJumpIfNotSmi()
  // and JumpPatchSite::EmitJumpIfSmi().
  // Changing
  //   tb(n)z xzr, #0, <target>
  // to
  //   tb(!n)z test_reg, #0, <target>
  Instruction* to_patch = info.SmiCheck();
  PatchingAssembler patcher(isolate, to_patch, 1);
  DCHECK(to_patch->IsTestBranch());
  DCHECK(to_patch->ImmTestBranchBit5() == 0);
  DCHECK(to_patch->ImmTestBranchBit40() == 0);

  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(kSmiTagMask == 1);

  int branch_imm = to_patch->ImmTestBranch();
  Register smi_reg;
  if (check == ENABLE_INLINED_SMI_CHECK) {
    DCHECK(to_patch->Rt() == xzr.code());
    smi_reg = info.SmiRegister();
  } else {
    DCHECK(check == DISABLE_INLINED_SMI_CHECK);
    DCHECK(to_patch->Rt() != xzr.code());
    smi_reg = xzr;
  }

  if (to_patch->Mask(TestBranchMask) == TBZ) {
    // This is JumpIfNotSmi(smi_reg, branch_imm).
    patcher.tbnz(smi_reg, 0, branch_imm);
  } else {
    DCHECK(to_patch->Mask(TestBranchMask) == TBNZ);
    // This is JumpIfSmi(smi_reg, branch_imm).
    patcher.tbz(smi_reg, 0, branch_imm);
  }
}
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM64
