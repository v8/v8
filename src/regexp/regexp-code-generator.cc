// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-code-generator.h"

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "src/codegen/label.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/objects/fixed-array-inl.h"
#include "src/regexp/regexp-bytecode-iterator-inl.h"
#include "src/regexp/regexp-bytecodes-inl.h"

namespace v8 {
namespace internal {

#define __ masm_->

RegExpCodeGenerator::RegExpCodeGenerator(
    Isolate* isolate, RegExpMacroAssembler* masm,
    DirectHandle<TrustedByteArray> bytecode)
    : isolate_(isolate),
      zone_(isolate_->allocator(), ZONE_NAME),
      masm_(masm),
      bytecode_(bytecode),
      iter_(bytecode_),
      labels_(zone_.AllocateArray<Label>(bytecode_->length())),
      jump_targets_(bytecode_->length(), &zone_),
      has_unsupported_bytecode_(false) {}

RegExpCodeGenerator::Result RegExpCodeGenerator::Assemble(
    DirectHandle<String> source, RegExpFlags flags) {
  USE(isolate_);
  USE(masm_);
  PreVisitBytecodes();
  iter_.reset();
  VisitBytecodes();
  if (has_unsupported_bytecode_) return Result::UnsupportedBytecode();
  DirectHandle<Code> code = CheckedCast<Code>(masm_->GetCode(source, flags));
  return Result{code};
}

template <typename Operands, typename Operands::Operand Operand>
auto RegExpCodeGenerator::GetArgumentValue(const uint8_t* pc) {
  constexpr RegExpBytecodeOperandType op_type = Operands::Type(Operand);
  auto value = Operands::template Get<Operand>(pc);

  // If the operand is a Label, we return the Label created during the first
  // pass instead of an offset to the bytecode.
  if constexpr (op_type == ReBcOpType::kLabel) {
    return &labels_[value];
  } else {
    return value;
  }
}

// Calls |func| passing all operand ids (except paddings) as a template
// parameter pack.
template <typename Operands, typename Func>
void DispatchByOperand(Func&& func) {
  // Filter padding operands.
  auto filtered_ops = []<std::size_t... Is>(std::index_sequence<Is...>) {
    return std::tuple_cat([]<std::size_t I>() {
      using Operand = typename Operands::Operand;
      constexpr auto id = static_cast<Operand>(I);
      if constexpr (Operands::Type(id) == ReBcOpType::kPadding) {
        return std::tuple<>();
      } else {
        return std::tuple(std::integral_constant<Operand, id>{});
      }
    }.template operator()<Is>()...);
  }(std::make_index_sequence<Operands::kCount>{});

  std::apply([&](auto... ops) { func.template operator()<ops.value...>(); },
             filtered_ops);
}

#define GENERATE_VISIT_METHOD(Name, Enum, OperandsTuple, Types)       \
  template <>                                                         \
  void RegExpCodeGenerator::Visit<RegExpBytecode::k##Name>() {        \
    using Operands = RegExpBytecodeOperands<RegExpBytecode::k##Name>; \
    const uint8_t* pc = iter_.current_address();                      \
                                                                      \
    DispatchByOperand<Operands>([&]<auto... operand_ids>() {          \
      __ Name(GetArgumentValue<Operands, operand_ids>(pc)...);        \
    });                                                               \
  }

BASIC_BYTECODE_LIST(GENERATE_VISIT_METHOD)

#undef GENERATE_VISIT_METHOD

template <RegExpBytecode bc>
void RegExpCodeGenerator::Visit() {
  if (v8_flags.trace_regexp_assembler) {
    std::cout << "RegExp Code Generator: Unsupported Bytecode "
              << RegExpBytecodes::Name(bc) << std::endl;
  }
  has_unsupported_bytecode_ = true;
}

void RegExpCodeGenerator::PreVisitBytecodes() {
  iter_.ForEachBytecode([&]<RegExpBytecode bc>() {
    using Operands = RegExpBytecodeOperands<bc>;
    auto ensure_label = [&]<auto operand>() {
      const uint8_t* pc = iter_.current_address();
      uint32_t offset = Operands::template Get<operand>(pc);
      if (!jump_targets_.Contains(offset)) {
        jump_targets_.Add(offset);
        Label* label = &labels_[offset];
        new (label) Label();
      }
    };
    Operands::template ForEachOperandOfType<RegExpBytecodeOperandType::kLabel>(
        ensure_label);
  });
}

void RegExpCodeGenerator::VisitBytecodes() {
  for (; !iter_.done() && !has_unsupported_bytecode_; iter_.advance()) {
    if (jump_targets_.Contains(iter_.current_offset())) {
      __ Bind(&labels_[iter_.current_offset()]);
    }
    RegExpBytecodes::DispatchOnBytecode(
        iter_.current_bytecode(), [this]<RegExpBytecode bc>() { Visit<bc>(); });
  }
}

}  // namespace internal
}  // namespace v8
