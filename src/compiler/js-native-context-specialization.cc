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
      dependencies_(dependencies),
      zone_(zone) {}


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

  // Load from constant/undefined global property can be constant-folded
  // with deoptimization support, by adding a code dependency on the cell.
  if ((property_details.cell_type() == PropertyCellType::kConstant ||
       property_details.cell_type() == PropertyCellType::kUndefined) &&
      (flags() & kDeoptimizationEnabled)) {
    dependencies()->AssumePropertyCell(property_cell);
    return Replace(node, property_cell_value);
  }

  // Load from constant type global property can benefit from representation
  // (and map) feedback with deoptimization support (requires code dependency).
  if (property_details.cell_type() == PropertyCellType::kConstantType &&
      (flags() & kDeoptimizationEnabled)) {
    dependencies()->AssumePropertyCell(property_cell);
    // Compute proper type based on the current value in the cell.
    Type* property_cell_value_type;
    if (property_cell_value->IsSmi()) {
      property_cell_value_type = Type::Intersect(
          Type::SignedSmall(), Type::TaggedSigned(), graph()->zone());
    } else if (property_cell_value->IsNumber()) {
      property_cell_value_type = Type::Intersect(
          Type::Number(), Type::TaggedPointer(), graph()->zone());
    } else {
      property_cell_value_type = Type::Of(property_cell_value, graph()->zone());
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
  enum Kind { kInvalid, kDataConstant, kDataField };

  static PropertyAccessInfo DataConstant(Type* receiver_type,
                                         Handle<Object> constant,
                                         MaybeHandle<JSObject> holder) {
    return PropertyAccessInfo(holder, constant, receiver_type);
  }
  static PropertyAccessInfo DataField(Type* receiver_type,
                                      FieldIndex field_index, Type* field_type,
                                      MaybeHandle<JSObject> holder) {
    return PropertyAccessInfo(holder, field_index, field_type, receiver_type);
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

  bool IsDataConstant() const { return kind() == kDataConstant; }
  bool IsDataField() const { return kind() == kDataField; }

  Kind kind() const { return kind_; }
  MaybeHandle<JSObject> holder() const { return holder_; }
  Handle<Object> constant() const { return constant_; }
  FieldIndex field_index() const { return field_index_; }
  Type* field_type() const { return field_type_; }
  Type* receiver_type() const { return receiver_type_; }

 private:
  Kind kind_;
  Type* receiver_type_;
  Handle<Object> constant_;
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
  MaybeHandle<JSObject> holder;
  Handle<Map> receiver_map = map;
  Type* receiver_type = Type::Class(receiver_map, graph()->zone());
  while (CanInlinePropertyAccess(map)) {
    // Check for special JSObject field accessors.
    int offset;
    if (Accessors::IsJSObjectFieldAccessor(map, name, &offset)) {
      // Don't bother optimizing stores to special JSObject field accessors.
      if (access_mode == kStore) {
        break;
      }
      FieldIndex field_index = FieldIndex::ForInObjectOffset(offset);
      Type* field_type = Type::Tagged();
      if (map->IsStringMap()) {
        DCHECK(Name::Equals(factory()->length_string(), name));
        // The String::length property is always a smi in the range
        // [0, String::kMaxLength].
        field_type = Type::Intersect(
            Type::Range(0.0, String::kMaxLength, graph()->zone()),
            Type::TaggedSigned(), graph()->zone());
      } else if (map->IsJSArrayMap()) {
        DCHECK(Name::Equals(factory()->length_string(), name));
        // The JSArray::length property is a smi in the range
        // [0, FixedDoubleArray::kMaxLength] in case of fast double
        // elements, a smi in the range [0, FixedArray::kMaxLength]
        // in case of other fast elements, and [0, kMaxUInt32-1] in
        // case of other arrays.
        double field_type_upper = kMaxUInt32 - 1;
        if (IsFastElementsKind(map->elements_kind())) {
          field_type_upper = IsFastDoubleElementsKind(map->elements_kind())
                                 ? FixedDoubleArray::kMaxLength
                                 : FixedArray::kMaxLength;
        }
        field_type =
            Type::Intersect(Type::Range(0.0, field_type_upper, graph()->zone()),
                            Type::TaggedSigned(), graph()->zone());
      }
      *access_info = PropertyAccessInfo::DataField(receiver_type, field_index,
                                                   field_type, holder);
      return true;
    }

    // Lookup the named property on the {map}.
    Handle<DescriptorArray> descriptors(map->instance_descriptors(), isolate());
    int const number = descriptors->SearchWithCache(*name, *map);
    if (number != DescriptorArray::kNotFound) {
      if (access_mode == kStore && !map.is_identical_to(receiver_map)) {
        return false;
      }
      PropertyDetails const details = descriptors->GetDetails(number);
      if (details.type() == DATA_CONSTANT) {
        *access_info = PropertyAccessInfo::DataConstant(
            receiver_type, handle(descriptors->GetValue(number), isolate()),
            holder);
        return true;
      } else if (details.type() == DATA) {
        // Don't bother optimizing stores to read-only properties.
        if (access_mode == kStore && details.IsReadOnly()) {
          break;
        }
        int index = descriptors->GetFieldIndex(number);
        Representation field_representation = details.representation();
        FieldIndex field_index = FieldIndex::ForPropertyIndex(
            *map, index, field_representation.IsDouble());
        Type* field_type = Type::Tagged();
        if (field_representation.IsSmi()) {
          field_type = Type::Intersect(Type::SignedSmall(),
                                       Type::TaggedSigned(), graph()->zone());
        } else if (field_representation.IsDouble()) {
          if (access_mode == kStore) {
            // TODO(bmeurer): Add support for storing to double fields.
            break;
          }
          field_type = Type::Intersect(Type::Number(), Type::UntaggedFloat64(),
                                       graph()->zone());
        } else if (field_representation.IsHeapObject()) {
          // Extract the field type from the property details (make sure its
          // representation is TaggedPointer to reflect the heap object case).
          field_type = Type::Intersect(
              Type::Convert<HeapType>(
                  handle(descriptors->GetFieldType(number), isolate()),
                  graph()->zone()),
              Type::TaggedPointer(), graph()->zone());
          if (field_type->Is(Type::None())) {
            if (access_mode == kStore) {
              // Store is not safe if the field type was cleared.
              break;
            }

            // The field type was cleared by the GC, so we don't know anything
            // about the contents now.
            // TODO(bmeurer): It would be awesome to make this saner in the
            // runtime/GC interaction.
            field_type = Type::TaggedPointer();
          } else {
            // Add proper code dependencies in case of stable field map(s).
            if (field_type->NumClasses() > 0 && field_type->NowStable()) {
              dependencies()->AssumeFieldType(
                  handle(map->FindFieldOwner(number), isolate()));
              for (auto i = field_type->Classes(); !i.Done(); i.Advance()) {
                dependencies()->AssumeMapStable(i.Current());
              }
            } else {
              field_type = Type::TaggedPointer();
            }
          }
          DCHECK(field_type->Is(Type::TaggedPointer()));
        }
        *access_info = PropertyAccessInfo::DataField(receiver_type, field_index,
                                                     field_type, holder);
        return true;
      } else {
        // TODO(bmeurer): Add support for accessors.
        break;
      }
    }

    // Don't search on the prototype chain for special indices in case of
    // integer indexed exotic objects (see ES6 section 9.4.5).
    if (map->IsJSTypedArrayMap() && name->IsString() &&
        IsSpecialIndex(isolate()->unicode_cache(), String::cast(*name))) {
      break;
    }

    // Walk up the prototype chain.
    if (!map->prototype()->IsJSObject()) {
      // TODO(bmeurer): Handle the not found case if the prototype is null.
      break;
    }
    Handle<JSObject> map_prototype(JSObject::cast(map->prototype()), isolate());
    if (map_prototype->map()->is_deprecated()) {
      // Try to migrate the prototype object so we don't embed the deprecated
      // map into the optimized code.
      JSObject::TryMigrateInstance(map_prototype);
    }
    map = handle(map_prototype->map(), isolate());
    holder = map_prototype;
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


Reduction JSNativeContextSpecialization::ReduceJSLoadNamed(Node* node) {
  DCHECK_EQ(IrOpcode::kJSLoadNamed, node->opcode());
  NamedAccess const& p = NamedAccessOf(node->op());
  Handle<Name> name = p.name();
  Node* receiver = NodeProperties::GetValueInput(node, 0);
  Node* frame_state = NodeProperties::GetFrameStateInput(node, 1);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  // Not much we can do if deoptimization support is disabled.
  if (!(flags() & kDeoptimizationEnabled)) return NoChange();

  // Extract receiver maps from the LOAD_IC using the LoadICNexus.
  MapHandleList receiver_maps;
  if (!p.feedback().IsValid()) return NoChange();
  LoadICNexus nexus(p.feedback().vector(), p.feedback().slot());
  if (nexus.ExtractMaps(&receiver_maps) == 0) return NoChange();
  DCHECK_LT(0, receiver_maps.length());

  // Compute property access infos for the receiver maps.
  ZoneVector<PropertyAccessInfo> access_infos(zone());
  if (!ComputePropertyAccessInfos(receiver_maps, name, kLoad, &access_infos)) {
    return NoChange();
  }

  // Nothing to do if we have no non-deprecated maps.
  if (access_infos.empty()) return NoChange();

  // The final states for every polymorphic branch. We join them with
  // Merge+Phi+EffectPhi at the bottom.
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
    Node* this_value = receiver;
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
      Node* branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                      check, fallthrough_control);
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
        Node* branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                        check, fallthrough_control);
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
      this_value = jsgraph()->Constant(holder);
      for (auto i = access_info.receiver_type()->Classes(); !i.Done();
           i.Advance()) {
        Handle<Map> map = i.Current();
        PrototypeIterator j(map);
        while (true) {
          // Check that the {prototype} still has the same map. For stable
          // maps, we can add a stability dependency on the prototype map;
          // for everything else we need to perform a map check at runtime.
          Handle<JSReceiver> prototype =
              PrototypeIterator::GetCurrent<JSReceiver>(j);
          if (prototype->map()->is_stable()) {
            dependencies()->AssumeMapStable(
                handle(prototype->map(), isolate()));
          } else {
            Node* prototype_map = this_effect = graph()->NewNode(
                simplified()->LoadField(AccessBuilder::ForMap()),
                jsgraph()->Constant(prototype), this_effect, this_control);
            Node* check = graph()->NewNode(
                simplified()->ReferenceEqual(Type::Internal()), prototype_map,
                jsgraph()->Constant(handle(prototype->map(), isolate())));
            Node* branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                            check, this_control);
            exit_controls.push_back(
                graph()->NewNode(common()->IfFalse(), branch));
            this_control = graph()->NewNode(common()->IfTrue(), branch);
          }
          // Stop once we get to the holder.
          if (prototype.is_identical_to(holder)) break;
          j.Advance();
        }
      }
    }

    // Generate the actual property access.
    if (access_info.IsDataConstant()) {
      this_value = jsgraph()->Constant(access_info.constant());
    } else {
      // TODO(bmeurer): This is sort of adhoc, and must be refactored into some
      // common code once we also have support for stores.
      DCHECK(access_info.IsDataField());
      FieldIndex const field_index = access_info.field_index();
      Type* const field_type = access_info.field_type();
      if (!field_index.is_inobject()) {
        this_value = this_effect = graph()->NewNode(
            simplified()->LoadField(AccessBuilder::ForJSObjectProperties()),
            this_value, this_effect, this_control);
      }
      FieldAccess field_access;
      field_access.base_is_tagged = kTaggedBase;
      field_access.offset = field_index.offset();
      field_access.name = name;
      field_access.type = field_type;
      field_access.machine_type = kMachAnyTagged;
      if (field_type->Is(Type::UntaggedFloat64())) {
        if (!field_index.is_inobject() || field_index.is_hidden_field() ||
            !FLAG_unbox_double_fields) {
          this_value = this_effect =
              graph()->NewNode(simplified()->LoadField(field_access),
                               this_value, this_effect, this_control);
          field_access.offset = HeapNumber::kValueOffset;
          field_access.name = MaybeHandle<Name>();
        }
        field_access.machine_type = kMachFloat64;
      }
      this_value = this_effect =
          graph()->NewNode(simplified()->LoadField(field_access), this_value,
                           this_effect, this_control);
    }

    // Remember the final state for this property access.
    values.push_back(this_value);
    effects.push_back(this_effect);
    controls.push_back(this_control);
  }

  // Collect the fallthru control as final "exit" control.
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
  Node* value;
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


Reduction JSNativeContextSpecialization::ReduceJSStoreNamed(Node* node) {
  DCHECK_EQ(IrOpcode::kJSStoreNamed, node->opcode());
  NamedAccess const& p = NamedAccessOf(node->op());
  Handle<Name> name = p.name();
  Node* receiver = NodeProperties::GetValueInput(node, 0);
  Node* value = NodeProperties::GetValueInput(node, 1);
  Node* frame_state = NodeProperties::GetFrameStateInput(node, 1);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  // Not much we can do if deoptimization support is disabled.
  if (!(flags() & kDeoptimizationEnabled)) return NoChange();

  // Extract receiver maps from the STORE_IC using the StoreICNexus.
  MapHandleList receiver_maps;
  if (!p.feedback().IsValid()) return NoChange();
  StoreICNexus nexus(p.feedback().vector(), p.feedback().slot());
  if (nexus.ExtractMaps(&receiver_maps) == 0) return NoChange();
  DCHECK_LT(0, receiver_maps.length());

  // Compute property access infos for the receiver maps.
  ZoneVector<PropertyAccessInfo> access_infos(zone());
  if (!ComputePropertyAccessInfos(receiver_maps, name, kStore, &access_infos)) {
    return NoChange();
  }

  // Nothing to do if we have no non-deprecated maps.
  if (access_infos.empty()) return NoChange();

  // The final states for every polymorphic branch. We join them with
  // Merge+EffectPhi at the bottom.
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
      Node* branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                      check, fallthrough_control);
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
        Node* branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                        check, fallthrough_control);
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
      for (auto i = access_info.receiver_type()->Classes(); !i.Done();
           i.Advance()) {
        Handle<Map> map = i.Current();
        PrototypeIterator j(map);
        while (true) {
          // Check that the {prototype} still has the same map. For stable
          // maps, we can add a stability dependency on the prototype map;
          // for everything else we need to perform a map check at runtime.
          Handle<JSReceiver> prototype =
              PrototypeIterator::GetCurrent<JSReceiver>(j);
          if (prototype->map()->is_stable()) {
            dependencies()->AssumeMapStable(
                handle(prototype->map(), isolate()));
          } else {
            Node* prototype_map = this_effect = graph()->NewNode(
                simplified()->LoadField(AccessBuilder::ForMap()),
                jsgraph()->Constant(prototype), this_effect, this_control);
            Node* check = graph()->NewNode(
                simplified()->ReferenceEqual(Type::Internal()), prototype_map,
                jsgraph()->Constant(handle(prototype->map(), isolate())));
            Node* branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                            check, this_control);
            exit_controls.push_back(
                graph()->NewNode(common()->IfFalse(), branch));
            this_control = graph()->NewNode(common()->IfTrue(), branch);
          }
          // Stop once we get to the holder.
          if (prototype.is_identical_to(holder)) break;
          j.Advance();
        }
      }
    }

    // Generate the actual property access.
    if (access_info.IsDataConstant()) {
      Node* check =
          graph()->NewNode(simplified()->ReferenceEqual(Type::Tagged()), value,
                           jsgraph()->Constant(access_info.constant()));
      Node* branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                      check, this_control);
      exit_controls.push_back(graph()->NewNode(common()->IfFalse(), branch));
      this_control = graph()->NewNode(common()->IfTrue(), branch);
    } else {
      // TODO(bmeurer): This is sort of adhoc, and must be refactored into some
      // common code once we also have support for stores.
      DCHECK(access_info.IsDataField());
      FieldIndex const field_index = access_info.field_index();
      Type* const field_type = access_info.field_type();
      if (!field_index.is_inobject()) {
        this_receiver = this_effect = graph()->NewNode(
            simplified()->LoadField(AccessBuilder::ForJSObjectProperties()),
            this_receiver, this_effect, this_control);
      }
      FieldAccess field_access;
      field_access.base_is_tagged = kTaggedBase;
      field_access.offset = field_index.offset();
      field_access.name = name;
      field_access.type = field_type;
      field_access.machine_type = kMachAnyTagged;
      if (field_type->Is(Type::TaggedSigned())) {
        Node* check = graph()->NewNode(simplified()->ObjectIsSmi(), value);
        Node* branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                        check, this_control);
        exit_controls.push_back(graph()->NewNode(common()->IfFalse(), branch));
        this_control = graph()->NewNode(common()->IfTrue(), branch);
      } else if (field_type->Is(Type::TaggedPointer())) {
        Node* check = graph()->NewNode(simplified()->ObjectIsSmi(), value);
        Node* branch = graph()->NewNode(common()->Branch(BranchHint::kFalse),
                                        check, this_control);
        exit_controls.push_back(graph()->NewNode(common()->IfTrue(), branch));
        this_control = graph()->NewNode(common()->IfFalse(), branch);
        if (field_type->NumClasses() > 0) {
          // Emit a (sequence of) map checks for the value.
          ZoneVector<Node*> this_controls(zone());
          Node* value_map = this_effect =
              graph()->NewNode(simplified()->LoadField(AccessBuilder::ForMap()),
                               value, this_effect, this_control);
          for (auto i = field_type->Classes(); !i.Done(); i.Advance()) {
            Handle<Map> field_map(i.Current());
            check =
                graph()->NewNode(simplified()->ReferenceEqual(Type::Internal()),
                                 value_map, jsgraph()->Constant(field_map));
            branch = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                      check, this_control);
            this_control = graph()->NewNode(common()->IfFalse(), branch);
            this_controls.push_back(
                graph()->NewNode(common()->IfTrue(), branch));
          }
          exit_controls.push_back(this_control);
          int const this_control_count = static_cast<int>(this_controls.size());
          this_control = (this_control_count == 1)
                             ? this_controls.front()
                             : graph()->NewNode(
                                   common()->Merge(this_control_count),
                                   this_control_count, &this_controls.front());
        }
      } else {
        DCHECK(field_type->Is(Type::Tagged()));
      }
      this_effect =
          graph()->NewNode(simplified()->StoreField(field_access),
                           this_receiver, value, this_effect, this_control);
    }

    // Remember the final state for this property access.
    effects.push_back(this_effect);
    controls.push_back(this_control);
  }

  // Collect the fallthru control as final "exit" control.
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
    effect = effects.front();
    control = controls.front();
  } else {
    control = graph()->NewNode(common()->Merge(control_count), control_count,
                               &controls.front());
    effects.push_back(control);
    effect = graph()->NewNode(common()->EffectPhi(control_count),
                              control_count + 1, &effects.front());
  }
  return Replace(node, value, effect, control);
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
