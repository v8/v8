// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-compilation-data.h"

#include "src/compiler/js-heap-broker.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/objects/js-function-inl.h"

namespace v8 {
namespace internal {
namespace maglev {

MaglevCompilationData::MaglevCompilationData(compiler::JSHeapBroker* broker)
    : broker(broker),
      isolate(broker->isolate()),
      zone(broker->isolate()->allocator(), "maglev-zone") {}

MaglevCompilationData::~MaglevCompilationData() = default;

MaglevCompilationUnit::MaglevCompilationUnit(MaglevCompilationData* data,
                                             Handle<JSFunction> function)
    : compilation_data(data),
      bytecode(
          MakeRef(broker(), function->shared().GetBytecodeArray(isolate()))),
      feedback(MakeRef(broker(), function->feedback_vector())),
      bytecode_analysis(bytecode.object(), zone(), BytecodeOffset::None(),
                        true),
      register_count_(bytecode.register_count()),
      parameter_count_(bytecode.parameter_count()) {}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
