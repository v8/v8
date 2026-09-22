// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-block-list.h"

#include <optional>

#include "src/debug/debug-scope-info.h"
#include "src/execution/isolate.h"
#include "src/objects/string-set-inl.h"

namespace v8 {
namespace internal {

namespace {

Handle<StringSet> AddScopeStackLocalsToBlockList(Isolate* isolate,
                                                 Handle<StringSet> blocklist,
                                                 DebugScriptScope scope) {
  if (scope.is_hidden() || scope.is_script_scope()) {
    return blocklist;
  }
  for (int i = 0; i < scope.variable_count(); ++i) {
    DebugVariableInfo var = scope.variable(i);
    if (var.location == VariableLocation::PARAMETER ||
        var.location == VariableLocation::LOCAL) {
      DirectHandle<String> name(var.name, isolate);
      blocklist = StringSet::Add(isolate, blocklist, name);
    }
  }
  return blocklist;
}

}  // namespace

Handle<StringSet> CalculateScopeBlockList(Isolate* isolate,
                                          DebugScriptScope scope) {
  Handle<StringSet> blocklist = StringSet::New(isolate);
  for (std::optional<DebugScriptScope> current = scope;
       current.has_value() && !current->needs_context();
       current = current->parent()) {
    blocklist = AddScopeStackLocalsToBlockList(isolate, blocklist, *current);
  }
  return blocklist;
}

}  // namespace internal
}  // namespace v8
