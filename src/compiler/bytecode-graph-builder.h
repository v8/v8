// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BYTECODE_GRAPH_BUILDER_H_
#define V8_COMPILER_BYTECODE_GRAPH_BUILDER_H_

#include "src/compiler/js-operator.h"
#include "src/compiler/js-type-hint-lowering.h"
#include "src/handles.h"
#include "src/utils.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class BytecodeArray;
class FeedbackVector;
class SharedFunctionInfo;

namespace compiler {

class JSGraph;
class SourcePositionTable;

void BuildGraphFromBytecode(
    Zone* local_zone, Handle<BytecodeArray> bytecode_array,
    Handle<SharedFunctionInfo> shared, Handle<FeedbackVector> feedback_vector,
    BailoutId osr_offset, JSGraph* jsgraph, CallFrequency invocation_frequency,
    SourcePositionTable* source_positions, Handle<Context> native_context,
    int inlining_id, JSTypeHintLowering::Flags flags,
    bool skip_first_stack_check, bool analyze_environment_liveness);

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BYTECODE_GRAPH_BUILDER_H_
