// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_MIPS64

#include "src/codegen.h"
#include "src/ic/ic.h"
#include "src/ic/ic-compiler.h"
#include "src/ic/stub-cache.h"

namespace v8 {
namespace internal {


// ----------------------------------------------------------------------------
// Static IC stub generators.
//

#define __ ACCESS_MASM(masm)

// Helper function used from LoadIC GenerateNormal.
//
// elements: Property dictionary. It is not clobbered if a jump to the miss
//           label is done.
// name:     Property name. It is not clobbered if a jump to the miss label is
//           done
// result:   Register for the result. It is only updated if a jump to the miss
//           label is not done. Can be the same as elements or name clobbering
//           one of these in the case of not jumping to the miss label.
// The two scratch registers need to be different from elements, name and
// result.
// The generated code assumes that the receiver has slow properties,
// is not a global object and does not have interceptors.
// The address returned from GenerateStringDictionaryProbes() in scratch2
// is used.
static void GenerateDictionaryLoad(MacroAssembler* masm, Label* miss,
                                   Register elements, Register name,
                                   Register result, Register scratch1,
                                   Register scratch2) {
  // Main use of the scratch registers.
  // scratch1: Used as temporary and to hold the capacity of the property
  //           dictionary.
  // scratch2: Used as temporary.
  Label done;

  // Probe the dictionary.
  NameDictionaryLookupStub::GeneratePositiveLookup(masm, miss, &done, elements,
                                                   name, scratch1, scratch2);

  // If probing finds an entry check that the value is a normal
  // property.
  __ bind(&done);  // scratch2 == elements + 4 * index.
  const int kElementsStartOffset =
      NameDictionary::kHeaderSize +
      NameDictionary::kElementsStartIndex * kPointerSize;
  const int kDetailsOffset = kElementsStartOffset + 2 * kPointerSize;
  __ ld(scratch1, FieldMemOperand(scratch2, kDetailsOffset));
  __ And(at, scratch1,
         Operand(Smi::FromInt(PropertyDetails::TypeField::kMask)));
  __ Branch(miss, ne, at, Operand(zero_reg));

  // Get the value at the masked, scaled index and return.
  __ ld(result,
        FieldMemOperand(scratch2, kElementsStartOffset + 1 * kPointerSize));
}


// Helper function used from StoreIC::GenerateNormal.
//
// elements: Property dictionary. It is not clobbered if a jump to the miss
//           label is done.
// name:     Property name. It is not clobbered if a jump to the miss label is
//           done
// value:    The value to store.
// The two scratch registers need to be different from elements, name and
// result.
// The generated code assumes that the receiver has slow properties,
// is not a global object and does not have interceptors.
// The address returned from GenerateStringDictionaryProbes() in scratch2
// is used.
static void GenerateDictionaryStore(MacroAssembler* masm, Label* miss,
                                    Register elements, Register name,
                                    Register value, Register scratch1,
                                    Register scratch2) {
  // Main use of the scratch registers.
  // scratch1: Used as temporary and to hold the capacity of the property
  //           dictionary.
  // scratch2: Used as temporary.
  Label done;

  // Probe the dictionary.
  NameDictionaryLookupStub::GeneratePositiveLookup(masm, miss, &done, elements,
                                                   name, scratch1, scratch2);

  // If probing finds an entry in the dictionary check that the value
  // is a normal property that is not read only.
  __ bind(&done);  // scratch2 == elements + 4 * index.
  const int kElementsStartOffset =
      NameDictionary::kHeaderSize +
      NameDictionary::kElementsStartIndex * kPointerSize;
  const int kDetailsOffset = kElementsStartOffset + 2 * kPointerSize;
  const int kTypeAndReadOnlyMask =
      (PropertyDetails::TypeField::kMask |
       PropertyDetails::AttributesField::encode(READ_ONLY));
  __ ld(scratch1, FieldMemOperand(scratch2, kDetailsOffset));
  __ And(at, scratch1, Operand(Smi::FromInt(kTypeAndReadOnlyMask)));
  __ Branch(miss, ne, at, Operand(zero_reg));

  // Store the value at the masked, scaled index and return.
  const int kValueOffset = kElementsStartOffset + kPointerSize;
  __ Daddu(scratch2, scratch2, Operand(kValueOffset - kHeapObjectTag));
  __ sd(value, MemOperand(scratch2));

  // Update the write barrier. Make sure not to clobber the value.
  __ mov(scratch1, value);
  __ RecordWrite(elements, scratch2, scratch1, kRAHasNotBeenSaved,
                 kDontSaveFPRegs);
}

void LoadIC::GenerateNormal(MacroAssembler* masm) {
  Register dictionary = a0;
  DCHECK(!dictionary.is(LoadDescriptor::ReceiverRegister()));
  DCHECK(!dictionary.is(LoadDescriptor::NameRegister()));
  Label slow;

  __ ld(dictionary, FieldMemOperand(LoadDescriptor::ReceiverRegister(),
                                    JSObject::kPropertiesOffset));
  GenerateDictionaryLoad(masm, &slow, dictionary,
                         LoadDescriptor::NameRegister(), v0, a3, a4);
  __ Ret();

  // Dictionary load failed, go slow (but don't miss).
  __ bind(&slow);
  GenerateRuntimeGetProperty(masm);
}


// A register that isn't one of the parameters to the load ic.
static const Register LoadIC_TempRegister() { return a3; }


static void LoadIC_PushArgs(MacroAssembler* masm) {
  Register receiver = LoadDescriptor::ReceiverRegister();
  Register name = LoadDescriptor::NameRegister();
  Register slot = LoadDescriptor::SlotRegister();
  Register vector = LoadWithVectorDescriptor::VectorRegister();

  __ Push(receiver, name, slot, vector);
}


void LoadIC::GenerateMiss(MacroAssembler* masm) {
  // The return address is on the stack.
  Isolate* isolate = masm->isolate();

  DCHECK(!AreAliased(a4, a5, LoadWithVectorDescriptor::SlotRegister(),
                     LoadWithVectorDescriptor::VectorRegister()));
  __ IncrementCounter(isolate->counters()->ic_load_miss(), 1, a4, a5);

  LoadIC_PushArgs(masm);

  // Perform tail call to the entry.
  __ TailCallRuntime(Runtime::kLoadIC_Miss);
}

void LoadIC::GenerateRuntimeGetProperty(MacroAssembler* masm) {
  // The return address is in ra.

  __ mov(LoadIC_TempRegister(), LoadDescriptor::ReceiverRegister());
  __ Push(LoadIC_TempRegister(), LoadDescriptor::NameRegister());

  // Do tail-call to runtime routine.
  __ TailCallRuntime(Runtime::kGetProperty);
}


void KeyedLoadIC::GenerateMiss(MacroAssembler* masm) {
  // The return address is in ra.
  Isolate* isolate = masm->isolate();

  DCHECK(!AreAliased(a4, a5, LoadWithVectorDescriptor::SlotRegister(),
                     LoadWithVectorDescriptor::VectorRegister()));
  __ IncrementCounter(isolate->counters()->ic_keyed_load_miss(), 1, a4, a5);

  LoadIC_PushArgs(masm);

  // Perform tail call to the entry.
  __ TailCallRuntime(Runtime::kKeyedLoadIC_Miss);
}

void KeyedLoadIC::GenerateRuntimeGetProperty(MacroAssembler* masm) {
  // The return address is in ra.

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
  StoreIC_PushArgs(masm);

  __ TailCallRuntime(Runtime::kKeyedStoreIC_Miss);
}

void KeyedStoreIC::GenerateSlow(MacroAssembler* masm) {
  StoreIC_PushArgs(masm);

  // The slow case calls into the runtime to complete the store without causing
  // an IC miss that would otherwise cause a transition to the generic stub.
  __ TailCallRuntime(Runtime::kKeyedStoreIC_Slow);
}

void StoreIC::GenerateMiss(MacroAssembler* masm) {
  StoreIC_PushArgs(masm);

  // Perform tail call to the entry.
  __ TailCallRuntime(Runtime::kStoreIC_Miss);
}


void StoreIC::GenerateNormal(MacroAssembler* masm) {
  Label miss;
  Register receiver = StoreDescriptor::ReceiverRegister();
  Register name = StoreDescriptor::NameRegister();
  Register value = StoreDescriptor::ValueRegister();
  Register dictionary = a5;
  DCHECK(!AreAliased(
      value, receiver, name, StoreWithVectorDescriptor::VectorRegister(),
      StoreWithVectorDescriptor::SlotRegister(), dictionary, a6, a7));

  __ ld(dictionary, FieldMemOperand(receiver, JSObject::kPropertiesOffset));

  GenerateDictionaryStore(masm, &miss, dictionary, name, value, a6, a7);
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->ic_store_normal_hit(), 1, a6, a7);
  __ Ret(USE_DELAY_SLOT);
  __ Move(v0, value);  // Ensure the stub returns correct value.

  __ bind(&miss);
  __ IncrementCounter(counters->ic_store_normal_miss(), 1, a6, a7);
  GenerateMiss(masm);
}


#undef __


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
      return kNoCondition;
  }
}


bool CompareIC::HasInlinedSmiCode(Address address) {
  // The address of the instruction following the call.
  Address andi_instruction_address =
      address + Assembler::kCallTargetAddressOffset;

  // If the instruction following the call is not a andi at, rx, #yyy, nothing
  // was inlined.
  Instr instr = Assembler::instr_at(andi_instruction_address);
  return Assembler::IsAndImmediate(instr) &&
         Assembler::GetRt(instr) == static_cast<uint32_t>(zero_reg.code());
}


void PatchInlinedSmiCode(Isolate* isolate, Address address,
                         InlinedSmiCheck check) {
  Address andi_instruction_address =
      address + Assembler::kCallTargetAddressOffset;

  // If the instruction following the call is not a andi at, rx, #yyy, nothing
  // was inlined.
  Instr instr = Assembler::instr_at(andi_instruction_address);
  if (!(Assembler::IsAndImmediate(instr) &&
        Assembler::GetRt(instr) == static_cast<uint32_t>(zero_reg.code()))) {
    return;
  }

  // The delta to the start of the map check instruction and the
  // condition code uses at the patched jump.
  int delta = Assembler::GetImmediate16(instr);
  delta += Assembler::GetRs(instr) * kImm16Mask;
  // If the delta is 0 the instruction is andi at, zero_reg, #0 which also
  // signals that nothing was inlined.
  if (delta == 0) {
    return;
  }

  if (FLAG_trace_ic) {
    PrintF("[  patching ic at %p, andi=%p, delta=%d\n",
           static_cast<void*>(address),
           static_cast<void*>(andi_instruction_address), delta);
  }

  Address patch_address =
      andi_instruction_address - delta * Instruction::kInstrSize;
  Instr instr_at_patch = Assembler::instr_at(patch_address);
  // This is patching a conditional "jump if not smi/jump if smi" site.
  // Enabling by changing from
  //   andi at, rx, 0
  //   Branch <target>, eq, at, Operand(zero_reg)
  // to:
  //   andi at, rx, #kSmiTagMask
  //   Branch <target>, ne, at, Operand(zero_reg)
  // and vice-versa to be disabled again.
  CodePatcher patcher(isolate, patch_address, 2);
  Register reg = Register::from_code(Assembler::GetRs(instr_at_patch));
  if (check == ENABLE_INLINED_SMI_CHECK) {
    DCHECK(Assembler::IsAndImmediate(instr_at_patch));
    DCHECK_EQ(0u, Assembler::GetImmediate16(instr_at_patch));
    patcher.masm()->andi(at, reg, kSmiTagMask);
  } else {
    DCHECK_EQ(check, DISABLE_INLINED_SMI_CHECK);
    DCHECK(Assembler::IsAndImmediate(instr_at_patch));
    patcher.masm()->andi(at, reg, 0);
  }
  Instr branch_instr =
      Assembler::instr_at(patch_address + Instruction::kInstrSize);
  DCHECK(Assembler::IsBranch(branch_instr));

  uint32_t opcode = Assembler::GetOpcodeField(branch_instr);
  // Currently only the 'eq' and 'ne' cond values are supported and the simple
  // branch instructions and their r6 variants (with opcode being the branch
  // type). There are some special cases (see Assembler::IsBranch()) so
  // extending this would be tricky.
  DCHECK(opcode == BEQ ||    // BEQ
         opcode == BNE ||    // BNE
         opcode == POP10 ||  // BEQC
         opcode == POP30 ||  // BNEC
         opcode == POP66 ||  // BEQZC
         opcode == POP76);   // BNEZC
  switch (opcode) {
    case BEQ:
      opcode = BNE;  // change BEQ to BNE.
      break;
    case POP10:
      opcode = POP30;  // change BEQC to BNEC.
      break;
    case POP66:
      opcode = POP76;  // change BEQZC to BNEZC.
      break;
    case BNE:
      opcode = BEQ;  // change BNE to BEQ.
      break;
    case POP30:
      opcode = POP10;  // change BNEC to BEQC.
      break;
    case POP76:
      opcode = POP66;  // change BNEZC to BEQZC.
      break;
    default:
      UNIMPLEMENTED();
  }
  patcher.ChangeBranchCondition(branch_instr, opcode);
}
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_MIPS64
