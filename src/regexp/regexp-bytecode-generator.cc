// Copyright 2008-2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-bytecode-generator.h"

#include <limits>
#include <type_traits>

#include "src/ast/ast.h"
#include "src/objects/fixed-array-inl.h"
#include "src/regexp/regexp-bytecode-generator-inl.h"
#include "src/regexp/regexp-bytecode-peephole.h"
#include "src/regexp/regexp-bytecodes-inl.h"
#include "src/regexp/regexp-bytecodes.h"
#include "src/regexp/regexp-macro-assembler.h"

namespace v8 {
namespace internal {

RegExpBytecodeGenerator::RegExpBytecodeGenerator(Isolate* isolate, Zone* zone)
    : RegExpMacroAssembler(isolate, zone),
      buffer_(kInitialBufferSize, zone),
      pc_(0),
      advance_current_end_(kInvalidPC),
      jump_edges_(zone),
      isolate_(isolate) {}

RegExpBytecodeGenerator::~RegExpBytecodeGenerator() {
  if (backtrack_.is_linked()) backtrack_.Unuse();
}

RegExpBytecodeGenerator::IrregexpImplementation
RegExpBytecodeGenerator::Implementation() {
  return kBytecodeImplementation;
}

namespace {

// Helper to determine if Operands update the current 4-byte word or emit full
// words on their own.
template <RegExpBytecodeOperandType OperandType>
struct OperandUpdatesWord {
  static constexpr bool value = true;
};

template <>
struct OperandUpdatesWord<RegExpBytecodeOperandType::kJumpTarget> {
  static constexpr bool value = false;
};

template <>
struct OperandUpdatesWord<RegExpBytecodeOperandType::kBitTable> {
  static constexpr bool value = false;
};

template <typename Operands, Operands::OptionalOperand op>
consteval bool EmitOperandUpdatesWord() {
  if constexpr (op.has_value) {
    constexpr RegExpBytecodeOperandType op_type = Operands::Type(op.value);
    return OperandUpdatesWord<op_type>::value;
  } else {
    return false;
  }
}

}  // namespace

template <RegExpBytecode bytecode, typename... Args>
void RegExpBytecodeGenerator::Emit(Args... args) {
  using Operands = RegExpBytecodeOperands<bytecode>;
  static_assert(sizeof...(Args) == Operands::kCountWithoutPadding,
                "Wrong number of operands");

  auto arguments_tuple = std::make_tuple(args...);
  EnsureCapacity(Operands::kTotalSize);
  int instruction_start = pc_;
  // We always write a 4-byte word at a time, accumulating the current bytes
  // in `cur_word`.
  BCWordT cur_word = RegExpBytecodes::ToByte(bytecode);
  Operands::ForEachOperandWithIndex([&]<auto op, size_t index>() {
    constexpr RegExpBytecodeOperandType type = Operands::Type(op);
    constexpr int offset = Operands::Offset(op);
    constexpr int offset_in_word = offset % kRegExpBytecodeAlignment;
    auto value = std::get<index>(arguments_tuple);
    // The current operand starts a new 4-byte word. If previous operands
    // accumulated any data in `cur_word`, we need to emit it now.
    if constexpr (offset_in_word == 0) {
      // Determine if the previous operand updated `cur_word`.
      constexpr bool word_updated =
          EmitOperandUpdatesWord<Operands, Operands::Previous(op)>();
      // Emit `cur_word` if either the previous operand updated it, or the first
      // operand is 4-byte aligned. In the latter case we need to emit the
      // bytecode stored in `cur_word`.
      if constexpr (index == 0 || word_updated) {
        EmitWord(cur_word);
        cur_word = kEmptyWord;
      } else {
        DCHECK_EQ(cur_word, kEmptyWord);
      }
    }
    constexpr int shift = offset_in_word * kBitsPerByte;
    cur_word = EmitOperand<type>(value, cur_word, shift);
  });
  // Determine if the last operand updated `cur_word`.
  constexpr bool word_updated =
      EmitOperandUpdatesWord<Operands, Operands::Last()>();
  // Emit `cur_word` if either the last operand updated it, or there are no
  // operands. In the latter case we need to emit the bytecode stored in
  // `cur_word`.
  if constexpr (Operands::kCount == 0 || word_updated) {
    EmitWord(cur_word);
  } else {
    DCHECK_EQ(cur_word, kEmptyWord);
  }
  USE(instruction_start);
  DCHECK_EQ(pc_, instruction_start + Operands::kTotalSize);
}

namespace {

// Helper to get the underlying type of an enum, or the type itself if it isn't
// an enum.
template <typename T>
struct get_underlying_or_self {
  using type = T;
};

template <typename T>
  requires std::is_enum_v<T>
struct get_underlying_or_self<T> {
  using type = std::underlying_type_t<T>;
};

}  // namespace

template <RegExpBytecodeOperandType OperandType, typename T>
RegExpBytecodeGenerator::BCWordT RegExpBytecodeGenerator::EmitOperand(
    T value, BCWordT cur_word, int shift) {
  static_assert(RegExpOperandTypeTraits<OperandType>::kIsBasic);
  static_assert(OperandUpdatesWord<OperandType>::value);
  using Traits = RegExpOperandTypeTraits<OperandType>;
  using EnumOrCType = Traits::kCType;
  using CType = get_underlying_or_self<EnumOrCType>::type;
  if constexpr (std::is_enum_v<EnumOrCType>) {
    static_assert(std::is_same_v<T, EnumOrCType>);
  } else {
    static_assert(std::is_convertible_v<T, CType>);
  }
  DCHECK_GE(value, Traits::kMinValue);
  DCHECK_LE(value, Traits::kMaxValue);
  return cur_word | static_cast<CType>(value) << shift;
}

template <>
RegExpBytecodeGenerator::BCWordT
RegExpBytecodeGenerator::EmitOperand<ReBcOpType::kJumpTarget>(Label* label,
                                                              BCWordT cur_word,
                                                              int shift) {
  static_assert(!OperandUpdatesWord<ReBcOpType::kJumpTarget>::value);
  DCHECK_EQ(cur_word, kEmptyWord);
  DCHECK_EQ(shift, 0);
  EmitOrLink(label);
  return kEmptyWord;
}

template <>
RegExpBytecodeGenerator::BCWordT
RegExpBytecodeGenerator::EmitOperand<ReBcOpType::kBitTable>(
    Handle<ByteArray> table, BCWordT cur_word, int shift) {
  static_assert(!OperandUpdatesWord<ReBcOpType::kBitTable>::value);
  DCHECK_EQ(cur_word, kEmptyWord);
  DCHECK_EQ(shift, 0);
  BCWordT word = kEmptyWord;
  static constexpr int kWordSizeBits = sizeof(word) * kBitsPerByte;
  for (int w = 0; w < kTableSize; w += kWordSizeBits) {
    for (int bit = 0; bit < kWordSizeBits; bit++) {
      if (table->get(w + bit) != 0) word |= 1 << bit;
    }
    EmitWord(word);
    word = kEmptyWord;
  }
  return kEmptyWord;
}

void RegExpBytecodeGenerator::Bind(Label* l) {
  advance_current_end_ = kInvalidPC;
  DCHECK(!l->is_bound());
  if (l->is_linked()) {
    int pos = l->pos();
    while (pos != 0) {
      int fixup = pos;
      pos = *reinterpret_cast<int32_t*>(buffer_.data() + fixup);
      *reinterpret_cast<BCWordT*>(buffer_.data() + fixup) = pc_;
      jump_edges_.emplace(fixup, pc_);
    }
  }
  l->bind_to(pc_);
}

void RegExpBytecodeGenerator::EmitOrLink(Label* l) {
  if (l == nullptr) l = &backtrack_;
  int pos = 0;
  if (l->is_bound()) {
    pos = l->pos();
    jump_edges_.emplace(pc_, pos);
  } else {
    if (l->is_linked()) {
      pos = l->pos();
    }
    l->link_to(pc_);
  }
  EmitWord(pos);
}

void RegExpBytecodeGenerator::PopRegister(int register_index) {
  Emit<RegExpBytecode::kPopRegister>(register_index);
}

void RegExpBytecodeGenerator::PushRegister(int register_index,
                                           StackCheckFlag check_stack_limit) {
  Emit<RegExpBytecode::kPushRegister>(register_index, check_stack_limit);
}

void RegExpBytecodeGenerator::WriteCurrentPositionToRegister(int register_index,
                                                             int cp_offset) {
  Emit<RegExpBytecode::kWriteCurrentPositionToRegister>(register_index,
                                                        cp_offset);
}

void RegExpBytecodeGenerator::ClearRegisters(int reg_from, int reg_to) {
  DCHECK_LE(reg_from, reg_to);
  Emit<RegExpBytecode::kClearRegisters>(reg_from, reg_to);
}

void RegExpBytecodeGenerator::ReadCurrentPositionFromRegister(
    int register_index) {
  Emit<RegExpBytecode::kReadCurrentPositionFromRegister>(register_index);
}

void RegExpBytecodeGenerator::WriteStackPointerToRegister(int register_index) {
  Emit<RegExpBytecode::kWriteStackPointerToRegister>(register_index);
}

void RegExpBytecodeGenerator::ReadStackPointerFromRegister(int register_index) {
  Emit<RegExpBytecode::kReadStackPointerFromRegister>(register_index);
}

void RegExpBytecodeGenerator::SetCurrentPositionFromEnd(int by) {
  Emit<RegExpBytecode::kSetCurrentPositionFromEnd>(by);
}

void RegExpBytecodeGenerator::SetRegister(int register_index, int to) {
  Emit<RegExpBytecode::kSetRegister>(register_index, to);
}

void RegExpBytecodeGenerator::AdvanceRegister(int register_index, int by) {
  Emit<RegExpBytecode::kAdvanceRegister>(register_index, by);
}

void RegExpBytecodeGenerator::PopCurrentPosition() {
  Emit<RegExpBytecode::kPopCurrentPosition>();
}

void RegExpBytecodeGenerator::PushCurrentPosition() {
  Emit<RegExpBytecode::kPushCurrentPosition>();
}

void RegExpBytecodeGenerator::Backtrack() {
  int error_code =
      can_fallback() ? RegExp::RE_FALLBACK_TO_EXPERIMENTAL : RegExp::RE_FAILURE;
  Emit<RegExpBytecode::kBacktrack>(error_code);
}

void RegExpBytecodeGenerator::GoTo(Label* l) {
  if (advance_current_end_ == pc_) {
    // Combine advance current and goto.
    pc_ = advance_current_start_;
    Emit<RegExpBytecode::kAdvanceCpAndGoto>(advance_current_offset_, l);
    advance_current_end_ = kInvalidPC;
  } else {
    // Regular goto.
    Emit<RegExpBytecode::kGoTo>(l);
  }
}

void RegExpBytecodeGenerator::PushBacktrack(Label* l) {
  Emit<RegExpBytecode::kPushBacktrack>(l);
}

bool RegExpBytecodeGenerator::Succeed() {
  Emit<RegExpBytecode::kSucceed>();
  return false;  // Restart matching for global regexp not supported.
}

void RegExpBytecodeGenerator::Fail() { Emit<RegExpBytecode::kFail>(); }

void RegExpBytecodeGenerator::AdvanceCurrentPosition(int by) {
  advance_current_start_ = pc_;
  advance_current_offset_ = by;
  Emit<RegExpBytecode::kAdvanceCurrentPosition>(by);
  advance_current_end_ = pc_;
}

void RegExpBytecodeGenerator::CheckFixedLengthLoop(
    Label* on_tos_equals_current_position) {
  Emit<RegExpBytecode::kCheckFixedLengthLoop>(on_tos_equals_current_position);
}

void RegExpBytecodeGenerator::CheckPosition(int cp_offset,
                                            Label* on_outside_input) {
  LoadCurrentCharacter(cp_offset, on_outside_input, true);
}

void RegExpBytecodeGenerator::LoadCurrentCharacterImpl(int cp_offset,
                                                       Label* on_failure,
                                                       bool check_bounds,
                                                       int characters,
                                                       int eats_at_least) {
  DCHECK_GE(eats_at_least, characters);
  if (eats_at_least > characters && check_bounds) {
    Emit<RegExpBytecode::kCheckPosition>(cp_offset + eats_at_least - 1,
                                         on_failure);
    check_bounds = false;  // Load below doesn't need to check.
  }

  DCHECK_LE(kMinCPOffset, cp_offset);
  DCHECK_GE(kMaxCPOffset, cp_offset);
  if (check_bounds) {
    if (characters == 4) {
      Emit<RegExpBytecode::kLoad4CurrentChars>(cp_offset, on_failure);
    } else if (characters == 2) {
      Emit<RegExpBytecode::kLoad2CurrentChars>(cp_offset, on_failure);
    } else {
      DCHECK_EQ(1, characters);
      Emit<RegExpBytecode::kLoadCurrentCharacter>(cp_offset, on_failure);
    }
  } else {
    if (characters == 4) {
      Emit<RegExpBytecode::kLoad4CurrentCharsUnchecked>(cp_offset);
    } else if (characters == 2) {
      Emit<RegExpBytecode::kLoad2CurrentCharsUnchecked>(cp_offset);
    } else {
      DCHECK_EQ(1, characters);
      Emit<RegExpBytecode::kLoadCurrentCharacterUnchecked>(cp_offset);
    }
  }
}

void RegExpBytecodeGenerator::CheckCharacterLT(base::uc16 limit,
                                               Label* on_less) {
  Emit<RegExpBytecode::kCheckCharacterLT>(limit, on_less);
}

void RegExpBytecodeGenerator::CheckCharacterGT(base::uc16 limit,
                                               Label* on_greater) {
  Emit<RegExpBytecode::kCheckCharacterGT>(limit, on_greater);
}

void RegExpBytecodeGenerator::CheckCharacter(uint32_t c, Label* on_equal) {
  if (c > MAX_FIRST_ARG) {
    Emit<RegExpBytecode::kCheck4Chars>(c, on_equal);
  } else {
    Emit<RegExpBytecode::kCheckCharacter>(c, on_equal);
  }
}

void RegExpBytecodeGenerator::CheckAtStart(int cp_offset, Label* on_at_start) {
  Emit<RegExpBytecode::kCheckAtStart>(cp_offset, on_at_start);
}

void RegExpBytecodeGenerator::CheckNotAtStart(int cp_offset,
                                              Label* on_not_at_start) {
  Emit<RegExpBytecode::kCheckNotAtStart>(cp_offset, on_not_at_start);
}

void RegExpBytecodeGenerator::CheckNotCharacter(uint32_t c,
                                                Label* on_not_equal) {
  if (c > MAX_FIRST_ARG) {
    Emit<RegExpBytecode::kCheckNot4Chars>(c, on_not_equal);
  } else {
    Emit<RegExpBytecode::kCheckNotCharacter>(c, on_not_equal);
  }
}

void RegExpBytecodeGenerator::CheckCharacterAfterAnd(uint32_t c, uint32_t mask,
                                                     Label* on_equal) {
  // TOOD(pthier): This is super hacky. We could still check for 4 characters
  // (with the last 2 being 0 after masking them), but not emit AndCheck4Chars.
  // This is rather confusing and should be changed.
  if (c > MAX_FIRST_ARG) {
    Emit<RegExpBytecode::kAndCheck4Chars>(c, mask, on_equal);
  } else {
    Emit<RegExpBytecode::kCheckCharacterAfterAnd>(c, mask, on_equal);
  }
}

void RegExpBytecodeGenerator::CheckNotCharacterAfterAnd(uint32_t c,
                                                        uint32_t mask,
                                                        Label* on_not_equal) {
  // TOOD(pthier): This is super hacky. We could still check for 4 characters
  // (with the last 2 being 0 after masking them), but not emit
  // AndCheckNot4Chars. This is rather confusing and should be changed.
  if (c > MAX_FIRST_ARG) {
    Emit<RegExpBytecode::kAndCheckNot4Chars>(c, mask, on_not_equal);
  } else {
    Emit<RegExpBytecode::kCheckNotCharacterAfterAnd>(c, mask, on_not_equal);
  }
}

void RegExpBytecodeGenerator::CheckNotCharacterAfterMinusAnd(
    base::uc16 c, base::uc16 minus, base::uc16 mask, Label* on_not_equal) {
  Emit<RegExpBytecode::kCheckNotCharacterAfterMinusAnd>(c, minus, mask,
                                                        on_not_equal);
}

void RegExpBytecodeGenerator::CheckCharacterInRange(base::uc16 from,
                                                    base::uc16 to,
                                                    Label* on_in_range) {
  Emit<RegExpBytecode::kCheckCharacterInRange>(from, to, on_in_range);
}

void RegExpBytecodeGenerator::CheckCharacterNotInRange(base::uc16 from,
                                                       base::uc16 to,
                                                       Label* on_not_in_range) {
  Emit<RegExpBytecode::kCheckCharacterNotInRange>(from, to, on_not_in_range);
}

void RegExpBytecodeGenerator::CheckBitInTable(Handle<ByteArray> table,
                                              Label* on_bit_set) {
  Emit<RegExpBytecode::kCheckBitInTable>(on_bit_set, table);
}

void RegExpBytecodeGenerator::SkipUntilBitInTable(
    int cp_offset, Handle<ByteArray> table, Handle<ByteArray> nibble_table,
    int advance_by, Label* on_match, Label* on_no_match) {
  Emit<RegExpBytecode::kSkipUntilBitInTable>(cp_offset, advance_by, table,
                                             on_match, on_no_match);
}

void RegExpBytecodeGenerator::SkipUntilCharAnd(int cp_offset, int advance_by,
                                               unsigned character,
                                               unsigned mask, int eats_at_least,
                                               Label* on_match,
                                               Label* on_no_match) {
  // Only generated by peephole optimization.
  UNREACHABLE();
}

void RegExpBytecodeGenerator::SkipUntilChar(int cp_offset, int advance_by,
                                            unsigned character, Label* on_match,
                                            Label* on_no_match) {
  // Only generated by peephole optimization.
  UNREACHABLE();
}

void RegExpBytecodeGenerator::SkipUntilCharPosChecked(
    int cp_offset, int advance_by, unsigned character, int eats_at_least,
    Label* on_match, Label* on_no_match) {
  // Only generated by peephole optimization.
  UNREACHABLE();
}

void RegExpBytecodeGenerator::SkipUntilCharOrChar(int cp_offset, int advance_by,
                                                  unsigned char1,
                                                  unsigned char2,
                                                  Label* on_match,
                                                  Label* on_no_match) {
  // Only generated by peephole optimization.
  UNREACHABLE();
}

void RegExpBytecodeGenerator::SkipUntilGtOrNotBitInTable(
    int cp_offset, int advance_by, unsigned character, Handle<ByteArray> table,
    Label* on_match, Label* on_no_match) {
  // Only generated by peephole optimization.
  UNREACHABLE();
}

void RegExpBytecodeGenerator::SkipUntilOneOfMasked(
    int cp_offset, int advance_by, unsigned both_chars, unsigned both_mask,
    int max_offset, unsigned chars1, unsigned mask1, unsigned chars2,
    unsigned mask2, Label* on_match1, Label* on_match2, Label* on_failure) {
  // Only generated by peephole optimization.
  UNREACHABLE();
}

void RegExpBytecodeGenerator::CheckNotBackReference(int start_reg,
                                                    bool read_backward,
                                                    Label* on_not_equal) {
  if (read_backward) {
    Emit<RegExpBytecode::kCheckNotBackRefBackward>(start_reg, on_not_equal);
  } else {
    Emit<RegExpBytecode::kCheckNotBackRef>(start_reg, on_not_equal);
  }
}

void RegExpBytecodeGenerator::CheckNotBackReferenceIgnoreCase(
    int start_reg, bool read_backward, bool unicode, Label* on_not_equal) {
  if (read_backward) {
    if (unicode) {
      Emit<RegExpBytecode::kCheckNotBackRefNoCaseUnicodeBackward>(start_reg,
                                                                  on_not_equal);
    } else {
      Emit<RegExpBytecode::kCheckNotBackRefNoCaseBackward>(start_reg,
                                                           on_not_equal);
    }
  } else {
    if (unicode) {
      Emit<RegExpBytecode::kCheckNotBackRefNoCaseUnicode>(start_reg,
                                                          on_not_equal);
    } else {
      Emit<RegExpBytecode::kCheckNotBackRefNoCase>(start_reg, on_not_equal);
    }
  }
}

void RegExpBytecodeGenerator::IfRegisterLT(int register_index, int comparand,
                                           Label* on_less_than) {
  Emit<RegExpBytecode::kIfRegisterLT>(register_index, comparand, on_less_than);
}

void RegExpBytecodeGenerator::IfRegisterGE(int register_index, int comparand,
                                           Label* on_greater_or_equal) {
  Emit<RegExpBytecode::kIfRegisterGE>(register_index, comparand,
                                      on_greater_or_equal);
}

void RegExpBytecodeGenerator::IfRegisterEqPos(int register_index,
                                              Label* on_eq) {
  Emit<RegExpBytecode::kIfRegisterEqPos>(register_index, on_eq);
}

DirectHandle<HeapObject> RegExpBytecodeGenerator::GetCode(
    DirectHandle<String> source, RegExpFlags flags) {
  Bind(&backtrack_);
  Backtrack();

  DirectHandle<TrustedByteArray> array;
  if (v8_flags.regexp_peephole_optimization) {
    array = RegExpBytecodePeepholeOptimization::OptimizeBytecode(
        isolate_, zone(), source, buffer_.data(), length(), jump_edges_);
  } else {
    array = isolate_->factory()->NewTrustedByteArray(length());
    Copy(array->begin());
  }

  return array;
}

int RegExpBytecodeGenerator::length() { return pc_; }

void RegExpBytecodeGenerator::Copy(uint8_t* a) {
  MemCopy(a, buffer_.data(), length());
}

void RegExpBytecodeGenerator::ExpandBuffer() {
  // TODO(jgruber): The growth strategy could be smarter for large sizes.
  // TODO(jgruber): It's not necessary to default-initialize new elements.
  buffer_.resize(buffer_.size() * 2);
}

}  // namespace internal
}  // namespace v8
