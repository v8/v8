// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_BUILTIN_REDUCER_H_
#define V8_COMPILER_JS_BUILTIN_REDUCER_H_

#include "src/compiler/graph-reducer.h"

namespace v8 {
namespace internal {

// Forward declarations.
class TypeCache;

namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
class JSGraph;
class SimplifiedOperatorBuilder;


class JSBuiltinReducer final : public AdvancedReducer {
 public:
  explicit JSBuiltinReducer(Editor* editor, JSGraph* jsgraph);
  ~JSBuiltinReducer() final {}

  Reduction Reduce(Node* node) final;

 private:
  Reduction ReduceMathAbs(Node* node);
  Reduction ReduceMathAtan(Node* node);
  Reduction ReduceMathAtan2(Node* node);
  Reduction ReduceMathAtanh(Node* node);
  Reduction ReduceMathCeil(Node* node);
  Reduction ReduceMathClz32(Node* node);
  Reduction ReduceMathCos(Node* node);
  Reduction ReduceMathExp(Node* node);
  Reduction ReduceMathFloor(Node* node);
  Reduction ReduceMathFround(Node* node);
  Reduction ReduceMathImul(Node* node);
  Reduction ReduceMathLog(Node* node);
  Reduction ReduceMathLog1p(Node* node);
  Reduction ReduceMathLog10(Node* node);
  Reduction ReduceMathLog2(Node* node);
  Reduction ReduceMathMax(Node* node);
  Reduction ReduceMathMin(Node* node);
  Reduction ReduceMathCbrt(Node* node);
  Reduction ReduceMathExpm1(Node* node);
  Reduction ReduceMathRound(Node* node);
  Reduction ReduceMathSin(Node* node);
  Reduction ReduceMathSqrt(Node* node);
  Reduction ReduceMathTan(Node* node);
  Reduction ReduceMathTrunc(Node* node);
  Reduction ReduceStringFromCharCode(Node* node);

  Node* ToNumber(Node* value);
  Node* ToUint32(Node* value);

  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  Isolate* isolate() const;
  CommonOperatorBuilder* common() const;
  SimplifiedOperatorBuilder* simplified() const;

  JSGraph* const jsgraph_;
  TypeCache const& type_cache_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_BUILTIN_REDUCER_H_
