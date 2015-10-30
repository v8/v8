// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-native-context-specialization.h"

#include "src/accessors.h"
#include "src/compilation-dependencies.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"
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
    Handle<GlobalObject> global_object, CompilationDependencies* dependencies,
    Zone* zone)
    : AdvancedReducer(editor),
      jsgraph_(jsgraph),
      flags_(flags),
      global_object_(global_object),
      native_context_(global_object->native_context(), isolate()),
      dependencies_(dependencies),
      zone_(zone),
      type_cache_(TypeCache::Get()) {}


Reduction JSNativeContextSpecialization::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kJSLoadGlobal:
      return ReduceJSLoadGlobal(node);
    case IrOpcode::kJSStoreGlobal:
      return ReduceJSStoreGlobal(node);
    case IrOpcode::kJSLoadNamed:
      return ReduceJSLoadNamed(node);
    case IrOpcode::kJSStoreNamed:
      return ReduceJSStoreNamed(node);
    default:
      break;
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


// This class encapsulates all information required to access a certain
// object property, either on the object itself or on the prototype chain.
class JSNativeContextSpecialization::PropertyAccessInfo final {
 public:
  enum Kind { kInvalid, kDataConstant, kDataField, kTransitionToField };

  static PropertyAccessInfo DataConstant(Type* receiver_type,
                                         Handle<Object> constant,
                                         MaybeHandle<JSObject> holder) {
    return PropertyAccessInfo(holder, constant, receiver_type);
  }
  static PropertyAccessInfo DataField(
      Type* receiver_type, FieldIndex field_index, Type* field_type,
      MaybeHandle<JSObject> holder = MaybeHandle<JSObject>()) {
    return PropertyAccessInfo(holder, field_index, field_type, receiver_type);
  }
  static PropertyAccessInfo TransitionToField(Type* receiver_type,
                                              FieldIndex field_index,
                                              Type* field_type,
                                              Handle<Map> transition_map,
                                              MaybeHandle<JSObject> holder) {
    return PropertyAccessInfo(holder, transition_map, field_index, field_type,
                              receiver_type);
  }

  PropertyAccessInfo() : kind_(kInvalid) {}
  PropertyAccessInfo(MaybeHandle<JSObject> holder, Handle<Object> constant,
                     Type* receiver_type)
      : kind_(kDataConstant),
        receiver_type_(receiver_type),
        constant_(constant),
        holder_(holder) {}
  PropertyAccessInfo(MaybeHandle<JSObject> holder, FieldIndex field_index,
                     Type* field_type, Type* receiver_type)
      : kind_(kDataField),
        receiver_type_(receiver_type),
        holder_(holder),
        field_index_(field_index),
        field_type_(field_type) {}
  PropertyAccessInfo(MaybeHandle<JSObject> holder, Handle<Map> transition_map,
                     FieldIndex field_index, Type* field_type,
                     Type* receiver_type)
      : kind_(kTransitionToField),
        receiver_type_(receiver_type),
        transition_map_(transition_map),
        holder_(holder),
        field_index_(field_index),
        field_type_(field_type) {}

  bool IsDataConstant() const { return kind() == kDataConstant; }
  bool IsDataField() const { return kind() == kDataField; }
  bool IsTransitionToField() const { return kind() == kTransitionToField; }

  Kind kind() const { return kind_; }
  MaybeHandle<JSObject> holder() const { return holder_; }
  Handle<Object> constant() const { return constant_; }
  Handle<Object> transition_map() const { return transition_map_; }
  FieldIndex field_index() const { return field_index_; }
  Type* field_type() const { return field_type_; }
  Type* receiver_type() const { return receiver_type_; }

 private:
  Kind kind_;
  Type* receiver_type_;
  Handle<Object> constant_;
  Handle<Map> transition_map_;
  MaybeHandle<JSObject> holder_;
  FieldIndex field_index_;
  Type* field_type_ = Type::Any();
};


namespace {

bool CanInlinePropertyAccess(Handle<Map> map) {
  // TODO(bmeurer): Do something about the number stuff.
  if (map->instance_type() == HEAP_NUMBER_TYPE) return false;
  if (map->instance_type() < FIRST_NONSTRING_TYPE) return true;
  return map->IsJSObjectMap() && !map->is_dictionary_map() &&
         !map->has_named_interceptor() &&
         // TODO(verwaest): Whitelist contexts to which we have access.
         !map->is_access_check_needed();
}

}  // namespace


bool JSNativeContextSpecialization::ComputePropertyAccessInfo(
    Handle<Map> map, Handle<Name> name, PropertyAccessMode access_mode,
    PropertyAccessInfo* access_info) {
  // Check if it is safe to inline property access for the {map}.
  if (!CanInlinePropertyAccess(map)) return false;

  // Compute the receiver type.
  Handle<Map> receiver_map = map;
  Type* receiver_type = Type::Class(receiver_map, graph()->zone());

  // We support fast inline cases for certain JSObject getters.
  if (access_mode == kLoad) {
    // Check for special JSObject field accessors.
    int offset;
    if (Accessors::IsJSObjectFieldAccessor(map, name, &offset)) {
      FieldIndex field_index = FieldIndex::ForInObjectOffset(offset);
      Type* field_type = Type::Tagged();
      if (map->IsStringMap()) {
        DCHECK(Name::Equals(factory()->length_string(), name));
        // The String::length property is always a smi in the range
        // [0, String::kMaxLength].
        field_type = type_cache_.kStringLengthType;
      } else if (map->IsJSArrayMap()) {
        DCHECK(Name::Equals(factory()->length_string(), name));
        // The JSArray::length property is a smi in the range
        // [0, FixedDoubleArray::kMaxLength] in case of fast double
        // elements, a smi in the range [0, FixedArray::kMaxLength]
        // in case of other fast elements, and [0, kMaxUInt32] in
        // case of other arrays.
        if (IsFastDoubleElementsKind(map->elements_kind())) {
          field_type = type_cache_.kFixedDoubleArrayLengthType;
        } else if (IsFastElementsKind(map->elements_kind())) {
          field_type = type_cache_.kFixedArrayLengthType;
        } else {
          field_type = type_cache_.kJSArrayLengthType;
        }
      }
      *access_info =
          PropertyAccessInfo::DataField(receiver_type, field_index, field_type);
      return true;
    }
  }

  MaybeHandle<JSObject> holder;
  while (true) {
    // Lookup the named property on the {map}.
    Handle<DescriptorArray> descriptors(map->instance_descriptors(), isolate());
    int const number = descriptors->SearchWithCache(*name, *map);
    if (number != DescriptorArray::kNotFound) {
      PropertyDetails const details = descriptors->GetDetails(number);
      if (access_mode == kStore) {
        // Don't bother optimizing stores to read-only properties.
        if (details.IsReadOnly()) {
          return false;
        }
        // Check for store to data property on a prototype.
        if (details.kind() == kData && !holder.is_null()) {
          // We need to add the data field to the receiver. Leave the loop
          // and check whether we already have a transition for this field.
          // Implemented according to ES6 section 9.1.9 [[Set]] (P, V, Receiver)
          break;
        }
      }
      if (details.type() == DATA_CONSTANT) {
        *access_info = PropertyAccessInfo::DataConstant(
            receiver_type, handle(descriptors->GetValue(number), isolate()),
            holder);
        return true;
      } else if (details.type() == DATA) {
        int index = descriptors->GetFieldIndex(number);
        Representation field_representation = details.representation();
        FieldIndex field_index = FieldIndex::ForPropertyIndex(
            *map, index, field_representation.IsDouble());
        Type* field_type = Type::Tagged();
        if (field_representation.IsSmi()) {
          field_type = type_cache_.kSmi;
        } else if (field_representation.IsDouble()) {
          field_type = type_cache_.kFloat64;
        } else if (field_representation.IsHeapObject()) {
          // Extract the field type from the property details (make sure its
          // representation is TaggedPointer to reflect the heap object case).
          field_type = Type::Intersect(
              Type::Convert<HeapType>(
                  handle(descriptors->GetFieldType(number), isolate()),
                  graph()->zone()),
              Type::TaggedPointer(), graph()->zone());
          if (field_type->Is(Type::None())) {
            // Store is not safe if the field type was cleared.
            if (access_mode == kStore) return false;

            // The field type was cleared by the GC, so we don't know anything
            // about the contents now.
            // TODO(bmeurer): It would be awesome to make this saner in the
            // runtime/GC interaction.
            field_type = Type::TaggedPointer();
          } else if (!Type::Any()->Is(field_type)) {
            // Add proper code dependencies in case of stable field map(s).
            Handle<Map> field_owner_map(map->FindFieldOwner(number), isolate());
            dependencies()->AssumeFieldType(field_owner_map);
          }
          DCHECK(field_type->Is(Type::TaggedPointer()));
        }
        *access_info = PropertyAccessInfo::DataField(receiver_type, field_index,
                                                     field_type, holder);
        return true;
      } else {
        // TODO(bmeurer): Add support for accessors.
        return false;
      }
    }

    // Don't search on the prototype chain for special indices in case of
    // integer indexed exotic objects (see ES6 section 9.4.5).
    if (map->IsJSTypedArrayMap() && name->IsString() &&
        IsSpecialIndex(isolate()->unicode_cache(), String::cast(*name))) {
      return false;
    }

    // Don't lookup private symbols on the prototype chain.
    if (name->IsPrivate()) return false;

    // Walk up the prototype chain.
    if (!map->prototype()->IsJSObject()) {
      // Perform the implicit ToObject for primitives here.
      // Implemented according to ES6 section 7.3.2 GetV (V, P).
      Handle<JSFunction> constructor;
      if (Map::GetConstructorFunction(map, native_context())
              .ToHandle(&constructor)) {
        map = handle(constructor->initial_map(), isolate());
        DCHECK(map->prototype()->IsJSObject());
      } else if (map->prototype()->IsNull()) {
        // Store to property not found on the receiver or any prototype, we need
        // to transition to a new data property.
        // Implemented according to ES6 section 9.1.9 [[Set]] (P, V, Receiver)
        if (access_mode == kStore) {
          break;
        }
        // TODO(bmeurer): Handle the not found case if the prototype is null.
        return false;
      } else {
        return false;
      }
    }
    Handle<JSObject> map_prototype(JSObject::cast(map->prototype()), isolate());
    if (map_prototype->map()->is_deprecated()) {
      // Try to migrate the prototype object so we don't embed the deprecated
      // map into the optimized code.
      JSObject::TryMigrateInstance(map_prototype);
    }
    map = handle(map_prototype->map(), isolate());
    holder = map_prototype;

    // Check if it is safe to inline property access for the {map}.
    if (!CanInlinePropertyAccess(map)) return false;
  }
  DCHECK_EQ(kStore, access_mode);

  // Check if the {receiver_map} has a data transition with the given {name}.
  if (receiver_map->unused_property_fields() == 0) return false;
  if (Map* transition = TransitionArray::SearchTransition(*receiver_map, kData,
                                                          *name, NONE)) {
    Handle<Map> transition_map(transition, isolate());
    int const number = transition_map->LastAdded();
    PropertyDetails const details =
        transition_map->instance_descriptors()->GetDetails(number);
    // Don't bother optimizing stores to read-only properties.
    if (details.IsReadOnly()) return false;
    // TODO(bmeurer): Handle transition to data constant?
    if (details.type() != DATA) return false;
    int const index = details.field_index();
    Representation field_representation = details.representation();
    FieldIndex field_index = FieldIndex::ForPropertyIndex(
        *transition_map, index, field_representation.IsDouble());
    Type* field_type = Type::Tagged();
    if (field_representation.IsSmi()) {
      field_type = type_cache_.kSmi;
    } else if (field_representation.IsDouble()) {
      // TODO(bmeurer): Add support for storing to double fields.
      return false;
    } else if (field_representation.IsHeapObject()) {
      // Extract the field type from the property details (make sure its
      // representation is TaggedPointer to reflect the heap object case).
      field_type = Type::Intersect(
          Type::Convert<HeapType>(
              handle(
                  transition_map->instance_descriptors()->GetFieldType(number),
                  isolate()),
              graph()->zone()),
          Type::TaggedPointer(), graph()->zone());
      if (field_type->Is(Type::None())) {
        // Store is not safe if the field type was cleared.
        return false;
      } else if (!Type::Any()->Is(field_type)) {
        // Add proper code dependencies in case of stable field map(s).
        Handle<Map> field_owner_map(transition_map->FindFieldOwner(number),
                                    isolate());
        dependencies()->AssumeFieldType(field_owner_map);
      }
      DCHECK(field_type->Is(Type::TaggedPointer()));
    }
    dependencies()->AssumeMapNotDeprecated(transition_map);
    *access_info = PropertyAccessInfo::TransitionToField(
        receiver_type, field_index, field_type, transition_map, holder);
    return true;
  }
  return false;
}


bool JSNativeContextSpecialization::ComputePropertyAccessInfos(
    MapHandleList const& maps, Handle<Name> name,
    PropertyAccessMode access_mode,
    ZoneVector<PropertyAccessInfo>* access_infos) {
  for (Handle<Map> map : maps) {
    if (Map::TryUpdate(map).ToHandle(&map)) {
      PropertyAccessInfo access_info;
      if (!ComputePropertyAccessInfo(map, name, access_mode, &access_info)) {
        return false;
      }
      access_infos->push_back(access_info);
    }
  }
  return true;
}


Reduction JSNativeContextSpecialization::ReduceNamedAccess(
    Node* node, Node* value, MapHandleList const& receiver_maps,
    Handle<Name> name, PropertyAccessMode access_mode) {
  DCHECK(node->opcode() == IrOpcode::kJSLoadNamed ||
         node->opcode() == IrOpcode::kJSStoreNamed);
  Node* receiver = NodeProperties::GetValueInput(node, 0);
  Node* frame_state = NodeProperties::GetFrameStateInput(node, 1);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  // Not much we can do if deoptimization support is disabled.
  if (!(flags() & kDeoptimizationEnabled)) return NoChange();

  // Compute property access infos for the receiver maps.
  ZoneVector<PropertyAccessInfo> access_infos(zone());
  if (!ComputePropertyAccessInfos(receiver_maps, name, access_mode,
                                  &access_infos)) {
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
    if (access_info.IsDataConstant()) {
      this_value = jsgraph()->Constant(access_info.constant());
      if (access_mode == kStore) {
        Node* check = graph()->NewNode(
            simplified()->ReferenceEqual(Type::Tagged()), value, this_value);
        Node* branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                        check, this_control);
        exit_controls.push_back(graph()->NewNode(common()->IfFalse(), branch));
        this_control = graph()->NewNode(common()->IfTrue(), branch);
      }
    } else {
      DCHECK(access_info.IsDataField() || access_info.IsTransitionToField());
      FieldIndex const field_index = access_info.field_index();
      Type* const field_type = access_info.field_type();
      if (access_mode == kLoad && access_info.holder().ToHandle(&holder)) {
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
      if (access_mode == kLoad) {
        this_value = this_effect =
            graph()->NewNode(simplified()->LoadField(field_access),
                             this_storage, this_effect, this_control);
      } else {
        DCHECK_EQ(kStore, access_mode);
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
        if (access_info.IsTransitionToField()) {
          this_effect = graph()->NewNode(common()->BeginRegion(), this_effect);
          this_effect = graph()->NewNode(
              simplified()->StoreField(AccessBuilder::ForMap()), this_receiver,
              jsgraph()->Constant(access_info.transition_map()), this_effect,
              this_control);
        }
        this_effect = graph()->NewNode(simplified()->StoreField(field_access),
                                       this_storage, this_value, this_effect,
                                       this_control);
        if (access_info.IsTransitionToField()) {
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
  if (control_count == 1) {
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
  return ReduceNamedAccess(node, value, receiver_maps, p.name(), kLoad);
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
  return ReduceNamedAccess(node, value, receiver_maps, p.name(), kStore);
}


Reduction JSNativeContextSpecialization::Replace(Node* node,
                                                 Handle<Object> value) {
  return Replace(node, jsgraph()->Constant(value));
}


bool JSNativeContextSpecialization::LookupInScriptContextTable(
    Handle<Name> name, ScriptContextTableLookupResult* result) {
  if (!name->IsString()) return false;
  Handle<ScriptContextTable> script_context_table(
      global_object()->native_context()->script_context_table());
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
