// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_MACHINE_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_MACHINE_LOWERING_REDUCER_H_

#include "src/common/globals.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/globals.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/turboshaft/optimization-phase.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/objects/bigint.h"

namespace v8::internal::compiler::turboshaft {

struct MachineLoweringReducerArgs {
  Factory* factory;
};

// MachineLoweringReducer, formerly known as EffectControlLinearizer, lowers
// simplified operations to machine operations.
template <typename Next>
class MachineLoweringReducer : public Next {
 public:
  using Next::Asm;
  using ArgT =
      base::append_tuple_type<typename Next::ArgT, MachineLoweringReducerArgs>;

  template <typename... Args>
  explicit MachineLoweringReducer(const std::tuple<Args...>& args)
      : Next(args),
        factory_(std::get<MachineLoweringReducerArgs>(args).factory) {}

  OpIndex ReduceCheck(OpIndex input, OpIndex frame_state, CheckOp::Kind kind,
                      FeedbackSource feedback) {
    switch (kind) {
      case CheckOp::Kind::kCheckBigInt: {
        // Check for Smi.
        OpIndex smi_check = Asm().IsSmiTagged(input);
        Asm().DeoptimizeIf(smi_check, frame_state, DeoptimizeReason::kSmi,
                           feedback);

        // Check for BigInt.
        OpIndex value_map = LoadField(input, AccessBuilder::ForMap());
        OpIndex bigint_check = Asm().TaggedEqual(
            value_map, Asm().HeapConstant(factory_->bigint_map()));
        Asm().DeoptimizeIfNot(bigint_check, frame_state,
                              DeoptimizeReason::kWrongInstanceType, feedback);
        return input;
      }
    }

    UNREACHABLE();
  }

  OpIndex ReduceConvertToObject(OpIndex input, ConvertToObjectOp::Kind kind) {
    switch (kind) {
      case ConvertToObjectOp::Kind::kInt64ToBigInt64: {
        DCHECK(Asm().Is64());

        // BigInts with value 0 must be of size 0 (canonical form).
        Block* non_zero_block = Asm().NewBlock();
        Block* zero_block = Asm().NewBlock();
        Block* merge_block = Asm().NewBlock();

        Variable result =
            Asm().NewFreshVariable(RegisterRepresentation::Tagged());

        Asm().Branch(Asm().Word64Equal(input, Asm().Word64Constant(int64_t{0})),
                     zero_block, non_zero_block, BranchHint::kTrue);

        if (Asm().Bind(zero_block)) {
          Asm().Set(result, BuildAllocateBigInt(OpIndex::Invalid(),
                                                OpIndex::Invalid()));
          Asm().Goto(merge_block);
        }

        if (Asm().Bind(non_zero_block)) {
          // Shift sign bit into BigInt's sign bit position.
          OpIndex bitfield = Asm().Word32BitwiseOr(
              Asm().Word32Constant(BigInt::LengthBits::encode(1)),
              Asm().Word64ShiftRightLogical(
                  input, Asm().Word64Constant(static_cast<int64_t>(
                             63 - BigInt::SignBits::kShift))));

          // We use (value XOR (value >> 63)) - (value >> 63) to compute the
          // absolute value, in a branchless fashion.
          OpIndex sign_mask = Asm().Word64ShiftRightArithmetic(
              input, Asm().Word64Constant(int64_t{63}));
          OpIndex absolute_value = Asm().Word64Sub(
              Asm().Word64BitwiseXor(input, sign_mask), sign_mask);
          Asm().Set(result, BuildAllocateBigInt(bitfield, absolute_value));
          Asm().Goto(merge_block);
        }

        Asm().BindReachable(merge_block);
        return Asm().Get(result);
      }
    }

    UNREACHABLE();
  }

 private:
  // TODO(nicohartmann@): Might move some of those helpers into the assembler
  // interface.
  OpIndex LoadField(OpIndex object, const FieldAccess& access) {
    MachineType machine_type = access.machine_type;
    if (machine_type.IsMapWord()) {
      machine_type = MachineType::TaggedPointer();
#ifdef V8_MAP_PACKING
      UNIMPLEMENTED();
#endif
    }
    MemoryRepresentation rep =
        MemoryRepresentation::FromMachineType(machine_type);
#ifdef V8_ENABLE_SANDBOX
    bool is_sandboxed_external =
        access.type.Is(compiler::Type::ExternalPointer());
    if (is_sandboxed_external) {
      // Fields for sandboxed external pointer contain a 32-bit handle, not a
      // 64-bit raw pointer.
      rep = MemoryRepresentation::Uint32();
    }
#endif  // V8_ENABLE_SANDBOX
    OpIndex value =
        Asm().Load(object, LoadOp::Kind::Aligned(access.base_is_tagged), rep,
                   access.offset);
#ifdef V8_ENABLE_SANDBOX
    if (is_sandboxed_external) {
      value = Asm().DecodeExternalPointer(value, access.external_pointer_tag);
    }
    if (access.is_bounded_size_access) {
      DCHECK(!is_sandboxed_external);
      value = Asm().ShiftRightLogical(value, kBoundedSizeShift,
                                      WordRepresentation::PointerSized());
    }
#endif  // V8_ENABLE_SANDBOX
    return value;
  }

  void StoreField(OpIndex object, const FieldAccess& access, OpIndex value) {
    // External pointer must never be stored by optimized code.
    DCHECK(!access.type.Is(compiler::Type::ExternalPointer()) ||
           !V8_ENABLE_SANDBOX_BOOL);
    // SandboxedPointers are not currently stored by optimized code.
    DCHECK(!access.type.Is(compiler::Type::SandboxedPointer()));

#ifdef V8_ENABLE_SANDBOX
    if (access.is_bounded_size_access) {
      value = Asm().ShiftLeft(value, kBoundedSizeShift,
                              WordRepresentation::PointerSized());
    }
#endif  // V8_ENABLE_SANDBOX

    StoreOp::Kind kind = StoreOp::Kind::Aligned(access.base_is_tagged);
    MachineType machine_type = access.machine_type;
    if (machine_type.IsMapWord()) {
      machine_type = MachineType::TaggedPointer();
#ifdef V8_MAP_PACKING
      UNIMPLEMENTED();
#endif
    }
    MemoryRepresentation rep =
        MemoryRepresentation::FromMachineType(machine_type);
    Asm().Store(object, value, kind, rep, access.write_barrier_kind,
                access.offset);
  }

  // Pass {bitfield} = {digit} = OpIndex::Invalid() to construct the canonical
  // 0n BigInt.
  OpIndex BuildAllocateBigInt(OpIndex bitfield, OpIndex digit) {
    DCHECK(Asm().Is64());
    DCHECK_EQ(bitfield.valid(), digit.valid());
    static constexpr auto zero_bitfield =
        BigInt::SignBits::update(BigInt::LengthBits::encode(0), false);

    OpIndex map = Asm().HeapConstant(factory_->bigint_map());
    OpIndex bigint = Asm().Allocate(
        Asm().IntPtrConstant(BigInt::SizeFor(digit.valid() ? 1 : 0)),
        AllocationType::kYoung, AllowLargeObjects::kFalse);
    StoreField(bigint, AccessBuilder::ForMap(), map);
    StoreField(
        bigint, AccessBuilder::ForBigIntBitfield(),
        bitfield.valid() ? bitfield : Asm().Word32Constant(zero_bitfield));

    // BigInts have no padding on 64 bit architectures with pointer compression.
    if (BigInt::HasOptionalPadding()) {
      StoreField(bigint, AccessBuilder::ForBigIntOptionalPadding(),
                 Asm().IntPtrConstant(0));
    }
    if (digit.valid()) {
      StoreField(bigint, AccessBuilder::ForBigIntLeastSignificantDigit64(),
                 digit);
    }
    return bigint;
  }

  Factory* factory_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_MACHINE_LOWERING_REDUCER_H_
