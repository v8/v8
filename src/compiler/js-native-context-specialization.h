// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_NATIVE_CONTEXT_SPECIALIZATION_H_
#define V8_COMPILER_JS_NATIVE_CONTEXT_SPECIALIZATION_H_

#include "src/base/flags.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/property-access-info.h"
#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {

// Forward declarations.
class CompilationDependencies;
class Factory;
class TypeCache;


namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
class JSGraph;
class JSOperatorBuilder;
class MachineOperatorBuilder;


// Specializes a given JSGraph to a given native context, potentially constant
// folding some {LoadGlobal} nodes or strength reducing some {StoreGlobal}
// nodes.  And also specializes {LoadNamed} and {StoreNamed} nodes according
// to type feedback (if available).
class JSNativeContextSpecialization final : public AdvancedReducer {
 public:
  // Flags that control the mode of operation.
  enum Flag {
    kNoFlags = 0u,
    kDeoptimizationEnabled = 1u << 0,
  };
  typedef base::Flags<Flag> Flags;

  JSNativeContextSpecialization(Editor* editor, JSGraph* jsgraph, Flags flags,
                                Handle<GlobalObject> global_object,
                                CompilationDependencies* dependencies,
                                Zone* zone);

  Reduction Reduce(Node* node) final;

 private:
  Reduction ReduceJSCallFunction(Node* node);
  Reduction ReduceJSLoadGlobal(Node* node);
  Reduction ReduceJSStoreGlobal(Node* node);
  Reduction ReduceJSLoadNamed(Node* node);
  Reduction ReduceJSStoreNamed(Node* node);

  Reduction Replace(Node* node, Node* value, Node* effect = nullptr,
                    Node* control = nullptr) {
    ReplaceWithValue(node, value, effect, control);
    return Changed(value);
  }
  Reduction Replace(Node* node, Handle<Object> value);

  Reduction ReduceNamedAccess(Node* node, Node* value,
                              MapHandleList const& receiver_maps,
                              Handle<Name> name, PropertyAccessMode access_mode,
                              LanguageMode language_mode);

  struct ScriptContextTableLookupResult;
  bool LookupInScriptContextTable(Handle<Name> name,
                                  ScriptContextTableLookupResult* result);

  // Adds stability dependencies on all prototypes of every class in
  // {receiver_type} up to (and including) the {holder}.
  void AssumePrototypesStable(Type* receiver_type, Handle<JSObject> holder);

  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  Isolate* isolate() const;
  Factory* factory() const;
  CommonOperatorBuilder* common() const;
  JSOperatorBuilder* javascript() const;
  SimplifiedOperatorBuilder* simplified() const;
  MachineOperatorBuilder* machine() const;
  Flags flags() const { return flags_; }
  Handle<GlobalObject> global_object() const { return global_object_; }
  Handle<Context> native_context() const { return native_context_; }
  CompilationDependencies* dependencies() const { return dependencies_; }
  Zone* zone() const { return zone_; }
  PropertyAccessInfoFactory& access_info_factory() {
    return access_info_factory_;
  }

  JSGraph* const jsgraph_;
  Flags const flags_;
  Handle<GlobalObject> global_object_;
  Handle<Context> native_context_;
  CompilationDependencies* const dependencies_;
  Zone* const zone_;
  TypeCache const& type_cache_;
  PropertyAccessInfoFactory access_info_factory_;

  DISALLOW_COPY_AND_ASSIGN(JSNativeContextSpecialization);
};

DEFINE_OPERATORS_FOR_FLAGS(JSNativeContextSpecialization::Flags)

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_NATIVE_CONTEXT_SPECIALIZATION_H_
