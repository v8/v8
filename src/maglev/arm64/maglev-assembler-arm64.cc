
// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-assembler.h"

namespace v8 {
namespace internal {
namespace maglev {

void MaglevAssembler::Prologue(Graph* graph,
                               Label* deferred_flags_need_processing,
                               Label* deferred_call_stack_guard,
                               Label* deferred_call_stack_guard_return) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
void MaglevAssembler::DeferredPrologue(
    Graph* graph, Label* deferred_flags_need_processing,
    Label* deferred_call_stack_guard, Label* deferred_call_stack_guard_return) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
