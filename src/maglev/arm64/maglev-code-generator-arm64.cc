// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-code-generator.h"
#include "src/maglev/maglev-graph.h"

namespace v8 {
namespace internal {
namespace maglev {

MaglevCodeGenerator::MaglevCodeGenerator(
    LocalIsolate* isolate, MaglevCompilationInfo* compilation_info,
    Graph* graph)
    : local_isolate_(isolate),
      safepoint_table_builder_(compilation_info->zone(),
                               graph->tagged_stack_slots(),
                               graph->untagged_stack_slots()),
      translation_array_builder_(compilation_info->zone()),
      code_gen_state_(compilation_info, &safepoint_table_builder_),
      masm_(isolate->GetMainThreadIsolateUnsafe(), &code_gen_state_),
      graph_(graph),
      deopt_literals_(isolate->heap()->heap()) {}

void MaglevCodeGenerator::Assemble() {
  // TODO(v8:7700): To implement! :)
}

MaybeHandle<Code> MaglevCodeGenerator::Generate(Isolate* isolate) {
  // TODO(v8:7700): To implement! :)
  return {};
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
