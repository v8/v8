// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-scope-info.h"

#include <cstddef>
#include <type_traits>

#include "src/ast/scopes.h"
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

DebugScriptScope DebugScriptScope::FromIndex(
    DirectHandle<DebugScriptScopeInfo> info, int scope_index) {
  CHECK_GE(scope_index, 0);
  CHECK_LT(scope_index, GetScopeCount(*info));
  uint32_t offset = GetScopeOffset(*info, scope_index);
  return DebugScriptScope(info, scope_index, offset);
}

int DebugScriptScope::start_position() const {
  return base::ReadUnalignedValue<ScopeRecord>(payload()).start_position;
}

int DebugScriptScope::end_position() const {
  return base::ReadUnalignedValue<ScopeRecord>(payload()).end_position;
}

Handle<DebugScriptScopeInfo> SerializeDebugScriptScopeInfo(
    Isolate* isolate, DeclarationScope* script_scope) {
  DCHECK_NOT_NULL(script_scope);
  DCHECK(script_scope->is_script_scope());

  std::vector<Scope*> all_scopes;

  auto collect = [&](auto& self, Scope* scope) -> void {
    all_scopes.push_back(scope);
    for (Scope* inner = scope->inner_scope(); inner != nullptr;
         inner = inner->sibling()) {
      self(self, inner);
    }
  };
  collect(collect, script_scope);

  // Stage 1: Pre-calculate exact required byte size and scope offsets.
  size_t total_size = kInt32Size + all_scopes.size() * kUInt32Size;
  std::vector<uint32_t> offsets;
  offsets.reserve(all_scopes.size());

  for (size_t i = 0; i < all_scopes.size(); ++i) {
    offsets.push_back(static_cast<uint32_t>(total_size));
    total_size += sizeof(ScopeRecord);
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

    base::WriteUnalignedValue<ScopeRecord>(
        record, ScopeRecord{
                    .start_position = scope->start_position(),
                    .end_position = scope->end_position(),
                    .parent_scope_index = -1,
                    .flags = 0,
                    .var_count = 0,
                });
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
    CHECK_LE(offset + sizeof(ScopeRecord),
             static_cast<size_t>(bytes->length().value()));

    DebugScriptScope scope = DebugScriptScope::FromIndex(info_handle, i);
    CHECK_EQ(scope.scope_index(), i);
    CHECK_LE(scope.start_position(), scope.end_position());
  }
}
#endif  // VERIFY_HEAP

}  // namespace internal
}  // namespace v8
