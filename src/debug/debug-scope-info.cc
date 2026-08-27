// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-scope-info.h"

#include <cstddef>
#include <type_traits>

#include "src/ast/scopes.h"
#include "src/base/bit-field.h"
#include "src/base/vector.h"
#include "src/common/globals.h"
#include "src/execution/isolate-inl.h"
#include "src/handles/handles-inl.h"
#include "src/heap/factory.h"
#include "src/objects/debug-objects-inl.h"

namespace v8 {
namespace internal {

// ===========================================================================
// ByteArray: numeric_data
// ===========================================================================
// +-------------------------------------------------------------------------+
// | Header (4 bytes):                                                       |
// |   int32_t scope_count                                                   |
// +-------------------------------------------------------------------------+
// | Offset Table (scope_count * 4 bytes):                                   |
// |   uint32_t scope_offsets[scope_count]                                   |
// +-------------------------------------------------------------------------+
// | Scope Record 0 (at scope_offsets[0]):                                   |
// |   +0: int32_t  start_position                                           |
// |   +4: int32_t  end_position                                             |
// |   +8: int32_t  parent_scope_index (-1 for root script scope)            |
// |  +12: uint16_t flags (ScopeType, IsHidden, NeedsContext, HasSibling, etc)|
// |  +14: uint16_t var_count                                                |
// |  --- Dynamic Optional Fields (present conditionally based on flags) --- |
// |  [+16: int32_t  next_sibling_index]        (if HasSiblingBit is set)    |
// |  [+..: int32_t  context_id]                (if NeedsContextBit is set)  |
// |  [+..: uint16_t receiver_allocation_info]  (if HasThisDeclBit is set)   |
// |  [+..: uint16_t arguments_allocation_info] (if HasArgumentsBit is set)  |
// |  [+..: uint16_t + int32_t function_var]    (if HasFunctionVarBit is set)|
// |  --- Variables Array (var_count entries, 12 bytes each) ---             |
// |  [DebugVariableEntry 0]:                                                |
// |     +0: int16_t  slot_index                                             |
// |     +2: uint16_t location_mode_flags                                    |
// |     +4: int32_t  initializer_position                                   |
// |     +8: int32_t  name_index (into string_table FixedArray)              |
// |  [DebugVariableEntry 1]...                                              |
// +-------------------------------------------------------------------------+
// | Scope Record 1 (at scope_offsets[1])...                                 |
// +-------------------------------------------------------------------------+

namespace {

struct ScopeRecord {
  int32_t start_position;
  int32_t end_position;
  int32_t parent_scope_index;
  uint16_t flags;
  uint16_t var_count;
};

static_assert(std::is_trivial_v<ScopeRecord>);
static_assert(std::is_standard_layout_v<ScopeRecord>);
// Ensure ScopeRecord has no padding. Update when adding new fields.
static_assert(sizeof(ScopeRecord) == 16);

using ScopeTypeBits = base::BitField<ScopeType, 0, 4, uint16_t>;
using HasChildrenBit = ScopeTypeBits::Next<bool, 1>;
using HasSiblingBit = HasChildrenBit::Next<bool, 1>;
using IsHiddenBit = HasSiblingBit::Next<bool, 1>;
using LanguageModeBit = IsHiddenBit::Next<LanguageMode, 1>;
using IsArrowScopeBit = LanguageModeBit::Next<bool, 1>;
using HasThisDeclarationBit = IsArrowScopeBit::Next<bool, 1>;
using HasThisReferenceBit = HasThisDeclarationBit::Next<bool, 1>;
using HasSimpleParametersBit = HasThisReferenceBit::Next<bool, 1>;
using SloppyEvalCanExtendVarsBit = HasSimpleParametersBit::Next<bool, 1>;
using NeedsContextBit = SloppyEvalCanExtendVarsBit::Next<bool, 1>;
static_assert(NeedsContextBit::kLastUsedBit < 16);

int32_t GetScopeCount(Tagged<DebugScriptScopeInfo> info) {
  Tagged<ByteArray> bytes = info->numeric_data();
  DCHECK_GE(bytes->length().value(), kInt32Size);
  return base::ReadUnalignedValue<int32_t>(bytes->begin());
}

uint32_t GetScopeOffset(Tagged<DebugScriptScopeInfo> info, int scope_index) {
  CHECK_GE(scope_index, 0);
  CHECK_LT(scope_index, GetScopeCount(info));
  const uint8_t* ptr =
      info->numeric_data()->begin() + kInt32Size + scope_index * kUInt32Size;
  return base::ReadUnalignedValue<uint32_t>(ptr);
}

}  // namespace

const uint8_t* DebugScriptScope::payload() const {
  return info_->numeric_data()->begin() + offset_;
}

uint16_t DebugScriptScope::flags() const {
  return base::ReadUnalignedValue<ScopeRecord>(payload()).flags;
}

DebugScriptScope DebugScriptScope::FromIndex(
    DirectHandle<DebugScriptScopeInfo> info, int scope_index) {
  CHECK_GE(scope_index, 0);
  CHECK_LT(scope_index, GetScopeCount(*info));
  uint32_t offset = GetScopeOffset(*info, scope_index);
  return DebugScriptScope(info, scope_index, offset);
}

std::optional<DebugScriptScope> DebugScriptScope::parent() const {
  int parent_idx = parent_index();
  if (parent_idx == -1) return std::nullopt;
  return FromIndex(info_, parent_idx);
}

std::optional<DebugScriptScope> DebugScriptScope::first_child() const {
  if (!HasChildrenBit::decode(flags())) return std::nullopt;
  return FromIndex(info_, scope_index_ + 1);
}

std::optional<DebugScriptScope> DebugScriptScope::next_sibling() const {
  if (!HasSiblingBit::decode(flags())) return std::nullopt;
  const uint8_t* sibling_ptr = payload() + sizeof(ScopeRecord);
  int sibling_idx = base::ReadUnalignedValue<int32_t>(sibling_ptr);
  return FromIndex(info_, sibling_idx);
}

int DebugScriptScope::start_position() const {
  return base::ReadUnalignedValue<ScopeRecord>(payload()).start_position;
}

int DebugScriptScope::end_position() const {
  return base::ReadUnalignedValue<ScopeRecord>(payload()).end_position;
}

int DebugScriptScope::parent_index() const {
  return base::ReadUnalignedValue<ScopeRecord>(payload()).parent_scope_index;
}

ScopeType DebugScriptScope::scope_type() const {
  return ScopeTypeBits::decode(flags());
}

bool DebugScriptScope::is_script_scope() const {
  return scope_type() == ScopeType::SCRIPT_SCOPE ||
         scope_type() == ScopeType::REPL_MODE_SCOPE;
}

bool DebugScriptScope::is_function_scope() const {
  return scope_type() == ScopeType::FUNCTION_SCOPE;
}

bool DebugScriptScope::is_block_scope() const {
  return scope_type() == ScopeType::BLOCK_SCOPE ||
         scope_type() == ScopeType::CLASS_SCOPE;
}

bool DebugScriptScope::is_declaration_scope() const {
  return is_script_scope() || is_function_scope() ||
         scope_type() == ScopeType::MODULE_SCOPE ||
         scope_type() == ScopeType::EVAL_SCOPE;
}

LanguageMode DebugScriptScope::language_mode() const {
  return LanguageModeBit::decode(flags());
}

bool DebugScriptScope::is_arrow_scope() const {
  return IsArrowScopeBit::decode(flags());
}

bool DebugScriptScope::is_class_scope() const {
  return scope_type() == ScopeType::CLASS_SCOPE;
}

bool DebugScriptScope::is_with_scope() const {
  return scope_type() == ScopeType::WITH_SCOPE;
}

bool DebugScriptScope::is_module_scope() const {
  return scope_type() == ScopeType::MODULE_SCOPE;
}

bool DebugScriptScope::is_eval_scope() const {
  return scope_type() == ScopeType::EVAL_SCOPE;
}

bool DebugScriptScope::is_catch_scope() const {
  return scope_type() == ScopeType::CATCH_SCOPE;
}

bool DebugScriptScope::is_repl_mode_scope() const {
  return scope_type() == ScopeType::REPL_MODE_SCOPE;
}

bool DebugScriptScope::is_hidden() const {
  return IsHiddenBit::decode(flags());
}

bool DebugScriptScope::has_this_declaration() const {
  return HasThisDeclarationBit::decode(flags());
}

bool DebugScriptScope::has_this_reference() const {
  return HasThisReferenceBit::decode(flags());
}

bool DebugScriptScope::has_simple_parameters() const {
  return HasSimpleParametersBit::decode(flags());
}

bool DebugScriptScope::sloppy_eval_can_extend_vars() const {
  return SloppyEvalCanExtendVarsBit::decode(flags());
}

bool DebugScriptScope::needs_context() const {
  return NeedsContextBit::decode(flags());
}

int DebugScriptScope::unique_id_in_script() const {
  if (!needs_context()) return -3;
  const uint8_t* ptr = payload() + sizeof(ScopeRecord);
  if (HasSiblingBit::decode(flags())) {
    ptr += kInt32Size;
  }
  return base::ReadUnalignedValue<int32_t>(ptr);
}

Handle<DebugScriptScopeInfo> SerializeDebugScriptScopeInfo(
    Isolate* isolate, DeclarationScope* script_scope) {
  DCHECK_NOT_NULL(script_scope);
  DCHECK(script_scope->is_script_scope());

  std::vector<Scope*> all_scopes;
  std::unordered_map<Scope*, int> scope_to_index;

  auto collect = [&](auto& self, Scope* scope) -> void {
    int index = static_cast<int>(all_scopes.size());
    all_scopes.push_back(scope);
    scope_to_index.emplace(scope, index);
    for (Scope* inner = scope->inner_scope(); inner != nullptr;
         inner = inner->sibling()) {
      self(self, inner);
    }
  };
  collect(collect, script_scope);

  auto find_scope_index = [&](Scope* s) -> int {
    if (s == nullptr) return -1;
    auto it = scope_to_index.find(s);
    return it != scope_to_index.end() ? it->second : -1;
  };

  // Stage 1: Pre-calculate exact required byte size and scope offsets.
  size_t total_size = kInt32Size + all_scopes.size() * kUInt32Size;
  std::vector<uint32_t> offsets;
  offsets.reserve(all_scopes.size());

  for (size_t i = 0; i < all_scopes.size(); ++i) {
    offsets.push_back(static_cast<uint32_t>(total_size));
    total_size += sizeof(ScopeRecord);
    if (all_scopes[i]->sibling() != nullptr) {
      total_size += kInt32Size;
    }
    if (all_scopes[i]->NeedsContext()) {
      total_size += kInt32Size;
    }
  }

  // Stage 2: Allocate a ByteArray and write directly into it with
  // base::WriteUnalignedValue.
  Handle<ByteArray> byte_array = isolate->factory()->NewByteArray(
      static_cast<int>(total_size), AllocationType::kOld);

  base::WriteUnalignedValue<int32_t>(
      reinterpret_cast<Address>(byte_array->begin()),
      static_cast<int32_t>(all_scopes.size()));

  for (size_t i = 0; i < all_scopes.size(); ++i) {
    size_t offset = kInt32Size + i * kUInt32Size;
    Address offset_ptr =
        reinterpret_cast<Address>(byte_array->begin()) + offset;
    base::WriteUnalignedValue<uint32_t>(offset_ptr, offsets[i]);

    Address record =
        reinterpret_cast<Address>(byte_array->begin()) + offsets[i];
    Scope* scope = all_scopes[i];

    int next_sibling = find_scope_index(scope->sibling());
    bool has_sibling = next_sibling != -1;
    bool needs_context = scope->NeedsContext();

    uint16_t flags = 0;
    flags = ScopeTypeBits::update(flags, scope->scope_type());
    flags = HasChildrenBit::update(flags, scope->inner_scope() != nullptr);
    flags = HasSiblingBit::update(flags, has_sibling);
    flags = IsHiddenBit::update(flags, scope->is_hidden());
    flags = LanguageModeBit::update(flags, scope->language_mode());
    flags = HasThisReferenceBit::update(flags, scope->HasThisReference());
    flags = NeedsContextBit::update(flags, needs_context);
    if (scope->is_declaration_scope()) {
      DeclarationScope* decl_scope = scope->AsDeclarationScope();
      flags = IsArrowScopeBit::update(flags, decl_scope->is_arrow_scope());
      flags = HasThisDeclarationBit::update(flags,
                                            decl_scope->has_this_declaration());
      flags = HasSimpleParametersBit::update(
          flags, decl_scope->has_simple_parameters());
      flags = SloppyEvalCanExtendVarsBit::update(
          flags, decl_scope->sloppy_eval_can_extend_vars());
    }

    base::WriteUnalignedValue<ScopeRecord>(
        record,
        ScopeRecord{
            .start_position = scope->start_position(),
            .end_position = scope->end_position(),
            .parent_scope_index = find_scope_index(scope->outer_scope()),
            .flags = flags,
            .var_count = 0,
        });

    size_t optional_offset = sizeof(ScopeRecord);
    if (has_sibling) {
      base::WriteUnalignedValue<int32_t>(record + optional_offset,
                                         next_sibling);
      optional_offset += kInt32Size;
    }
    if (needs_context) {
      // We only need the context ID to match DebugScopeInfo against runtime
      // ScopeInfo. V8 omits runtime ScopeInfo for any scope that doesn't need a
      // context so we don't need to waste the bytes.
      base::WriteUnalignedValue<int32_t>(record + optional_offset,
                                         scope->UniqueIdInScript());
    }
  }

  Handle<FixedArray> string_table = isolate->factory()->empty_fixed_array();
  return isolate->factory()->NewDebugScriptScopeInfo(byte_array, string_table);
}

#ifdef VERIFY_HEAP
// DebugScriptScopeInfo::DebugScriptScopeInfoVerify is placed here instead of in
// objects-debug.cc to keep the exact layout of numeric_data local to
// debug-scope-info.cc.
void DebugScriptScopeInfo::DebugScriptScopeInfoVerify(Isolate* isolate) {
  CHECK(Is<Struct>(this));
  CHECK(Is<DebugScriptScopeInfo>(this));
  Object::VerifyPointer(isolate, numeric_data_.load());
  Object::VerifyPointer(isolate, string_table_.load());

  Tagged<ByteArray> bytes = numeric_data();
  CHECK_GE(bytes->length().value(), kInt32Size);
  int scope_count = GetScopeCount(this);
  CHECK_GE(scope_count, 0);

  size_t header_and_table_size = kInt32Size + scope_count * kUInt32Size;
  CHECK_GE(static_cast<size_t>(bytes->length().value()), header_and_table_size);

  HandleScope handle_scope(isolate);
  DirectHandle<DebugScriptScopeInfo> info_handle(this, isolate);

  for (int i = 0; i < scope_count; ++i) {
    uint32_t offset = GetScopeOffset(this, i);
    CHECK_EQ(offset % kUInt32Size, 0);
    CHECK_GE(offset, header_and_table_size);
    DebugScriptScope scope = DebugScriptScope::FromIndex(info_handle, i);
    CHECK_EQ(scope.scope_index(), i);
    CHECK_LE(scope.start_position(), scope.end_position());

    size_t record_size = sizeof(ScopeRecord) +
                         (scope.next_sibling().has_value() ? kInt32Size : 0) +
                         (scope.needs_context() ? kInt32Size : 0);
    CHECK_LE(offset + record_size,
             static_cast<size_t>(bytes->length().value()));

    if (scope.needs_context()) {
      CHECK_GE(scope.unique_id_in_script(), -2);
    } else {
      CHECK_EQ(scope.unique_id_in_script(), -3);
    }

    if (i == 0) {
      CHECK(!scope.parent().has_value());
    } else {
      CHECK(scope.parent().has_value());
      CHECK_GE(scope.parent()->scope_index(), 0);
      CHECK_LT(scope.parent()->scope_index(), i);
    }

    if (auto first_child = scope.first_child()) {
      CHECK_LT(i + 1, scope_count);
      CHECK_EQ(first_child->scope_index(), i + 1);
      CHECK(first_child->parent().has_value());
      CHECK_EQ(first_child->parent()->scope_index(), i);
    }

    if (auto sibling = scope.next_sibling()) {
      CHECK_GT(sibling->scope_index(), i);
      CHECK_LT(sibling->scope_index(), scope_count);
      if (i == 0) {
        CHECK(!sibling->parent().has_value());
      } else {
        CHECK(sibling->parent().has_value());
        CHECK_EQ(sibling->parent()->scope_index(),
                 scope.parent()->scope_index());
      }
    }
  }
}
#endif  // VERIFY_HEAP

}  // namespace internal
}  // namespace v8
