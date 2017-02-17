// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_TYPE_HINT_LOWERING_H_
#define V8_COMPILER_JS_TYPE_HINT_LOWERING_H_

#include "src/handles.h"

namespace v8 {
namespace internal {

// Forward declarations.
class FeedbackSlot;

namespace compiler {

// Forward declarations.
class EarlyReduction;
class Node;
class Operator;
class JSGraph;

// The type-hint lowering consumes feedback about data operations (i.e. unary
// and binary operations) to emit nodes using speculative simplified operators
// in favor of the generic JavaScript operators.
//
// This lowering is implemented as an early reduction and can be applied before
// nodes are placed into the initial graph. It provides the ability to shortcut
// the JavaScript-level operators and directly emit simplified-level operators
// even during initial graph building. This is the reason this lowering doesn't
// follow the interface of the reducer framework used after graph construction.
class JSTypeHintLowering {
 public:
  JSTypeHintLowering(JSGraph* jsgraph, Handle<FeedbackVector> feedback_vector);

  // Potential reduction of binary (arithmetic, logical, shift, equalities and
  // relational comparison) operations.
  EarlyReduction ReduceBinaryOperation(const Operator*, Node* left, Node* right,
                                       Node* effect, Node* control,
                                       FeedbackSlot slot);

 private:
  friend class JSSpeculativeBinopBuilder;

  JSGraph* jsgraph() const { return jsgraph_; }
  const Handle<FeedbackVector>& feedback_vector() const {
    return feedback_vector_;
  }

  JSGraph* jsgraph_;
  Handle<FeedbackVector> feedback_vector_;

  DISALLOW_COPY_AND_ASSIGN(JSTypeHintLowering);
};

// The result of a successful early reduction is a {value} node and an optional
// {effect} node (which might be different from the value). In case reduction
// failed, none of the above nodes are provided.
class EarlyReduction final {
 public:
  EarlyReduction() : value_(nullptr), effect_(nullptr) {}
  EarlyReduction(Node* value, Node* effect) : value_(value), effect_(effect) {}

  Node* value() const { return value_; }
  Node* effect() const { return effect_; }

  bool has_reduction() const { return value_ != nullptr; }
  bool has_effect() const { return effect_ != nullptr; }

 private:
  Node* value_;
  Node* effect_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_TYPE_HINT_LOWERING_H_
