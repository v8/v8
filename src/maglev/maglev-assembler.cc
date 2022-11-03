// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/interface-descriptors-inl.h"
#include "src/maglev/maglev-assembler-inl.h"
#include "src/objects/heap-number.h"

namespace v8 {
namespace internal {
namespace maglev {

#define __ masm->

void MaglevAssembler::Allocate(RegisterSnapshot& register_snapshot,
                               Register object, int size_in_bytes,
                               AllocationType alloc_type,
                               AllocationAlignment alignment) {
  // TODO(victorgomes): Call the runtime for large object allocation.
  // TODO(victorgomes): Support double alignment.
  DCHECK_EQ(alignment, kTaggedAligned);
  size_in_bytes = ALIGN_TO_ALLOCATION_ALIGNMENT(size_in_bytes);
  if (v8_flags.single_generation) {
    alloc_type = AllocationType::kOld;
  }
  bool in_new_space = alloc_type == AllocationType::kYoung;
  ExternalReference top =
      in_new_space
          ? ExternalReference::new_space_allocation_top_address(isolate_)
          : ExternalReference::old_space_allocation_top_address(isolate_);
  ExternalReference limit =
      in_new_space
          ? ExternalReference::new_space_allocation_limit_address(isolate_)
          : ExternalReference::old_space_allocation_limit_address(isolate_);

  ZoneLabelRef done(this);
  Register new_top = kScratchRegister;
  // Check if there is enough space.
  Move(object, ExternalReferenceAsOperand(top));
  leaq(new_top, Operand(object, size_in_bytes));
  cmpq(new_top, ExternalReferenceAsOperand(limit));
  // Otherwise call runtime.
  JumpToDeferredIf(
      greater_equal,
      [](MaglevAssembler* masm, RegisterSnapshot register_snapshot,
         Register object, Builtin builtin, int size_in_bytes,
         ZoneLabelRef done) {
        // Remove {object} from snapshot, since it is the returned allocated
        // HeapObject.
        register_snapshot.live_registers.clear(object);
        register_snapshot.live_tagged_registers.clear(object);
        {
          SaveRegisterStateForCall save_register_state(masm, register_snapshot);
          using D = AllocateDescriptor;
          __ Move(D::GetRegisterParameter(D::kRequestedSize), size_in_bytes);
          __ CallBuiltin(builtin);
          save_register_state.DefineSafepoint();
          __ Move(object, kReturnRegister0);
        }
        __ jmp(*done);
      },
      register_snapshot, object,
      in_new_space ? Builtin::kAllocateRegularInYoungGeneration
                   : Builtin::kAllocateRegularInOldGeneration,
      size_in_bytes, done);
  // Store new top and tag object.
  movq(ExternalReferenceAsOperand(top), new_top);
  addq(object, Immediate(kHeapObjectTag));
  bind(*done);
}

void MaglevAssembler::ToBoolean(Register value, ZoneLabelRef is_true,
                                ZoneLabelRef is_false,
                                bool fallthrough_when_true) {
  Register map = kScratchRegister;

  // Check if {{value}} is Smi.
  CheckSmi(value);
  JumpToDeferredIf(
      zero,
      [](MaglevAssembler* masm, Register value, ZoneLabelRef is_true,
         ZoneLabelRef is_false) {
        // Check if {value} is not zero.
        __ SmiCompare(value, Smi::FromInt(0));
        __ j(equal, *is_false);
        __ jmp(*is_true);
      },
      value, is_true, is_false);

  // Check if {{value}} is false.
  CompareRoot(value, RootIndex::kFalseValue);
  j(equal, *is_false);

  // Check if {{value}} is empty string.
  CompareRoot(value, RootIndex::kempty_string);
  j(equal, *is_false);

  // Check if {{value}} is undetectable.
  LoadMap(map, value);
  testl(FieldOperand(map, Map::kBitFieldOffset),
        Immediate(Map::Bits1::IsUndetectableBit::kMask));
  j(not_zero, *is_false);

  // Check if {{value}} is a HeapNumber.
  CompareRoot(map, RootIndex::kHeapNumberMap);
  JumpToDeferredIf(
      equal,
      [](MaglevAssembler* masm, Register value, ZoneLabelRef is_true,
         ZoneLabelRef is_false) {
        // Sets scratch register to 0.0.
        __ Xorpd(kScratchDoubleReg, kScratchDoubleReg);
        // Sets ZF if equal to 0.0, -0.0 or NaN.
        __ Ucomisd(kScratchDoubleReg,
                   FieldOperand(value, HeapNumber::kValueOffset));
        __ j(zero, *is_false);
        __ jmp(*is_true);
      },
      value, is_true, is_false);

  // Check if {{value}} is a BigInt.
  CompareRoot(map, RootIndex::kBigIntMap);
  JumpToDeferredIf(
      equal,
      [](MaglevAssembler* masm, Register value, ZoneLabelRef is_true,
         ZoneLabelRef is_false) {
        __ testl(FieldOperand(value, BigInt::kBitfieldOffset),
                 Immediate(BigInt::LengthBits::kMask));
        __ j(zero, *is_false);
        __ jmp(*is_true);
      },
      value, is_true, is_false);

  // Otherwise true.
  if (!fallthrough_when_true) {
    jmp(*is_true);
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
