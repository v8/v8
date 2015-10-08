// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_GLOBAL_SPECIALIZATION_H_
#define V8_COMPILER_JS_GLOBAL_SPECIALIZATION_H_

#include "src/base/flags.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {

// Forward declarations.
class CompilationDependencies;


namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
class JSGraph;
class JSOperatorBuilder;


// Specializes a given JSGraph to a given GlobalObject, potentially constant
// folding some {LoadGlobal} nodes or strength reducing some {StoreGlobal}
// nodes.
class JSGlobalSpecialization final : public AdvancedReducer {
 public:
  // Flags that control the mode of operation.
  enum Flag {
    kNoFlags = 0u,
    kDeoptimizationEnabled = 1u << 0,
    kTypingEnabled = 1u << 1
  };
  typedef base::Flags<Flag> Flags;

  JSGlobalSpecialization(Editor* editor, JSGraph* jsgraph, Flags flags,
                         Handle<GlobalObject> global_object,
                         CompilationDependencies* dependencies);

  Reduction Reduce(Node* node) final;

 private:
  Reduction ReduceJSLoadGlobal(Node* node);
  Reduction ReduceJSStoreGlobal(Node* node);
  Reduction ReduceLoadFromPropertyCell(Node* node,
                                       Handle<PropertyCell> property_cell);
  Reduction ReduceStoreToPropertyCell(Node* node,
                                      Handle<PropertyCell> property_cell);

  Reduction Replace(Node* node, Node* value, Node* effect = nullptr,
                    Node* control = nullptr) {
    ReplaceWithValue(node, value, effect, control);
    return Changed(value);
  }
  Reduction Replace(Node* node, Handle<Object> value);

  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  Isolate* isolate() const;
  CommonOperatorBuilder* common() const;
  JSOperatorBuilder* javascript() const;
  SimplifiedOperatorBuilder* simplified() { return &simplified_; }
  Flags flags() const { return flags_; }
  Handle<GlobalObject> global_object() const { return global_object_; }
  CompilationDependencies* dependencies() const { return dependencies_; }

  JSGraph* const jsgraph_;
  Flags const flags_;
  Handle<GlobalObject> global_object_;
  CompilationDependencies* const dependencies_;
  SimplifiedOperatorBuilder simplified_;

  DISALLOW_COPY_AND_ASSIGN(JSGlobalSpecialization);
};

DEFINE_OPERATORS_FOR_FLAGS(JSGlobalSpecialization::Flags)

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_GLOBAL_SPECIALIZATION_H_
