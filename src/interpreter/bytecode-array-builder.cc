// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-array-builder.h"

#include "src/globals.h"
#include "src/interpreter/bytecode-jump-table.h"
#include "src/interpreter/bytecode-label.h"
#include "src/interpreter/bytecode-register-optimizer.h"
#include "src/interpreter/bytecode-source-info.h"
#include "src/interpreter/bytecode-traits.h"
#include "src/interpreter/interpreter-intrinsics.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace interpreter {

class RegisterTransferWriter final
    : public NON_EXPORTED_BASE(BytecodeRegisterOptimizer::BytecodeWriter),
      public NON_EXPORTED_BASE(ZoneObject) {
 public:
  RegisterTransferWriter(BytecodeArrayBuilder* builder) : builder_(builder) {}
  ~RegisterTransferWriter() override {}

  void EmitLdar(Register input) override { builder_->OutputLdarRaw(input); }

  void EmitStar(Register output) override { builder_->OutputStarRaw(output); }

  void EmitMov(Register input, Register output) override {
    builder_->OutputMovRaw(input, output);
  }

 private:
  BytecodeArrayBuilder* builder_;
};

BytecodeArrayBuilder::BytecodeArrayBuilder(
    Isolate* isolate, Zone* zone, int parameter_count, int locals_count,
    FunctionLiteral* literal,
    SourcePositionTableBuilder::RecordingMode source_position_mode)
    : zone_(zone),
      bytecodes_(zone),
      literal_(literal),

      constant_array_builder_(zone),
      handler_table_builder_(zone),
      source_position_table_builder_(zone, source_position_mode),

      register_allocator_(locals_count),
      register_optimizer_(nullptr),

      parameter_count_(parameter_count),
      local_register_count_(locals_count),
      return_position_(literal ? literal->return_position()
                               : kNoSourcePosition),
      unbound_jumps_(0),

      bytecode_generated_(false),
      elide_noneffectful_bytecodes_(FLAG_ignition_elide_noneffectful_bytecodes),
      exit_seen_in_block_(false),
      last_bytecode_had_source_info_(false),

      last_bytecode_offset_(0),
      last_bytecode_(Bytecode::kIllegal) {
  DCHECK_GE(parameter_count_, 0);
  DCHECK_GE(local_register_count_, 0);

  if (FLAG_ignition_reo) {
    register_optimizer_ = new (zone) BytecodeRegisterOptimizer(
        zone, &register_allocator_, fixed_register_count(), parameter_count,
        new (zone) RegisterTransferWriter(this));
  }
}

Register BytecodeArrayBuilder::Parameter(int parameter_index) const {
  DCHECK_GE(parameter_index, 0);
  // The parameter indices are shifted by 1 (receiver is the
  // first entry).
  return Register::FromParameterIndex(parameter_index + 1, parameter_count());
}

Register BytecodeArrayBuilder::Receiver() const {
  return Register::FromParameterIndex(0, parameter_count());
}

Register BytecodeArrayBuilder::Local(int index) const {
  // TODO(marja): Make a DCHECK once crbug.com/706234 is fixed.
  CHECK_LT(index, locals_count());
  return Register(index);
}

Handle<BytecodeArray> BytecodeArrayBuilder::ToBytecodeArray(Isolate* isolate) {
  DCHECK(exit_seen_in_block_);
  DCHECK(!bytecode_generated_);
  bytecode_generated_ = true;

  int register_count = total_register_count();

  if (register_optimizer_) {
    register_optimizer_->Flush();
    register_count = register_optimizer_->maxiumum_register_index() + 1;
  }

  int bytecode_size = static_cast<int>(bytecodes()->size());
  int frame_size = register_count * kPointerSize;

  Handle<FixedArray> constant_pool =
      constant_array_builder()->ToFixedArray(isolate);
  Handle<BytecodeArray> bytecode_array = isolate->factory()->NewBytecodeArray(
      bytecode_size, &bytecodes()->front(), frame_size, parameter_count(),
      constant_pool);

  Handle<FixedArray> handler_table =
      handler_table_builder()->ToHandlerTable(isolate);
  bytecode_array->set_handler_table(*handler_table);

  Handle<ByteArray> source_position_table =
      source_position_table_builder()->ToSourcePositionTable(
          isolate, Handle<AbstractCode>::cast(bytecode_array));
  bytecode_array->set_source_position_table(*source_position_table);

  return bytecode_array;
}

BytecodeSourceInfo BytecodeArrayBuilder::CurrentSourcePosition(
    Bytecode bytecode) {
  BytecodeSourceInfo source_position;
  if (latest_source_info_.is_valid()) {
    // Statement positions need to be emitted immediately.  Expression
    // positions can be pushed back until a bytecode is found that can
    // throw (if expression position filtering is turned on). We only
    // invalidate the existing source position information if it is used.
    if (latest_source_info_.is_statement() ||
        !FLAG_ignition_filter_expression_positions ||
        !Bytecodes::IsWithoutExternalSideEffects(bytecode)) {
      source_position = latest_source_info_;
      latest_source_info_.set_invalid();
    }
  }
  return source_position;
}

void BytecodeArrayBuilder::PatchJump(size_t jump_target, size_t jump_location) {
  Bytecode jump_bytecode = Bytecodes::FromByte(bytecodes()->at(jump_location));
  int delta = static_cast<int>(jump_target - jump_location);
  int prefix_offset = 0;
  OperandScale operand_scale = OperandScale::kSingle;
  if (Bytecodes::IsPrefixScalingBytecode(jump_bytecode)) {
    // If a prefix scaling bytecode is emitted the target offset is one
    // less than the case of no prefix scaling bytecode.
    delta -= 1;
    prefix_offset = 1;
    operand_scale = Bytecodes::PrefixBytecodeToOperandScale(jump_bytecode);
    jump_bytecode =
        Bytecodes::FromByte(bytecodes()->at(jump_location + prefix_offset));
  }

  DCHECK(Bytecodes::IsJump(jump_bytecode));
  switch (operand_scale) {
    case OperandScale::kSingle:
      PatchJumpWith8BitOperand(jump_location, delta);
      break;
    case OperandScale::kDouble:
      PatchJumpWith16BitOperand(jump_location + prefix_offset, delta);
      break;
    case OperandScale::kQuadruple:
      PatchJumpWith32BitOperand(jump_location + prefix_offset, delta);
      break;
    default:
      UNREACHABLE();
  }
  unbound_jumps_--;
}

void BytecodeArrayBuilder::PatchJumpWith8BitOperand(size_t jump_location,
                                                    int delta) {
  Bytecode jump_bytecode = Bytecodes::FromByte(bytecodes()->at(jump_location));
  DCHECK(Bytecodes::IsForwardJump(jump_bytecode));
  DCHECK(Bytecodes::IsJumpImmediate(jump_bytecode));
  DCHECK_EQ(Bytecodes::GetOperandType(jump_bytecode, 0), OperandType::kUImm);
  DCHECK_GT(delta, 0);
  size_t operand_location = jump_location + 1;
  DCHECK_EQ(bytecodes()->at(operand_location), k8BitJumpPlaceholder);
  if (Bytecodes::ScaleForUnsignedOperand(delta) == OperandScale::kSingle) {
    // The jump fits within the range of an UImm8 operand, so cancel
    // the reservation and jump directly.
    constant_array_builder()->DiscardReservedEntry(OperandSize::kByte);
    bytecodes()->at(operand_location) = static_cast<uint8_t>(delta);
  } else {
    // The jump does not fit within the range of an UImm8 operand, so
    // commit reservation putting the offset into the constant pool,
    // and update the jump instruction and operand.
    size_t entry = constant_array_builder()->CommitReservedEntry(
        OperandSize::kByte, Smi::FromInt(delta));
    DCHECK_EQ(Bytecodes::SizeForUnsignedOperand(static_cast<uint32_t>(entry)),
              OperandSize::kByte);
    jump_bytecode = Bytecodes::GetJumpWithConstantOperand(jump_bytecode);
    bytecodes()->at(jump_location) = Bytecodes::ToByte(jump_bytecode);
    bytecodes()->at(operand_location) = static_cast<uint8_t>(entry);
  }
}

void BytecodeArrayBuilder::PatchJumpWith16BitOperand(size_t jump_location,
                                                     int delta) {
  Bytecode jump_bytecode = Bytecodes::FromByte(bytecodes()->at(jump_location));
  DCHECK(Bytecodes::IsForwardJump(jump_bytecode));
  DCHECK(Bytecodes::IsJumpImmediate(jump_bytecode));
  DCHECK_EQ(Bytecodes::GetOperandType(jump_bytecode, 0), OperandType::kUImm);
  DCHECK_GT(delta, 0);
  size_t operand_location = jump_location + 1;
  uint8_t operand_bytes[2];
  if (Bytecodes::ScaleForUnsignedOperand(delta) <= OperandScale::kDouble) {
    // The jump fits within the range of an Imm16 operand, so cancel
    // the reservation and jump directly.
    constant_array_builder()->DiscardReservedEntry(OperandSize::kShort);
    WriteUnalignedUInt16(operand_bytes, static_cast<uint16_t>(delta));
  } else {
    // The jump does not fit within the range of an Imm16 operand, so
    // commit reservation putting the offset into the constant pool,
    // and update the jump instruction and operand.
    size_t entry = constant_array_builder()->CommitReservedEntry(
        OperandSize::kShort, Smi::FromInt(delta));
    jump_bytecode = Bytecodes::GetJumpWithConstantOperand(jump_bytecode);
    bytecodes()->at(jump_location) = Bytecodes::ToByte(jump_bytecode);
    WriteUnalignedUInt16(operand_bytes, static_cast<uint16_t>(entry));
  }
  DCHECK(bytecodes()->at(operand_location) == k8BitJumpPlaceholder &&
         bytecodes()->at(operand_location + 1) == k8BitJumpPlaceholder);
  bytecodes()->at(operand_location++) = operand_bytes[0];
  bytecodes()->at(operand_location) = operand_bytes[1];
}

void BytecodeArrayBuilder::PatchJumpWith32BitOperand(size_t jump_location,
                                                     int delta) {
  DCHECK(Bytecodes::IsJumpImmediate(
      Bytecodes::FromByte(bytecodes()->at(jump_location))));
  constant_array_builder()->DiscardReservedEntry(OperandSize::kQuad);
  uint8_t operand_bytes[4];
  WriteUnalignedUInt32(operand_bytes, static_cast<uint32_t>(delta));
  size_t operand_location = jump_location + 1;
  DCHECK(bytecodes()->at(operand_location) == k8BitJumpPlaceholder &&
         bytecodes()->at(operand_location + 1) == k8BitJumpPlaceholder &&
         bytecodes()->at(operand_location + 2) == k8BitJumpPlaceholder &&
         bytecodes()->at(operand_location + 3) == k8BitJumpPlaceholder);
  bytecodes()->at(operand_location++) = operand_bytes[0];
  bytecodes()->at(operand_location++) = operand_bytes[1];
  bytecodes()->at(operand_location++) = operand_bytes[2];
  bytecodes()->at(operand_location) = operand_bytes[3];
}

void BytecodeArrayBuilder::OutputLdarRaw(Register reg) {
  // Exit early for dead code.
  if (exit_seen_in_block_) return;
  Write<AccumulatorUse::kWrite, OperandType::kReg>(
      Bytecode::kLdar, BytecodeSourceInfo(),
      {{static_cast<uint32_t>(reg.ToOperand())}});
}

void BytecodeArrayBuilder::OutputStarRaw(Register reg) {
  // Exit early for dead code.
  if (exit_seen_in_block_) return;
  Write<AccumulatorUse::kRead, OperandType::kRegOut>(
      Bytecode::kStar, BytecodeSourceInfo(),
      {{static_cast<uint32_t>(reg.ToOperand())}});
}

void BytecodeArrayBuilder::OutputMovRaw(Register src, Register dest) {
  // Exit early for dead code.
  if (exit_seen_in_block_) return;
  Write<AccumulatorUse::kNone, OperandType::kReg, OperandType::kRegOut>(
      Bytecode::kMov, BytecodeSourceInfo(),
      {{static_cast<uint32_t>(src.ToOperand()),
        static_cast<uint32_t>(dest.ToOperand())}});
}

namespace {

template <OperandTypeInfo type_info>
class UnsignedOperandHelper {
 public:
  INLINE(static uint32_t Convert(BytecodeArrayBuilder* builder, size_t value)) {
    DCHECK(IsValid(value));
    return static_cast<uint32_t>(value);
  }

  INLINE(static uint32_t Convert(BytecodeArrayBuilder* builder, int value)) {
    DCHECK_GE(value, 0);
    return Convert(builder, static_cast<size_t>(value));
  }

 private:
  static bool IsValid(size_t value) {
    switch (type_info) {
      case OperandTypeInfo::kFixedUnsignedByte:
        return value <= kMaxUInt8;
      case OperandTypeInfo::kFixedUnsignedShort:
        return value <= kMaxUInt16;
      case OperandTypeInfo::kScalableUnsignedByte:
        return value <= kMaxUInt32;
      default:
        UNREACHABLE();
    }
  }
};

template <OperandType>
class OperandHelper {};

#define DEFINE_UNSIGNED_OPERAND_HELPER(Name, Type) \
  template <>                                      \
  class OperandHelper<OperandType::k##Name>        \
      : public UnsignedOperandHelper<Type> {};
UNSIGNED_FIXED_SCALAR_OPERAND_TYPE_LIST(DEFINE_UNSIGNED_OPERAND_HELPER)
UNSIGNED_SCALABLE_SCALAR_OPERAND_TYPE_LIST(DEFINE_UNSIGNED_OPERAND_HELPER)
#undef DEFINE_UNSIGNED_OPERAND_HELPER

template <>
class OperandHelper<OperandType::kImm> {
 public:
  INLINE(static uint32_t Convert(BytecodeArrayBuilder* builder, int value)) {
    return static_cast<uint32_t>(value);
  }
};

template <>
class OperandHelper<OperandType::kReg> {
 public:
  INLINE(static uint32_t Convert(BytecodeArrayBuilder* builder, Register reg)) {
    return builder->GetInputRegisterOperand(reg);
  }
};

template <>
class OperandHelper<OperandType::kRegList> {
 public:
  INLINE(static uint32_t Convert(BytecodeArrayBuilder* builder,
                                 RegisterList reg_list)) {
    return builder->GetInputRegisterListOperand(reg_list);
  }
};

template <>
class OperandHelper<OperandType::kRegPair> {
 public:
  INLINE(static uint32_t Convert(BytecodeArrayBuilder* builder,
                                 RegisterList reg_list)) {
    DCHECK_EQ(reg_list.register_count(), 2);
    return builder->GetInputRegisterListOperand(reg_list);
  }
};

template <>
class OperandHelper<OperandType::kRegOut> {
 public:
  INLINE(static uint32_t Convert(BytecodeArrayBuilder* builder, Register reg)) {
    return builder->GetOutputRegisterOperand(reg);
  }
};

template <>
class OperandHelper<OperandType::kRegOutList> {
 public:
  INLINE(static uint32_t Convert(BytecodeArrayBuilder* builder,
                                 RegisterList reg_list)) {
    return builder->GetOutputRegisterListOperand(reg_list);
  }
};

template <>
class OperandHelper<OperandType::kRegOutPair> {
 public:
  INLINE(static uint32_t Convert(BytecodeArrayBuilder* builder,
                                 RegisterList reg_list)) {
    DCHECK_EQ(2, reg_list.register_count());
    return builder->GetOutputRegisterListOperand(reg_list);
  }
};

template <>
class OperandHelper<OperandType::kRegOutTriple> {
 public:
  INLINE(static uint32_t Convert(BytecodeArrayBuilder* builder,
                                 RegisterList reg_list)) {
    DCHECK_EQ(3, reg_list.register_count());
    return builder->GetOutputRegisterListOperand(reg_list);
  }
};

// Recursively defined helper for operating on an array of operands (calculating
// the maximum scale needed, and emitting them). Each recursion peels off one of
// the operand types, and increments the index we check in the value array, e.g.
// for:
//
//    OperandArrayHelper<0, OperandType::kReg, OperandType::kImm>
//
// then OperandArrayHelper::ScaleForOperands is defined for std::array<uint32_t,
// 2>, and recursively calls
//
//    OperandArrayHelper<1, OperandType::kImm>::ScaleForOperands()
//    OperandArrayHelper<2>::ScaleForOperands()

template <size_t I, OperandType... operand_types>
struct OperandArrayHelper;

// Base case: I points past the end of the array.
template <size_t I>
struct OperandArrayHelper<I> {
 private:
  static const int kArraySize = I;

 public:
  typedef std::array<uint32_t, kArraySize> OperandArray;

  static OperandScale ScaleForOperands(const OperandArray& operand_values) {
    return OperandScale::kSingle;
  }

  template <OperandScale operand_scale>
  static void Emit(ZoneVector<uint8_t>* out,
                   const OperandArray& operand_values) {}

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(OperandArrayHelper);
};

// Recursive case.
template <size_t I, OperandType operand_type, OperandType... operand_types>
struct OperandArrayHelper<I, operand_type, operand_types...> {
 private:
  // The array has had I items iterated through already, and has remaning the
  // current operand type plus the remainining operand types.
  static const int kArraySize = I + 1 + sizeof...(operand_types);
  typedef OperandArrayHelper<I + 1, operand_types...> NextRecursion;

 public:
  typedef std::array<uint32_t, kArraySize> OperandArray;

  static OperandScale ScaleForOperands(const OperandArray& operand_values) {
    uint32_t operand_value = std::get<I>(operand_values);
    OperandScale operand_scale = OperandScale::kSingle;
    if (BytecodeOperands::IsScalableUnsignedByte(operand_type)) {
      operand_scale = Bytecodes::ScaleForUnsignedOperand(operand_value);
    } else if (BytecodeOperands::IsScalableSignedByte(operand_type)) {
      operand_scale = Bytecodes::ScaleForSignedOperand(operand_value);
    }

    return std::max(operand_scale,
                    NextRecursion::ScaleForOperands(operand_values));
  }

  template <OperandScale operand_scale>
  static void Emit(ZoneVector<uint8_t>* out,
                   const OperandArray& operand_values) {
    uint32_t operand_value = std::get<I>(operand_values);
    switch (OperandScaler<operand_type, operand_scale>::kOperandSize) {
      case OperandSize::kNone:
        UNREACHABLE();
        break;
      case OperandSize::kByte:
        out->push_back(static_cast<uint8_t>(operand_value));
        break;
      case OperandSize::kShort: {
        uint16_t operand_u16 = static_cast<uint16_t>(operand_value);
        const uint8_t* raw_operand =
            reinterpret_cast<const uint8_t*>(&operand_u16);
        out->push_back(raw_operand[0]);
        out->push_back(raw_operand[1]);
        break;
      }
      case OperandSize::kQuad: {
        const uint8_t* raw_operand =
            reinterpret_cast<const uint8_t*>(&operand_value);
        out->push_back(raw_operand[0]);
        out->push_back(raw_operand[1]);
        out->push_back(raw_operand[2]);
        out->push_back(raw_operand[3]);
        break;
      }
    }

    NextRecursion::template Emit<operand_scale>(out, operand_values);
  }

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(OperandArrayHelper);
};

// Helper class for building an array of integer operand values given template
// packs of operand types and generic operand values. This has to be a separate
// struct to allow us to initialize the operand_types template pack using a
// variadic macro.
template <Bytecode bytecode, AccumulatorUse accumulator_use,
          OperandType... operand_types>
class OperandArrayBuilder {
 public:
  typedef std::array<uint32_t, sizeof...(operand_types)> Array;

  template <typename... Operands>
  INLINE(static Array Build(BytecodeArrayBuilder* builder,
                            Operands... operands)) {
    static_assert(sizeof...(Operands) <= Bytecodes::kMaxOperands,
                  "too many operands for bytecode");
    static_assert(sizeof...(Operands) == sizeof...(operand_types),
                  "wrong number of operands");

    // Calculate and store the converted operand values.
    //
    // The "OperandHelper<operand_types>::Convert(builder, operands)..."
    // will expand both the OperandType... and Operands... parameter packs
    // e.g. for:
    //
    //   OperandArrayBuilder<..., OperandType::kReg, OperandType::kImm>
    //       ::Build<Register, int>(..., Register reg, int immediate)
    //
    // the code will expand into:
    //
    //   return {{
    //     OperandHelper<OperandType::kReg>::Convert(builder, reg),
    //     OperandHelper<OperandType::kImm>::Convert(builder, immediate)
    //   }};
    return {{OperandHelper<operand_types>::Convert(builder, operands)...}};
  }

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(OperandArrayBuilder);
};

}  // namespace

void BytecodeArrayBuilder::AttachSourceInfo(
    const BytecodeSourceInfo& source_info) {
  if (!source_info.is_valid()) return;

  int bytecode_offset = static_cast<int>(bytecodes()->size());
  source_position_table_builder()->AddPosition(
      bytecode_offset, SourcePosition(source_info.source_position()),
      source_info.is_statement());
}

void BytecodeArrayBuilder::AttachDeferredAndCurrentSourceInfo(
    BytecodeSourceInfo source_info) {
  if (deferred_source_info_.is_valid()) {
    if (source_info.is_valid()) {
      // We need to attach the current source info to the current bytecode, so
      // attach the deferred source to a nop instead.
      AttachSourceInfo(deferred_source_info_);
      bytecodes()->push_back(Bytecodes::ToByte(Bytecode::kNop));
    } else {
      if (last_bytecode_had_source_info_) {
        // We've taken over an elided source info, but don't have source info
        // for ourselves. Emit a nop for the elided source info, since we're
        // attaching deferred source info to the current bytecode.
        // TODO(leszeks): Eliding and deferring feel very similar, maybe we can
        // unify them.
        bytecodes()->push_back(Bytecodes::ToByte(Bytecode::kNop));
      }
      // Attach the deferred source info to the current bytecode.
      source_info = deferred_source_info_;
    }
    // Either way, we can invalidate the stored deferred source info.
    deferred_source_info_.set_invalid();
  }

  AttachSourceInfo(source_info);
  // We may have decided to attach the last bytecode's source info to the
  // current one, so include that decision in the builder state.
  last_bytecode_had_source_info_ |= source_info.is_valid();
}

void BytecodeArrayBuilder::SetDeferredSourceInfo(
    BytecodeSourceInfo source_info) {
  if (!source_info.is_valid()) return;
  if (deferred_source_info_.is_valid()) {
    // Emit any previous deferred source info now as a nop.
    AttachSourceInfo(deferred_source_info_);
    bytecodes()->push_back(Bytecodes::ToByte(Bytecode::kNop));
  }
  deferred_source_info_ = source_info;
}

template <AccumulatorUse accumulator_use, OperandType... operand_types>
void BytecodeArrayBuilder::Write(
    Bytecode bytecode, BytecodeSourceInfo source_info,
    std::array<uint32_t, sizeof...(operand_types)> operand_values) {
  DCHECK(!Bytecodes::IsJump(bytecode));
  DCHECK(!Bytecodes::IsSwitch(bytecode));
  DCHECK(!exit_seen_in_block_);  // Don't emit dead code.

  MaybeElideLastBytecode(bytecode, source_info.is_valid());
  AttachDeferredAndCurrentSourceInfo(source_info);
  EmitBytecode<operand_types...>(bytecode, operand_values);
}

template <AccumulatorUse accumulator_use, OperandType... operand_types>
void BytecodeArrayBuilder::WriteJump(
    Bytecode bytecode, BytecodeSourceInfo source_info, BytecodeLabel* label,
    std::array<uint32_t, sizeof...(operand_types)> operand_values) {
  DCHECK(Bytecodes::IsJump(bytecode));
  DCHECK_EQ(0u, operand_values[0]);
  DCHECK(!exit_seen_in_block_);  // Don't emit dead code.

  MaybeElideLastBytecode(bytecode, source_info.is_valid());
  AttachDeferredAndCurrentSourceInfo(source_info);

  size_t current_offset = bytecodes()->size();
  if (bytecode == Bytecode::kJumpLoop) {
    // This is a backwards jump, so label has already been bound.
    DCHECK(label->is_bound());
    CHECK_GE(current_offset, label->offset());
    CHECK_LE(current_offset, static_cast<size_t>(kMaxUInt32));
    uint32_t delta = static_cast<uint32_t>(current_offset - label->offset());
    OperandScale operand_scale = Bytecodes::ScaleForUnsignedOperand(delta);
    if (operand_scale > OperandScale::kSingle) {
      // Adjust for scaling byte prefix for wide jump offset.
      delta += 1;
    }
    operand_values[0] = delta;
  } else {
    DCHECK(Bytecodes::IsForwardJump(bytecode));
    DCHECK(!label->is_bound());
    // The label has not yet been bound so this is a forward reference
    // that will be patched when the label is bound. We create a
    // reservation in the constant pool so the jump can be patched
    // when the label is bound. The reservation means the maximum size
    // of the operand for the constant is known and the jump can
    // be emitted into the bytecode stream with space for the operand.
    unbound_jumps_++;
    label->set_referrer(current_offset);
    OperandSize reserved_operand_size =
        constant_array_builder()->CreateReservedEntry();
    switch (reserved_operand_size) {
      case OperandSize::kNone:
        UNREACHABLE();
        break;
      case OperandSize::kByte:
        operand_values[0] = k8BitJumpPlaceholder;
        break;
      case OperandSize::kShort:
        operand_values[0] = k16BitJumpPlaceholder;
        break;
      case OperandSize::kQuad:
        operand_values[0] = k32BitJumpPlaceholder;
        break;
    }
  }

  EmitBytecode<operand_types...>(bytecode, operand_values);
}

template <AccumulatorUse accumulator_use, OperandType... operand_types>
void BytecodeArrayBuilder::WriteSwitch(
    Bytecode bytecode, BytecodeSourceInfo source_info,
    BytecodeJumpTable* jump_table,
    std::array<uint32_t, sizeof...(operand_types)> operand_values) {
  DCHECK(Bytecodes::IsSwitch(bytecode));
  DCHECK(!exit_seen_in_block_);  // Don't emit dead code.

  MaybeElideLastBytecode(bytecode, source_info.is_valid());
  AttachDeferredAndCurrentSourceInfo(source_info);

  size_t current_offset = bytecodes()->size();
  const OperandScale operand_scale =
      OperandArrayHelper<0, operand_types...>::ScaleForOperands(operand_values);
  if (operand_scale > OperandScale::kSingle) {
    // Adjust for scaling byte prefix.
    current_offset += 1;
  }
  jump_table->set_switch_bytecode_offset(current_offset);

  EmitBytecode<operand_types...>(bytecode, operand_values);
}

template <OperandType... operand_types>
void BytecodeArrayBuilder::EmitBytecode(
    Bytecode bytecode,
    std::array<uint32_t, sizeof...(operand_types)> operand_values) {
  // Create a typedef for the OperandArrayHelper we'll be using for operating
  // on the operands.
  typedef OperandArrayHelper<0, operand_types...> OperandArrayHelper;

  // Calculate the maximum scale needed by the operand values.
  const OperandScale operand_scale =
      OperandArrayHelper::ScaleForOperands(operand_values);

  // Update the state of the builder
  last_bytecode_ = bytecode;
  last_bytecode_offset_ = bytecodes()->size();
  exit_seen_in_block_ = Bytecodes::EndsBasicBlock(bytecode);

  // Emit a prefix scaling bytecode if needed.
  if (operand_scale != OperandScale::kSingle) {
    Bytecode prefix = Bytecodes::OperandScaleToPrefixBytecode(operand_scale);
    bytecodes()->push_back(Bytecodes::ToByte(prefix));
  }

  // Emit the current bytecode.
  bytecodes()->push_back(Bytecodes::ToByte(bytecode));

  // Emit the operands using the helper. We switch once on the operand scale
  // and call OperandArrayHelper::Emit with a static operand scale, to avoid
  // testing the operand scale multiple times.
  switch (operand_scale) {
    case OperandScale::kSingle:
      OperandArrayHelper::template Emit<OperandScale::kSingle>(bytecodes(),
                                                               operand_values);
      break;
    case OperandScale::kDouble:
      OperandArrayHelper::template Emit<OperandScale::kDouble>(bytecodes(),
                                                               operand_values);
      break;
    case OperandScale::kQuadruple:
      OperandArrayHelper::template Emit<OperandScale::kQuadruple>(
          bytecodes(), operand_values);
      break;
  }
}

void BytecodeArrayBuilder::MaybeElideLastBytecode(Bytecode next_bytecode,
                                                  bool has_source_info) {
  if (!elide_noneffectful_bytecodes_) return;

  // If the last bytecode loaded the accumulator without any external effect,
  // and the next bytecode clobbers this load without reading the accumulator,
  // then the previous bytecode can be elided as it has no effect.
  if (Bytecodes::IsAccumulatorLoadWithoutEffects(last_bytecode_) &&
      Bytecodes::GetAccumulatorUse(next_bytecode) == AccumulatorUse::kWrite) {
    DCHECK_GT(bytecodes()->size(), last_bytecode_offset_);
    bytecodes()->resize(last_bytecode_offset_);

    if (last_bytecode_had_source_info_) {
      // If we can, attach the last bytecode's source info to the current
      // bytecode, otherwise emit a nop to attach it to.
      if (!has_source_info) {
        has_source_info = true;
      } else {
        bytecodes()->push_back(Bytecodes::ToByte(Bytecode::kNop));
      }
    }
  }
  // We may have decided to attach the last bytecode's source info to the
  // current one, so update the builder state now.
  last_bytecode_had_source_info_ = has_source_info;
}

void BytecodeArrayBuilder::InvalidateLastBytecode() {
  last_bytecode_ = Bytecode::kIllegal;
}

#define DEFINE_BYTECODE_OUTPUT(Name, ...)                                      \
                                                                               \
  template <typename... Operands>                                              \
  void BytecodeArrayBuilder::Output##Name(Operands... operands) {              \
    PrepareToOutputBytecode<Bytecode::k##Name, __VA_ARGS__>();                 \
    /* Exit early for dead code. */                                            \
    if (exit_seen_in_block_) return;                                           \
    BytecodeSourceInfo source_info = CurrentSourcePosition(Bytecode::k##Name); \
    Write<__VA_ARGS__>(                                                        \
        Bytecode::k##Name, source_info,                                        \
        OperandArrayBuilder<Bytecode::k##Name, __VA_ARGS__>::Build(            \
            this, operands...));                                               \
  }                                                                            \
                                                                               \
  template <typename... Operands>                                              \
  void BytecodeArrayBuilder::Output##Name(BytecodeLabel* label,                \
                                          Operands... operands) {              \
    PrepareToOutputBytecode<Bytecode::k##Name, __VA_ARGS__>();                 \
    /* Exit early for dead code. */                                            \
    if (exit_seen_in_block_) return;                                           \
    BytecodeSourceInfo source_info = CurrentSourcePosition(Bytecode::k##Name); \
    WriteJump<__VA_ARGS__>(                                                    \
        Bytecode::k##Name, source_info, label,                                 \
        OperandArrayBuilder<Bytecode::k##Name, __VA_ARGS__>::Build(            \
            this, operands...));                                               \
  }                                                                            \
                                                                               \
  template <typename... Operands>                                              \
  void BytecodeArrayBuilder::Output##Name(BytecodeJumpTable* jump_table,       \
                                          Operands... operands) {              \
    PrepareToOutputBytecode<Bytecode::k##Name, __VA_ARGS__>();                 \
    /* Exit early for dead code. */                                            \
    if (exit_seen_in_block_) return;                                           \
    BytecodeSourceInfo source_info = CurrentSourcePosition(Bytecode::k##Name); \
    WriteSwitch<__VA_ARGS__>(                                                  \
        Bytecode::k##Name, source_info, jump_table,                            \
        OperandArrayBuilder<Bytecode::k##Name, __VA_ARGS__>::Build(            \
            this, operands...));                                               \
  }

BYTECODE_LIST(DEFINE_BYTECODE_OUTPUT)
#undef DEFINE_BYTECODE_OUTPUT

void BytecodeArrayBuilder::OutputSwitchOnSmiNoFeedback(
    BytecodeJumpTable* jump_table) {
  // We pass in the jump table object so that its offset can be updated, as well
  // as the jump table's parameters which are operands for the bytecode.
  // TODO(leszeks): Do this parameter extraction in the macro-defined function,
  // to avoid overloading OutputSwitchOnSmiNoFeedback.
  OutputSwitchOnSmiNoFeedback(jump_table, jump_table->constant_pool_index(),
                              jump_table->size(),
                              jump_table->case_value_base());
}

BytecodeArrayBuilder& BytecodeArrayBuilder::BinaryOperation(Token::Value op,
                                                            Register reg,
                                                            int feedback_slot) {
  switch (op) {
    case Token::Value::ADD:
      OutputAdd(reg, feedback_slot);
      break;
    case Token::Value::SUB:
      OutputSub(reg, feedback_slot);
      break;
    case Token::Value::MUL:
      OutputMul(reg, feedback_slot);
      break;
    case Token::Value::DIV:
      OutputDiv(reg, feedback_slot);
      break;
    case Token::Value::MOD:
      OutputMod(reg, feedback_slot);
      break;
    case Token::Value::BIT_OR:
      OutputBitwiseOr(reg, feedback_slot);
      break;
    case Token::Value::BIT_XOR:
      OutputBitwiseXor(reg, feedback_slot);
      break;
    case Token::Value::BIT_AND:
      OutputBitwiseAnd(reg, feedback_slot);
      break;
    case Token::Value::SHL:
      OutputShiftLeft(reg, feedback_slot);
      break;
    case Token::Value::SAR:
      OutputShiftRight(reg, feedback_slot);
      break;
    case Token::Value::SHR:
      OutputShiftRightLogical(reg, feedback_slot);
      break;
    default:
      UNREACHABLE();
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::BinaryOperationSmiLiteral(
    Token::Value op, Smi* literal, int feedback_slot) {
  switch (op) {
    case Token::Value::ADD:
      OutputAddSmi(literal->value(), feedback_slot);
      break;
    case Token::Value::SUB:
      OutputSubSmi(literal->value(), feedback_slot);
      break;
    case Token::Value::MUL:
      OutputMulSmi(literal->value(), feedback_slot);
      break;
    case Token::Value::DIV:
      OutputDivSmi(literal->value(), feedback_slot);
      break;
    case Token::Value::MOD:
      OutputModSmi(literal->value(), feedback_slot);
      break;
    case Token::Value::BIT_OR:
      OutputBitwiseOrSmi(literal->value(), feedback_slot);
      break;
    case Token::Value::BIT_XOR:
      OutputBitwiseXorSmi(literal->value(), feedback_slot);
      break;
    case Token::Value::BIT_AND:
      OutputBitwiseAndSmi(literal->value(), feedback_slot);
      break;
    case Token::Value::SHL:
      OutputShiftLeftSmi(literal->value(), feedback_slot);
      break;
    case Token::Value::SAR:
      OutputShiftRightSmi(literal->value(), feedback_slot);
      break;
    case Token::Value::SHR:
      OutputShiftRightLogicalSmi(literal->value(), feedback_slot);
      break;
    default:
      UNREACHABLE();
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CountOperation(Token::Value op,
                                                           int feedback_slot) {
  if (op == Token::Value::ADD) {
    OutputInc(feedback_slot);
  } else {
    DCHECK_EQ(op, Token::Value::SUB);
    OutputDec(feedback_slot);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LogicalNot(ToBooleanMode mode) {
  if (mode == ToBooleanMode::kAlreadyBoolean) {
    OutputLogicalNot();
  } else {
    DCHECK_EQ(mode, ToBooleanMode::kConvertToBoolean);
    OutputToBooleanLogicalNot();
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::TypeOf() {
  OutputTypeOf();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::GetSuperConstructor(Register out) {
  OutputGetSuperConstructor(out);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CompareOperation(
    Token::Value op, Register reg, int feedback_slot) {
  DCHECK(feedback_slot != kNoFeedbackSlot);
  switch (op) {
    case Token::Value::EQ:
      OutputTestEqual(reg, feedback_slot);
      break;
    case Token::Value::EQ_STRICT:
      OutputTestEqualStrict(reg, feedback_slot);
      break;
    case Token::Value::LT:
      OutputTestLessThan(reg, feedback_slot);
      break;
    case Token::Value::GT:
      OutputTestGreaterThan(reg, feedback_slot);
      break;
    case Token::Value::LTE:
      OutputTestLessThanOrEqual(reg, feedback_slot);
      break;
    case Token::Value::GTE:
      OutputTestGreaterThanOrEqual(reg, feedback_slot);
      break;
    default:
      UNREACHABLE();
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CompareOperation(Token::Value op,
                                                             Register reg) {
  switch (op) {
    case Token::Value::EQ_STRICT:
      OutputTestEqualStrictNoFeedback(reg);
      break;
    case Token::Value::INSTANCEOF:
      OutputTestInstanceOf(reg);
      break;
    case Token::Value::IN:
      OutputTestIn(reg);
      break;
    default:
      UNREACHABLE();
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CompareUndetectable() {
  OutputTestUndetectable();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CompareUndefined() {
  OutputTestUndefined();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CompareNull() {
  OutputTestNull();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CompareNil(Token::Value op,
                                                       NilValue nil) {
  if (op == Token::EQ) {
    return CompareUndetectable();
  } else {
    DCHECK_EQ(Token::EQ_STRICT, op);
    if (nil == kUndefinedValue) {
      return CompareUndefined();
    } else {
      DCHECK_EQ(kNullValue, nil);
      return CompareNull();
    }
  }
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CompareTypeOf(
    TestTypeOfFlags::LiteralFlag literal_flag) {
  DCHECK(literal_flag != TestTypeOfFlags::LiteralFlag::kOther);
  OutputTestTypeOf(TestTypeOfFlags::Encode(literal_flag));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadConstantPoolEntry(
    size_t entry) {
  OutputLdaConstant(entry);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadLiteral(
    v8::internal::Smi* smi) {
  int32_t raw_smi = smi->value();
  if (raw_smi == 0) {
    OutputLdaZero();
  } else {
    OutputLdaSmi(raw_smi);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadLiteral(
    const AstRawString* raw_string) {
  size_t entry = GetConstantPoolEntry(raw_string);
  OutputLdaConstant(entry);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadLiteral(const Scope* scope) {
  size_t entry = GetConstantPoolEntry(scope);
  OutputLdaConstant(entry);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadLiteral(
    const AstValue* ast_value) {
  if (ast_value->IsSmi()) {
    return LoadLiteral(ast_value->AsSmi());
  } else if (ast_value->IsUndefined()) {
    return LoadUndefined();
  } else if (ast_value->IsTrue()) {
    return LoadTrue();
  } else if (ast_value->IsFalse()) {
    return LoadFalse();
  } else if (ast_value->IsNull()) {
    return LoadNull();
  } else if (ast_value->IsTheHole()) {
    return LoadTheHole();
  } else if (ast_value->IsString()) {
    return LoadLiteral(ast_value->AsString());
  } else if (ast_value->IsHeapNumber()) {
    size_t entry = GetConstantPoolEntry(ast_value);
    OutputLdaConstant(entry);
    return *this;
  } else {
    // This should be the only ast value type left.
    DCHECK(ast_value->IsSymbol());
    size_t entry;
    switch (ast_value->AsSymbol()) {
      case AstSymbol::kHomeObjectSymbol:
        entry = HomeObjectSymbolConstantPoolEntry();
        break;
        // No default case so that we get a warning if AstSymbol changes
    }
    OutputLdaConstant(entry);
    return *this;
  }
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadUndefined() {
  OutputLdaUndefined();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadNull() {
  OutputLdaNull();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadTheHole() {
  OutputLdaTheHole();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadTrue() {
  OutputLdaTrue();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadFalse() {
  OutputLdaFalse();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadAccumulatorWithRegister(
    Register reg) {
  if (register_optimizer_) {
    // Defer source info so that if we elide the bytecode transfer, we attach
    // the source info to a subsequent bytecode or to a nop.
    SetDeferredSourceInfo(CurrentSourcePosition(Bytecode::kLdar));
    register_optimizer_->DoLdar(reg);
  } else {
    OutputLdar(reg);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreAccumulatorInRegister(
    Register reg) {
  if (register_optimizer_) {
    // Defer source info so that if we elide the bytecode transfer, we attach
    // the source info to a subsequent bytecode or to a nop.
    SetDeferredSourceInfo(CurrentSourcePosition(Bytecode::kStar));
    register_optimizer_->DoStar(reg);
  } else {
    OutputStar(reg);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::MoveRegister(Register from,
                                                         Register to) {
  DCHECK(from != to);
  if (register_optimizer_) {
    // Defer source info so that if we elide the bytecode transfer, we attach
    // the source info to a subsequent bytecode or to a nop.
    SetDeferredSourceInfo(CurrentSourcePosition(Bytecode::kMov));
    register_optimizer_->DoMov(from, to);
  } else {
    OutputMov(from, to);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadGlobal(const AstRawString* name,
                                                       int feedback_slot,
                                                       TypeofMode typeof_mode) {
  size_t name_index = GetConstantPoolEntry(name);
  // Ensure that typeof mode is in sync with the IC slot kind if the function
  // literal is available (not a unit test case).
  // TODO(ishell): check only in debug mode.
  if (literal_) {
    FeedbackSlot slot = FeedbackVector::ToSlot(feedback_slot);
    CHECK_EQ(GetTypeofModeFromSlotKind(feedback_vector_spec()->GetKind(slot)),
             typeof_mode);
  }
  if (typeof_mode == INSIDE_TYPEOF) {
    OutputLdaGlobalInsideTypeof(name_index, feedback_slot);
  } else {
    DCHECK_EQ(typeof_mode, NOT_INSIDE_TYPEOF);
    OutputLdaGlobal(name_index, feedback_slot);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreGlobal(
    const AstRawString* name, int feedback_slot, LanguageMode language_mode) {
  size_t name_index = GetConstantPoolEntry(name);
  if (language_mode == SLOPPY) {
    OutputStaGlobalSloppy(name_index, feedback_slot);
  } else {
    DCHECK_EQ(language_mode, STRICT);
    OutputStaGlobalStrict(name_index, feedback_slot);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadContextSlot(
    Register context, int slot_index, int depth,
    ContextSlotMutability mutability) {
  if (context.is_current_context() && depth == 0) {
    if (mutability == kImmutableSlot) {
      OutputLdaImmutableCurrentContextSlot(slot_index);
    } else {
      DCHECK_EQ(kMutableSlot, mutability);
      OutputLdaCurrentContextSlot(slot_index);
    }
  } else if (mutability == kImmutableSlot) {
    OutputLdaImmutableContextSlot(context, slot_index, depth);
  } else {
    DCHECK_EQ(mutability, kMutableSlot);
    OutputLdaContextSlot(context, slot_index, depth);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreContextSlot(Register context,
                                                             int slot_index,
                                                             int depth) {
  if (context.is_current_context() && depth == 0) {
    OutputStaCurrentContextSlot(slot_index);
  } else {
    OutputStaContextSlot(context, slot_index, depth);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadLookupSlot(
    const AstRawString* name, TypeofMode typeof_mode) {
  size_t name_index = GetConstantPoolEntry(name);
  if (typeof_mode == INSIDE_TYPEOF) {
    OutputLdaLookupSlotInsideTypeof(name_index);
  } else {
    DCHECK_EQ(typeof_mode, NOT_INSIDE_TYPEOF);
    OutputLdaLookupSlot(name_index);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadLookupContextSlot(
    const AstRawString* name, TypeofMode typeof_mode, int slot_index,
    int depth) {
  size_t name_index = GetConstantPoolEntry(name);
  if (typeof_mode == INSIDE_TYPEOF) {
    OutputLdaLookupContextSlotInsideTypeof(name_index, slot_index, depth);
  } else {
    DCHECK(typeof_mode == NOT_INSIDE_TYPEOF);
    OutputLdaLookupContextSlot(name_index, slot_index, depth);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadLookupGlobalSlot(
    const AstRawString* name, TypeofMode typeof_mode, int feedback_slot,
    int depth) {
  size_t name_index = GetConstantPoolEntry(name);
  if (typeof_mode == INSIDE_TYPEOF) {
    OutputLdaLookupGlobalSlotInsideTypeof(name_index, feedback_slot, depth);
  } else {
    DCHECK(typeof_mode == NOT_INSIDE_TYPEOF);
    OutputLdaLookupGlobalSlot(name_index, feedback_slot, depth);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreLookupSlot(
    const AstRawString* name, LanguageMode language_mode,
    LookupHoistingMode lookup_hoisting_mode) {
  size_t name_index = GetConstantPoolEntry(name);
  uint8_t flags =
      StoreLookupSlotFlags::Encode(language_mode, lookup_hoisting_mode);
  OutputStaLookupSlot(name_index, flags);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadNamedProperty(
    Register object, const AstRawString* name, int feedback_slot) {
  size_t name_index = GetConstantPoolEntry(name);
  OutputLdaNamedProperty(object, name_index, feedback_slot);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadKeyedProperty(
    Register object, int feedback_slot) {
  OutputLdaKeyedProperty(object, feedback_slot);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadIteratorProperty(
    Register object, int feedback_slot) {
  size_t name_index = IteratorSymbolConstantPoolEntry();
  OutputLdaNamedProperty(object, name_index, feedback_slot);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadAsyncIteratorProperty(
    Register object, int feedback_slot) {
  size_t name_index = AsyncIteratorSymbolConstantPoolEntry();
  OutputLdaNamedProperty(object, name_index, feedback_slot);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreDataPropertyInLiteral(
    Register object, Register name, DataPropertyInLiteralFlags flags,
    int feedback_slot) {
  OutputStaDataPropertyInLiteral(object, name, flags, feedback_slot);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CollectTypeProfile(int position) {
  DCHECK(FLAG_type_profile);
  OutputCollectTypeProfile(position);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreNamedProperty(
    Register object, size_t name_index, int feedback_slot,
    LanguageMode language_mode) {
  // Ensure that language mode is in sync with the IC slot kind if the function
  // literal is available (not a unit test case).
  // TODO(ishell): check only in debug mode.
  if (literal_) {
    FeedbackSlot slot = FeedbackVector::ToSlot(feedback_slot);
    CHECK_EQ(GetLanguageModeFromSlotKind(feedback_vector_spec()->GetKind(slot)),
             language_mode);
  }
  if (language_mode == SLOPPY) {
    OutputStaNamedPropertySloppy(object, name_index, feedback_slot);
  } else {
    DCHECK_EQ(language_mode, STRICT);
    OutputStaNamedPropertyStrict(object, name_index, feedback_slot);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreNamedProperty(
    Register object, const AstRawString* name, int feedback_slot,
    LanguageMode language_mode) {
  size_t name_index = GetConstantPoolEntry(name);
  return StoreNamedProperty(object, name_index, feedback_slot, language_mode);
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreNamedOwnProperty(
    Register object, const AstRawString* name, int feedback_slot) {
  size_t name_index = GetConstantPoolEntry(name);
  // Ensure that the store operation is in sync with the IC slot kind if
  // the function literal is available (not a unit test case).
  // TODO(ishell): check only in debug mode.
  if (literal_) {
    FeedbackSlot slot = FeedbackVector::ToSlot(feedback_slot);
    CHECK_EQ(FeedbackSlotKind::kStoreOwnNamed,
             feedback_vector_spec()->GetKind(slot));
  }
  OutputStaNamedOwnProperty(object, name_index, feedback_slot);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreKeyedProperty(
    Register object, Register key, int feedback_slot,
    LanguageMode language_mode) {
  // Ensure that language mode is in sync with the IC slot kind if the function
  // literal is available (not a unit test case).
  // TODO(ishell): check only in debug mode.
  if (literal_) {
    FeedbackSlot slot = FeedbackVector::ToSlot(feedback_slot);
    CHECK_EQ(GetLanguageModeFromSlotKind(feedback_vector_spec()->GetKind(slot)),
             language_mode);
  }
  if (language_mode == SLOPPY) {
    OutputStaKeyedPropertySloppy(object, key, feedback_slot);
  } else {
    DCHECK_EQ(language_mode, STRICT);
    OutputStaKeyedPropertyStrict(object, key, feedback_slot);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreHomeObjectProperty(
    Register object, int feedback_slot, LanguageMode language_mode) {
  size_t name_index = HomeObjectSymbolConstantPoolEntry();
  return StoreNamedProperty(object, name_index, feedback_slot, language_mode);
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateClosure(
    size_t shared_function_info_entry, int slot, int flags) {
  OutputCreateClosure(shared_function_info_entry, slot, flags);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateBlockContext(
    const Scope* scope) {
  size_t entry = GetConstantPoolEntry(scope);
  OutputCreateBlockContext(entry);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateCatchContext(
    Register exception, const AstRawString* name, const Scope* scope) {
  size_t name_index = GetConstantPoolEntry(name);
  size_t scope_index = GetConstantPoolEntry(scope);
  OutputCreateCatchContext(exception, name_index, scope_index);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateFunctionContext(int slots) {
  OutputCreateFunctionContext(slots);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateEvalContext(int slots) {
  OutputCreateEvalContext(slots);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateWithContext(
    Register object, const Scope* scope) {
  size_t scope_index = GetConstantPoolEntry(scope);
  OutputCreateWithContext(object, scope_index);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateArguments(
    CreateArgumentsType type) {
  switch (type) {
    case CreateArgumentsType::kMappedArguments:
      OutputCreateMappedArguments();
      break;
    case CreateArgumentsType::kUnmappedArguments:
      OutputCreateUnmappedArguments();
      break;
    case CreateArgumentsType::kRestParameter:
      OutputCreateRestParameter();
      break;
    default:
      UNREACHABLE();
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateRegExpLiteral(
    const AstRawString* pattern, int literal_index, int flags) {
  size_t pattern_entry = GetConstantPoolEntry(pattern);
  OutputCreateRegExpLiteral(pattern_entry, literal_index, flags);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateArrayLiteral(
    size_t constant_elements_entry, int literal_index, int flags) {
  OutputCreateArrayLiteral(constant_elements_entry, literal_index, flags);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateObjectLiteral(
    size_t constant_properties_entry, int literal_index, int flags,
    Register output) {
  OutputCreateObjectLiteral(constant_properties_entry, literal_index, flags,
                            output);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::PushContext(Register context) {
  OutputPushContext(context);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::PopContext(Register context) {
  OutputPopContext(context);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ToObject(Register out) {
  OutputToObject(out);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ToName(Register out) {
  OutputToName(out);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ToNumber(Register out,
                                                     int feedback_slot) {
  OutputToNumber(out, feedback_slot);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ToPrimitiveToString(
    Register out, int feedback_slot) {
  OutputToPrimitiveToString(out, feedback_slot);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StringConcat(
    RegisterList operand_registers) {
  OutputStringConcat(operand_registers, operand_registers.register_count());
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Bind(BytecodeLabel* label) {
  // Flush the register optimizer when binding a label to ensure all
  // expected registers are valid when jumping to this label.
  if (register_optimizer_) register_optimizer_->Flush();

  size_t current_offset = bytecodes()->size();
  if (label->is_forward_target()) {
    // An earlier jump instruction refers to this label. Update it's location.
    PatchJump(current_offset, label->offset());
    // Now treat as if the label will only be back referred to.
  }
  label->bind_to(current_offset);
  InvalidateLastBytecode();

  // Starting a new basic block.
  LeaveBasicBlock();

  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Bind(const BytecodeLabel& target,
                                                 BytecodeLabel* label) {
  DCHECK(!label->is_bound());
  DCHECK(target.is_bound());
  if (label->is_forward_target()) {
    // An earlier jump instruction refers to this label. Update it's location.
    PatchJump(target.offset(), label->offset());
    // Now treat as if the label will only be back referred to.
  }
  label->bind_to(target.offset());
  InvalidateLastBytecode();
  // exit_seen_in_block_ was reset when target was bound, so shouldn't be
  // changed here.

  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Bind(BytecodeJumpTable* jump_table,
                                                 int case_value) {
  // Flush the register optimizer when binding a jump table entry to ensure
  // all expected registers are valid when jumping to this location.
  if (register_optimizer_) register_optimizer_->Flush();

  DCHECK(!jump_table->is_bound(case_value));

  size_t current_offset = bytecodes()->size();
  size_t relative_jump = current_offset - jump_table->switch_bytecode_offset();

  constant_array_builder()->SetJumpTableSmi(
      jump_table->ConstantPoolEntryFor(case_value),
      Smi::FromInt(static_cast<int>(relative_jump)));
  jump_table->mark_bound(case_value);
  InvalidateLastBytecode();

  // Starting a new basic block.
  LeaveBasicBlock();

  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Jump(BytecodeLabel* label) {
  DCHECK(!label->is_bound());
  OutputJump(label, 0);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfTrue(ToBooleanMode mode,
                                                       BytecodeLabel* label) {
  DCHECK(!label->is_bound());
  if (mode == ToBooleanMode::kAlreadyBoolean) {
    OutputJumpIfTrue(label, 0);
  } else {
    DCHECK_EQ(mode, ToBooleanMode::kConvertToBoolean);
    OutputJumpIfToBooleanTrue(label, 0);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfFalse(ToBooleanMode mode,
                                                        BytecodeLabel* label) {
  DCHECK(!label->is_bound());
  if (mode == ToBooleanMode::kAlreadyBoolean) {
    OutputJumpIfFalse(label, 0);
  } else {
    DCHECK_EQ(mode, ToBooleanMode::kConvertToBoolean);
    OutputJumpIfToBooleanFalse(label, 0);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfNull(BytecodeLabel* label) {
  DCHECK(!label->is_bound());
  OutputJumpIfNull(label, 0);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfNotNull(
    BytecodeLabel* label) {
  DCHECK(!label->is_bound());
  OutputJumpIfNotNull(label, 0);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfUndefined(
    BytecodeLabel* label) {
  DCHECK(!label->is_bound());
  OutputJumpIfUndefined(label, 0);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfNotUndefined(
    BytecodeLabel* label) {
  DCHECK(!label->is_bound());
  OutputJumpIfNotUndefined(label, 0);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfNil(BytecodeLabel* label,
                                                      Token::Value op,
                                                      NilValue nil) {
  if (op == Token::EQ) {
    // TODO(rmcilroy): Implement JumpIfUndetectable.
    return CompareUndetectable().JumpIfTrue(ToBooleanMode::kAlreadyBoolean,
                                            label);
  } else {
    DCHECK_EQ(Token::EQ_STRICT, op);
    if (nil == kUndefinedValue) {
      return JumpIfUndefined(label);
    } else {
      DCHECK_EQ(kNullValue, nil);
      return JumpIfNull(label);
    }
  }
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfNotNil(BytecodeLabel* label,
                                                         Token::Value op,
                                                         NilValue nil) {
  if (op == Token::EQ) {
    // TODO(rmcilroy): Implement JumpIfUndetectable.
    return CompareUndetectable().JumpIfFalse(ToBooleanMode::kAlreadyBoolean,
                                             label);
  } else {
    DCHECK_EQ(Token::EQ_STRICT, op);
    if (nil == kUndefinedValue) {
      return JumpIfNotUndefined(label);
    } else {
      DCHECK_EQ(kNullValue, nil);
      return JumpIfNotNull(label);
    }
  }
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfJSReceiver(
    BytecodeLabel* label) {
  DCHECK(!label->is_bound());
  OutputJumpIfJSReceiver(label, 0);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpLoop(BytecodeLabel* label,
                                                     int loop_depth) {
  DCHECK(label->is_bound());
  OutputJumpLoop(label, 0, loop_depth);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::SwitchOnSmiNoFeedback(
    BytecodeJumpTable* jump_table) {
  OutputSwitchOnSmiNoFeedback(jump_table);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StackCheck(int position) {
  if (position != kNoSourcePosition) {
    // We need to attach a non-breakable source position to a stack
    // check, so we simply add it as expression position. There can be
    // a prior statement position from constructs like:
    //
    //    do var x;  while (false);
    //
    // A Nop could be inserted for empty statements, but since no code
    // is associated with these positions, instead we force the stack
    // check's expression position which eliminates the empty
    // statement's position.
    latest_source_info_.ForceExpressionPosition(position);
  }
  OutputStackCheck();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::SetPendingMessage() {
  OutputSetPendingMessage();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Throw() {
  OutputThrow();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ReThrow() {
  OutputReThrow();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Return() {
  SetReturnPosition();
  OutputReturn();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ThrowReferenceErrorIfHole(
    const AstRawString* name) {
  size_t entry = GetConstantPoolEntry(name);
  OutputThrowReferenceErrorIfHole(entry);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ThrowSuperNotCalledIfHole() {
  OutputThrowSuperNotCalledIfHole();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ThrowSuperAlreadyCalledIfNotHole() {
  OutputThrowSuperAlreadyCalledIfNotHole();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Debugger() {
  OutputDebugger();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::IncBlockCounter(
    int coverage_array_slot) {
  OutputIncBlockCounter(coverage_array_slot);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ForInPrepare(
    Register receiver, RegisterList cache_info_triple) {
  DCHECK_EQ(3, cache_info_triple.register_count());
  OutputForInPrepare(receiver, cache_info_triple);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ForInContinue(
    Register index, Register cache_length) {
  OutputForInContinue(index, cache_length);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ForInNext(
    Register receiver, Register index, RegisterList cache_type_array_pair,
    int feedback_slot) {
  DCHECK_EQ(2, cache_type_array_pair.register_count());
  OutputForInNext(receiver, index, cache_type_array_pair, feedback_slot);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ForInStep(Register index) {
  OutputForInStep(index);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreModuleVariable(int cell_index,
                                                                int depth) {
  OutputStaModuleVariable(cell_index, depth);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadModuleVariable(int cell_index,
                                                               int depth) {
  OutputLdaModuleVariable(cell_index, depth);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::SuspendGenerator(
    Register generator, RegisterList registers, SuspendFlags flags) {
  OutputSuspendGenerator(generator, registers, registers.register_count(),
                         SuspendGeneratorBytecodeFlags::Encode(flags));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::RestoreGeneratorState(
    Register generator) {
  OutputRestoreGeneratorState(generator);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::RestoreGeneratorRegisters(
    Register generator, RegisterList registers) {
  OutputRestoreGeneratorRegisters(generator, registers,
                                  registers.register_count());
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::MarkHandler(
    int handler_id, HandlerTable::CatchPrediction catch_prediction) {
  BytecodeLabel handler;
  Bind(&handler);
  handler_table_builder()->SetHandlerTarget(handler_id, handler.offset());
  handler_table_builder()->SetPrediction(handler_id, catch_prediction);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::MarkTryBegin(int handler_id,
                                                         Register context) {
  BytecodeLabel try_begin;
  Bind(&try_begin);
  handler_table_builder()->SetTryRegionStart(handler_id, try_begin.offset());
  handler_table_builder()->SetContextRegister(handler_id, context);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::MarkTryEnd(int handler_id) {
  BytecodeLabel try_end;
  Bind(&try_end);
  handler_table_builder()->SetTryRegionEnd(handler_id, try_end.offset());
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallProperty(Register callable,
                                                         RegisterList args,
                                                         int feedback_slot) {
  if (args.register_count() == 1) {
    OutputCallProperty0(callable, args[0], feedback_slot);
  } else if (args.register_count() == 2) {
    OutputCallProperty1(callable, args[0], args[1], feedback_slot);
  } else if (args.register_count() == 3) {
    OutputCallProperty2(callable, args[0], args[1], args[2], feedback_slot);
  } else {
    OutputCallProperty(callable, args, args.register_count(), feedback_slot);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallUndefinedReceiver(
    Register callable, RegisterList args, int feedback_slot) {
  if (args.register_count() == 0) {
    OutputCallUndefinedReceiver0(callable, feedback_slot);
  } else if (args.register_count() == 1) {
    OutputCallUndefinedReceiver1(callable, args[0], feedback_slot);
  } else if (args.register_count() == 2) {
    OutputCallUndefinedReceiver2(callable, args[0], args[1], feedback_slot);
  } else {
    OutputCallUndefinedReceiver(callable, args, args.register_count(),
                                feedback_slot);
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallAnyReceiver(Register callable,
                                                            RegisterList args,
                                                            int feedback_slot) {
  OutputCallAnyReceiver(callable, args, args.register_count(), feedback_slot);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::TailCall(Register callable,
                                                     RegisterList args,
                                                     int feedback_slot) {
  OutputTailCall(callable, args, args.register_count(), feedback_slot);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallWithSpread(Register callable,
                                                           RegisterList args) {
  OutputCallWithSpread(callable, args, args.register_count());
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Construct(Register constructor,
                                                      RegisterList args,
                                                      int feedback_slot_id) {
  OutputConstruct(constructor, args, args.register_count(), feedback_slot_id);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ConstructWithSpread(
    Register constructor, RegisterList args) {
  OutputConstructWithSpread(constructor, args, args.register_count());
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallRuntime(
    Runtime::FunctionId function_id, RegisterList args) {
  DCHECK_EQ(1, Runtime::FunctionForId(function_id)->result_size);
  DCHECK(Bytecodes::SizeForUnsignedOperand(function_id) <= OperandSize::kShort);
  if (IntrinsicsHelper::IsSupported(function_id)) {
    IntrinsicsHelper::IntrinsicId intrinsic_id =
        IntrinsicsHelper::FromRuntimeId(function_id);
    OutputInvokeIntrinsic(static_cast<int>(intrinsic_id), args,
                          args.register_count());
  } else {
    OutputCallRuntime(static_cast<int>(function_id), args,
                      args.register_count());
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallRuntime(
    Runtime::FunctionId function_id, Register arg) {
  return CallRuntime(function_id, RegisterList(arg.index(), 1));
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallRuntime(
    Runtime::FunctionId function_id) {
  return CallRuntime(function_id, RegisterList());
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallRuntimeForPair(
    Runtime::FunctionId function_id, RegisterList args,
    RegisterList return_pair) {
  DCHECK_EQ(2, Runtime::FunctionForId(function_id)->result_size);
  DCHECK(Bytecodes::SizeForUnsignedOperand(function_id) <= OperandSize::kShort);
  DCHECK_EQ(2, return_pair.register_count());
  OutputCallRuntimeForPair(static_cast<uint16_t>(function_id), args,
                           args.register_count(), return_pair);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallRuntimeForPair(
    Runtime::FunctionId function_id, Register arg, RegisterList return_pair) {
  return CallRuntimeForPair(function_id, RegisterList(arg.index(), 1),
                            return_pair);
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallJSRuntime(int context_index,
                                                          RegisterList args) {
  OutputCallJSRuntime(context_index, args, args.register_count());
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Delete(Register object,
                                                   LanguageMode language_mode) {
  if (language_mode == SLOPPY) {
    OutputDeletePropertySloppy(object);
  } else {
    DCHECK_EQ(language_mode, STRICT);
    OutputDeletePropertyStrict(object);
  }
  return *this;
}

size_t BytecodeArrayBuilder::GetConstantPoolEntry(
    const AstRawString* raw_string) {
  return constant_array_builder()->Insert(raw_string);
}

size_t BytecodeArrayBuilder::GetConstantPoolEntry(const AstValue* heap_number) {
  DCHECK(heap_number->IsHeapNumber());
  return constant_array_builder()->Insert(heap_number);
}

size_t BytecodeArrayBuilder::GetConstantPoolEntry(const Scope* scope) {
  return constant_array_builder()->Insert(scope);
}

#define ENTRY_GETTER(NAME, ...)                            \
  size_t BytecodeArrayBuilder::NAME##ConstantPoolEntry() { \
    return constant_array_builder()->Insert##NAME();       \
  }
SINGLETON_CONSTANT_ENTRY_TYPES(ENTRY_GETTER)
#undef ENTRY_GETTER

BytecodeJumpTable* BytecodeArrayBuilder::AllocateJumpTable(
    int size, int case_value_base) {
  DCHECK_GT(size, 0);

  size_t constant_pool_index = constant_array_builder()->InsertJumpTable(size);

  return new (zone())
      BytecodeJumpTable(constant_pool_index, size, case_value_base, zone());
}

size_t BytecodeArrayBuilder::AllocateDeferredConstantPoolEntry() {
  return constant_array_builder()->InsertDeferred();
}

void BytecodeArrayBuilder::SetDeferredConstantPoolEntry(size_t entry,
                                                        Handle<Object> object) {
  constant_array_builder()->SetDeferredAt(entry, object);
}

void BytecodeArrayBuilder::SetReturnPosition() {
  if (return_position_ == kNoSourcePosition) return;
  latest_source_info_.MakeStatementPosition(return_position_);
}

bool BytecodeArrayBuilder::RegisterIsValid(Register reg) const {
  if (!reg.is_valid()) {
    return false;
  }

  if (reg.is_current_context() || reg.is_function_closure() ||
      reg.is_new_target()) {
    return true;
  } else if (reg.is_parameter()) {
    int parameter_index = reg.ToParameterIndex(parameter_count());
    return parameter_index >= 0 && parameter_index < parameter_count();
  } else if (reg.index() < fixed_register_count()) {
    return true;
  } else {
    return register_allocator()->RegisterIsLive(reg);
  }
}

bool BytecodeArrayBuilder::RegisterListIsValid(RegisterList reg_list) const {
  if (reg_list.register_count() == 0) {
    return reg_list.first_register() == Register(0);
  } else {
    int first_reg_index = reg_list.first_register().index();
    for (int i = 0; i < reg_list.register_count(); i++) {
      if (!RegisterIsValid(Register(first_reg_index + i))) {
        return false;
      }
    }
    return true;
  }
}

template <Bytecode bytecode, AccumulatorUse accumulator_use,
          OperandType... operand_types>
void BytecodeArrayBuilder::PrepareToOutputBytecode() {
  if (register_optimizer_)
    register_optimizer_->PrepareForBytecode<bytecode, accumulator_use>();
}

uint32_t BytecodeArrayBuilder::GetInputRegisterOperand(Register reg) {
  DCHECK(RegisterIsValid(reg));
  if (register_optimizer_) reg = register_optimizer_->GetInputRegister(reg);
  return static_cast<uint32_t>(reg.ToOperand());
}

uint32_t BytecodeArrayBuilder::GetOutputRegisterOperand(Register reg) {
  DCHECK(RegisterIsValid(reg));
  if (register_optimizer_) register_optimizer_->PrepareOutputRegister(reg);
  return static_cast<uint32_t>(reg.ToOperand());
}

uint32_t BytecodeArrayBuilder::GetInputRegisterListOperand(
    RegisterList reg_list) {
  DCHECK(RegisterListIsValid(reg_list));
  if (register_optimizer_)
    reg_list = register_optimizer_->GetInputRegisterList(reg_list);
  return static_cast<uint32_t>(reg_list.first_register().ToOperand());
}

uint32_t BytecodeArrayBuilder::GetOutputRegisterListOperand(
    RegisterList reg_list) {
  DCHECK(RegisterListIsValid(reg_list));
  if (register_optimizer_)
    register_optimizer_->PrepareOutputRegisterList(reg_list);
  return static_cast<uint32_t>(reg_list.first_register().ToOperand());
}

std::ostream& operator<<(std::ostream& os,
                         const BytecodeArrayBuilder::ToBooleanMode& mode) {
  switch (mode) {
    case BytecodeArrayBuilder::ToBooleanMode::kAlreadyBoolean:
      return os << "AlreadyBoolean";
    case BytecodeArrayBuilder::ToBooleanMode::kConvertToBoolean:
      return os << "ConvertToBoolean";
  }
  UNREACHABLE();
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
