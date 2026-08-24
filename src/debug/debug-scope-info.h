// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_DEBUG_SCOPE_INFO_H_
#define V8_DEBUG_DEBUG_SCOPE_INFO_H_

#include <optional>

#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/objects/debug-objects.h"

namespace v8 {
namespace internal {

class DeclarationScope;
class Isolate;

// Stack-allocated cursor for navigating and querying serialized scope trees
// stored in DebugScriptScopeInfo.
class V8_EXPORT_PRIVATE DebugScriptScope {
  V8_STACK_ALLOCATED();

 public:
  static DebugScriptScope FromIndex(DirectHandle<DebugScriptScopeInfo> info,
                                    int scope_index);

  // Tree Navigation
  std::optional<DebugScriptScope> parent() const;
  std::optional<DebugScriptScope> first_child() const;
  std::optional<DebugScriptScope> next_sibling() const;

  // Position & Type Accessors
  int start_position() const;
  int end_position() const;
  int scope_index() const { return scope_index_; }
  ScopeType scope_type() const;

  // Scope Predicates
  bool is_script_scope() const;
  bool is_function_scope() const;
  bool is_block_scope() const;
  bool is_declaration_scope() const;

 private:
  DebugScriptScope(DirectHandle<DebugScriptScopeInfo> info, int scope_index,
                   uint32_t offset)
      : info_(info), scope_index_(scope_index), offset_(offset) {}

  const uint8_t* payload() const;
  uint16_t flags() const;
  int parent_index() const;

  DirectHandle<DebugScriptScopeInfo> info_;
  int scope_index_;
  uint32_t offset_;
};

// Serializes the start/end positions of the AST scopes rooted at `script_scope`
// into a DebugScriptScopeInfo.
V8_EXPORT_PRIVATE Handle<DebugScriptScopeInfo> SerializeDebugScriptScopeInfo(
    Isolate* isolate, DeclarationScope* script_scope);

}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_DEBUG_SCOPE_INFO_H_
