// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-global-specialization.h"

#include "src/compilation-dependencies.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"
#include "src/contexts.h"
#include "src/lookup.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

JSGlobalSpecialization::JSGlobalSpecialization(
    Editor* editor, JSGraph* jsgraph, Flags flags,
    Handle<GlobalObject> global_object, CompilationDependencies* dependencies)
    : AdvancedReducer(editor),
      jsgraph_(jsgraph),
      flags_(flags),
      global_object_(global_object),
      dependencies_(dependencies),
      simplified_(graph()->zone()) {}


Reduction JSGlobalSpecialization::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kJSLoadGlobal:
      return ReduceJSLoadGlobal(node);
    case IrOpcode::kJSStoreGlobal:
      return ReduceJSStoreGlobal(node);
    default:
      break;
  }
  return NoChange();
}


Reduction JSGlobalSpecialization::ReduceJSLoadGlobal(Node* node) {
  DCHECK_EQ(IrOpcode::kJSLoadGlobal, node->opcode());
  Handle<Name> name = LoadGlobalParametersOf(node->op()).name();
  Node* effect = NodeProperties::GetEffectInput(node);

  // Try to lookup the name on the script context table first (lexical scoping).
  if (name->IsString()) {
    Handle<ScriptContextTable> script_context_table(
        global_object()->native_context()->script_context_table());
    ScriptContextTable::LookupResult result;
    if (ScriptContextTable::Lookup(script_context_table,
                                   Handle<String>::cast(name), &result)) {
      Handle<Context> script_context = ScriptContextTable::GetContext(
          script_context_table, result.context_index);
      if (script_context->is_the_hole(result.slot_index)) {
        // TODO(bmeurer): Is this relevant in practice?
        return NoChange();
      }
      Node* context = jsgraph()->Constant(script_context);
      Node* value = effect = graph()->NewNode(
          javascript()->LoadContext(0, result.slot_index,
                                    IsImmutableVariableMode(result.mode)),
          context, context, effect);
      return Replace(node, value, effect);
    }
  }

  // Lookup on the global object instead.
  LookupIterator it(global_object(), name, LookupIterator::OWN);
  if (it.state() == LookupIterator::DATA) {
    return ReduceLoadFromPropertyCell(node, it.GetPropertyCell());
  }

  return NoChange();
}


Reduction JSGlobalSpecialization::ReduceJSStoreGlobal(Node* node) {
  DCHECK_EQ(IrOpcode::kJSStoreGlobal, node->opcode());
  Handle<Name> name = StoreGlobalParametersOf(node->op()).name();
  Node* value = NodeProperties::GetValueInput(node, 2);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  // Try to lookup the name on the script context table first (lexical scoping).
  if (name->IsString()) {
    Handle<ScriptContextTable> script_context_table(
        global_object()->native_context()->script_context_table());
    ScriptContextTable::LookupResult result;
    if (ScriptContextTable::Lookup(script_context_table,
                                   Handle<String>::cast(name), &result)) {
      if (IsImmutableVariableMode(result.mode)) return NoChange();
      Handle<Context> script_context = ScriptContextTable::GetContext(
          script_context_table, result.context_index);
      if (script_context->is_the_hole(result.slot_index)) {
        // TODO(bmeurer): Is this relevant in practice?
        return NoChange();
      }
      Node* context = jsgraph()->Constant(script_context);
      effect =
          graph()->NewNode(javascript()->StoreContext(0, result.slot_index),
                           context, value, context, effect, control);
      return Replace(node, value, effect, control);
    }
  }

  // Lookup on the global object instead.
  LookupIterator it(global_object(), name, LookupIterator::OWN);
  if (it.state() == LookupIterator::DATA) {
    return ReduceStoreToPropertyCell(node, it.GetPropertyCell());
  }

  return NoChange();
}


Reduction JSGlobalSpecialization::ReduceLoadFromPropertyCell(
    Node* node, Handle<PropertyCell> property_cell) {
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  // We only specialize global data property access.
  PropertyDetails property_details = property_cell->property_details();
  DCHECK_EQ(kData, property_details.kind());
  Handle<Object> property_cell_value(property_cell->value(), isolate());
  DCHECK(!property_cell_value->IsTheHole());
  // Load from non-configurable, read-only data property on the global
  // object can be constant-folded, even without deoptimization support.
  if (!property_details.IsConfigurable() && property_details.IsReadOnly()) {
    return Replace(node, property_cell_value);
  }
  // Load from constant/undefined global property can be constant-folded
  // with deoptimization support, by adding a code dependency on the cell.
  if ((property_details.cell_type() == PropertyCellType::kConstant ||
       property_details.cell_type() == PropertyCellType::kUndefined) &&
      (flags() & kDeoptimizationEnabled)) {
    dependencies()->AssumePropertyCell(property_cell);
    return Replace(node, property_cell_value);
  }
  // Not much we can do if we run the generic pipeline here.
  if (!(flags() & kTypingEnabled)) return NoChange();
  // Load from constant type global property can benefit from representation
  // (and map) feedback with deoptimization support (requires code dependency).
  if (property_details.cell_type() == PropertyCellType::kConstantType &&
      (flags() & kDeoptimizationEnabled)) {
    dependencies()->AssumePropertyCell(property_cell);
    Type* property_cell_value_type = Type::Any();
    switch (property_cell->GetConstantType()) {
      case PropertyCellConstantType::kSmi:
        property_cell_value_type = Type::Intersect(
            Type::SignedSmall(), Type::TaggedSigned(), graph()->zone());
        break;
      case PropertyCellConstantType::kStableMap: {
        // TODO(bmeurer): Determine type based on the map's instance type.
        property_cell_value_type = Type::TaggedPointer();
        break;
      }
    }
    Node* value = effect = graph()->NewNode(
        simplified()->LoadField(
            AccessBuilder::ForPropertyCellValue(property_cell_value_type)),
        jsgraph()->Constant(property_cell), effect, control);
    return Replace(node, value, effect);
  }
  // Load from non-configurable, data property on the global can be lowered to
  // a field load, even without deoptimization, because the property cannot be
  // deleted or reconfigured to an accessor/interceptor property.
  if (property_details.IsConfigurable()) {
    // With deoptimization support, we can lower loads even from configurable
    // data properties on the global object, by adding a code dependency on
    // the cell.
    if (!(flags() & kDeoptimizationEnabled)) return NoChange();
    dependencies()->AssumePropertyCell(property_cell);
  }
  Node* value = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForPropertyCellValue()),
      jsgraph()->Constant(property_cell), effect, control);
  return Replace(node, value, effect);
}


Reduction JSGlobalSpecialization::ReduceStoreToPropertyCell(
    Node* node, Handle<PropertyCell> property_cell) {
  Node* value = NodeProperties::GetValueInput(node, 2);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  // We only specialize global data property access.
  PropertyDetails property_details = property_cell->property_details();
  DCHECK_EQ(kData, property_details.kind());
  Handle<Object> property_cell_value(property_cell->value(), isolate());
  DCHECK(!property_cell_value->IsTheHole());
  // Don't even bother trying to lower stores to read-only data properties.
  if (property_details.IsReadOnly()) return NoChange();
  // Not much we can do if we run the generic pipeline here.
  if (!(flags() & kTypingEnabled)) return NoChange();
  // TODO(bmeurer): For now we deal only with cells in mutable state.
  if (property_details.cell_type() != PropertyCellType::kMutable) {
    return NoChange();
  }
  // Store to non-configurable, data property on the global can be lowered to
  // a field store, even without deoptimization, because the property cannot be
  // deleted or reconfigured to an accessor/interceptor property.
  if (property_details.IsConfigurable()) {
    // With deoptimization support, we can lower stores even to configurable
    // data properties on the global object, by adding a code dependency on
    // the cell.
    if (!(flags() & kDeoptimizationEnabled)) return NoChange();
    dependencies()->AssumePropertyCell(property_cell);
  }
  effect = graph()->NewNode(
      simplified()->StoreField(AccessBuilder::ForPropertyCellValue()),
      jsgraph()->Constant(property_cell), value, effect, control);
  return Replace(node, value, effect, control);
}


Reduction JSGlobalSpecialization::Replace(Node* node, Handle<Object> value) {
  if (value->IsConsString()) {
    value = String::Flatten(Handle<String>::cast(value), TENURED);
  }
  return Replace(node, jsgraph()->Constant(value));
}


Graph* JSGlobalSpecialization::graph() const { return jsgraph()->graph(); }


Isolate* JSGlobalSpecialization::isolate() const {
  return jsgraph()->isolate();
}


JSOperatorBuilder* JSGlobalSpecialization::javascript() const {
  return jsgraph()->javascript();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
