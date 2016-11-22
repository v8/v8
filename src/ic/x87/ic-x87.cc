// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_X87

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

// Helper function used to load a property from a dictionary backing
// storage. This function may fail to load a property even though it is
// in the dictionary, so code at miss_label must always call a backup
// property load that is complete. This function is safe to call if
// name is not internalized, and will jump to the miss_label in that
// case. The generated code assumes that the receiver has slow
// properties, is not a global object and does not have interceptors.
static void GenerateDictionaryLoad(MacroAssembler* masm, Label* miss_label,
                                   Register elements, Register name,
                                   Register r0, Register r1, Register result) {
  // Register use:
  //
  // elements - holds the property dictionary on entry and is unchanged.
  //
  // name - holds the name of the property on entry and is unchanged.
  //
  // Scratch registers:
  //
  // r0   - used for the index into the property dictionary
  //
  // r1   - used to hold the capacity of the property dictionary.
  //
  // result - holds the result on exit.

  Label done;

  // Probe the dictionary.
  NameDictionaryLookupStub::GeneratePositiveLookup(masm, miss_label, &done,
                                                   elements, name, r0, r1);

  // If probing finds an entry in the dictionary, r0 contains the
  // index into the dictionary. Check that the value is a normal
  // property.
  __ bind(&done);
  const int kElementsStartOffset =
      NameDictionary::kHeaderSize +
      NameDictionary::kElementsStartIndex * kPointerSize;
  const int kDetailsOffset = kElementsStartOffset + 2 * kPointerSize;
  __ test(Operand(elements, r0, times_4, kDetailsOffset - kHeapObjectTag),
          Immediate(PropertyDetails::TypeField::kMask << kSmiTagSize));
  __ j(not_zero, miss_label);

  // Get the value at the masked, scaled index.
  const int kValueOffset = kElementsStartOffset + kPointerSize;
  __ mov(result, Operand(elements, r0, times_4, kValueOffset - kHeapObjectTag));
}


// Helper function used to store a property to a dictionary backing
// storage. This function may fail to store a property eventhough it
// is in the dictionary, so code at miss_label must always call a
// backup property store that is complete. This function is safe to
// call if name is not internalized, and will jump to the miss_label in
// that case. The generated code assumes that the receiver has slow
// properties, is not a global object and does not have interceptors.
static void GenerateDictionaryStore(MacroAssembler* masm, Label* miss_label,
                                    Register elements, Register name,
                                    Register value, Register r0, Register r1) {
  // Register use:
  //
  // elements - holds the property dictionary on entry and is clobbered.
  //
  // name - holds the name of the property on entry and is unchanged.
  //
  // value - holds the value to store and is unchanged.
  //
  // r0 - used for index into the property dictionary and is clobbered.
  //
  // r1 - used to hold the capacity of the property dictionary and is clobbered.
  Label done;


  // Probe the dictionary.
  NameDictionaryLookupStub::GeneratePositiveLookup(masm, miss_label, &done,
                                                   elements, name, r0, r1);

  // If probing finds an entry in the dictionary, r0 contains the
  // index into the dictionary. Check that the value is a normal
  // property that is not read only.
  __ bind(&done);
  const int kElementsStartOffset =
      NameDictionary::kHeaderSize +
      NameDictionary::kElementsStartIndex * kPointerSize;
  const int kDetailsOffset = kElementsStartOffset + 2 * kPointerSize;
  const int kTypeAndReadOnlyMask =
      (PropertyDetails::TypeField::kMask |
       PropertyDetails::AttributesField::encode(READ_ONLY))
      << kSmiTagSize;
  __ test(Operand(elements, r0, times_4, kDetailsOffset - kHeapObjectTag),
          Immediate(kTypeAndReadOnlyMask));
  __ j(not_zero, miss_label);

  // Store the value at the masked, scaled index.
  const int kValueOffset = kElementsStartOffset + kPointerSize;
  __ lea(r0, Operand(elements, r0, times_4, kValueOffset - kHeapObjectTag));
  __ mov(Operand(r0, 0), value);

  // Update write barrier. Make sure not to clobber the value.
  __ mov(r1, value);
  __ RecordWrite(elements, r0, r1, kDontSaveFPRegs);
}

void LoadIC::GenerateNormal(MacroAssembler* masm) {
  Register dictionary = eax;
  DCHECK(!dictionary.is(LoadDescriptor::ReceiverRegister()));
  DCHECK(!dictionary.is(LoadDescriptor::NameRegister()));

  Label slow;

  __ mov(dictionary, FieldOperand(LoadDescriptor::ReceiverRegister(),
                                  JSObject::kPropertiesOffset));
  GenerateDictionaryLoad(masm, &slow, dictionary,
                         LoadDescriptor::NameRegister(), edi, ebx, eax);
  __ ret(0);

  // Dictionary load failed, go slow (but don't miss).
  __ bind(&slow);
  GenerateRuntimeGetProperty(masm);
}


static void LoadIC_PushArgs(MacroAssembler* masm) {
  Register receiver = LoadDescriptor::ReceiverRegister();
  Register name = LoadDescriptor::NameRegister();

  Register slot = LoadDescriptor::SlotRegister();
  Register vector = LoadWithVectorDescriptor::VectorRegister();
  DCHECK(!edi.is(receiver) && !edi.is(name) && !edi.is(slot) &&
         !edi.is(vector));

  __ pop(edi);
  __ push(receiver);
  __ push(name);
  __ push(slot);
  __ push(vector);
  __ push(edi);
}


void LoadIC::GenerateMiss(MacroAssembler* masm) {
  // Return address is on the stack.
  __ IncrementCounter(masm->isolate()->counters()->ic_load_miss(), 1);
  LoadIC_PushArgs(masm);

  // Perform tail call to the entry.
  __ TailCallRuntime(Runtime::kLoadIC_Miss);
}

void LoadIC::GenerateRuntimeGetProperty(MacroAssembler* masm) {
  // Return address is on the stack.
  Register receiver = LoadDescriptor::ReceiverRegister();
  Register name = LoadDescriptor::NameRegister();
  DCHECK(!ebx.is(receiver) && !ebx.is(name));

  __ pop(ebx);
  __ push(receiver);
  __ push(name);
  __ push(ebx);

  // Do tail-call to runtime routine.
  __ TailCallRuntime(Runtime::kGetProperty);
}


void KeyedLoadIC::GenerateMiss(MacroAssembler* masm) {
  // Return address is on the stack.
  __ IncrementCounter(masm->isolate()->counters()->ic_keyed_load_miss(), 1);

  LoadIC_PushArgs(masm);

  // Perform tail call to the entry.
  __ TailCallRuntime(Runtime::kKeyedLoadIC_Miss);
}

void KeyedLoadIC::GenerateRuntimeGetProperty(MacroAssembler* masm) {
  // Return address is on the stack.
  Register receiver = LoadDescriptor::ReceiverRegister();
  Register name = LoadDescriptor::NameRegister();
  DCHECK(!ebx.is(receiver) && !ebx.is(name));

  __ pop(ebx);
  __ push(receiver);
  __ push(name);
  __ push(ebx);

  // Do tail-call to runtime routine.
  __ TailCallRuntime(Runtime::kKeyedGetProperty);
}

static void StoreIC_PushArgs(MacroAssembler* masm) {
  Register receiver = StoreWithVectorDescriptor::ReceiverRegister();
  Register name = StoreWithVectorDescriptor::NameRegister();

  STATIC_ASSERT(StoreWithVectorDescriptor::kStackArgumentsCount == 3);
  // Current stack layout:
  // - esp[12]   -- value
  // - esp[8]    -- slot
  // - esp[4]    -- vector
  // - esp[0]    -- return address

  Register return_address = StoreWithVectorDescriptor::SlotRegister();
  __ pop(return_address);
  __ push(receiver);
  __ push(name);
  __ push(return_address);
}


void StoreIC::GenerateMiss(MacroAssembler* masm) {
  // Return address is on the stack.
  StoreIC_PushArgs(masm);

  // Perform tail call to the entry.
  __ TailCallRuntime(Runtime::kStoreIC_Miss);
}


void StoreIC::GenerateNormal(MacroAssembler* masm) {
  typedef StoreWithVectorDescriptor Descriptor;
  Label restore_miss;
  Register receiver = Descriptor::ReceiverRegister();
  Register name = Descriptor::NameRegister();
  Register value = Descriptor::ValueRegister();
  // Since the slot and vector values are passed on the stack we can use
  // respective registers as scratch registers.
  Register scratch1 = Descriptor::VectorRegister();
  Register scratch2 = Descriptor::SlotRegister();

  __ LoadParameterFromStack<Descriptor>(value, Descriptor::kValue);

  // A lot of registers are needed for storing to slow case objects.
  // Push and restore receiver but rely on GenerateDictionaryStore preserving
  // the value and name.
  __ push(receiver);

  Register dictionary = receiver;
  __ mov(dictionary, FieldOperand(receiver, JSObject::kPropertiesOffset));
  GenerateDictionaryStore(masm, &restore_miss, dictionary, name, value,
                          scratch1, scratch2);
  __ Drop(1);
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->ic_store_normal_hit(), 1);
  __ ret(Descriptor::kStackArgumentsCount * kPointerSize);

  __ bind(&restore_miss);
  __ pop(receiver);
  __ IncrementCounter(counters->ic_store_normal_miss(), 1);
  GenerateMiss(masm);
}


void KeyedStoreIC::GenerateMiss(MacroAssembler* masm) {
  // Return address is on the stack.
  StoreIC_PushArgs(masm);

  // Do tail-call to runtime routine.
  __ TailCallRuntime(Runtime::kKeyedStoreIC_Miss);
}

void KeyedStoreIC::GenerateSlow(MacroAssembler* masm) {
  // Return address is on the stack.
  StoreIC_PushArgs(masm);

  // Do tail-call to runtime routine.
  __ TailCallRuntime(Runtime::kKeyedStoreIC_Slow);
}

#undef __


Condition CompareIC::ComputeCondition(Token::Value op) {
  switch (op) {
    case Token::EQ_STRICT:
    case Token::EQ:
      return equal;
    case Token::LT:
      return less;
    case Token::GT:
      return greater;
    case Token::LTE:
      return less_equal;
    case Token::GTE:
      return greater_equal;
    default:
      UNREACHABLE();
      return no_condition;
  }
}


bool CompareIC::HasInlinedSmiCode(Address address) {
  // The address of the instruction following the call.
  Address test_instruction_address =
      address + Assembler::kCallTargetAddressOffset;

  // If the instruction following the call is not a test al, nothing
  // was inlined.
  return *test_instruction_address == Assembler::kTestAlByte;
}


void PatchInlinedSmiCode(Isolate* isolate, Address address,
                         InlinedSmiCheck check) {
  // The address of the instruction following the call.
  Address test_instruction_address =
      address + Assembler::kCallTargetAddressOffset;

  // If the instruction following the call is not a test al, nothing
  // was inlined.
  if (*test_instruction_address != Assembler::kTestAlByte) {
    DCHECK(*test_instruction_address == Assembler::kNopByte);
    return;
  }

  Address delta_address = test_instruction_address + 1;
  // The delta to the start of the map check instruction and the
  // condition code uses at the patched jump.
  uint8_t delta = *reinterpret_cast<uint8_t*>(delta_address);
  if (FLAG_trace_ic) {
    PrintF("[  patching ic at %p, test=%p, delta=%d\n",
           static_cast<void*>(address),
           static_cast<void*>(test_instruction_address), delta);
  }

  // Patch with a short conditional jump. Enabling means switching from a short
  // jump-if-carry/not-carry to jump-if-zero/not-zero, whereas disabling is the
  // reverse operation of that.
  Address jmp_address = test_instruction_address - delta;
  DCHECK((check == ENABLE_INLINED_SMI_CHECK)
             ? (*jmp_address == Assembler::kJncShortOpcode ||
                *jmp_address == Assembler::kJcShortOpcode)
             : (*jmp_address == Assembler::kJnzShortOpcode ||
                *jmp_address == Assembler::kJzShortOpcode));
  Condition cc =
      (check == ENABLE_INLINED_SMI_CHECK)
          ? (*jmp_address == Assembler::kJncShortOpcode ? not_zero : zero)
          : (*jmp_address == Assembler::kJnzShortOpcode ? not_carry : carry);
  *jmp_address = static_cast<byte>(Assembler::kJccShortPrefix | cc);
}
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_X87
