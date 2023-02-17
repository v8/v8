// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_MACHINE_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_MACHINE_LOWERING_REDUCER_H_

#include "src/base/v8-fallthrough.h"
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

  bool NeedsHeapObjectCheck(ObjectIsOp::InputAssumptions input_assumptions) {
    // TODO(nicohartmann@): Consider type information once we have that.
    switch (input_assumptions) {
      case ObjectIsOp::InputAssumptions::kNone:
        return true;
      case ObjectIsOp::InputAssumptions::kHeapObject:
      case ObjectIsOp::InputAssumptions::kBigInt:
        return false;
    }
  }

  V<Word32> ReduceObjectIs(V<Tagged> input, ObjectIsOp::Kind kind,
                           ObjectIsOp::InputAssumptions input_assumptions) {
    switch (kind) {
      case ObjectIsOp::Kind::kBigInt:
      case ObjectIsOp::Kind::kBigInt64: {
        DCHECK_IMPLIES(kind == ObjectIsOp::Kind::kBigInt64, __ Is64());

        Label<Word32> done(this);

        if (input_assumptions != ObjectIsOp::InputAssumptions::kBigInt) {
          if (NeedsHeapObjectCheck(input_assumptions)) {
            // Check for Smi.
            GOTO_IF(IsSmi(input), done, 0);
          }

          // Check for BigInt.
          V<Tagged> map = LoadField<Tagged>(input, AccessBuilder::ForMap());
          V<Word32> is_bigint_map =
              __ TaggedEqual(map, __ HeapConstant(factory_->bigint_map()));
          GOTO_IF_NOT(is_bigint_map, done, 0);
        }

        if (kind == ObjectIsOp::Kind::kBigInt) {
          GOTO(done, 1);
        } else {
          DCHECK_EQ(kind, ObjectIsOp::Kind::kBigInt64);
          // We have to perform check for BigInt64 range.
          V<Word32> bitfield =
              LoadField<Word32>(input, AccessBuilder::ForBigIntBitfield());
          GOTO_IF(__ Word32Equal(bitfield, 0), done, 1);

          // Length must be 1.
          V<Word32> length_field =
              __ Word32BitwiseAnd(bitfield, BigInt::LengthBits::kMask);
          GOTO_IF_NOT(__ Word32Equal(length_field,
                                     uint32_t{1} << BigInt::LengthBits::kShift),
                      done, 0);

          // Check if it fits in 64 bit signed int.
          V<Word64> lsd = LoadField<Word64>(
              input, AccessBuilder::ForBigIntLeastSignificantDigit64());
          V<Word32> magnitude_check = __ Uint64LessThanOrEqual(
              lsd, std::numeric_limits<int64_t>::max());
          GOTO_IF(magnitude_check, done, 1);

          // The BigInt probably doesn't fit into signed int64. The only
          // exception is int64_t::min. We check for this.
          V<Word32> sign =
              __ Word32BitwiseAnd(bitfield, BigInt::SignBits::kMask);
          V<Word32> sign_check = __ Word32Equal(sign, BigInt::SignBits::kMask);
          GOTO_IF_NOT(sign_check, done, 0);

          V<Word32> min_check =
              __ Word64Equal(lsd, std::numeric_limits<int64_t>::min());
          GOTO_IF(min_check, done, 1);

          GOTO(done, 0);
        }

        BIND(done, result);
        return result;
      }
      case ObjectIsOp::Kind::kCallable:
      case ObjectIsOp::Kind::kConstructor:
      case ObjectIsOp::Kind::kDetectableCallable:
      case ObjectIsOp::Kind::kNonCallable:
      case ObjectIsOp::Kind::kReceiver:
      case ObjectIsOp::Kind::kUndetectable: {
        Label<Word32> done(this);

        // Check for Smi if necessary.
        if (NeedsHeapObjectCheck(input_assumptions)) {
          GOTO_IF(IsSmi(input), done, 0);
        }

        // Load bitfield from map.
        V<Tagged> map = LoadField<Tagged>(input, AccessBuilder::ForMap());
        V<Word32> bitfield =
            LoadField<Word32>(map, AccessBuilder::ForMapBitField());

        V<Word32> check;
        switch (kind) {
          case ObjectIsOp::Kind::kCallable:
            check =
                __ Word32Equal(Map::Bits1::IsCallableBit::kMask,
                               __ Word32BitwiseAnd(
                                   bitfield, Map::Bits1::IsCallableBit::kMask));
            break;
          case ObjectIsOp::Kind::kConstructor:
            check = __ Word32Equal(
                Map::Bits1::IsConstructorBit::kMask,
                __ Word32BitwiseAnd(bitfield,
                                    Map::Bits1::IsConstructorBit::kMask));
            break;
          case ObjectIsOp::Kind::kDetectableCallable:
            check = __ Word32Equal(
                Map::Bits1::IsCallableBit::kMask,
                __ Word32BitwiseAnd(
                    bitfield, (Map::Bits1::IsCallableBit::kMask) |
                                  (Map::Bits1::IsUndetectableBit::kMask)));
            break;
          case ObjectIsOp::Kind::kNonCallable:
            check = __ Word32Equal(
                0, __ Word32BitwiseAnd(bitfield,
                                       Map::Bits1::IsCallableBit::kMask));
            GOTO_IF_NOT(check, done, 0);
            // Fallthrough into receiver check.
            V8_FALLTHROUGH;
          case ObjectIsOp::Kind::kReceiver: {
            static_assert(LAST_TYPE == LAST_JS_RECEIVER_TYPE);
            V<Word32> instance_type =
                LoadField<Word32>(map, AccessBuilder::ForMapInstanceType());
            check =
                __ Uint32LessThanOrEqual(FIRST_JS_RECEIVER_TYPE, instance_type);
            break;
          }
          case ObjectIsOp::Kind::kUndetectable:
            check = __ Word32Equal(
                Map::Bits1::IsUndetectableBit::kMask,
                __ Word32BitwiseAnd(bitfield,
                                    Map::Bits1::IsUndetectableBit::kMask));
            break;
          default:
            UNREACHABLE();
        }
        GOTO(done, check);

        BIND(done, result);
        return result;
      }
      case ObjectIsOp::Kind::kSmi: {
        // If we statically know that this is a heap object, it cannot be a Smi.
        if (!NeedsHeapObjectCheck(input_assumptions)) {
          return __ Word32Constant(0);
        }
        return IsSmi(input);
      }
      case ObjectIsOp::Kind::kNumber: {
        Label<Word32> done(this);

        // Check for Smi if necessary.
        if (NeedsHeapObjectCheck(input_assumptions)) {
          GOTO_IF(IsSmi(input), done, 1);
        }

        V<Tagged> map = LoadField<Tagged>(input, AccessBuilder::ForMap());
        GOTO(done,
             __ TaggedEqual(map, __ HeapConstant(factory_->heap_number_map())));

        BIND(done, result);
        return result;
      }
      case ObjectIsOp::Kind::kSymbol:
      case ObjectIsOp::Kind::kString:
      case ObjectIsOp::Kind::kArrayBufferView: {
        Label<Word32> done(this);

        // Check for Smi if necessary.
        if (NeedsHeapObjectCheck(input_assumptions)) {
          GOTO_IF(IsSmi(input), done, 0);
        }

        // Load instance type from map.
        V<Tagged> map = LoadField<Tagged>(input, AccessBuilder::ForMap());
        V<Word32> instance_type =
            LoadField<Word32>(map, AccessBuilder::ForMapInstanceType());

        V<Word32> check;
        switch (kind) {
          case ObjectIsOp::Kind::kSymbol:
            check = __ Word32Equal(instance_type, SYMBOL_TYPE);
            break;
          case ObjectIsOp::Kind::kString:
            check = __ Uint32LessThan(instance_type, FIRST_NONSTRING_TYPE);
            break;
          case ObjectIsOp::Kind::kArrayBufferView:
            check = __ Uint32LessThan(
                __ Word32Sub(instance_type, FIRST_JS_ARRAY_BUFFER_VIEW_TYPE),
                LAST_JS_ARRAY_BUFFER_VIEW_TYPE -
                    FIRST_JS_ARRAY_BUFFER_VIEW_TYPE + 1);
            break;
          default:
            UNREACHABLE();
        }
        GOTO(done, check);

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
        IF(__ Word64Equal(input, int64_t{0})) {
          GOTO(done, AllocateBigInt(OpIndex::Invalid(), OpIndex::Invalid()));
        }
        ELSE {
          // Shift sign bit into BigInt's sign bit position.
          V<Word32> bitfield = __ Word32BitwiseOr(
              BigInt::LengthBits::encode(1),
              __ Word64ShiftRightLogical(
                  input, static_cast<int64_t>(63 - BigInt::SignBits::kShift)));

          // We use (value XOR (value >> 63)) - (value >> 63) to compute the
          // absolute value, in a branchless fashion.
          V<Word64> sign_mask =
              __ Word64ShiftRightArithmetic(input, int64_t{63});
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
        IF(__ Word64Equal(input, uint64_t{0})) {
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

  V<Word32> IsSmi(V<Tagged> input) {
    return __ Word32Equal(
        __ Word32BitwiseAnd(V<Word32>::Cast(input),
                            static_cast<uint32_t>(kSmiTagMask)),
        static_cast<uint32_t>(kSmiTag));
  }

  Factory* factory_;
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_MACHINE_LOWERING_REDUCER_H_
