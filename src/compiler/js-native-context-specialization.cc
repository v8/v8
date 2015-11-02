// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-native-context-specialization.h"

#include "src/accessors.h"
#include "src/code-factory.h"
#include "src/compilation-dependencies.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-matchers.h"
#include "src/contexts.h"
#include "src/field-index-inl.h"
#include "src/lookup.h"
#include "src/objects-inl.h"  // TODO(mstarzinger): Temporary cycle breaker!
#include "src/type-cache.h"
#include "src/type-feedback-vector.h"

namespace v8 {
namespace internal {
namespace compiler {

struct JSNativeContextSpecialization::ScriptContextTableLookupResult {
  Handle<Context> context;
  bool immutable;
  int index;
};


JSNativeContextSpecialization::JSNativeContextSpecialization(
    Editor* editor, JSGraph* jsgraph, Flags flags,
    Handle<JSGlobalObject> global_object, CompilationDependencies* dependencies,
    Zone* zone)
    : AdvancedReducer(editor),
      jsgraph_(jsgraph),
      flags_(flags),
      global_object_(global_object),
      native_context_(global_object->native_context(), isolate()),
      dependencies_(dependencies),
      zone_(zone),
      type_cache_(TypeCache::Get()),
      access_info_factory_(dependencies, native_context(), graph()->zone()) {}


Reduction JSNativeContextSpecialization::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kJSCallFunction:
      return ReduceJSCallFunction(node);
    case IrOpcode::kJSLoadGlobal:
      return ReduceJSLoadGlobal(node);
    case IrOpcode::kJSStoreGlobal:
      return ReduceJSStoreGlobal(node);
    case IrOpcode::kJSLoadNamed:
      return ReduceJSLoadNamed(node);
    case IrOpcode::kJSStoreNamed:
      return ReduceJSStoreNamed(node);
    case IrOpcode::kJSLoadProperty:
      return ReduceJSLoadProperty(node);
    case IrOpcode::kJSStoreProperty:
      return ReduceJSStoreProperty(node);
    default:
      break;
  }
  return NoChange();
}


Reduction JSNativeContextSpecialization::ReduceJSCallFunction(Node* node) {
  DCHECK_EQ(IrOpcode::kJSCallFunction, node->opcode());
  CallFunctionParameters const& p = CallFunctionParametersOf(node->op());
  Node* target = NodeProperties::GetValueInput(node, 0);
  Node* frame_state = NodeProperties::GetFrameStateInput(node, 1);
  Node* control = NodeProperties::GetControlInput(node);
  Node* effect = NodeProperties::GetEffectInput(node);

  // Not much we can do if deoptimization support is disabled.
  if (!(flags() & kDeoptimizationEnabled)) return NoChange();

  // Don't mess with JSCallFunction nodes that have a constant {target}.
  if (HeapObjectMatcher(target).HasValue()) return NoChange();
  if (!p.feedback().IsValid()) return NoChange();
  CallICNexus nexus(p.feedback().vector(), p.feedback().slot());
  Handle<Object> feedback(nexus.GetFeedback(), isolate());
  if (feedback->IsWeakCell()) {
    Handle<WeakCell> cell = Handle<WeakCell>::cast(feedback);
    if (cell->value()->IsJSFunction()) {
      // Avoid cross-context leaks, meaning don't embed references to functions
      // in other native contexts.
      Handle<JSFunction> function(JSFunction::cast(cell->value()), isolate());
      if (function->context()->native_context() !=
          global_object()->native_context()) {
        return NoChange();
      }

      // Check that the {target} is still the {target_function}.
      Node* target_function = jsgraph()->HeapConstant(function);
      Node* check = graph()->NewNode(simplified()->ReferenceEqual(Type::Any()),
                                     target, target_function);
      Node* branch =
          graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);
      Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
      Node* deoptimize = graph()->NewNode(common()->Deoptimize(), frame_state,
                                          effect, if_false);
      // TODO(bmeurer): This should be on the AdvancedReducer somehow.
      NodeProperties::MergeControlToEnd(graph(), common(), deoptimize);
      control = graph()->NewNode(common()->IfTrue(), branch);

      // Specialize the JSCallFunction node to the {target_function}.
      NodeProperties::ReplaceValueInput(node, target_function, 0);
      NodeProperties::ReplaceControlInput(node, control);
      return Changed(node);
    }
    // TODO(bmeurer): Also support optimizing bound functions and proxies here.
  }
  return NoChange();
}


Reduction JSNativeContextSpecialization::ReduceJSLoadGlobal(Node* node) {
  DCHECK_EQ(IrOpcode::kJSLoadGlobal, node->opcode());
  Handle<Name> name = LoadGlobalParametersOf(node->op()).name();
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  // Try to lookup the name on the script context table first (lexical scoping).
  ScriptContextTableLookupResult result;
  if (LookupInScriptContextTable(name, &result)) {
    if (result.context->is_the_hole(result.index)) return NoChange();
    Node* context = jsgraph()->Constant(result.context);
    Node* value = effect = graph()->NewNode(
        javascript()->LoadContext(0, result.index, result.immutable), context,
        context, effect);
    return Replace(node, value, effect);
  }

  // Lookup on the global object instead.  We only deal with own data
  // properties of the global object here (represented as PropertyCell).
  LookupIterator it(global_object(), name, LookupIterator::OWN);
  if (it.state() != LookupIterator::DATA) return NoChange();
  Handle<PropertyCell> property_cell = it.GetPropertyCell();
  PropertyDetails property_details = property_cell->property_details();
  Handle<Object> property_cell_value(property_cell->value(), isolate());

  // Load from non-configurable, read-only data property on the global
  // object can be constant-folded, even without deoptimization support.
  if (!property_details.IsConfigurable() && property_details.IsReadOnly()) {
    return Replace(node, property_cell_value);
  }

  // Load from non-configurable, data property on the global can be lowered to
  // a field load, even without deoptimization, because the property cannot be
  // deleted or reconfigured to an accessor/interceptor property.  Yet, if
  // deoptimization support is available, we can constant-fold certain global
  // properties or at least lower them to field loads annotated with more
  // precise type feedback.
  Type* property_cell_value_type =
      Type::Intersect(Type::Any(), Type::Tagged(), graph()->zone());
  if (flags() & kDeoptimizationEnabled) {
    // Record a code dependency on the cell if we can benefit from the
    // additional feedback, or the global property is configurable (i.e.
    // can be deleted or reconfigured to an accessor property).
    if (property_details.cell_type() != PropertyCellType::kMutable ||
        property_details.IsConfigurable()) {
      dependencies()->AssumePropertyCell(property_cell);
    }

    // Load from constant/undefined global property can be constant-folded.
    if ((property_details.cell_type() == PropertyCellType::kConstant ||
         property_details.cell_type() == PropertyCellType::kUndefined)) {
      return Replace(node, property_cell_value);
    }

    // Load from constant type cell can benefit from type feedback.
    if (property_details.cell_type() == PropertyCellType::kConstantType) {
      // Compute proper type based on the current value in the cell.
      if (property_cell_value->IsSmi()) {
        property_cell_value_type = type_cache_.kSmi;
      } else if (property_cell_value->IsNumber()) {
        property_cell_value_type = type_cache_.kHeapNumber;
      } else {
        Handle<Map> property_cell_value_map(
            Handle<HeapObject>::cast(property_cell_value)->map(), isolate());
        property_cell_value_type =
            Type::Class(property_cell_value_map, graph()->zone());
      }
    }
  } else if (property_details.IsConfigurable()) {
    // Access to configurable global properties requires deoptimization support.
    return NoChange();
  }
  Node* value = effect = graph()->NewNode(
      simplified()->LoadField(
          AccessBuilder::ForPropertyCellValue(property_cell_value_type)),
      jsgraph()->Constant(property_cell), effect, control);
  return Replace(node, value, effect);
}


Reduction JSNativeContextSpecialization::ReduceJSStoreGlobal(Node* node) {
  DCHECK_EQ(IrOpcode::kJSStoreGlobal, node->opcode());
  Handle<Name> name = StoreGlobalParametersOf(node->op()).name();
  Node* value = NodeProperties::GetValueInput(node, 0);
  Node* frame_state = NodeProperties::GetFrameStateInput(node, 1);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  // Try to lookup the name on the script context table first (lexical scoping).
  ScriptContextTableLookupResult result;
  if (LookupInScriptContextTable(name, &result)) {
    if (result.context->is_the_hole(result.index)) return NoChange();
    if (result.immutable) return NoChange();
    Node* context = jsgraph()->Constant(result.context);
    effect = graph()->NewNode(javascript()->StoreContext(0, result.index),
                              context, value, context, effect, control);
    return Replace(node, value, effect, control);
  }

  // Lookup on the global object instead.  We only deal with own data
  // properties of the global object here (represented as PropertyCell).
  LookupIterator it(global_object(), name, LookupIterator::OWN);
  if (it.state() != LookupIterator::DATA) return NoChange();
  Handle<PropertyCell> property_cell = it.GetPropertyCell();
  PropertyDetails property_details = property_cell->property_details();
  Handle<Object> property_cell_value(property_cell->value(), isolate());

  // Don't even bother trying to lower stores to read-only data properties.
  if (property_details.IsReadOnly()) return NoChange();
  switch (property_details.cell_type()) {
    case PropertyCellType::kUndefined: {
      return NoChange();
    }
    case PropertyCellType::kConstant: {
      // Store to constant property cell requires deoptimization support,
      // because we might even need to eager deoptimize for mismatch.
      if (!(flags() & kDeoptimizationEnabled)) return NoChange();
      dependencies()->AssumePropertyCell(property_cell);
      Node* check =
          graph()->NewNode(simplified()->ReferenceEqual(Type::Tagged()), value,
                           jsgraph()->Constant(property_cell_value));
      Node* branch =
          graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);
      Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
      Node* deoptimize = graph()->NewNode(common()->Deoptimize(), frame_state,
                                          effect, if_false);
      // TODO(bmeurer): This should be on the AdvancedReducer somehow.
      NodeProperties::MergeControlToEnd(graph(), common(), deoptimize);
      control = graph()->NewNode(common()->IfTrue(), branch);
      return Replace(node, value, effect, control);
    }
    case PropertyCellType::kConstantType: {
      // Store to constant-type property cell requires deoptimization support,
      // because we might even need to eager deoptimize for mismatch.
      if (!(flags() & kDeoptimizationEnabled)) return NoChange();
      dependencies()->AssumePropertyCell(property_cell);
      Node* check = graph()->NewNode(simplified()->ObjectIsSmi(), value);
      if (property_cell_value->IsHeapObject()) {
        Node* branch = graph()->NewNode(common()->Branch(BranchHint::kFalse),
                                        check, control);
        Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
        Node* deoptimize = graph()->NewNode(common()->Deoptimize(), frame_state,
                                            effect, if_true);
        // TODO(bmeurer): This should be on the AdvancedReducer somehow.
        NodeProperties::MergeControlToEnd(graph(), common(), deoptimize);
        control = graph()->NewNode(common()->IfFalse(), branch);
        Node* value_map =
            graph()->NewNode(simplified()->LoadField(AccessBuilder::ForMap()),
                             value, effect, control);
        Handle<Map> property_cell_value_map(
            Handle<HeapObject>::cast(property_cell_value)->map(), isolate());
        check = graph()->NewNode(simplified()->ReferenceEqual(Type::Internal()),
                                 value_map,
                                 jsgraph()->Constant(property_cell_value_map));
      }
      Node* branch =
          graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);
      Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
      Node* deoptimize = graph()->NewNode(common()->Deoptimize(), frame_state,
                                          effect, if_false);
      // TODO(bmeurer): This should be on the AdvancedReducer somehow.
      NodeProperties::MergeControlToEnd(graph(), common(), deoptimize);
      control = graph()->NewNode(common()->IfTrue(), branch);
      break;
    }
    case PropertyCellType::kMutable: {
      // Store to non-configurable, data property on the global can be lowered
      // to a field store, even without deoptimization, because the property
      // cannot be deleted or reconfigured to an accessor/interceptor property.
      if (property_details.IsConfigurable()) {
        // With deoptimization support, we can lower stores even to configurable
        // data properties on the global object, by adding a code dependency on
        // the cell.
        if (!(flags() & kDeoptimizationEnabled)) return NoChange();
        dependencies()->AssumePropertyCell(property_cell);
      }
      break;
    }
  }
  effect = graph()->NewNode(
      simplified()->StoreField(AccessBuilder::ForPropertyCellValue()),
      jsgraph()->Constant(property_cell), value, effect, control);
  return Replace(node, value, effect, control);
}


Reduction JSNativeContextSpecialization::ReduceNamedAccess(
    Node* node, Node* value, MapHandleList const& receiver_maps,
    Handle<Name> name, PropertyAccessMode access_mode,
    LanguageMode language_mode, Node* index) {
  DCHECK(node->opcode() == IrOpcode::kJSLoadNamed ||
         node->opcode() == IrOpcode::kJSStoreNamed ||
         node->opcode() == IrOpcode::kJSLoadProperty ||
         node->opcode() == IrOpcode::kJSStoreProperty);
  Node* receiver = NodeProperties::GetValueInput(node, 0);
  Node* frame_state = NodeProperties::GetFrameStateInput(node, 1);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  // Not much we can do if deoptimization support is disabled.
  if (!(flags() & kDeoptimizationEnabled)) return NoChange();

  // Compute property access infos for the receiver maps.
  ZoneVector<PropertyAccessInfo> access_infos(zone());
  if (!access_info_factory().ComputePropertyAccessInfos(
          receiver_maps, name, access_mode, &access_infos)) {
    return NoChange();
  }

  // Nothing to do if we have no non-deprecated maps.
  if (access_infos.empty()) return NoChange();

  // The final states for every polymorphic branch. We join them with
  // Merge++Phi+EffectPhi at the bottom.
  ZoneVector<Node*> values(zone());
  ZoneVector<Node*> effects(zone());
  ZoneVector<Node*> controls(zone());

  // The list of "exiting" controls, which currently go to a single deoptimize.
  // TODO(bmeurer): Consider using an IC as fallback.
  Node* const exit_effect = effect;
  ZoneVector<Node*> exit_controls(zone());

  // Ensure that {index} matches the specified {name} (if {index} is given).
  if (index != nullptr) {
    Node* check = graph()->NewNode(simplified()->ReferenceEqual(Type::Name()),
                                   index, jsgraph()->HeapConstant(name));
    Node* branch =
        graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);
    exit_controls.push_back(graph()->NewNode(common()->IfFalse(), branch));
    control = graph()->NewNode(common()->IfTrue(), branch);
  }

  // Ensure that {receiver} is a heap object.
  Node* check = graph()->NewNode(simplified()->ObjectIsSmi(), receiver);
  Node* branch =
      graph()->NewNode(common()->Branch(BranchHint::kFalse), check, control);
  exit_controls.push_back(graph()->NewNode(common()->IfTrue(), branch));
  control = graph()->NewNode(common()->IfFalse(), branch);

  // Load the {receiver} map. The resulting effect is the dominating effect for
  // all (polymorphic) branches.
  Node* receiver_map = effect =
      graph()->NewNode(simplified()->LoadField(AccessBuilder::ForMap()),
                       receiver, effect, control);

  // Generate code for the various different property access patterns.
  Node* fallthrough_control = control;
  for (PropertyAccessInfo const& access_info : access_infos) {
    Node* this_value = value;
    Node* this_receiver = receiver;
    Node* this_effect = effect;
    Node* this_control;

    // Perform map check on {receiver}.
    Type* receiver_type = access_info.receiver_type();
    if (receiver_type->Is(Type::String())) {
      // Emit an instance type check for strings.
      Node* receiver_instance_type = this_effect = graph()->NewNode(
          simplified()->LoadField(AccessBuilder::ForMapInstanceType()),
          receiver_map, this_effect, fallthrough_control);
      Node* check =
          graph()->NewNode(machine()->Uint32LessThan(), receiver_instance_type,
                           jsgraph()->Uint32Constant(FIRST_NONSTRING_TYPE));
      Node* branch =
          graph()->NewNode(common()->Branch(), check, fallthrough_control);
      fallthrough_control = graph()->NewNode(common()->IfFalse(), branch);
      this_control = graph()->NewNode(common()->IfTrue(), branch);
    } else {
      // Emit a (sequence of) map checks for other properties.
      ZoneVector<Node*> this_controls(zone());
      for (auto i = access_info.receiver_type()->Classes(); !i.Done();
           i.Advance()) {
        Handle<Map> map = i.Current();
        Node* check =
            graph()->NewNode(simplified()->ReferenceEqual(Type::Internal()),
                             receiver_map, jsgraph()->Constant(map));
        Node* branch =
            graph()->NewNode(common()->Branch(), check, fallthrough_control);
        this_controls.push_back(graph()->NewNode(common()->IfTrue(), branch));
        fallthrough_control = graph()->NewNode(common()->IfFalse(), branch);
      }
      int const this_control_count = static_cast<int>(this_controls.size());
      this_control =
          (this_control_count == 1)
              ? this_controls.front()
              : graph()->NewNode(common()->Merge(this_control_count),
                                 this_control_count, &this_controls.front());
    }

    // Determine actual holder and perform prototype chain checks.
    Handle<JSObject> holder;
    if (access_info.holder().ToHandle(&holder)) {
      AssumePrototypesStable(receiver_type, holder);
    }

    // Generate the actual property access.
    if (access_info.IsNotFound()) {
      DCHECK_EQ(PropertyAccessMode::kLoad, access_mode);
      if (is_strong(language_mode)) {
        // TODO(bmeurer/mstarzinger): Add support for lowering inside try
        // blocks rewiring the IfException edge to a runtime call/throw.
        exit_controls.push_back(this_control);
        continue;
      } else {
        this_value = jsgraph()->UndefinedConstant();
      }
    } else if (access_info.IsDataConstant()) {
      this_value = jsgraph()->Constant(access_info.constant());
      if (access_mode == PropertyAccessMode::kStore) {
        Node* check = graph()->NewNode(
            simplified()->ReferenceEqual(Type::Tagged()), value, this_value);
        Node* branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                        check, this_control);
        exit_controls.push_back(graph()->NewNode(common()->IfFalse(), branch));
        this_control = graph()->NewNode(common()->IfTrue(), branch);
      }
    } else {
      DCHECK(access_info.IsDataField());
      FieldIndex const field_index = access_info.field_index();
      Type* const field_type = access_info.field_type();
      if (access_mode == PropertyAccessMode::kLoad &&
          access_info.holder().ToHandle(&holder)) {
        this_receiver = jsgraph()->Constant(holder);
      }
      Node* this_storage = this_receiver;
      if (!field_index.is_inobject()) {
        this_storage = this_effect = graph()->NewNode(
            simplified()->LoadField(AccessBuilder::ForJSObjectProperties()),
            this_storage, this_effect, this_control);
      }
      FieldAccess field_access = {kTaggedBase, field_index.offset(), name,
                                  field_type, kMachAnyTagged};
      if (access_mode == PropertyAccessMode::kLoad) {
        if (field_type->Is(Type::UntaggedFloat64())) {
          if (!field_index.is_inobject() || field_index.is_hidden_field() ||
              !FLAG_unbox_double_fields) {
            this_storage = this_effect =
                graph()->NewNode(simplified()->LoadField(field_access),
                                 this_storage, this_effect, this_control);
            field_access.offset = HeapNumber::kValueOffset;
            field_access.name = MaybeHandle<Name>();
          }
          field_access.machine_type = kMachFloat64;
        }
        this_value = this_effect =
            graph()->NewNode(simplified()->LoadField(field_access),
                             this_storage, this_effect, this_control);
      } else {
        DCHECK_EQ(PropertyAccessMode::kStore, access_mode);
        if (field_type->Is(Type::UntaggedFloat64())) {
          Node* check =
              graph()->NewNode(simplified()->ObjectIsNumber(), this_value);
          Node* branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                          check, this_control);
          exit_controls.push_back(
              graph()->NewNode(common()->IfFalse(), branch));
          this_control = graph()->NewNode(common()->IfTrue(), branch);
          this_value = graph()->NewNode(common()->Guard(Type::Number()),
                                        this_value, this_control);

          if (!field_index.is_inobject() || field_index.is_hidden_field() ||
              !FLAG_unbox_double_fields) {
            if (access_info.HasTransitionMap()) {
              // Allocate a MutableHeapNumber for the new property.
              Callable callable =
                  CodeFactory::AllocateMutableHeapNumber(isolate());
              CallDescriptor* desc = Linkage::GetStubCallDescriptor(
                  isolate(), jsgraph()->zone(), callable.descriptor(), 0,
                  CallDescriptor::kNoFlags, Operator::kNoThrow);
              Node* this_box = this_effect = graph()->NewNode(
                  common()->Call(desc),
                  jsgraph()->HeapConstant(callable.code()),
                  jsgraph()->NoContextConstant(), this_effect, this_control);
              this_effect = graph()->NewNode(
                  simplified()->StoreField(AccessBuilder::ForHeapNumberValue()),
                  this_box, this_value, this_effect, this_control);
              this_value = this_box;

              field_access.type = Type::TaggedPointer();
            } else {
              // We just store directly to the MutableHeapNumber.
              this_storage = this_effect =
                  graph()->NewNode(simplified()->LoadField(field_access),
                                   this_storage, this_effect, this_control);
              field_access.offset = HeapNumber::kValueOffset;
              field_access.name = MaybeHandle<Name>();
              field_access.machine_type = kMachFloat64;
            }
          } else {
            // Unboxed double field, we store directly to the field.
            field_access.machine_type = kMachFloat64;
          }
        } else if (field_type->Is(Type::TaggedSigned())) {
          Node* check =
              graph()->NewNode(simplified()->ObjectIsSmi(), this_value);
          Node* branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                          check, this_control);
          exit_controls.push_back(
              graph()->NewNode(common()->IfFalse(), branch));
          this_control = graph()->NewNode(common()->IfTrue(), branch);
        } else if (field_type->Is(Type::TaggedPointer())) {
          Node* check =
              graph()->NewNode(simplified()->ObjectIsSmi(), this_value);
          Node* branch = graph()->NewNode(common()->Branch(BranchHint::kFalse),
                                          check, this_control);
          exit_controls.push_back(graph()->NewNode(common()->IfTrue(), branch));
          this_control = graph()->NewNode(common()->IfFalse(), branch);
          if (field_type->NumClasses() > 0) {
            // Emit a (sequence of) map checks for the value.
            ZoneVector<Node*> this_controls(zone());
            Node* this_value_map = this_effect = graph()->NewNode(
                simplified()->LoadField(AccessBuilder::ForMap()), this_value,
                this_effect, this_control);
            for (auto i = field_type->Classes(); !i.Done(); i.Advance()) {
              Handle<Map> field_map(i.Current());
              check = graph()->NewNode(
                  simplified()->ReferenceEqual(Type::Internal()),
                  this_value_map, jsgraph()->Constant(field_map));
              branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                        check, this_control);
              this_control = graph()->NewNode(common()->IfFalse(), branch);
              this_controls.push_back(
                  graph()->NewNode(common()->IfTrue(), branch));
            }
            exit_controls.push_back(this_control);
            int const this_control_count =
                static_cast<int>(this_controls.size());
            this_control =
                (this_control_count == 1)
                    ? this_controls.front()
                    : graph()->NewNode(common()->Merge(this_control_count),
                                       this_control_count,
                                       &this_controls.front());
          }
        } else {
          DCHECK(field_type->Is(Type::Tagged()));
        }
        Handle<Map> transition_map;
        if (access_info.transition_map().ToHandle(&transition_map)) {
          this_effect = graph()->NewNode(common()->BeginRegion(), this_effect);
          this_effect = graph()->NewNode(
              simplified()->StoreField(AccessBuilder::ForMap()), this_receiver,
              jsgraph()->Constant(transition_map), this_effect, this_control);
        }
        this_effect = graph()->NewNode(simplified()->StoreField(field_access),
                                       this_storage, this_value, this_effect,
                                       this_control);
        if (access_info.HasTransitionMap()) {
          this_effect =
              graph()->NewNode(common()->FinishRegion(),
                               jsgraph()->UndefinedConstant(), this_effect);
        }
      }
    }

    // Remember the final state for this property access.
    values.push_back(this_value);
    effects.push_back(this_effect);
    controls.push_back(this_control);
  }

  // Collect the fallthru control as final "exit" control.
  if (fallthrough_control != control) {
    // Mark the last fallthru branch as deferred.
    Node* branch = NodeProperties::GetControlInput(fallthrough_control);
    DCHECK_EQ(IrOpcode::kBranch, branch->opcode());
    if (fallthrough_control->opcode() == IrOpcode::kIfTrue) {
      NodeProperties::ChangeOp(branch, common()->Branch(BranchHint::kFalse));
    } else {
      DCHECK_EQ(IrOpcode::kIfFalse, fallthrough_control->opcode());
      NodeProperties::ChangeOp(branch, common()->Branch(BranchHint::kTrue));
    }
  }
  exit_controls.push_back(fallthrough_control);

  // Generate the single "exit" point, where we get if either all map/instance
  // type checks failed, or one of the assumptions inside one of the cases
  // failes (i.e. failing prototype chain check).
  // TODO(bmeurer): Consider falling back to IC here if deoptimization is
  // disabled.
  int const exit_control_count = static_cast<int>(exit_controls.size());
  Node* exit_control =
      (exit_control_count == 1)
          ? exit_controls.front()
          : graph()->NewNode(common()->Merge(exit_control_count),
                             exit_control_count, &exit_controls.front());
  Node* deoptimize = graph()->NewNode(common()->Deoptimize(), frame_state,
                                      exit_effect, exit_control);
  // TODO(bmeurer): This should be on the AdvancedReducer somehow.
  NodeProperties::MergeControlToEnd(graph(), common(), deoptimize);

  // Generate the final merge point for all (polymorphic) branches.
  int const control_count = static_cast<int>(controls.size());
  if (control_count == 0) {
    value = effect = control = jsgraph()->Dead();
  } else if (control_count == 1) {
    value = values.front();
    effect = effects.front();
    control = controls.front();
  } else {
    control = graph()->NewNode(common()->Merge(control_count), control_count,
                               &controls.front());
    values.push_back(control);
    value = graph()->NewNode(common()->Phi(kMachAnyTagged, control_count),
                             control_count + 1, &values.front());
    effects.push_back(control);
    effect = graph()->NewNode(common()->EffectPhi(control_count),
                              control_count + 1, &effects.front());
  }
  return Replace(node, value, effect, control);
}


Reduction JSNativeContextSpecialization::ReduceJSLoadNamed(Node* node) {
  DCHECK_EQ(IrOpcode::kJSLoadNamed, node->opcode());
  NamedAccess const& p = NamedAccessOf(node->op());
  Node* const value = jsgraph()->Dead();

  // Extract receiver maps from the LOAD_IC using the LoadICNexus.
  MapHandleList receiver_maps;
  if (!p.feedback().IsValid()) return NoChange();
  LoadICNexus nexus(p.feedback().vector(), p.feedback().slot());
  if (nexus.ExtractMaps(&receiver_maps) == 0) return NoChange();
  DCHECK_LT(0, receiver_maps.length());

  // Try to lower the named access based on the {receiver_maps}.
  return ReduceNamedAccess(node, value, receiver_maps, p.name(),
                           PropertyAccessMode::kLoad, p.language_mode());
}


Reduction JSNativeContextSpecialization::ReduceJSStoreNamed(Node* node) {
  DCHECK_EQ(IrOpcode::kJSStoreNamed, node->opcode());
  NamedAccess const& p = NamedAccessOf(node->op());
  Node* const value = NodeProperties::GetValueInput(node, 1);

  // Extract receiver maps from the STORE_IC using the StoreICNexus.
  MapHandleList receiver_maps;
  if (!p.feedback().IsValid()) return NoChange();
  StoreICNexus nexus(p.feedback().vector(), p.feedback().slot());
  if (nexus.ExtractMaps(&receiver_maps) == 0) return NoChange();
  DCHECK_LT(0, receiver_maps.length());

  // Try to lower the named access based on the {receiver_maps}.
  return ReduceNamedAccess(node, value, receiver_maps, p.name(),
                           PropertyAccessMode::kStore, p.language_mode());
}


Reduction JSNativeContextSpecialization::ReduceKeyedAccess(
    Node* node, Node* index, Node* value, FeedbackNexus const& nexus,
    PropertyAccessMode access_mode, LanguageMode language_mode) {
  DCHECK(node->opcode() == IrOpcode::kJSLoadProperty ||
         node->opcode() == IrOpcode::kJSStoreProperty);

  // Extract receiver maps from the {nexus}.
  MapHandleList receiver_maps;
  if (nexus.ExtractMaps(&receiver_maps) == 0) return NoChange();
  DCHECK_LT(0, receiver_maps.length());

  // Optimize access for constant {index}.
  HeapObjectMatcher mindex(index);
  if (mindex.HasValue() && mindex.Value()->IsPrimitive()) {
    // Keyed access requires a ToPropertyKey on the {index} first before
    // looking up the property on the object (see ES6 section 12.3.2.1).
    // We can only do this for non-observable ToPropertyKey invocations,
    // so we limit the constant indices to primitives at this point.
    Handle<Name> name;
    if (Object::ToName(isolate(), mindex.Value()).ToHandle(&name)) {
      uint32_t array_index;
      if (name->AsArrayIndex(&array_index)) {
        // TODO(bmeurer): Optimize element access with constant {index}.
      } else {
        name = factory()->InternalizeName(name);
        return ReduceNamedAccess(node, value, receiver_maps, name, access_mode,
                                 language_mode);
      }
    }
  }

  // Check if we have feedback for a named access.
  if (Name* name = nexus.FindFirstName()) {
    return ReduceNamedAccess(node, value, receiver_maps,
                             handle(name, isolate()), access_mode,
                             language_mode, index);
  }

  return NoChange();
}


Reduction JSNativeContextSpecialization::ReduceJSLoadProperty(Node* node) {
  DCHECK_EQ(IrOpcode::kJSLoadProperty, node->opcode());
  PropertyAccess const& p = PropertyAccessOf(node->op());
  Node* const index = NodeProperties::GetValueInput(node, 1);
  Node* const value = jsgraph()->Dead();

  // Extract receiver maps from the KEYED_LOAD_IC using the KeyedLoadICNexus.
  if (!p.feedback().IsValid()) return NoChange();
  KeyedLoadICNexus nexus(p.feedback().vector(), p.feedback().slot());

  // Try to lower the keyed access based on the {nexus}.
  return ReduceKeyedAccess(node, index, value, nexus, PropertyAccessMode::kLoad,
                           p.language_mode());
}


Reduction JSNativeContextSpecialization::ReduceJSStoreProperty(Node* node) {
  DCHECK_EQ(IrOpcode::kJSStoreProperty, node->opcode());
  PropertyAccess const& p = PropertyAccessOf(node->op());
  Node* const index = NodeProperties::GetValueInput(node, 1);
  Node* const value = NodeProperties::GetValueInput(node, 2);

  // Extract receiver maps from the KEYED_STORE_IC using the KeyedStoreICNexus.
  if (!p.feedback().IsValid()) return NoChange();
  KeyedStoreICNexus nexus(p.feedback().vector(), p.feedback().slot());

  // Try to lower the keyed access based on the {nexus}.
  return ReduceKeyedAccess(node, index, value, nexus,
                           PropertyAccessMode::kStore, p.language_mode());
}


Reduction JSNativeContextSpecialization::Replace(Node* node,
                                                 Handle<Object> value) {
  return Replace(node, jsgraph()->Constant(value));
}


bool JSNativeContextSpecialization::LookupInScriptContextTable(
    Handle<Name> name, ScriptContextTableLookupResult* result) {
  if (!name->IsString()) return false;
  Handle<ScriptContextTable> script_context_table(
      native_context()->script_context_table());
  ScriptContextTable::LookupResult lookup_result;
  if (!ScriptContextTable::Lookup(script_context_table,
                                  Handle<String>::cast(name), &lookup_result)) {
    return false;
  }
  Handle<Context> script_context = ScriptContextTable::GetContext(
      script_context_table, lookup_result.context_index);
  result->context = script_context;
  result->immutable = IsImmutableVariableMode(lookup_result.mode);
  result->index = lookup_result.slot_index;
  return true;
}


void JSNativeContextSpecialization::AssumePrototypesStable(
    Type* receiver_type, Handle<JSObject> holder) {
  // Determine actual holder and perform prototype chain checks.
  for (auto i = receiver_type->Classes(); !i.Done(); i.Advance()) {
    Handle<Map> map = i.Current();
    // Perform the implicit ToObject for primitives here.
    // Implemented according to ES6 section 7.3.2 GetV (V, P).
    Handle<JSFunction> constructor;
    if (Map::GetConstructorFunction(map, native_context())
            .ToHandle(&constructor)) {
      map = handle(constructor->initial_map(), isolate());
    }
    for (PrototypeIterator j(map);; j.Advance()) {
      // Check that the {prototype} still has the same map.  All prototype
      // maps are guaranteed to be stable, so it's sufficient to add a
      // stability dependency here.
      Handle<JSReceiver> const prototype =
          PrototypeIterator::GetCurrent<JSReceiver>(j);
      dependencies()->AssumeMapStable(handle(prototype->map(), isolate()));
      // Stop once we get to the holder.
      if (prototype.is_identical_to(holder)) break;
    }
  }
}


Graph* JSNativeContextSpecialization::graph() const {
  return jsgraph()->graph();
}


Isolate* JSNativeContextSpecialization::isolate() const {
  return jsgraph()->isolate();
}


Factory* JSNativeContextSpecialization::factory() const {
  return isolate()->factory();
}


MachineOperatorBuilder* JSNativeContextSpecialization::machine() const {
  return jsgraph()->machine();
}


CommonOperatorBuilder* JSNativeContextSpecialization::common() const {
  return jsgraph()->common();
}


JSOperatorBuilder* JSNativeContextSpecialization::javascript() const {
  return jsgraph()->javascript();
}


SimplifiedOperatorBuilder* JSNativeContextSpecialization::simplified() const {
  return jsgraph()->simplified();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
