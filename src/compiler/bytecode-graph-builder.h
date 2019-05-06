// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BYTECODE_GRAPH_BUILDER_H_
#define V8_COMPILER_BYTECODE_GRAPH_BUILDER_H_

#include "src/compiler/js-operator.h"
#include "src/compiler/js-type-hint-lowering.h"
#include "src/handles.h"
#include "src/utils.h"

namespace v8 {
namespace internal {

class BytecodeArray;
class FeedbackVector;
class SharedFunctionInfo;
class Zone;

namespace compiler {

class JSGraph;
class SourcePositionTable;

enum class BytecodeGraphBuilderFlag : uint8_t {
  kSkipFirstStackCheck = 1 << 0,
  kAnalyzeEnvironmentLiveness = 1 << 1,
  kBailoutOnUninitialized = 1 << 2,
};
using BytecodeGraphBuilderFlags = base::Flags<BytecodeGraphBuilderFlag>;

void BuildGraphFromBytecode(JSHeapBroker* broker, Zone* local_zone,
                            Handle<BytecodeArray> bytecode_array,
                            Handle<SharedFunctionInfo> shared,
                            Handle<FeedbackVector> feedback_vector,
                            BailoutId osr_offset, JSGraph* jsgraph,
                            CallFrequency invocation_frequency,
                            SourcePositionTable* source_positions,
                            Handle<Context> native_context, int inlining_id,
                            BytecodeGraphBuilderFlags flags);

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BYTECODE_GRAPH_BUILDER_H_
