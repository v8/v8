// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_MACHINE_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_MACHINE_LOWERING_REDUCER_H_

#include "src/common/globals.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/globals.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/optimization-phase.h"
#include "src/compiler/turboshaft/reducer-traits.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/objects/bigint.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

struct MachineLoweringReducerArgs {
  Factory* factory;
};

// MachineLoweringReducer, formerly known as EffectControlLinearizer, lowers
// simplified operations to machine operations.
template <typename Next>
class MachineLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

  using ArgT =
      base::append_tuple_type<typename Next::ArgT, MachineLoweringReducerArgs>;

  template <typename... Args>
  explicit MachineLoweringReducer(const std::tuple<Args...>& args)
      : Next(args),
        factory_(std::get<MachineLoweringReducerArgs>(args).factory) {}

  bool NeedsHeapObjectCheck(IsObjectOp::InputAssumptions input_assumptions) {
    // TODO(nicohartmann@): Consider type information once we have that.
    switch (input_assumptions) {
      case IsObjectOp::InputAssumptions::kNone:
        return true;
      case IsObjectOp::InputAssumptions::kHeapObject:
      case IsObjectOp::InputAssumptions::kBigInt:
        return false;
    }
  }

  V<Word32> ReduceIsObject(V<Tagged> input, IsObjectOp::Kind kind,
                           IsObjectOp::InputAssumptions input_assumptions) {
    switch (kind) {
      case IsObjectOp::Kind::kBigInt:
      case IsObjectOp::Kind::kBigInt64: {
        DCHECK_IMPLIES(kind == IsObjectOp::Kind::kBigInt64, __ Is64());

        Label<Word32> done(this);

        if (input_assumptions != IsObjectOp::InputAssumptions::kBigInt) {
          if (NeedsHeapObjectCheck(input_assumptions)) {
            // Check for Smi.
            GOTO_IF(__ IsSmiTagged(input), done, __ Word32Constant(0));
          }

          // Check for BigInt.
          V<Tagged> map = LoadField<Tagged>(input, AccessBuilder::ForMap());
          V<Word32> is_bigint_map =
              __ TaggedEqual(map, __ HeapConstant(factory_->bigint_map()));
          GOTO_IF_NOT(is_bigint_map, done, __ Word32Constant(0));
        }

        if (kind == IsObjectOp::Kind::kBigInt) {
          GOTO(done, __ Word32Constant(1));
        } else {
          DCHECK_EQ(kind, IsObjectOp::Kind::kBigInt64);
          // We have to perform check for BigInt64 range.
          V<Word32> bitfield =
              LoadField<Word32>(input, AccessBuilder::ForBigIntBitfield());
          GOTO_IF(__ Word32Equal(bitfield, __ Word32Constant(0)), done,
                  __ Word32Constant(1));

          // Length must be 1.
          V<Word32> length_field = __ Word32BitwiseAnd(
              bitfield, __ Word32Constant(BigInt::LengthBits::kMask));
          GOTO_IF_NOT(
              __ Word32Equal(
                  length_field,
                  __ Word32Constant(uint32_t{1} << BigInt::LengthBits::kShift)),
              done, __ Word32Constant(0));

          // Check if it fits in 64 bit signed int.
          V<Word64> lsd = LoadField<Word64>(
              input, AccessBuilder::ForBigIntLeastSignificantDigit64());
          V<Word32> magnitude_check = __ Uint64LessThanOrEqual(
              lsd, __ Word64Constant(std::numeric_limits<int64_t>::max()));
          GOTO_IF(magnitude_check, done, __ Word32Constant(1));

          // The BigInt probably doesn't fit into signed int64. The only
          // exception is int64_t::min. We check for this.
          V<Word32> sign = __ Word32BitwiseAnd(
              bitfield, __ Word32Constant(BigInt::SignBits::kMask));
          V<Word32> sign_check =
              __ Word32Equal(sign, __ Word32Constant(BigInt::SignBits::kMask));
          GOTO_IF_NOT(sign_check, done, __ Word32Constant(0));

          V<Word32> min_check = __ Word64Equal(
              lsd, __ Word64Constant(std::numeric_limits<int64_t>::min()));
          GOTO_IF(min_check, done, __ Word32Constant(1));

          GOTO(done, __ Word32Constant(0));
        }

        BIND(done, result);
        return result;
      }
    }

    UNREACHABLE();
  }

  OpIndex ReduceConvertToObject(OpIndex input, ConvertToObjectOp::Kind kind) {
    switch (kind) {
      case ConvertToObjectOp::Kind::kInt64ToBigInt64: {
        DCHECK(__ Is64());

        Label<Tagged> done(this);

        // BigInts with value 0 must be of size 0 (canonical form).
        IF(__ Word64Equal(input, __ Word64Constant(int64_t{0}))) {
          GOTO(done, AllocateBigInt(OpIndex::Invalid(), OpIndex::Invalid()));
        }
        ELSE {
          // Shift sign bit into BigInt's sign bit position.
          V<Word32> bitfield = __ Word32BitwiseOr(
              __ Word32Constant(BigInt::LengthBits::encode(1)),
              __ Word64ShiftRightLogical(input,
                                         __ Word64Constant(static_cast<int64_t>(
                                             63 - BigInt::SignBits::kShift))));

          // We use (value XOR (value >> 63)) - (value >> 63) to compute the
          // absolute value, in a branchless fashion.
          V<Word64> sign_mask = __ Word64ShiftRightArithmetic(
              input, __ Word64Constant(int64_t{63}));
          V<Word64> absolute_value =
              __ Word64Sub(__ Word64BitwiseXor(input, sign_mask), sign_mask);
          GOTO(done, AllocateBigInt(bitfield, absolute_value));
        }
        END_IF

        BIND(done, result);
        return result;
      }
      case ConvertToObjectOp::Kind::kUint64ToBigInt64: {
        DCHECK(__ Is64());

        Label<Tagged> done(this);

        // BigInts with value 0 must be of size 0 (canonical form).
        IF(__ Word64Equal(input, __ Word64Constant(uint64_t{0}))) {
          GOTO(done, AllocateBigInt(OpIndex::Invalid(), OpIndex::Invalid()));
        }
        ELSE {
          const auto bitfield = BigInt::LengthBits::encode(1);
          GOTO(done, AllocateBigInt(__ Word32Constant(bitfield), input));
        }
        END_IF

        BIND(done, result);
        return result;
      }
    }

    UNREACHABLE();
  }

 private:
  // TODO(nicohartmann@): Might move some of those helpers into the assembler
  // interface.
  template <typename Rep = Any>
  V<Rep> LoadField(V<Tagged> object, const FieldAccess& access) {
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
    V<Rep> value = __ Load(object, LoadOp::Kind::Aligned(access.base_is_tagged),
                           rep, access.offset);
#ifdef V8_ENABLE_SANDBOX
    if (is_sandboxed_external) {
      value = __ DecodeExternalPointer(value, access.external_pointer_tag);
    }
    if (access.is_bounded_size_access) {
      DCHECK(!is_sandboxed_external);
      value = __ ShiftRightLogical(value, kBoundedSizeShift,
                                   WordRepresentation::PointerSized());
    }
#endif  // V8_ENABLE_SANDBOX
    return value;
  }

  void StoreField(V<Tagged> object, const FieldAccess& access, V<Any> value) {
    // External pointer must never be stored by optimized code.
    DCHECK(!access.type.Is(compiler::Type::ExternalPointer()) ||
           !V8_ENABLE_SANDBOX_BOOL);
    // SandboxedPointers are not currently stored by optimized code.
    DCHECK(!access.type.Is(compiler::Type::SandboxedPointer()));

#ifdef V8_ENABLE_SANDBOX
    if (access.is_bounded_size_access) {
      value = __ ShiftLeft(value, kBoundedSizeShift,
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
    __ Store(object, value, kind, rep, access.write_barrier_kind,
             access.offset);
  }

  // Pass {bitfield} = {digit} = OpIndex::Invalid() to construct the canonical
  // 0n BigInt.
  V<Tagged> AllocateBigInt(V<Word32> bitfield, V<Word64> digit) {
    DCHECK(__ Is64());
    DCHECK_EQ(bitfield.valid(), digit.valid());
    static constexpr auto zero_bitfield =
        BigInt::SignBits::update(BigInt::LengthBits::encode(0), false);

    V<Tagged> map = __ HeapConstant(factory_->bigint_map());
    V<Tagged> bigint =
        __ Allocate(__ IntPtrConstant(BigInt::SizeFor(digit.valid() ? 1 : 0)),
                    AllocationType::kYoung, AllowLargeObjects::kFalse);
    StoreField(bigint, AccessBuilder::ForMap(), map);
    StoreField(bigint, AccessBuilder::ForBigIntBitfield(),
               bitfield.valid() ? bitfield : __ Word32Constant(zero_bitfield));

    // BigInts have no padding on 64 bit architectures with pointer compression.
    if (BigInt::HasOptionalPadding()) {
      StoreField(bigint, AccessBuilder::ForBigIntOptionalPadding(),
                 __ IntPtrConstant(0));
    }
    if (digit.valid()) {
      StoreField(bigint, AccessBuilder::ForBigIntLeastSignificantDigit64(),
                 digit);
    }
    return bigint;
  }

  Factory* factory_;
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_MACHINE_LOWERING_REDUCER_H_
