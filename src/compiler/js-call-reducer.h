// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_CALL_REDUCER_H_
#define V8_COMPILER_JS_CALL_REDUCER_H_

#include "src/compiler/graph-reducer.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class JSGraph;
class JSOperatorBuilder;


// Performs strength reduction on {JSCallFunction} nodes, which might allow
// inlining or other optimizations to be performed afterwards.
class JSCallReducer final : public Reducer {
 public:
  explicit JSCallReducer(JSGraph* jsgraph) : jsgraph_(jsgraph) {}

  Reduction Reduce(Node* node) final;

 private:
  Reduction ReduceFunctionPrototypeApply(Node* node);
  Reduction ReduceFunctionPrototypeCall(Node* node);

  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  Isolate* isolate() const;
  JSOperatorBuilder* javascript() const;

  JSGraph* const jsgraph_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_CALL_REDUCER_H_
