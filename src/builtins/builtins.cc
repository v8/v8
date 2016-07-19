// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/builtins/builtins-utils.h"

#include "src/api-arguments.h"
#include "src/api-natives.h"
#include "src/base/ieee754.h"
#include "src/base/once.h"
#include "src/bootstrapper.h"
#include "src/code-factory.h"
#include "src/dateparser-inl.h"
#include "src/frames-inl.h"
#include "src/gdb-jit.h"
#include "src/globals.h"
#include "src/ic/handler-compiler.h"
#include "src/ic/ic.h"
#include "src/isolate-inl.h"
#include "src/json-parser.h"
#include "src/json-stringifier.h"
#include "src/messages.h"
#include "src/property-descriptor.h"
#include "src/prototype.h"
#include "src/string-builder.h"
#include "src/uri.h"
#include "src/vm-state-inl.h"

namespace v8 {
namespace internal {

// Forward declarations for C++ builtins.
#define FORWARD_DECLARE(Name) \
  Object* Builtin_##Name(int argc, Object** args, Isolate* isolate);
BUILTIN_LIST_C(FORWARD_DECLARE)
#undef FORWARD_DECLARE

BUILTIN(Illegal) {
  UNREACHABLE();
  return isolate->heap()->undefined_value();  // Make compiler happy.
}

BUILTIN(EmptyFunction) { return isolate->heap()->undefined_value(); }


void Builtins::Generate_ObjectHasOwnProperty(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;

  Node* object = assembler->Parameter(0);
  Node* key = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);

  Label call_runtime(assembler), return_true(assembler),
      return_false(assembler);

  // Smi receivers do not have own properties.
  Label if_objectisnotsmi(assembler);
  assembler->Branch(assembler->WordIsSmi(object), &return_false,
                    &if_objectisnotsmi);
  assembler->Bind(&if_objectisnotsmi);

  Node* map = assembler->LoadMap(object);
  Node* instance_type = assembler->LoadMapInstanceType(map);

  Variable var_index(assembler, MachineRepresentation::kWord32);

  Label keyisindex(assembler), if_iskeyunique(assembler);
  assembler->TryToName(key, &keyisindex, &var_index, &if_iskeyunique,
                       &call_runtime);

  assembler->Bind(&if_iskeyunique);
  assembler->TryHasOwnProperty(object, map, instance_type, key, &return_true,
                               &return_false, &call_runtime);

  assembler->Bind(&keyisindex);
  assembler->TryLookupElement(object, map, instance_type, var_index.value(),
                              &return_true, &return_false, &call_runtime);

  assembler->Bind(&return_true);
  assembler->Return(assembler->BooleanConstant(true));

  assembler->Bind(&return_false);
  assembler->Return(assembler->BooleanConstant(false));

  assembler->Bind(&call_runtime);
  assembler->Return(assembler->CallRuntime(Runtime::kObjectHasOwnProperty,
                                           context, object, key));
}

namespace {

MUST_USE_RESULT Maybe<bool> FastAssign(Handle<JSReceiver> to,
                                       Handle<Object> next_source) {
  // Non-empty strings are the only non-JSReceivers that need to be handled
  // explicitly by Object.assign.
  if (!next_source->IsJSReceiver()) {
    return Just(!next_source->IsString() ||
                String::cast(*next_source)->length() == 0);
  }

  // If the target is deprecated, the object will be updated on first store. If
  // the source for that store equals the target, this will invalidate the
  // cached representation of the source. Preventively upgrade the target.
  // Do this on each iteration since any property load could cause deprecation.
  if (to->map()->is_deprecated()) {
    JSObject::MigrateInstance(Handle<JSObject>::cast(to));
  }

  Isolate* isolate = to->GetIsolate();
  Handle<Map> map(JSReceiver::cast(*next_source)->map(), isolate);

  if (!map->IsJSObjectMap()) return Just(false);
  if (!map->OnlyHasSimpleProperties()) return Just(false);

  Handle<JSObject> from = Handle<JSObject>::cast(next_source);
  if (from->elements() != isolate->heap()->empty_fixed_array()) {
    return Just(false);
  }

  Handle<DescriptorArray> descriptors(map->instance_descriptors(), isolate);
  int length = map->NumberOfOwnDescriptors();

  bool stable = true;

  for (int i = 0; i < length; i++) {
    Handle<Name> next_key(descriptors->GetKey(i), isolate);
    Handle<Object> prop_value;
    // Directly decode from the descriptor array if |from| did not change shape.
    if (stable) {
      PropertyDetails details = descriptors->GetDetails(i);
      if (!details.IsEnumerable()) continue;
      if (details.kind() == kData) {
        if (details.location() == kDescriptor) {
          prop_value = handle(descriptors->GetValue(i), isolate);
        } else {
          Representation representation = details.representation();
          FieldIndex index = FieldIndex::ForDescriptor(*map, i);
          prop_value = JSObject::FastPropertyAt(from, representation, index);
        }
      } else {
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, prop_value, JSReceiver::GetProperty(from, next_key),
            Nothing<bool>());
        stable = from->map() == *map;
      }
    } else {
      // If the map did change, do a slower lookup. We are still guaranteed that
      // the object has a simple shape, and that the key is a name.
      LookupIterator it(from, next_key, from,
                        LookupIterator::OWN_SKIP_INTERCEPTOR);
      if (!it.IsFound()) continue;
      DCHECK(it.state() == LookupIterator::DATA ||
             it.state() == LookupIterator::ACCESSOR);
      if (!it.IsEnumerable()) continue;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, prop_value, Object::GetProperty(&it), Nothing<bool>());
    }
    LookupIterator it(to, next_key, to);
    bool call_to_js = it.IsFound() && it.state() != LookupIterator::DATA;
    Maybe<bool> result = Object::SetProperty(
        &it, prop_value, STRICT, Object::CERTAINLY_NOT_STORE_FROM_KEYED);
    if (result.IsNothing()) return result;
    if (stable && call_to_js) stable = from->map() == *map;
  }

  return Just(true);
}

}  // namespace

// ES6 19.1.2.1 Object.assign
BUILTIN(ObjectAssign) {
  HandleScope scope(isolate);
  Handle<Object> target = args.atOrUndefined(isolate, 1);

  // 1. Let to be ? ToObject(target).
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, target,
                                     Object::ToObject(isolate, target));
  Handle<JSReceiver> to = Handle<JSReceiver>::cast(target);
  // 2. If only one argument was passed, return to.
  if (args.length() == 2) return *to;
  // 3. Let sources be the List of argument values starting with the
  //    second argument.
  // 4. For each element nextSource of sources, in ascending index order,
  for (int i = 2; i < args.length(); ++i) {
    Handle<Object> next_source = args.at<Object>(i);
    Maybe<bool> fast_assign = FastAssign(to, next_source);
    if (fast_assign.IsNothing()) return isolate->heap()->exception();
    if (fast_assign.FromJust()) continue;
    // 4a. If nextSource is undefined or null, let keys be an empty List.
    // 4b. Else,
    // 4b i. Let from be ToObject(nextSource).
    // Only non-empty strings and JSReceivers have enumerable properties.
    Handle<JSReceiver> from =
        Object::ToObject(isolate, next_source).ToHandleChecked();
    // 4b ii. Let keys be ? from.[[OwnPropertyKeys]]().
    Handle<FixedArray> keys;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, keys, KeyAccumulator::GetKeys(
                           from, KeyCollectionMode::kOwnOnly, ALL_PROPERTIES,
                           GetKeysConversion::kKeepNumbers));
    // 4c. Repeat for each element nextKey of keys in List order,
    for (int j = 0; j < keys->length(); ++j) {
      Handle<Object> next_key(keys->get(j), isolate);
      // 4c i. Let desc be ? from.[[GetOwnProperty]](nextKey).
      PropertyDescriptor desc;
      Maybe<bool> found =
          JSReceiver::GetOwnPropertyDescriptor(isolate, from, next_key, &desc);
      if (found.IsNothing()) return isolate->heap()->exception();
      // 4c ii. If desc is not undefined and desc.[[Enumerable]] is true, then
      if (found.FromJust() && desc.enumerable()) {
        // 4c ii 1. Let propValue be ? Get(from, nextKey).
        Handle<Object> prop_value;
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
            isolate, prop_value,
            Runtime::GetObjectProperty(isolate, from, next_key));
        // 4c ii 2. Let status be ? Set(to, nextKey, propValue, true).
        Handle<Object> status;
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
            isolate, status, Runtime::SetObjectProperty(isolate, to, next_key,
                                                        prop_value, STRICT));
      }
    }
  }
  // 5. Return to.
  return *to;
}

namespace {  // anonymous namespace for ObjectProtoToString()

void IsString(CodeStubAssembler* assembler, compiler::Node* object,
              CodeStubAssembler::Label* if_string,
              CodeStubAssembler::Label* if_notstring) {
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;

  Label if_notsmi(assembler);
  assembler->Branch(assembler->WordIsSmi(object), if_notstring, &if_notsmi);

  assembler->Bind(&if_notsmi);
  {
    Node* instance_type = assembler->LoadInstanceType(object);

    assembler->Branch(
        assembler->Int32LessThan(
            instance_type, assembler->Int32Constant(FIRST_NONSTRING_TYPE)),
        if_string, if_notstring);
  }
}

void ReturnToStringFormat(CodeStubAssembler* assembler, compiler::Node* context,
                          compiler::Node* string) {
  typedef compiler::Node Node;

  Node* lhs = assembler->HeapConstant(
      assembler->factory()->NewStringFromStaticChars("[object "));
  Node* rhs = assembler->HeapConstant(
      assembler->factory()->NewStringFromStaticChars("]"));

  Callable callable = CodeFactory::StringAdd(
      assembler->isolate(), STRING_ADD_CHECK_NONE, NOT_TENURED);

  assembler->Return(assembler->CallStub(
      callable, context, assembler->CallStub(callable, context, lhs, string),
      rhs));
}

void ReturnIfPrimitive(CodeStubAssembler* assembler,
                       compiler::Node* instance_type,
                       CodeStubAssembler::Label* return_string,
                       CodeStubAssembler::Label* return_boolean,
                       CodeStubAssembler::Label* return_number) {
  assembler->GotoIf(
      assembler->Int32LessThan(instance_type,
                               assembler->Int32Constant(FIRST_NONSTRING_TYPE)),
      return_string);

  assembler->GotoIf(assembler->Word32Equal(
                        instance_type, assembler->Int32Constant(ODDBALL_TYPE)),
                    return_boolean);

  assembler->GotoIf(
      assembler->Word32Equal(instance_type,
                             assembler->Int32Constant(HEAP_NUMBER_TYPE)),
      return_number);
}

}  // namespace

// ES6 section 19.1.3.6 Object.prototype.toString
void Builtins::Generate_ObjectProtoToString(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;

  Label return_undefined(assembler, Label::kDeferred),
      return_null(assembler, Label::kDeferred),
      return_arguments(assembler, Label::kDeferred), return_array(assembler),
      return_api(assembler, Label::kDeferred), return_object(assembler),
      return_regexp(assembler), return_function(assembler),
      return_error(assembler), return_date(assembler), return_string(assembler),
      return_boolean(assembler), return_jsvalue(assembler),
      return_jsproxy(assembler, Label::kDeferred), return_number(assembler);

  Label if_isproxy(assembler, Label::kDeferred);

  Label checkstringtag(assembler);
  Label if_tostringtag(assembler), if_notostringtag(assembler);

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  assembler->GotoIf(
      assembler->Word32Equal(receiver, assembler->UndefinedConstant()),
      &return_undefined);

  assembler->GotoIf(assembler->Word32Equal(receiver, assembler->NullConstant()),
                    &return_null);

  assembler->GotoIf(assembler->WordIsSmi(receiver), &return_number);

  Node* receiver_instance_type = assembler->LoadInstanceType(receiver);
  ReturnIfPrimitive(assembler, receiver_instance_type, &return_string,
                    &return_boolean, &return_number);

  // for proxies, check IsArray before getting @@toStringTag
  Variable var_proxy_is_array(assembler, MachineRepresentation::kTagged);
  var_proxy_is_array.Bind(assembler->BooleanConstant(false));

  assembler->Branch(
      assembler->Word32Equal(receiver_instance_type,
                             assembler->Int32Constant(JS_PROXY_TYPE)),
      &if_isproxy, &checkstringtag);

  assembler->Bind(&if_isproxy);
  {
    // This can throw
    var_proxy_is_array.Bind(
        assembler->CallRuntime(Runtime::kArrayIsArray, context, receiver));
    assembler->Goto(&checkstringtag);
  }

  assembler->Bind(&checkstringtag);
  {
    Node* to_string_tag_symbol = assembler->HeapConstant(
        assembler->isolate()->factory()->to_string_tag_symbol());

    GetPropertyStub stub(assembler->isolate());
    Callable get_property =
        Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
    Node* to_string_tag_value = assembler->CallStub(
        get_property, context, receiver, to_string_tag_symbol);

    IsString(assembler, to_string_tag_value, &if_tostringtag,
             &if_notostringtag);

    assembler->Bind(&if_tostringtag);
    ReturnToStringFormat(assembler, context, to_string_tag_value);
  }
  assembler->Bind(&if_notostringtag);
  {
    size_t const kNumCases = 11;
    Label* case_labels[kNumCases];
    int32_t case_values[kNumCases];
    case_labels[0] = &return_api;
    case_values[0] = JS_API_OBJECT_TYPE;
    case_labels[1] = &return_api;
    case_values[1] = JS_SPECIAL_API_OBJECT_TYPE;
    case_labels[2] = &return_arguments;
    case_values[2] = JS_ARGUMENTS_TYPE;
    case_labels[3] = &return_array;
    case_values[3] = JS_ARRAY_TYPE;
    case_labels[4] = &return_function;
    case_values[4] = JS_BOUND_FUNCTION_TYPE;
    case_labels[5] = &return_function;
    case_values[5] = JS_FUNCTION_TYPE;
    case_labels[6] = &return_error;
    case_values[6] = JS_ERROR_TYPE;
    case_labels[7] = &return_date;
    case_values[7] = JS_DATE_TYPE;
    case_labels[8] = &return_regexp;
    case_values[8] = JS_REGEXP_TYPE;
    case_labels[9] = &return_jsvalue;
    case_values[9] = JS_VALUE_TYPE;
    case_labels[10] = &return_jsproxy;
    case_values[10] = JS_PROXY_TYPE;

    assembler->Switch(receiver_instance_type, &return_object, case_values,
                      case_labels, arraysize(case_values));

    assembler->Bind(&return_undefined);
    assembler->Return(assembler->HeapConstant(
        assembler->isolate()->factory()->undefined_to_string()));

    assembler->Bind(&return_null);
    assembler->Return(assembler->HeapConstant(
        assembler->isolate()->factory()->null_to_string()));

    assembler->Bind(&return_number);
    assembler->Return(assembler->HeapConstant(
        assembler->isolate()->factory()->number_to_string()));

    assembler->Bind(&return_string);
    assembler->Return(assembler->HeapConstant(
        assembler->isolate()->factory()->string_to_string()));

    assembler->Bind(&return_boolean);
    assembler->Return(assembler->HeapConstant(
        assembler->isolate()->factory()->boolean_to_string()));

    assembler->Bind(&return_arguments);
    assembler->Return(assembler->HeapConstant(
        assembler->isolate()->factory()->arguments_to_string()));

    assembler->Bind(&return_array);
    assembler->Return(assembler->HeapConstant(
        assembler->isolate()->factory()->array_to_string()));

    assembler->Bind(&return_function);
    assembler->Return(assembler->HeapConstant(
        assembler->isolate()->factory()->function_to_string()));

    assembler->Bind(&return_error);
    assembler->Return(assembler->HeapConstant(
        assembler->isolate()->factory()->error_to_string()));

    assembler->Bind(&return_date);
    assembler->Return(assembler->HeapConstant(
        assembler->isolate()->factory()->date_to_string()));

    assembler->Bind(&return_regexp);
    assembler->Return(assembler->HeapConstant(
        assembler->isolate()->factory()->regexp_to_string()));

    assembler->Bind(&return_api);
    {
      Node* class_name =
          assembler->CallRuntime(Runtime::kClassOf, context, receiver);
      ReturnToStringFormat(assembler, context, class_name);
    }

    assembler->Bind(&return_jsvalue);
    {
      Node* value = assembler->LoadJSValueValue(receiver);
      assembler->GotoIf(assembler->WordIsSmi(value), &return_number);

      ReturnIfPrimitive(assembler, assembler->LoadInstanceType(value),
                        &return_string, &return_boolean, &return_number);
      assembler->Goto(&return_object);
    }

    assembler->Bind(&return_jsproxy);
    {
      assembler->GotoIf(assembler->WordEqual(var_proxy_is_array.value(),
                                             assembler->BooleanConstant(true)),
                        &return_array);

      Node* map = assembler->LoadMap(receiver);

      // Return object if the proxy {receiver} is not callable.
      assembler->Branch(
          assembler->Word32Equal(
              assembler->Word32And(
                  assembler->LoadMapBitField(map),
                  assembler->Int32Constant(1 << Map::kIsCallable)),
              assembler->Int32Constant(0)),
          &return_object, &return_function);
    }

    // Default
    assembler->Bind(&return_object);
    assembler->Return(assembler->HeapConstant(
        assembler->isolate()->factory()->object_to_string()));
  }
}

// ES6 section 19.1.2.2 Object.create ( O [ , Properties ] )
// TODO(verwaest): Support the common cases with precached map directly in
// an Object.create stub.
BUILTIN(ObjectCreate) {
  HandleScope scope(isolate);
  Handle<Object> prototype = args.atOrUndefined(isolate, 1);
  if (!prototype->IsNull(isolate) && !prototype->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kProtoObjectOrNull, prototype));
  }

  // Generate the map with the specified {prototype} based on the Object
  // function's initial map from the current native context.
  // TODO(bmeurer): Use a dedicated cache for Object.create; think about
  // slack tracking for Object.create.
  Handle<Map> map(isolate->native_context()->object_function()->initial_map(),
                  isolate);
  if (map->prototype() != *prototype) {
    if (prototype->IsNull(isolate)) {
      map = isolate->object_with_null_prototype_map();
    } else if (prototype->IsJSObject()) {
      Handle<JSObject> js_prototype = Handle<JSObject>::cast(prototype);
      if (!js_prototype->map()->is_prototype_map()) {
        JSObject::OptimizeAsPrototype(js_prototype, FAST_PROTOTYPE);
      }
      Handle<PrototypeInfo> info =
          Map::GetOrCreatePrototypeInfo(js_prototype, isolate);
      // TODO(verwaest): Use inobject slack tracking for this map.
      if (info->HasObjectCreateMap()) {
        map = handle(info->ObjectCreateMap(), isolate);
      } else {
        map = Map::CopyInitialMap(map);
        Map::SetPrototype(map, prototype, FAST_PROTOTYPE);
        PrototypeInfo::SetObjectCreateMap(info, map);
      }
    } else {
      map = Map::TransitionToPrototype(map, prototype, REGULAR_PROTOTYPE);
    }
  }

  // Actually allocate the object.
  Handle<JSObject> object = isolate->factory()->NewJSObjectFromMap(map);

  // Define the properties if properties was specified and is not undefined.
  Handle<Object> properties = args.atOrUndefined(isolate, 2);
  if (!properties->IsUndefined(isolate)) {
    RETURN_FAILURE_ON_EXCEPTION(
        isolate, JSReceiver::DefineProperties(isolate, object, properties));
  }

  return *object;
}

// ES6 section 19.1.2.3 Object.defineProperties
BUILTIN(ObjectDefineProperties) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<Object> target = args.at<Object>(1);
  Handle<Object> properties = args.at<Object>(2);

  RETURN_RESULT_OR_FAILURE(
      isolate, JSReceiver::DefineProperties(isolate, target, properties));
}

// ES6 section 19.1.2.4 Object.defineProperty
BUILTIN(ObjectDefineProperty) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  Handle<Object> target = args.at<Object>(1);
  Handle<Object> key = args.at<Object>(2);
  Handle<Object> attributes = args.at<Object>(3);

  return JSReceiver::DefineProperty(isolate, target, key, attributes);
}

namespace {

template <AccessorComponent which_accessor>
Object* ObjectDefineAccessor(Isolate* isolate, Handle<Object> object,
                             Handle<Object> name, Handle<Object> accessor) {
  // 1. Let O be ? ToObject(this value).
  Handle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver,
                                     Object::ConvertReceiver(isolate, object));
  // 2. If IsCallable(getter) is false, throw a TypeError exception.
  if (!accessor->IsCallable()) {
    MessageTemplate::Template message =
        which_accessor == ACCESSOR_GETTER
            ? MessageTemplate::kObjectGetterExpectingFunction
            : MessageTemplate::kObjectSetterExpectingFunction;
    THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewTypeError(message));
  }
  // 3. Let desc be PropertyDescriptor{[[Get]]: getter, [[Enumerable]]: true,
  //                                   [[Configurable]]: true}.
  PropertyDescriptor desc;
  if (which_accessor == ACCESSOR_GETTER) {
    desc.set_get(accessor);
  } else {
    DCHECK(which_accessor == ACCESSOR_SETTER);
    desc.set_set(accessor);
  }
  desc.set_enumerable(true);
  desc.set_configurable(true);
  // 4. Let key be ? ToPropertyKey(P).
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, name,
                                     Object::ToPropertyKey(isolate, name));
  // 5. Perform ? DefinePropertyOrThrow(O, key, desc).
  // To preserve legacy behavior, we ignore errors silently rather than
  // throwing an exception.
  Maybe<bool> success = JSReceiver::DefineOwnProperty(
      isolate, receiver, name, &desc, Object::DONT_THROW);
  MAYBE_RETURN(success, isolate->heap()->exception());
  if (!success.FromJust()) {
    isolate->CountUsage(v8::Isolate::kDefineGetterOrSetterWouldThrow);
  }
  // 6. Return undefined.
  return isolate->heap()->undefined_value();
}

Object* ObjectLookupAccessor(Isolate* isolate, Handle<Object> object,
                             Handle<Object> key, AccessorComponent component) {
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, object,
                                     Object::ConvertReceiver(isolate, object));
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, key,
                                     Object::ToPropertyKey(isolate, key));
  bool success = false;
  LookupIterator it = LookupIterator::PropertyOrElement(
      isolate, object, key, &success,
      LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  DCHECK(success);

  for (; it.IsFound(); it.Next()) {
    switch (it.state()) {
      case LookupIterator::INTERCEPTOR:
      case LookupIterator::NOT_FOUND:
      case LookupIterator::TRANSITION:
        UNREACHABLE();

      case LookupIterator::ACCESS_CHECK:
        if (it.HasAccess()) continue;
        isolate->ReportFailedAccessCheck(it.GetHolder<JSObject>());
        RETURN_FAILURE_IF_SCHEDULED_EXCEPTION(isolate);
        return isolate->heap()->undefined_value();

      case LookupIterator::JSPROXY:
        return isolate->heap()->undefined_value();

      case LookupIterator::INTEGER_INDEXED_EXOTIC:
        return isolate->heap()->undefined_value();
      case LookupIterator::DATA:
        continue;
      case LookupIterator::ACCESSOR: {
        Handle<Object> maybe_pair = it.GetAccessors();
        if (maybe_pair->IsAccessorPair()) {
          return *AccessorPair::GetComponent(
              Handle<AccessorPair>::cast(maybe_pair), component);
        }
      }
    }
  }

  return isolate->heap()->undefined_value();
}

}  // namespace

// ES6 B.2.2.2 a.k.a.
// https://tc39.github.io/ecma262/#sec-object.prototype.__defineGetter__
BUILTIN(ObjectDefineGetter) {
  HandleScope scope(isolate);
  Handle<Object> object = args.at<Object>(0);  // Receiver.
  Handle<Object> name = args.at<Object>(1);
  Handle<Object> getter = args.at<Object>(2);
  return ObjectDefineAccessor<ACCESSOR_GETTER>(isolate, object, name, getter);
}

// ES6 B.2.2.3 a.k.a.
// https://tc39.github.io/ecma262/#sec-object.prototype.__defineSetter__
BUILTIN(ObjectDefineSetter) {
  HandleScope scope(isolate);
  Handle<Object> object = args.at<Object>(0);  // Receiver.
  Handle<Object> name = args.at<Object>(1);
  Handle<Object> setter = args.at<Object>(2);
  return ObjectDefineAccessor<ACCESSOR_SETTER>(isolate, object, name, setter);
}

// ES6 B.2.2.4 a.k.a.
// https://tc39.github.io/ecma262/#sec-object.prototype.__lookupGetter__
BUILTIN(ObjectLookupGetter) {
  HandleScope scope(isolate);
  Handle<Object> object = args.at<Object>(0);
  Handle<Object> name = args.at<Object>(1);
  return ObjectLookupAccessor(isolate, object, name, ACCESSOR_GETTER);
}

// ES6 B.2.2.5 a.k.a.
// https://tc39.github.io/ecma262/#sec-object.prototype.__lookupSetter__
BUILTIN(ObjectLookupSetter) {
  HandleScope scope(isolate);
  Handle<Object> object = args.at<Object>(0);
  Handle<Object> name = args.at<Object>(1);
  return ObjectLookupAccessor(isolate, object, name, ACCESSOR_SETTER);
}

// ES6 section 19.1.2.5 Object.freeze ( O )
BUILTIN(ObjectFreeze) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  if (object->IsJSReceiver()) {
    MAYBE_RETURN(JSReceiver::SetIntegrityLevel(Handle<JSReceiver>::cast(object),
                                               FROZEN, Object::THROW_ON_ERROR),
                 isolate->heap()->exception());
  }
  return *object;
}

// ES section 19.1.2.9 Object.getPrototypeOf ( O )
BUILTIN(ObjectGetPrototypeOf) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);

  Handle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver,
                                     Object::ToObject(isolate, object));

  RETURN_RESULT_OR_FAILURE(isolate,
                           JSReceiver::GetPrototype(isolate, receiver));
}

// ES6 section 19.1.2.6 Object.getOwnPropertyDescriptor ( O, P )
BUILTIN(ObjectGetOwnPropertyDescriptor) {
  HandleScope scope(isolate);
  // 1. Let obj be ? ToObject(O).
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  Handle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver,
                                     Object::ToObject(isolate, object));
  // 2. Let key be ? ToPropertyKey(P).
  Handle<Object> property = args.atOrUndefined(isolate, 2);
  Handle<Name> key;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, key,
                                     Object::ToName(isolate, property));
  // 3. Let desc be ? obj.[[GetOwnProperty]](key).
  PropertyDescriptor desc;
  Maybe<bool> found =
      JSReceiver::GetOwnPropertyDescriptor(isolate, receiver, key, &desc);
  MAYBE_RETURN(found, isolate->heap()->exception());
  // 4. Return FromPropertyDescriptor(desc).
  if (!found.FromJust()) return isolate->heap()->undefined_value();
  return *desc.ToObject(isolate);
}

namespace {

Object* GetOwnPropertyKeys(Isolate* isolate, BuiltinArguments args,
                           PropertyFilter filter) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  Handle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver,
                                     Object::ToObject(isolate, object));
  Handle<FixedArray> keys;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, keys,
      KeyAccumulator::GetKeys(receiver, KeyCollectionMode::kOwnOnly, filter,
                              GetKeysConversion::kConvertToString));
  return *isolate->factory()->NewJSArrayWithElements(keys);
}

}  // namespace

// ES6 section 19.1.2.7 Object.getOwnPropertyNames ( O )
BUILTIN(ObjectGetOwnPropertyNames) {
  return GetOwnPropertyKeys(isolate, args, SKIP_SYMBOLS);
}

// ES6 section 19.1.2.8 Object.getOwnPropertySymbols ( O )
BUILTIN(ObjectGetOwnPropertySymbols) {
  return GetOwnPropertyKeys(isolate, args, SKIP_STRINGS);
}

// ES#sec-object.is Object.is ( value1, value2 )
BUILTIN(ObjectIs) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(3, args.length());
  Handle<Object> value1 = args.at<Object>(1);
  Handle<Object> value2 = args.at<Object>(2);
  return isolate->heap()->ToBoolean(value1->SameValue(*value2));
}

// ES6 section 19.1.2.11 Object.isExtensible ( O )
BUILTIN(ObjectIsExtensible) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  Maybe<bool> result =
      object->IsJSReceiver()
          ? JSReceiver::IsExtensible(Handle<JSReceiver>::cast(object))
          : Just(false);
  MAYBE_RETURN(result, isolate->heap()->exception());
  return isolate->heap()->ToBoolean(result.FromJust());
}

// ES6 section 19.1.2.12 Object.isFrozen ( O )
BUILTIN(ObjectIsFrozen) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  Maybe<bool> result = object->IsJSReceiver()
                           ? JSReceiver::TestIntegrityLevel(
                                 Handle<JSReceiver>::cast(object), FROZEN)
                           : Just(true);
  MAYBE_RETURN(result, isolate->heap()->exception());
  return isolate->heap()->ToBoolean(result.FromJust());
}

// ES6 section 19.1.2.13 Object.isSealed ( O )
BUILTIN(ObjectIsSealed) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  Maybe<bool> result = object->IsJSReceiver()
                           ? JSReceiver::TestIntegrityLevel(
                                 Handle<JSReceiver>::cast(object), SEALED)
                           : Just(true);
  MAYBE_RETURN(result, isolate->heap()->exception());
  return isolate->heap()->ToBoolean(result.FromJust());
}

// ES6 section 19.1.2.14 Object.keys ( O )
BUILTIN(ObjectKeys) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  Handle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver,
                                     Object::ToObject(isolate, object));

  Handle<FixedArray> keys;
  int enum_length = receiver->map()->EnumLength();
  if (enum_length != kInvalidEnumCacheSentinel &&
      JSObject::cast(*receiver)->elements() ==
          isolate->heap()->empty_fixed_array()) {
    DCHECK(receiver->IsJSObject());
    DCHECK(!JSObject::cast(*receiver)->HasNamedInterceptor());
    DCHECK(!JSObject::cast(*receiver)->IsAccessCheckNeeded());
    DCHECK(!receiver->map()->has_hidden_prototype());
    DCHECK(JSObject::cast(*receiver)->HasFastProperties());
    if (enum_length == 0) {
      keys = isolate->factory()->empty_fixed_array();
    } else {
      Handle<FixedArray> cache(
          receiver->map()->instance_descriptors()->GetEnumCache());
      keys = isolate->factory()->CopyFixedArrayUpTo(cache, enum_length);
    }
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, keys,
        KeyAccumulator::GetKeys(receiver, KeyCollectionMode::kOwnOnly,
                                ENUMERABLE_STRINGS,
                                GetKeysConversion::kConvertToString));
  }
  return *isolate->factory()->NewJSArrayWithElements(keys, FAST_ELEMENTS);
}

BUILTIN(ObjectValues) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  Handle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver,
                                     Object::ToObject(isolate, object));
  Handle<FixedArray> values;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, values, JSReceiver::GetOwnValues(receiver, ENUMERABLE_STRINGS));
  return *isolate->factory()->NewJSArrayWithElements(values);
}

BUILTIN(ObjectEntries) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  Handle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver,
                                     Object::ToObject(isolate, object));
  Handle<FixedArray> entries;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, entries,
      JSReceiver::GetOwnEntries(receiver, ENUMERABLE_STRINGS));
  return *isolate->factory()->NewJSArrayWithElements(entries);
}

BUILTIN(ObjectGetOwnPropertyDescriptors) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);

  Handle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver,
                                     Object::ToObject(isolate, object));

  Handle<FixedArray> keys;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, keys, KeyAccumulator::GetKeys(
                         receiver, KeyCollectionMode::kOwnOnly, ALL_PROPERTIES,
                         GetKeysConversion::kConvertToString));

  Handle<JSObject> descriptors =
      isolate->factory()->NewJSObject(isolate->object_function());

  for (int i = 0; i < keys->length(); ++i) {
    Handle<Name> key = Handle<Name>::cast(FixedArray::get(*keys, i, isolate));
    PropertyDescriptor descriptor;
    Maybe<bool> did_get_descriptor = JSReceiver::GetOwnPropertyDescriptor(
        isolate, receiver, key, &descriptor);
    MAYBE_RETURN(did_get_descriptor, isolate->heap()->exception());

    if (!did_get_descriptor.FromJust()) continue;
    Handle<Object> from_descriptor = descriptor.ToObject(isolate);

    LookupIterator it = LookupIterator::PropertyOrElement(
        isolate, descriptors, key, descriptors, LookupIterator::OWN);
    Maybe<bool> success = JSReceiver::CreateDataProperty(&it, from_descriptor,
                                                         Object::DONT_THROW);
    CHECK(success.FromJust());
  }

  return *descriptors;
}

// ES6 section 19.1.2.15 Object.preventExtensions ( O )
BUILTIN(ObjectPreventExtensions) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  if (object->IsJSReceiver()) {
    MAYBE_RETURN(JSReceiver::PreventExtensions(Handle<JSReceiver>::cast(object),
                                               Object::THROW_ON_ERROR),
                 isolate->heap()->exception());
  }
  return *object;
}

// ES6 section 19.1.2.17 Object.seal ( O )
BUILTIN(ObjectSeal) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  if (object->IsJSReceiver()) {
    MAYBE_RETURN(JSReceiver::SetIntegrityLevel(Handle<JSReceiver>::cast(object),
                                               SEALED, Object::THROW_ON_ERROR),
                 isolate->heap()->exception());
  }
  return *object;
}

// ES6 section 18.2.6.2 decodeURI (encodedURI)
BUILTIN(GlobalDecodeURI) {
  HandleScope scope(isolate);
  Handle<String> encoded_uri;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, encoded_uri,
      Object::ToString(isolate, args.atOrUndefined(isolate, 1)));

  RETURN_RESULT_OR_FAILURE(isolate, Uri::DecodeUri(isolate, encoded_uri));
}

// ES6 section 18.2.6.3 decodeURIComponent (encodedURIComponent)
BUILTIN(GlobalDecodeURIComponent) {
  HandleScope scope(isolate);
  Handle<String> encoded_uri_component;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, encoded_uri_component,
      Object::ToString(isolate, args.atOrUndefined(isolate, 1)));

  RETURN_RESULT_OR_FAILURE(
      isolate, Uri::DecodeUriComponent(isolate, encoded_uri_component));
}

// ES6 section 18.2.6.4 encodeURI (uri)
BUILTIN(GlobalEncodeURI) {
  HandleScope scope(isolate);
  Handle<String> uri;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, uri, Object::ToString(isolate, args.atOrUndefined(isolate, 1)));

  RETURN_RESULT_OR_FAILURE(isolate, Uri::EncodeUri(isolate, uri));
}

// ES6 section 18.2.6.5 encodeURIComponenet (uriComponent)
BUILTIN(GlobalEncodeURIComponent) {
  HandleScope scope(isolate);
  Handle<String> uri_component;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, uri_component,
      Object::ToString(isolate, args.atOrUndefined(isolate, 1)));

  RETURN_RESULT_OR_FAILURE(isolate,
                           Uri::EncodeUriComponent(isolate, uri_component));
}

// ES6 section B.2.1.1 escape (string)
BUILTIN(GlobalEscape) {
  HandleScope scope(isolate);
  Handle<String> string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, string,
      Object::ToString(isolate, args.atOrUndefined(isolate, 1)));

  RETURN_RESULT_OR_FAILURE(isolate, Uri::Escape(isolate, string));
}

// ES6 section B.2.1.2 unescape (string)
BUILTIN(GlobalUnescape) {
  HandleScope scope(isolate);
  Handle<String> string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, string,
      Object::ToString(isolate, args.atOrUndefined(isolate, 1)));

  RETURN_RESULT_OR_FAILURE(isolate, Uri::Unescape(isolate, string));
}

namespace {

bool CodeGenerationFromStringsAllowed(Isolate* isolate,
                                      Handle<Context> context) {
  DCHECK(context->allow_code_gen_from_strings()->IsFalse(isolate));
  // Check with callback if set.
  AllowCodeGenerationFromStringsCallback callback =
      isolate->allow_code_gen_callback();
  if (callback == NULL) {
    // No callback set and code generation disallowed.
    return false;
  } else {
    // Callback set. Let it decide if code generation is allowed.
    VMState<EXTERNAL> state(isolate);
    return callback(v8::Utils::ToLocal(context));
  }
}

MaybeHandle<JSFunction> CompileString(Handle<Context> context,
                                      Handle<String> source,
                                      ParseRestriction restriction) {
  Isolate* const isolate = context->GetIsolate();
  Handle<Context> native_context(context->native_context(), isolate);

  // Check if native context allows code generation from
  // strings. Throw an exception if it doesn't.
  if (native_context->allow_code_gen_from_strings()->IsFalse(isolate) &&
      !CodeGenerationFromStringsAllowed(isolate, native_context)) {
    Handle<Object> error_message =
        native_context->ErrorMessageForCodeGenerationFromStrings();
    THROW_NEW_ERROR(isolate, NewEvalError(MessageTemplate::kCodeGenFromStrings,
                                          error_message),
                    JSFunction);
  }

  // Compile source string in the native context.
  int eval_scope_position = 0;
  int eval_position = kNoSourcePosition;
  Handle<SharedFunctionInfo> outer_info(native_context->closure()->shared());
  return Compiler::GetFunctionFromEval(source, outer_info, native_context,
                                       SLOPPY, restriction, eval_scope_position,
                                       eval_position);
}

}  // namespace

// ES6 section 18.2.1 eval (x)
BUILTIN(GlobalEval) {
  HandleScope scope(isolate);
  Handle<Object> x = args.atOrUndefined(isolate, 1);
  Handle<JSFunction> target = args.target<JSFunction>();
  Handle<JSObject> target_global_proxy(target->global_proxy(), isolate);
  if (!x->IsString()) return *x;
  Handle<JSFunction> function;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, function,
      CompileString(handle(target->native_context(), isolate),
                    Handle<String>::cast(x), NO_PARSE_RESTRICTION));
  RETURN_RESULT_OR_FAILURE(
      isolate,
      Execution::Call(isolate, function, target_global_proxy, 0, nullptr));
}

// ES6 section 24.3.1 JSON.parse.
BUILTIN(JsonParse) {
  HandleScope scope(isolate);
  Handle<Object> source = args.atOrUndefined(isolate, 1);
  Handle<Object> reviver = args.atOrUndefined(isolate, 2);
  Handle<String> string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, string,
                                     Object::ToString(isolate, source));
  string = String::Flatten(string);
  RETURN_RESULT_OR_FAILURE(
      isolate, string->IsSeqOneByteString()
                   ? JsonParser<true>::Parse(isolate, string, reviver)
                   : JsonParser<false>::Parse(isolate, string, reviver));
}

// ES6 section 24.3.2 JSON.stringify.
BUILTIN(JsonStringify) {
  HandleScope scope(isolate);
  JsonStringifier stringifier(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  Handle<Object> replacer = args.atOrUndefined(isolate, 2);
  Handle<Object> indent = args.atOrUndefined(isolate, 3);
  RETURN_RESULT_OR_FAILURE(isolate,
                           stringifier.Stringify(object, replacer, indent));
}

// -----------------------------------------------------------------------------
// ES6 section 20.1 Number Objects

// ES6 section 20.1.3.2 Number.prototype.toExponential ( fractionDigits )
BUILTIN(NumberPrototypeToExponential) {
  HandleScope scope(isolate);
  Handle<Object> value = args.at<Object>(0);
  Handle<Object> fraction_digits = args.atOrUndefined(isolate, 1);

  // Unwrap the receiver {value}.
  if (value->IsJSValue()) {
    value = handle(Handle<JSValue>::cast(value)->value(), isolate);
  }
  if (!value->IsNumber()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotGeneric,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Number.prototype.toExponential")));
  }
  double const value_number = value->Number();

  // Convert the {fraction_digits} to an integer first.
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, fraction_digits, Object::ToInteger(isolate, fraction_digits));
  double const fraction_digits_number = fraction_digits->Number();

  if (std::isnan(value_number)) return isolate->heap()->nan_string();
  if (std::isinf(value_number)) {
    return (value_number < 0.0) ? isolate->heap()->minus_infinity_string()
                                : isolate->heap()->infinity_string();
  }
  if (fraction_digits_number < 0.0 || fraction_digits_number > 20.0) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kNumberFormatRange,
                               isolate->factory()->NewStringFromAsciiChecked(
                                   "toExponential()")));
  }
  int const f = args.atOrUndefined(isolate, 1)->IsUndefined(isolate)
                    ? -1
                    : static_cast<int>(fraction_digits_number);
  char* const str = DoubleToExponentialCString(value_number, f);
  Handle<String> result = isolate->factory()->NewStringFromAsciiChecked(str);
  DeleteArray(str);
  return *result;
}

// ES6 section 20.1.3.3 Number.prototype.toFixed ( fractionDigits )
BUILTIN(NumberPrototypeToFixed) {
  HandleScope scope(isolate);
  Handle<Object> value = args.at<Object>(0);
  Handle<Object> fraction_digits = args.atOrUndefined(isolate, 1);

  // Unwrap the receiver {value}.
  if (value->IsJSValue()) {
    value = handle(Handle<JSValue>::cast(value)->value(), isolate);
  }
  if (!value->IsNumber()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotGeneric,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Number.prototype.toFixed")));
  }
  double const value_number = value->Number();

  // Convert the {fraction_digits} to an integer first.
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, fraction_digits, Object::ToInteger(isolate, fraction_digits));
  double const fraction_digits_number = fraction_digits->Number();

  // Check if the {fraction_digits} are in the supported range.
  if (fraction_digits_number < 0.0 || fraction_digits_number > 20.0) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kNumberFormatRange,
                               isolate->factory()->NewStringFromAsciiChecked(
                                   "toFixed() digits")));
  }

  if (std::isnan(value_number)) return isolate->heap()->nan_string();
  if (std::isinf(value_number)) {
    return (value_number < 0.0) ? isolate->heap()->minus_infinity_string()
                                : isolate->heap()->infinity_string();
  }
  char* const str = DoubleToFixedCString(
      value_number, static_cast<int>(fraction_digits_number));
  Handle<String> result = isolate->factory()->NewStringFromAsciiChecked(str);
  DeleteArray(str);
  return *result;
}

// ES6 section 20.1.3.4 Number.prototype.toLocaleString ( [ r1 [ , r2 ] ] )
BUILTIN(NumberPrototypeToLocaleString) {
  HandleScope scope(isolate);
  Handle<Object> value = args.at<Object>(0);

  // Unwrap the receiver {value}.
  if (value->IsJSValue()) {
    value = handle(Handle<JSValue>::cast(value)->value(), isolate);
  }
  if (!value->IsNumber()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotGeneric,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Number.prototype.toLocaleString")));
  }

  // Turn the {value} into a String.
  return *isolate->factory()->NumberToString(value);
}

// ES6 section 20.1.3.5 Number.prototype.toPrecision ( precision )
BUILTIN(NumberPrototypeToPrecision) {
  HandleScope scope(isolate);
  Handle<Object> value = args.at<Object>(0);
  Handle<Object> precision = args.atOrUndefined(isolate, 1);

  // Unwrap the receiver {value}.
  if (value->IsJSValue()) {
    value = handle(Handle<JSValue>::cast(value)->value(), isolate);
  }
  if (!value->IsNumber()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotGeneric,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Number.prototype.toPrecision")));
  }
  double const value_number = value->Number();

  // If no {precision} was specified, just return ToString of {value}.
  if (precision->IsUndefined(isolate)) {
    return *isolate->factory()->NumberToString(value);
  }

  // Convert the {precision} to an integer first.
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, precision,
                                     Object::ToInteger(isolate, precision));
  double const precision_number = precision->Number();

  if (std::isnan(value_number)) return isolate->heap()->nan_string();
  if (std::isinf(value_number)) {
    return (value_number < 0.0) ? isolate->heap()->minus_infinity_string()
                                : isolate->heap()->infinity_string();
  }
  if (precision_number < 1.0 || precision_number > 21.0) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kToPrecisionFormatRange));
  }
  char* const str = DoubleToPrecisionCString(
      value_number, static_cast<int>(precision_number));
  Handle<String> result = isolate->factory()->NewStringFromAsciiChecked(str);
  DeleteArray(str);
  return *result;
}

// ES6 section 20.1.3.6 Number.prototype.toString ( [ radix ] )
BUILTIN(NumberPrototypeToString) {
  HandleScope scope(isolate);
  Handle<Object> value = args.at<Object>(0);
  Handle<Object> radix = args.atOrUndefined(isolate, 1);

  // Unwrap the receiver {value}.
  if (value->IsJSValue()) {
    value = handle(Handle<JSValue>::cast(value)->value(), isolate);
  }
  if (!value->IsNumber()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotGeneric,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Number.prototype.toString")));
  }
  double const value_number = value->Number();

  // If no {radix} was specified, just return ToString of {value}.
  if (radix->IsUndefined(isolate)) {
    return *isolate->factory()->NumberToString(value);
  }

  // Convert the {radix} to an integer first.
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, radix,
                                     Object::ToInteger(isolate, radix));
  double const radix_number = radix->Number();

  // If {radix} is 10, just return ToString of {value}.
  if (radix_number == 10.0) return *isolate->factory()->NumberToString(value);

  // Make sure the {radix} is within the valid range.
  if (radix_number < 2.0 || radix_number > 36.0) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kToRadixFormatRange));
  }

  // Fast case where the result is a one character string.
  if (IsUint32Double(value_number) && value_number < radix_number) {
    // Character array used for conversion.
    static const char kCharTable[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    return *isolate->factory()->LookupSingleCharacterStringFromCode(
        kCharTable[static_cast<uint32_t>(value_number)]);
  }

  // Slow case.
  if (std::isnan(value_number)) return isolate->heap()->nan_string();
  if (std::isinf(value_number)) {
    return (value_number < 0.0) ? isolate->heap()->minus_infinity_string()
                                : isolate->heap()->infinity_string();
  }
  char* const str =
      DoubleToRadixCString(value_number, static_cast<int>(radix_number));
  Handle<String> result = isolate->factory()->NewStringFromAsciiChecked(str);
  DeleteArray(str);
  return *result;
}

// ES6 section 20.1.3.7 Number.prototype.valueOf ( )
void Builtins::Generate_NumberPrototypeValueOf(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  Node* result = assembler->ToThisValue(
      context, receiver, PrimitiveType::kNumber, "Number.prototype.valueOf");
  assembler->Return(result);
}

// -----------------------------------------------------------------------------
// ES6 section 20.2.2 Function Properties of the Math Object

// ES6 section - 20.2.2.1 Math.abs ( x )
void Builtins::Generate_MathAbs(CodeStubAssembler* assembler) {
  using compiler::Node;
  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Abs(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.2 Math.acos ( x )
void Builtins::Generate_MathAcos(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Acos(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.3 Math.acosh ( x )
void Builtins::Generate_MathAcosh(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Acosh(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.4 Math.asin ( x )
void Builtins::Generate_MathAsin(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Asin(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.5 Math.asinh ( x )
void Builtins::Generate_MathAsinh(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Asinh(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.6 Math.atan ( x )
void Builtins::Generate_MathAtan(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Atan(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.7 Math.atanh ( x )
void Builtins::Generate_MathAtanh(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Atanh(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.8 Math.atan2 ( y, x )
void Builtins::Generate_MathAtan2(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* y = assembler->Parameter(1);
  Node* x = assembler->Parameter(2);
  Node* context = assembler->Parameter(5);
  Node* y_value = assembler->TruncateTaggedToFloat64(context, y);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Atan2(y_value, x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

namespace {

void Generate_MathRoundingOperation(
    CodeStubAssembler* assembler,
    compiler::Node* (CodeStubAssembler::*float64op)(compiler::Node*)) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* context = assembler->Parameter(4);

  // We might need to loop once for ToNumber conversion.
  Variable var_x(assembler, MachineRepresentation::kTagged);
  Label loop(assembler, &var_x);
  var_x.Bind(assembler->Parameter(1));
  assembler->Goto(&loop);
  assembler->Bind(&loop);
  {
    // Load the current {x} value.
    Node* x = var_x.value();

    // Check if {x} is a Smi or a HeapObject.
    Label if_xissmi(assembler), if_xisnotsmi(assembler);
    assembler->Branch(assembler->WordIsSmi(x), &if_xissmi, &if_xisnotsmi);

    assembler->Bind(&if_xissmi);
    {
      // Nothing to do when {x} is a Smi.
      assembler->Return(x);
    }

    assembler->Bind(&if_xisnotsmi);
    {
      // Check if {x} is a HeapNumber.
      Label if_xisheapnumber(assembler),
          if_xisnotheapnumber(assembler, Label::kDeferred);
      assembler->Branch(
          assembler->WordEqual(assembler->LoadMap(x),
                               assembler->HeapNumberMapConstant()),
          &if_xisheapnumber, &if_xisnotheapnumber);

      assembler->Bind(&if_xisheapnumber);
      {
        Node* x_value = assembler->LoadHeapNumberValue(x);
        Node* value = (assembler->*float64op)(x_value);
        Node* result = assembler->ChangeFloat64ToTagged(value);
        assembler->Return(result);
      }

      assembler->Bind(&if_xisnotheapnumber);
      {
        // Need to convert {x} to a Number first.
        Callable callable =
            CodeFactory::NonNumberToNumber(assembler->isolate());
        var_x.Bind(assembler->CallStub(callable, context, x));
        assembler->Goto(&loop);
      }
    }
  }
}

}  // namespace

// ES6 section 20.2.2.10 Math.ceil ( x )
void Builtins::Generate_MathCeil(CodeStubAssembler* assembler) {
  Generate_MathRoundingOperation(assembler, &CodeStubAssembler::Float64Ceil);
}

// ES6 section 20.2.2.9 Math.cbrt ( x )
void Builtins::Generate_MathCbrt(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Cbrt(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.11 Math.clz32 ( x )
void Builtins::Generate_MathClz32(CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* context = assembler->Parameter(4);

  // Shared entry point for the clz32 operation.
  Variable var_clz32_x(assembler, MachineRepresentation::kWord32);
  Label do_clz32(assembler);

  // We might need to loop once for ToNumber conversion.
  Variable var_x(assembler, MachineRepresentation::kTagged);
  Label loop(assembler, &var_x);
  var_x.Bind(assembler->Parameter(1));
  assembler->Goto(&loop);
  assembler->Bind(&loop);
  {
    // Load the current {x} value.
    Node* x = var_x.value();

    // Check if {x} is a Smi or a HeapObject.
    Label if_xissmi(assembler), if_xisnotsmi(assembler);
    assembler->Branch(assembler->WordIsSmi(x), &if_xissmi, &if_xisnotsmi);

    assembler->Bind(&if_xissmi);
    {
      var_clz32_x.Bind(assembler->SmiToWord32(x));
      assembler->Goto(&do_clz32);
    }

    assembler->Bind(&if_xisnotsmi);
    {
      // Check if {x} is a HeapNumber.
      Label if_xisheapnumber(assembler),
          if_xisnotheapnumber(assembler, Label::kDeferred);
      assembler->Branch(
          assembler->WordEqual(assembler->LoadMap(x),
                               assembler->HeapNumberMapConstant()),
          &if_xisheapnumber, &if_xisnotheapnumber);

      assembler->Bind(&if_xisheapnumber);
      {
        var_clz32_x.Bind(assembler->TruncateHeapNumberValueToWord32(x));
        assembler->Goto(&do_clz32);
      }

      assembler->Bind(&if_xisnotheapnumber);
      {
        // Need to convert {x} to a Number first.
        Callable callable =
            CodeFactory::NonNumberToNumber(assembler->isolate());
        var_x.Bind(assembler->CallStub(callable, context, x));
        assembler->Goto(&loop);
      }
    }
  }

  assembler->Bind(&do_clz32);
  {
    Node* x_value = var_clz32_x.value();
    Node* value = assembler->Word32Clz(x_value);
    Node* result = assembler->ChangeInt32ToTagged(value);
    assembler->Return(result);
  }
}

// ES6 section 20.2.2.12 Math.cos ( x )
void Builtins::Generate_MathCos(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Cos(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.13 Math.cosh ( x )
void Builtins::Generate_MathCosh(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Cosh(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.14 Math.exp ( x )
void Builtins::Generate_MathExp(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Exp(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.16 Math.floor ( x )
void Builtins::Generate_MathFloor(CodeStubAssembler* assembler) {
  Generate_MathRoundingOperation(assembler, &CodeStubAssembler::Float64Floor);
}

// ES6 section 20.2.2.17 Math.fround ( x )
void Builtins::Generate_MathFround(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value32 = assembler->TruncateFloat64ToFloat32(x_value);
  Node* value = assembler->ChangeFloat32ToFloat64(value32);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.18 Math.hypot ( value1, value2, ...values )
BUILTIN(MathHypot) {
  HandleScope scope(isolate);
  int const length = args.length() - 1;
  if (length == 0) return Smi::FromInt(0);
  DCHECK_LT(0, length);
  double max = 0;
  bool one_arg_is_nan = false;
  List<double> abs_values(length);
  for (int i = 0; i < length; i++) {
    Handle<Object> x = args.at<Object>(i + 1);
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, x, Object::ToNumber(x));
    double abs_value = std::abs(x->Number());

    if (std::isnan(abs_value)) {
      one_arg_is_nan = true;
    } else {
      abs_values.Add(abs_value);
      if (max < abs_value) {
        max = abs_value;
      }
    }
  }

  if (max == V8_INFINITY) {
    return *isolate->factory()->NewNumber(V8_INFINITY);
  }

  if (one_arg_is_nan) {
    return *isolate->factory()->nan_value();
  }

  if (max == 0) {
    return Smi::FromInt(0);
  }
  DCHECK_GT(max, 0);

  // Kahan summation to avoid rounding errors.
  // Normalize the numbers to the largest one to avoid overflow.
  double sum = 0;
  double compensation = 0;
  for (int i = 0; i < length; i++) {
    double n = abs_values.at(i) / max;
    double summand = n * n - compensation;
    double preliminary = sum + summand;
    compensation = (preliminary - sum) - summand;
    sum = preliminary;
  }

  return *isolate->factory()->NewNumber(std::sqrt(sum) * max);
}

// ES6 section 20.2.2.19 Math.imul ( x, y )
void Builtins::Generate_MathImul(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* y = assembler->Parameter(2);
  Node* context = assembler->Parameter(5);
  Node* x_value = assembler->TruncateTaggedToWord32(context, x);
  Node* y_value = assembler->TruncateTaggedToWord32(context, y);
  Node* value = assembler->Int32Mul(x_value, y_value);
  Node* result = assembler->ChangeInt32ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.20 Math.log ( x )
void Builtins::Generate_MathLog(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Log(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.21 Math.log1p ( x )
void Builtins::Generate_MathLog1p(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Log1p(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.22 Math.log10 ( x )
void Builtins::Generate_MathLog10(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Log10(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.23 Math.log2 ( x )
void Builtins::Generate_MathLog2(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Log2(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.15 Math.expm1 ( x )
void Builtins::Generate_MathExpm1(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Expm1(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.26 Math.pow ( x, y )
void Builtins::Generate_MathPow(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* y = assembler->Parameter(2);
  Node* context = assembler->Parameter(5);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* y_value = assembler->TruncateTaggedToFloat64(context, y);
  Node* value = assembler->Float64Pow(x_value, y_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.28 Math.round ( x )
void Builtins::Generate_MathRound(CodeStubAssembler* assembler) {
  Generate_MathRoundingOperation(assembler, &CodeStubAssembler::Float64Round);
}

// ES6 section 20.2.2.29 Math.sign ( x )
void Builtins::Generate_MathSign(CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  using compiler::Node;

  // Convert the {x} value to a Number.
  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);

  // Return -1 if {x} is negative, 1 if {x} is positive, or {x} itself.
  Label if_xisnegative(assembler), if_xispositive(assembler);
  assembler->GotoIf(
      assembler->Float64LessThan(x_value, assembler->Float64Constant(0.0)),
      &if_xisnegative);
  assembler->GotoIf(
      assembler->Float64LessThan(assembler->Float64Constant(0.0), x_value),
      &if_xispositive);
  assembler->Return(assembler->ChangeFloat64ToTagged(x_value));

  assembler->Bind(&if_xisnegative);
  assembler->Return(assembler->SmiConstant(Smi::FromInt(-1)));

  assembler->Bind(&if_xispositive);
  assembler->Return(assembler->SmiConstant(Smi::FromInt(1)));
}

// ES6 section 20.2.2.30 Math.sin ( x )
void Builtins::Generate_MathSin(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Sin(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.31 Math.sinh ( x )
void Builtins::Generate_MathSinh(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Sinh(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.32 Math.sqrt ( x )
void Builtins::Generate_MathSqrt(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Sqrt(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.33 Math.tan ( x )
void Builtins::Generate_MathTan(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Tan(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.34 Math.tanh ( x )
void Builtins::Generate_MathTanh(CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* x = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* x_value = assembler->TruncateTaggedToFloat64(context, x);
  Node* value = assembler->Float64Tanh(x_value);
  Node* result = assembler->ChangeFloat64ToTagged(value);
  assembler->Return(result);
}

// ES6 section 20.2.2.35 Math.trunc ( x )
void Builtins::Generate_MathTrunc(CodeStubAssembler* assembler) {
  Generate_MathRoundingOperation(assembler, &CodeStubAssembler::Float64Trunc);
}

// -----------------------------------------------------------------------------
// ES6 section 19.2 Function Objects

// ES6 section 19.2.3.6 Function.prototype [ @@hasInstance ] ( V )
void Builtins::Generate_FunctionPrototypeHasInstance(
    CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* f = assembler->Parameter(0);
  Node* v = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* result = assembler->OrdinaryHasInstance(context, f, v);
  assembler->Return(result);
}

// -----------------------------------------------------------------------------
// ES6 section 25.3 Generator Objects

namespace {

void Generate_GeneratorPrototypeResume(
    CodeStubAssembler* assembler, JSGeneratorObject::ResumeMode resume_mode,
    char const* const method_name) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* value = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* closed =
      assembler->SmiConstant(Smi::FromInt(JSGeneratorObject::kGeneratorClosed));

  // Check if the {receiver} is actually a JSGeneratorObject.
  Label if_receiverisincompatible(assembler, Label::kDeferred);
  assembler->GotoIf(assembler->WordIsSmi(receiver), &if_receiverisincompatible);
  Node* receiver_instance_type = assembler->LoadInstanceType(receiver);
  assembler->GotoUnless(assembler->Word32Equal(
                            receiver_instance_type,
                            assembler->Int32Constant(JS_GENERATOR_OBJECT_TYPE)),
                        &if_receiverisincompatible);

  // Check if the {receiver} is running or already closed.
  Node* receiver_continuation = assembler->LoadObjectField(
      receiver, JSGeneratorObject::kContinuationOffset);
  Label if_receiverisclosed(assembler, Label::kDeferred),
      if_receiverisrunning(assembler, Label::kDeferred);
  assembler->GotoIf(assembler->SmiEqual(receiver_continuation, closed),
                    &if_receiverisclosed);
  DCHECK_LT(JSGeneratorObject::kGeneratorExecuting,
            JSGeneratorObject::kGeneratorClosed);
  assembler->GotoIf(assembler->SmiLessThan(receiver_continuation, closed),
                    &if_receiverisrunning);

  // Resume the {receiver} using our trampoline.
  Node* result = assembler->CallStub(
      CodeFactory::ResumeGenerator(assembler->isolate()), context, value,
      receiver, assembler->SmiConstant(Smi::FromInt(resume_mode)));
  assembler->Return(result);

  assembler->Bind(&if_receiverisincompatible);
  {
    // The {receiver} is not a valid JSGeneratorObject.
    Node* result = assembler->CallRuntime(
        Runtime::kThrowIncompatibleMethodReceiver, context,
        assembler->HeapConstant(assembler->factory()->NewStringFromAsciiChecked(
            method_name, TENURED)),
        receiver);
    assembler->Return(result);  // Never reached.
  }

  assembler->Bind(&if_receiverisclosed);
  {
    // The {receiver} is closed already.
    Node* result = nullptr;
    switch (resume_mode) {
      case JSGeneratorObject::kNext:
        result = assembler->CallRuntime(Runtime::kCreateIterResultObject,
                                        context, assembler->UndefinedConstant(),
                                        assembler->BooleanConstant(true));
        break;
      case JSGeneratorObject::kReturn:
        result =
            assembler->CallRuntime(Runtime::kCreateIterResultObject, context,
                                   value, assembler->BooleanConstant(true));
        break;
      case JSGeneratorObject::kThrow:
        result = assembler->CallRuntime(Runtime::kThrow, context, value);
        break;
    }
    assembler->Return(result);
  }

  assembler->Bind(&if_receiverisrunning);
  {
    Node* result =
        assembler->CallRuntime(Runtime::kThrowGeneratorRunning, context);
    assembler->Return(result);  // Never reached.
  }
}

}  // namespace

// ES6 section 25.3.1.2 Generator.prototype.next ( value )
void Builtins::Generate_GeneratorPrototypeNext(CodeStubAssembler* assembler) {
  Generate_GeneratorPrototypeResume(assembler, JSGeneratorObject::kNext,
                                    "[Generator].prototype.next");
}

// ES6 section 25.3.1.3 Generator.prototype.return ( value )
void Builtins::Generate_GeneratorPrototypeReturn(CodeStubAssembler* assembler) {
  Generate_GeneratorPrototypeResume(assembler, JSGeneratorObject::kReturn,
                                    "[Generator].prototype.return");
}

// ES6 section 25.3.1.4 Generator.prototype.throw ( exception )
void Builtins::Generate_GeneratorPrototypeThrow(CodeStubAssembler* assembler) {
  Generate_GeneratorPrototypeResume(assembler, JSGeneratorObject::kThrow,
                                    "[Generator].prototype.throw");
}

// -----------------------------------------------------------------------------
// ES6 section 26.1 The Reflect Object

// ES6 section 26.1.3 Reflect.defineProperty
BUILTIN(ReflectDefineProperty) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  Handle<Object> target = args.at<Object>(1);
  Handle<Object> key = args.at<Object>(2);
  Handle<Object> attributes = args.at<Object>(3);

  if (!target->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNonObject,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Reflect.defineProperty")));
  }

  Handle<Name> name;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, name,
                                     Object::ToName(isolate, key));

  PropertyDescriptor desc;
  if (!PropertyDescriptor::ToPropertyDescriptor(isolate, attributes, &desc)) {
    return isolate->heap()->exception();
  }

  Maybe<bool> result =
      JSReceiver::DefineOwnProperty(isolate, Handle<JSReceiver>::cast(target),
                                    name, &desc, Object::DONT_THROW);
  MAYBE_RETURN(result, isolate->heap()->exception());
  return *isolate->factory()->ToBoolean(result.FromJust());
}

// ES6 section 26.1.4 Reflect.deleteProperty
BUILTIN(ReflectDeleteProperty) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<Object> target = args.at<Object>(1);
  Handle<Object> key = args.at<Object>(2);

  if (!target->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNonObject,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Reflect.deleteProperty")));
  }

  Handle<Name> name;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, name,
                                     Object::ToName(isolate, key));

  Maybe<bool> result = JSReceiver::DeletePropertyOrElement(
      Handle<JSReceiver>::cast(target), name, SLOPPY);
  MAYBE_RETURN(result, isolate->heap()->exception());
  return *isolate->factory()->ToBoolean(result.FromJust());
}

// ES6 section 26.1.6 Reflect.get
BUILTIN(ReflectGet) {
  HandleScope scope(isolate);
  Handle<Object> target = args.atOrUndefined(isolate, 1);
  Handle<Object> key = args.atOrUndefined(isolate, 2);
  Handle<Object> receiver = args.length() > 3 ? args.at<Object>(3) : target;

  if (!target->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNonObject,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Reflect.get")));
  }

  Handle<Name> name;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, name,
                                     Object::ToName(isolate, key));

  RETURN_RESULT_OR_FAILURE(
      isolate, Object::GetPropertyOrElement(receiver, name,
                                            Handle<JSReceiver>::cast(target)));
}

// ES6 section 26.1.7 Reflect.getOwnPropertyDescriptor
BUILTIN(ReflectGetOwnPropertyDescriptor) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<Object> target = args.at<Object>(1);
  Handle<Object> key = args.at<Object>(2);

  if (!target->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNonObject,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Reflect.getOwnPropertyDescriptor")));
  }

  Handle<Name> name;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, name,
                                     Object::ToName(isolate, key));

  PropertyDescriptor desc;
  Maybe<bool> found = JSReceiver::GetOwnPropertyDescriptor(
      isolate, Handle<JSReceiver>::cast(target), name, &desc);
  MAYBE_RETURN(found, isolate->heap()->exception());
  if (!found.FromJust()) return isolate->heap()->undefined_value();
  return *desc.ToObject(isolate);
}

// ES6 section 26.1.8 Reflect.getPrototypeOf
BUILTIN(ReflectGetPrototypeOf) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<Object> target = args.at<Object>(1);

  if (!target->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNonObject,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Reflect.getPrototypeOf")));
  }
  Handle<JSReceiver> receiver = Handle<JSReceiver>::cast(target);
  RETURN_RESULT_OR_FAILURE(isolate,
                           JSReceiver::GetPrototype(isolate, receiver));
}

// ES6 section 26.1.9 Reflect.has
BUILTIN(ReflectHas) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<Object> target = args.at<Object>(1);
  Handle<Object> key = args.at<Object>(2);

  if (!target->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNonObject,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Reflect.has")));
  }

  Handle<Name> name;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, name,
                                     Object::ToName(isolate, key));

  Maybe<bool> result =
      JSReceiver::HasProperty(Handle<JSReceiver>::cast(target), name);
  return result.IsJust() ? *isolate->factory()->ToBoolean(result.FromJust())
                         : isolate->heap()->exception();
}

// ES6 section 26.1.10 Reflect.isExtensible
BUILTIN(ReflectIsExtensible) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<Object> target = args.at<Object>(1);

  if (!target->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNonObject,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Reflect.isExtensible")));
  }

  Maybe<bool> result =
      JSReceiver::IsExtensible(Handle<JSReceiver>::cast(target));
  MAYBE_RETURN(result, isolate->heap()->exception());
  return *isolate->factory()->ToBoolean(result.FromJust());
}

// ES6 section 26.1.11 Reflect.ownKeys
BUILTIN(ReflectOwnKeys) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<Object> target = args.at<Object>(1);

  if (!target->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNonObject,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Reflect.ownKeys")));
  }

  Handle<FixedArray> keys;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, keys,
      KeyAccumulator::GetKeys(Handle<JSReceiver>::cast(target),
                              KeyCollectionMode::kOwnOnly, ALL_PROPERTIES,
                              GetKeysConversion::kConvertToString));
  return *isolate->factory()->NewJSArrayWithElements(keys);
}

// ES6 section 26.1.12 Reflect.preventExtensions
BUILTIN(ReflectPreventExtensions) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<Object> target = args.at<Object>(1);

  if (!target->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNonObject,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Reflect.preventExtensions")));
  }

  Maybe<bool> result = JSReceiver::PreventExtensions(
      Handle<JSReceiver>::cast(target), Object::DONT_THROW);
  MAYBE_RETURN(result, isolate->heap()->exception());
  return *isolate->factory()->ToBoolean(result.FromJust());
}

// ES6 section 26.1.13 Reflect.set
BUILTIN(ReflectSet) {
  HandleScope scope(isolate);
  Handle<Object> target = args.atOrUndefined(isolate, 1);
  Handle<Object> key = args.atOrUndefined(isolate, 2);
  Handle<Object> value = args.atOrUndefined(isolate, 3);
  Handle<Object> receiver = args.length() > 4 ? args.at<Object>(4) : target;

  if (!target->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNonObject,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Reflect.set")));
  }

  Handle<Name> name;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, name,
                                     Object::ToName(isolate, key));

  LookupIterator it = LookupIterator::PropertyOrElement(
      isolate, receiver, name, Handle<JSReceiver>::cast(target));
  Maybe<bool> result = Object::SetSuperProperty(
      &it, value, SLOPPY, Object::MAY_BE_STORE_FROM_KEYED);
  MAYBE_RETURN(result, isolate->heap()->exception());
  return *isolate->factory()->ToBoolean(result.FromJust());
}

// ES6 section 26.1.14 Reflect.setPrototypeOf
BUILTIN(ReflectSetPrototypeOf) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<Object> target = args.at<Object>(1);
  Handle<Object> proto = args.at<Object>(2);

  if (!target->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNonObject,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Reflect.setPrototypeOf")));
  }

  if (!proto->IsJSReceiver() && !proto->IsNull(isolate)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kProtoObjectOrNull, proto));
  }

  Maybe<bool> result = JSReceiver::SetPrototype(
      Handle<JSReceiver>::cast(target), proto, true, Object::DONT_THROW);
  MAYBE_RETURN(result, isolate->heap()->exception());
  return *isolate->factory()->ToBoolean(result.FromJust());
}

// -----------------------------------------------------------------------------
// ES6 section 19.3 Boolean Objects

// ES6 section 19.3.1.1 Boolean ( value ) for the [[Call]] case.
BUILTIN(BooleanConstructor) {
  HandleScope scope(isolate);
  Handle<Object> value = args.atOrUndefined(isolate, 1);
  return isolate->heap()->ToBoolean(value->BooleanValue());
}

// ES6 section 19.3.1.1 Boolean ( value ) for the [[Construct]] case.
BUILTIN(BooleanConstructor_ConstructStub) {
  HandleScope scope(isolate);
  Handle<Object> value = args.atOrUndefined(isolate, 1);
  Handle<JSFunction> target = args.target<JSFunction>();
  Handle<JSReceiver> new_target = Handle<JSReceiver>::cast(args.new_target());
  DCHECK(*target == target->native_context()->boolean_function());
  Handle<JSObject> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, result,
                                     JSObject::New(target, new_target));
  Handle<JSValue>::cast(result)->set_value(
      isolate->heap()->ToBoolean(value->BooleanValue()));
  return *result;
}

// ES6 section 19.3.3.2 Boolean.prototype.toString ( )
void Builtins::Generate_BooleanPrototypeToString(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  Node* value = assembler->ToThisValue(
      context, receiver, PrimitiveType::kBoolean, "Boolean.prototype.toString");
  Node* result = assembler->LoadObjectField(value, Oddball::kToStringOffset);
  assembler->Return(result);
}

// ES6 section 19.3.3.3 Boolean.prototype.valueOf ( )
void Builtins::Generate_BooleanPrototypeValueOf(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  Node* result = assembler->ToThisValue(
      context, receiver, PrimitiveType::kBoolean, "Boolean.prototype.valueOf");
  assembler->Return(result);
}

// -----------------------------------------------------------------------------
// ES6 section 24.2 DataView Objects

// ES6 section 24.2.2 The DataView Constructor for the [[Call]] case.
BUILTIN(DataViewConstructor) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate,
      NewTypeError(MessageTemplate::kConstructorNotFunction,
                   isolate->factory()->NewStringFromAsciiChecked("DataView")));
}

// ES6 section 24.2.2 The DataView Constructor for the [[Construct]] case.
BUILTIN(DataViewConstructor_ConstructStub) {
  HandleScope scope(isolate);
  Handle<JSFunction> target = args.target<JSFunction>();
  Handle<JSReceiver> new_target = Handle<JSReceiver>::cast(args.new_target());
  Handle<Object> buffer = args.atOrUndefined(isolate, 1);
  Handle<Object> byte_offset = args.atOrUndefined(isolate, 2);
  Handle<Object> byte_length = args.atOrUndefined(isolate, 3);

  // 2. If Type(buffer) is not Object, throw a TypeError exception.
  // 3. If buffer does not have an [[ArrayBufferData]] internal slot, throw a
  //    TypeError exception.
  if (!buffer->IsJSArrayBuffer()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kDataViewNotArrayBuffer));
  }
  Handle<JSArrayBuffer> array_buffer = Handle<JSArrayBuffer>::cast(buffer);

  // 4. Let numberOffset be ? ToNumber(byteOffset).
  Handle<Object> number_offset;
  if (byte_offset->IsUndefined(isolate)) {
    // We intentionally violate the specification at this point to allow
    // for new DataView(buffer) invocations to be equivalent to the full
    // new DataView(buffer, 0) invocation.
    number_offset = handle(Smi::FromInt(0), isolate);
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, number_offset,
                                       Object::ToNumber(byte_offset));
  }

  // 5. Let offset be ToInteger(numberOffset).
  Handle<Object> offset;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, offset,
                                     Object::ToInteger(isolate, number_offset));

  // 6. If numberOffset ≠ offset or offset < 0, throw a RangeError exception.
  if (number_offset->Number() != offset->Number() || offset->Number() < 0.0) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kInvalidDataViewOffset));
  }

  // 7. If IsDetachedBuffer(buffer) is true, throw a TypeError exception.
  // We currently violate the specification at this point.

  // 8. Let bufferByteLength be the value of buffer's [[ArrayBufferByteLength]]
  // internal slot.
  double const buffer_byte_length = array_buffer->byte_length()->Number();

  // 9. If offset > bufferByteLength, throw a RangeError exception
  if (offset->Number() > buffer_byte_length) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kInvalidDataViewOffset));
  }

  Handle<Object> view_byte_length;
  if (byte_length->IsUndefined(isolate)) {
    // 10. If byteLength is undefined, then
    //       a. Let viewByteLength be bufferByteLength - offset.
    view_byte_length =
        isolate->factory()->NewNumber(buffer_byte_length - offset->Number());
  } else {
    // 11. Else,
    //       a. Let viewByteLength be ? ToLength(byteLength).
    //       b. If offset+viewByteLength > bufferByteLength, throw a RangeError
    //          exception
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, view_byte_length,
                                       Object::ToLength(isolate, byte_length));
    if (offset->Number() + view_byte_length->Number() > buffer_byte_length) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewRangeError(MessageTemplate::kInvalidDataViewLength));
    }
  }

  // 12. Let O be ? OrdinaryCreateFromConstructor(NewTarget,
  //     "%DataViewPrototype%", «[[DataView]], [[ViewedArrayBuffer]],
  //     [[ByteLength]], [[ByteOffset]]»).
  // 13. Set O's [[DataView]] internal slot to true.
  Handle<JSObject> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, result,
                                     JSObject::New(target, new_target));
  for (int i = 0; i < ArrayBufferView::kInternalFieldCount; ++i) {
    Handle<JSDataView>::cast(result)->SetInternalField(i, Smi::FromInt(0));
  }

  // 14. Set O's [[ViewedArrayBuffer]] internal slot to buffer.
  Handle<JSDataView>::cast(result)->set_buffer(*array_buffer);

  // 15. Set O's [[ByteLength]] internal slot to viewByteLength.
  Handle<JSDataView>::cast(result)->set_byte_length(*view_byte_length);

  // 16. Set O's [[ByteOffset]] internal slot to offset.
  Handle<JSDataView>::cast(result)->set_byte_offset(*offset);

  // 17. Return O.
  return *result;
}

// ES6 section 24.2.4.1 get DataView.prototype.buffer
BUILTIN(DataViewPrototypeGetBuffer) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDataView, data_view, "get DataView.prototype.buffer");
  return data_view->buffer();
}

// ES6 section 24.2.4.2 get DataView.prototype.byteLength
BUILTIN(DataViewPrototypeGetByteLength) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDataView, data_view, "get DataView.prototype.byteLength");
  // TODO(bmeurer): According to the ES6 spec, we should throw a TypeError
  // here if the JSArrayBuffer of the {data_view} was neutered.
  return data_view->byte_length();
}

// ES6 section 24.2.4.3 get DataView.prototype.byteOffset
BUILTIN(DataViewPrototypeGetByteOffset) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDataView, data_view, "get DataView.prototype.byteOffset");
  // TODO(bmeurer): According to the ES6 spec, we should throw a TypeError
  // here if the JSArrayBuffer of the {data_view} was neutered.
  return data_view->byte_offset();
}

// -----------------------------------------------------------------------------
// ES6 section 22.2 TypedArray Objects

// ES6 section 22.2.3.1 get %TypedArray%.prototype.buffer
BUILTIN(TypedArrayPrototypeBuffer) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSTypedArray, typed_array, "get TypedArray.prototype.buffer");
  return *typed_array->GetBuffer();
}

namespace {

void Generate_TypedArrayProtoypeGetter(CodeStubAssembler* assembler,
                                       const char* method_name,
                                       int object_offset) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  // Check if the {receiver} is actually a JSTypedArray.
  Label if_receiverisincompatible(assembler, Label::kDeferred);
  assembler->GotoIf(assembler->WordIsSmi(receiver), &if_receiverisincompatible);
  Node* receiver_instance_type = assembler->LoadInstanceType(receiver);
  assembler->GotoUnless(
      assembler->Word32Equal(receiver_instance_type,
                             assembler->Int32Constant(JS_TYPED_ARRAY_TYPE)),
      &if_receiverisincompatible);

  // Check if the {receiver}'s JSArrayBuffer was neutered.
  Node* receiver_buffer =
      assembler->LoadObjectField(receiver, JSTypedArray::kBufferOffset);
  Node* receiver_buffer_bit_field = assembler->LoadObjectField(
      receiver_buffer, JSArrayBuffer::kBitFieldOffset, MachineType::Uint32());
  Label if_receiverisneutered(assembler, Label::kDeferred);
  assembler->GotoUnless(
      assembler->Word32Equal(
          assembler->Word32And(
              receiver_buffer_bit_field,
              assembler->Int32Constant(JSArrayBuffer::WasNeutered::kMask)),
          assembler->Int32Constant(0)),
      &if_receiverisneutered);
  assembler->Return(assembler->LoadObjectField(receiver, object_offset));

  assembler->Bind(&if_receiverisneutered);
  {
    // The {receiver}s buffer was neutered, default to zero.
    assembler->Return(assembler->SmiConstant(0));
  }

  assembler->Bind(&if_receiverisincompatible);
  {
    // The {receiver} is not a valid JSGeneratorObject.
    Node* result = assembler->CallRuntime(
        Runtime::kThrowIncompatibleMethodReceiver, context,
        assembler->HeapConstant(assembler->factory()->NewStringFromAsciiChecked(
            method_name, TENURED)),
        receiver);
    assembler->Return(result);  // Never reached.
  }
}

}  // namespace

// ES6 section 22.2.3.2 get %TypedArray%.prototype.byteLength
void Builtins::Generate_TypedArrayPrototypeByteLength(
    CodeStubAssembler* assembler) {
  Generate_TypedArrayProtoypeGetter(assembler,
                                    "get TypedArray.prototype.byteLength",
                                    JSTypedArray::kByteLengthOffset);
}

// ES6 section 22.2.3.3 get %TypedArray%.prototype.byteOffset
void Builtins::Generate_TypedArrayPrototypeByteOffset(
    CodeStubAssembler* assembler) {
  Generate_TypedArrayProtoypeGetter(assembler,
                                    "get TypedArray.prototype.byteOffset",
                                    JSTypedArray::kByteOffsetOffset);
}

// ES6 section 22.2.3.18 get %TypedArray%.prototype.length
void Builtins::Generate_TypedArrayPrototypeLength(
    CodeStubAssembler* assembler) {
  Generate_TypedArrayProtoypeGetter(assembler,
                                    "get TypedArray.prototype.length",
                                    JSTypedArray::kLengthOffset);
}

// -----------------------------------------------------------------------------
// ES6 section 20.3 Date Objects

namespace {

// ES6 section 20.3.1.1 Time Values and Time Range
const double kMinYear = -1000000.0;
const double kMaxYear = -kMinYear;
const double kMinMonth = -10000000.0;
const double kMaxMonth = -kMinMonth;

// 20.3.1.2 Day Number and Time within Day
const double kMsPerDay = 86400000.0;

// ES6 section 20.3.1.11 Hours, Minutes, Second, and Milliseconds
const double kMsPerSecond = 1000.0;
const double kMsPerMinute = 60000.0;
const double kMsPerHour = 3600000.0;

// ES6 section 20.3.1.14 MakeDate (day, time)
double MakeDate(double day, double time) {
  if (std::isfinite(day) && std::isfinite(time)) {
    return time + day * kMsPerDay;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

// ES6 section 20.3.1.13 MakeDay (year, month, date)
double MakeDay(double year, double month, double date) {
  if ((kMinYear <= year && year <= kMaxYear) &&
      (kMinMonth <= month && month <= kMaxMonth) && std::isfinite(date)) {
    int y = FastD2I(year);
    int m = FastD2I(month);
    y += m / 12;
    m %= 12;
    if (m < 0) {
      m += 12;
      y -= 1;
    }
    DCHECK_LE(0, m);
    DCHECK_LT(m, 12);

    // kYearDelta is an arbitrary number such that:
    // a) kYearDelta = -1 (mod 400)
    // b) year + kYearDelta > 0 for years in the range defined by
    //    ECMA 262 - 15.9.1.1, i.e. upto 100,000,000 days on either side of
    //    Jan 1 1970. This is required so that we don't run into integer
    //    division of negative numbers.
    // c) there shouldn't be an overflow for 32-bit integers in the following
    //    operations.
    static const int kYearDelta = 399999;
    static const int kBaseDay =
        365 * (1970 + kYearDelta) + (1970 + kYearDelta) / 4 -
        (1970 + kYearDelta) / 100 + (1970 + kYearDelta) / 400;
    int day_from_year = 365 * (y + kYearDelta) + (y + kYearDelta) / 4 -
                        (y + kYearDelta) / 100 + (y + kYearDelta) / 400 -
                        kBaseDay;
    if ((y % 4 != 0) || (y % 100 == 0 && y % 400 != 0)) {
      static const int kDayFromMonth[] = {0,   31,  59,  90,  120, 151,
                                          181, 212, 243, 273, 304, 334};
      day_from_year += kDayFromMonth[m];
    } else {
      static const int kDayFromMonth[] = {0,   31,  60,  91,  121, 152,
                                          182, 213, 244, 274, 305, 335};
      day_from_year += kDayFromMonth[m];
    }
    return static_cast<double>(day_from_year - 1) + date;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

// ES6 section 20.3.1.12 MakeTime (hour, min, sec, ms)
double MakeTime(double hour, double min, double sec, double ms) {
  if (std::isfinite(hour) && std::isfinite(min) && std::isfinite(sec) &&
      std::isfinite(ms)) {
    double const h = DoubleToInteger(hour);
    double const m = DoubleToInteger(min);
    double const s = DoubleToInteger(sec);
    double const milli = DoubleToInteger(ms);
    return h * kMsPerHour + m * kMsPerMinute + s * kMsPerSecond + milli;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

// ES6 section 20.3.1.15 TimeClip (time)
double TimeClip(double time) {
  if (-DateCache::kMaxTimeInMs <= time && time <= DateCache::kMaxTimeInMs) {
    return DoubleToInteger(time) + 0.0;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

const char* kShortWeekDays[] = {"Sun", "Mon", "Tue", "Wed",
                                "Thu", "Fri", "Sat"};
const char* kShortMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// ES6 section 20.3.1.16 Date Time String Format
double ParseDateTimeString(Handle<String> str) {
  Isolate* const isolate = str->GetIsolate();
  str = String::Flatten(str);
  // TODO(bmeurer): Change DateParser to not use the FixedArray.
  Handle<FixedArray> tmp =
      isolate->factory()->NewFixedArray(DateParser::OUTPUT_SIZE);
  DisallowHeapAllocation no_gc;
  String::FlatContent str_content = str->GetFlatContent();
  bool result;
  if (str_content.IsOneByte()) {
    result = DateParser::Parse(isolate, str_content.ToOneByteVector(), *tmp);
  } else {
    result = DateParser::Parse(isolate, str_content.ToUC16Vector(), *tmp);
  }
  if (!result) return std::numeric_limits<double>::quiet_NaN();
  double const day = MakeDay(tmp->get(0)->Number(), tmp->get(1)->Number(),
                             tmp->get(2)->Number());
  double const time = MakeTime(tmp->get(3)->Number(), tmp->get(4)->Number(),
                               tmp->get(5)->Number(), tmp->get(6)->Number());
  double date = MakeDate(day, time);
  if (tmp->get(7)->IsNull(isolate)) {
    if (!std::isnan(date)) {
      date = isolate->date_cache()->ToUTC(static_cast<int64_t>(date));
    }
  } else {
    date -= tmp->get(7)->Number() * 1000.0;
  }
  return date;
}

enum ToDateStringMode { kDateOnly, kTimeOnly, kDateAndTime };

// ES6 section 20.3.4.41.1 ToDateString(tv)
void ToDateString(double time_val, Vector<char> str, DateCache* date_cache,
                  ToDateStringMode mode = kDateAndTime) {
  if (std::isnan(time_val)) {
    SNPrintF(str, "Invalid Date");
    return;
  }
  int64_t time_ms = static_cast<int64_t>(time_val);
  int64_t local_time_ms = date_cache->ToLocal(time_ms);
  int year, month, day, weekday, hour, min, sec, ms;
  date_cache->BreakDownTime(local_time_ms, &year, &month, &day, &weekday, &hour,
                            &min, &sec, &ms);
  int timezone_offset = -date_cache->TimezoneOffset(time_ms);
  int timezone_hour = std::abs(timezone_offset) / 60;
  int timezone_min = std::abs(timezone_offset) % 60;
  const char* local_timezone = date_cache->LocalTimezone(time_ms);
  switch (mode) {
    case kDateOnly:
      SNPrintF(str, "%s %s %02d %4d", kShortWeekDays[weekday],
               kShortMonths[month], day, year);
      return;
    case kTimeOnly:
      SNPrintF(str, "%02d:%02d:%02d GMT%c%02d%02d (%s)", hour, min, sec,
               (timezone_offset < 0) ? '-' : '+', timezone_hour, timezone_min,
               local_timezone);
      return;
    case kDateAndTime:
      SNPrintF(str, "%s %s %02d %4d %02d:%02d:%02d GMT%c%02d%02d (%s)",
               kShortWeekDays[weekday], kShortMonths[month], day, year, hour,
               min, sec, (timezone_offset < 0) ? '-' : '+', timezone_hour,
               timezone_min, local_timezone);
      return;
  }
  UNREACHABLE();
}

Object* SetLocalDateValue(Handle<JSDate> date, double time_val) {
  if (time_val >= -DateCache::kMaxTimeBeforeUTCInMs &&
      time_val <= DateCache::kMaxTimeBeforeUTCInMs) {
    Isolate* const isolate = date->GetIsolate();
    time_val = isolate->date_cache()->ToUTC(static_cast<int64_t>(time_val));
  } else {
    time_val = std::numeric_limits<double>::quiet_NaN();
  }
  return *JSDate::SetValue(date, TimeClip(time_val));
}

}  // namespace

// ES6 section 20.3.2 The Date Constructor for the [[Call]] case.
BUILTIN(DateConstructor) {
  HandleScope scope(isolate);
  double const time_val = JSDate::CurrentTimeValue(isolate);
  char buffer[128];
  ToDateString(time_val, ArrayVector(buffer), isolate->date_cache());
  RETURN_RESULT_OR_FAILURE(
      isolate, isolate->factory()->NewStringFromUtf8(CStrVector(buffer)));
}

// ES6 section 20.3.2 The Date Constructor for the [[Construct]] case.
BUILTIN(DateConstructor_ConstructStub) {
  HandleScope scope(isolate);
  int const argc = args.length() - 1;
  Handle<JSFunction> target = args.target<JSFunction>();
  Handle<JSReceiver> new_target = Handle<JSReceiver>::cast(args.new_target());
  double time_val;
  if (argc == 0) {
    time_val = JSDate::CurrentTimeValue(isolate);
  } else if (argc == 1) {
    Handle<Object> value = args.at<Object>(1);
    if (value->IsJSDate()) {
      time_val = Handle<JSDate>::cast(value)->value()->Number();
    } else {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value,
                                         Object::ToPrimitive(value));
      if (value->IsString()) {
        time_val = ParseDateTimeString(Handle<String>::cast(value));
      } else {
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value,
                                           Object::ToNumber(value));
        time_val = value->Number();
      }
    }
  } else {
    Handle<Object> year_object;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, year_object,
                                       Object::ToNumber(args.at<Object>(1)));
    Handle<Object> month_object;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, month_object,
                                       Object::ToNumber(args.at<Object>(2)));
    double year = year_object->Number();
    double month = month_object->Number();
    double date = 1.0, hours = 0.0, minutes = 0.0, seconds = 0.0, ms = 0.0;
    if (argc >= 3) {
      Handle<Object> date_object;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, date_object,
                                         Object::ToNumber(args.at<Object>(3)));
      date = date_object->Number();
      if (argc >= 4) {
        Handle<Object> hours_object;
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
            isolate, hours_object, Object::ToNumber(args.at<Object>(4)));
        hours = hours_object->Number();
        if (argc >= 5) {
          Handle<Object> minutes_object;
          ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
              isolate, minutes_object, Object::ToNumber(args.at<Object>(5)));
          minutes = minutes_object->Number();
          if (argc >= 6) {
            Handle<Object> seconds_object;
            ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
                isolate, seconds_object, Object::ToNumber(args.at<Object>(6)));
            seconds = seconds_object->Number();
            if (argc >= 7) {
              Handle<Object> ms_object;
              ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
                  isolate, ms_object, Object::ToNumber(args.at<Object>(7)));
              ms = ms_object->Number();
            }
          }
        }
      }
    }
    if (!std::isnan(year)) {
      double const y = DoubleToInteger(year);
      if (0.0 <= y && y <= 99) year = 1900 + y;
    }
    double const day = MakeDay(year, month, date);
    double const time = MakeTime(hours, minutes, seconds, ms);
    time_val = MakeDate(day, time);
    if (time_val >= -DateCache::kMaxTimeBeforeUTCInMs &&
        time_val <= DateCache::kMaxTimeBeforeUTCInMs) {
      time_val = isolate->date_cache()->ToUTC(static_cast<int64_t>(time_val));
    } else {
      time_val = std::numeric_limits<double>::quiet_NaN();
    }
  }
  RETURN_RESULT_OR_FAILURE(isolate, JSDate::New(target, new_target, time_val));
}

// ES6 section 20.3.3.1 Date.now ( )
BUILTIN(DateNow) {
  HandleScope scope(isolate);
  return *isolate->factory()->NewNumber(JSDate::CurrentTimeValue(isolate));
}

// ES6 section 20.3.3.2 Date.parse ( string )
BUILTIN(DateParse) {
  HandleScope scope(isolate);
  Handle<String> string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, string,
      Object::ToString(isolate, args.atOrUndefined(isolate, 1)));
  return *isolate->factory()->NewNumber(ParseDateTimeString(string));
}

// ES6 section 20.3.3.4 Date.UTC (year,month,date,hours,minutes,seconds,ms)
BUILTIN(DateUTC) {
  HandleScope scope(isolate);
  int const argc = args.length() - 1;
  double year = std::numeric_limits<double>::quiet_NaN();
  double month = std::numeric_limits<double>::quiet_NaN();
  double date = 1.0, hours = 0.0, minutes = 0.0, seconds = 0.0, ms = 0.0;
  if (argc >= 1) {
    Handle<Object> year_object;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, year_object,
                                       Object::ToNumber(args.at<Object>(1)));
    year = year_object->Number();
    if (argc >= 2) {
      Handle<Object> month_object;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, month_object,
                                         Object::ToNumber(args.at<Object>(2)));
      month = month_object->Number();
      if (argc >= 3) {
        Handle<Object> date_object;
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
            isolate, date_object, Object::ToNumber(args.at<Object>(3)));
        date = date_object->Number();
        if (argc >= 4) {
          Handle<Object> hours_object;
          ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
              isolate, hours_object, Object::ToNumber(args.at<Object>(4)));
          hours = hours_object->Number();
          if (argc >= 5) {
            Handle<Object> minutes_object;
            ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
                isolate, minutes_object, Object::ToNumber(args.at<Object>(5)));
            minutes = minutes_object->Number();
            if (argc >= 6) {
              Handle<Object> seconds_object;
              ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
                  isolate, seconds_object,
                  Object::ToNumber(args.at<Object>(6)));
              seconds = seconds_object->Number();
              if (argc >= 7) {
                Handle<Object> ms_object;
                ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
                    isolate, ms_object, Object::ToNumber(args.at<Object>(7)));
                ms = ms_object->Number();
              }
            }
          }
        }
      }
    }
  }
  if (!std::isnan(year)) {
    double const y = DoubleToInteger(year);
    if (0.0 <= y && y <= 99) year = 1900 + y;
  }
  double const day = MakeDay(year, month, date);
  double const time = MakeTime(hours, minutes, seconds, ms);
  return *isolate->factory()->NewNumber(TimeClip(MakeDate(day, time)));
}

// ES6 section 20.3.4.20 Date.prototype.setDate ( date )
BUILTIN(DatePrototypeSetDate) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setDate");
  Handle<Object> value = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value, Object::ToNumber(value));
  double time_val = date->value()->Number();
  if (!std::isnan(time_val)) {
    int64_t const time_ms = static_cast<int64_t>(time_val);
    int64_t local_time_ms = isolate->date_cache()->ToLocal(time_ms);
    int const days = isolate->date_cache()->DaysFromTime(local_time_ms);
    int time_within_day = isolate->date_cache()->TimeInDay(local_time_ms, days);
    int year, month, day;
    isolate->date_cache()->YearMonthDayFromDays(days, &year, &month, &day);
    time_val = MakeDate(MakeDay(year, month, value->Number()), time_within_day);
  }
  return SetLocalDateValue(date, time_val);
}

// ES6 section 20.3.4.21 Date.prototype.setFullYear (year, month, date)
BUILTIN(DatePrototypeSetFullYear) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setFullYear");
  int const argc = args.length() - 1;
  Handle<Object> year = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, year, Object::ToNumber(year));
  double y = year->Number(), m = 0.0, dt = 1.0;
  int time_within_day = 0;
  if (!std::isnan(date->value()->Number())) {
    int64_t const time_ms = static_cast<int64_t>(date->value()->Number());
    int64_t local_time_ms = isolate->date_cache()->ToLocal(time_ms);
    int const days = isolate->date_cache()->DaysFromTime(local_time_ms);
    time_within_day = isolate->date_cache()->TimeInDay(local_time_ms, days);
    int year, month, day;
    isolate->date_cache()->YearMonthDayFromDays(days, &year, &month, &day);
    m = month;
    dt = day;
  }
  if (argc >= 2) {
    Handle<Object> month = args.at<Object>(2);
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, month, Object::ToNumber(month));
    m = month->Number();
    if (argc >= 3) {
      Handle<Object> date = args.at<Object>(3);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, date, Object::ToNumber(date));
      dt = date->Number();
    }
  }
  double time_val = MakeDate(MakeDay(y, m, dt), time_within_day);
  return SetLocalDateValue(date, time_val);
}

// ES6 section 20.3.4.22 Date.prototype.setHours(hour, min, sec, ms)
BUILTIN(DatePrototypeSetHours) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setHours");
  int const argc = args.length() - 1;
  Handle<Object> hour = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, hour, Object::ToNumber(hour));
  double h = hour->Number();
  double time_val = date->value()->Number();
  if (!std::isnan(time_val)) {
    int64_t const time_ms = static_cast<int64_t>(time_val);
    int64_t local_time_ms = isolate->date_cache()->ToLocal(time_ms);
    int day = isolate->date_cache()->DaysFromTime(local_time_ms);
    int time_within_day = isolate->date_cache()->TimeInDay(local_time_ms, day);
    double m = (time_within_day / (60 * 1000)) % 60;
    double s = (time_within_day / 1000) % 60;
    double milli = time_within_day % 1000;
    if (argc >= 2) {
      Handle<Object> min = args.at<Object>(2);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, min, Object::ToNumber(min));
      m = min->Number();
      if (argc >= 3) {
        Handle<Object> sec = args.at<Object>(3);
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, sec, Object::ToNumber(sec));
        s = sec->Number();
        if (argc >= 4) {
          Handle<Object> ms = args.at<Object>(4);
          ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, ms, Object::ToNumber(ms));
          milli = ms->Number();
        }
      }
    }
    time_val = MakeDate(day, MakeTime(h, m, s, milli));
  }
  return SetLocalDateValue(date, time_val);
}

// ES6 section 20.3.4.23 Date.prototype.setMilliseconds(ms)
BUILTIN(DatePrototypeSetMilliseconds) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setMilliseconds");
  Handle<Object> ms = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, ms, Object::ToNumber(ms));
  double time_val = date->value()->Number();
  if (!std::isnan(time_val)) {
    int64_t const time_ms = static_cast<int64_t>(time_val);
    int64_t local_time_ms = isolate->date_cache()->ToLocal(time_ms);
    int day = isolate->date_cache()->DaysFromTime(local_time_ms);
    int time_within_day = isolate->date_cache()->TimeInDay(local_time_ms, day);
    int h = time_within_day / (60 * 60 * 1000);
    int m = (time_within_day / (60 * 1000)) % 60;
    int s = (time_within_day / 1000) % 60;
    time_val = MakeDate(day, MakeTime(h, m, s, ms->Number()));
  }
  return SetLocalDateValue(date, time_val);
}

// ES6 section 20.3.4.24 Date.prototype.setMinutes ( min, sec, ms )
BUILTIN(DatePrototypeSetMinutes) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setMinutes");
  int const argc = args.length() - 1;
  Handle<Object> min = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, min, Object::ToNumber(min));
  double time_val = date->value()->Number();
  if (!std::isnan(time_val)) {
    int64_t const time_ms = static_cast<int64_t>(time_val);
    int64_t local_time_ms = isolate->date_cache()->ToLocal(time_ms);
    int day = isolate->date_cache()->DaysFromTime(local_time_ms);
    int time_within_day = isolate->date_cache()->TimeInDay(local_time_ms, day);
    int h = time_within_day / (60 * 60 * 1000);
    double m = min->Number();
    double s = (time_within_day / 1000) % 60;
    double milli = time_within_day % 1000;
    if (argc >= 2) {
      Handle<Object> sec = args.at<Object>(2);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, sec, Object::ToNumber(sec));
      s = sec->Number();
      if (argc >= 3) {
        Handle<Object> ms = args.at<Object>(3);
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, ms, Object::ToNumber(ms));
        milli = ms->Number();
      }
    }
    time_val = MakeDate(day, MakeTime(h, m, s, milli));
  }
  return SetLocalDateValue(date, time_val);
}

// ES6 section 20.3.4.25 Date.prototype.setMonth ( month, date )
BUILTIN(DatePrototypeSetMonth) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setMonth");
  int const argc = args.length() - 1;
  Handle<Object> month = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, month, Object::ToNumber(month));
  double time_val = date->value()->Number();
  if (!std::isnan(time_val)) {
    int64_t const time_ms = static_cast<int64_t>(time_val);
    int64_t local_time_ms = isolate->date_cache()->ToLocal(time_ms);
    int days = isolate->date_cache()->DaysFromTime(local_time_ms);
    int time_within_day = isolate->date_cache()->TimeInDay(local_time_ms, days);
    int year, unused, day;
    isolate->date_cache()->YearMonthDayFromDays(days, &year, &unused, &day);
    double m = month->Number();
    double dt = day;
    if (argc >= 2) {
      Handle<Object> date = args.at<Object>(2);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, date, Object::ToNumber(date));
      dt = date->Number();
    }
    time_val = MakeDate(MakeDay(year, m, dt), time_within_day);
  }
  return SetLocalDateValue(date, time_val);
}

// ES6 section 20.3.4.26 Date.prototype.setSeconds ( sec, ms )
BUILTIN(DatePrototypeSetSeconds) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setSeconds");
  int const argc = args.length() - 1;
  Handle<Object> sec = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, sec, Object::ToNumber(sec));
  double time_val = date->value()->Number();
  if (!std::isnan(time_val)) {
    int64_t const time_ms = static_cast<int64_t>(time_val);
    int64_t local_time_ms = isolate->date_cache()->ToLocal(time_ms);
    int day = isolate->date_cache()->DaysFromTime(local_time_ms);
    int time_within_day = isolate->date_cache()->TimeInDay(local_time_ms, day);
    int h = time_within_day / (60 * 60 * 1000);
    double m = (time_within_day / (60 * 1000)) % 60;
    double s = sec->Number();
    double milli = time_within_day % 1000;
    if (argc >= 2) {
      Handle<Object> ms = args.at<Object>(2);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, ms, Object::ToNumber(ms));
      milli = ms->Number();
    }
    time_val = MakeDate(day, MakeTime(h, m, s, milli));
  }
  return SetLocalDateValue(date, time_val);
}

// ES6 section 20.3.4.27 Date.prototype.setTime ( time )
BUILTIN(DatePrototypeSetTime) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setTime");
  Handle<Object> value = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value, Object::ToNumber(value));
  return *JSDate::SetValue(date, TimeClip(value->Number()));
}

// ES6 section 20.3.4.28 Date.prototype.setUTCDate ( date )
BUILTIN(DatePrototypeSetUTCDate) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setUTCDate");
  Handle<Object> value = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value, Object::ToNumber(value));
  if (std::isnan(date->value()->Number())) return date->value();
  int64_t const time_ms = static_cast<int64_t>(date->value()->Number());
  int const days = isolate->date_cache()->DaysFromTime(time_ms);
  int const time_within_day = isolate->date_cache()->TimeInDay(time_ms, days);
  int year, month, day;
  isolate->date_cache()->YearMonthDayFromDays(days, &year, &month, &day);
  double const time_val =
      MakeDate(MakeDay(year, month, value->Number()), time_within_day);
  return *JSDate::SetValue(date, TimeClip(time_val));
}

// ES6 section 20.3.4.29 Date.prototype.setUTCFullYear (year, month, date)
BUILTIN(DatePrototypeSetUTCFullYear) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setUTCFullYear");
  int const argc = args.length() - 1;
  Handle<Object> year = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, year, Object::ToNumber(year));
  double y = year->Number(), m = 0.0, dt = 1.0;
  int time_within_day = 0;
  if (!std::isnan(date->value()->Number())) {
    int64_t const time_ms = static_cast<int64_t>(date->value()->Number());
    int const days = isolate->date_cache()->DaysFromTime(time_ms);
    time_within_day = isolate->date_cache()->TimeInDay(time_ms, days);
    int year, month, day;
    isolate->date_cache()->YearMonthDayFromDays(days, &year, &month, &day);
    m = month;
    dt = day;
  }
  if (argc >= 2) {
    Handle<Object> month = args.at<Object>(2);
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, month, Object::ToNumber(month));
    m = month->Number();
    if (argc >= 3) {
      Handle<Object> date = args.at<Object>(3);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, date, Object::ToNumber(date));
      dt = date->Number();
    }
  }
  double const time_val = MakeDate(MakeDay(y, m, dt), time_within_day);
  return *JSDate::SetValue(date, TimeClip(time_val));
}

// ES6 section 20.3.4.30 Date.prototype.setUTCHours(hour, min, sec, ms)
BUILTIN(DatePrototypeSetUTCHours) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setUTCHours");
  int const argc = args.length() - 1;
  Handle<Object> hour = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, hour, Object::ToNumber(hour));
  double h = hour->Number();
  double time_val = date->value()->Number();
  if (!std::isnan(time_val)) {
    int64_t const time_ms = static_cast<int64_t>(time_val);
    int day = isolate->date_cache()->DaysFromTime(time_ms);
    int time_within_day = isolate->date_cache()->TimeInDay(time_ms, day);
    double m = (time_within_day / (60 * 1000)) % 60;
    double s = (time_within_day / 1000) % 60;
    double milli = time_within_day % 1000;
    if (argc >= 2) {
      Handle<Object> min = args.at<Object>(2);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, min, Object::ToNumber(min));
      m = min->Number();
      if (argc >= 3) {
        Handle<Object> sec = args.at<Object>(3);
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, sec, Object::ToNumber(sec));
        s = sec->Number();
        if (argc >= 4) {
          Handle<Object> ms = args.at<Object>(4);
          ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, ms, Object::ToNumber(ms));
          milli = ms->Number();
        }
      }
    }
    time_val = MakeDate(day, MakeTime(h, m, s, milli));
  }
  return *JSDate::SetValue(date, TimeClip(time_val));
}

// ES6 section 20.3.4.31 Date.prototype.setUTCMilliseconds(ms)
BUILTIN(DatePrototypeSetUTCMilliseconds) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setUTCMilliseconds");
  Handle<Object> ms = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, ms, Object::ToNumber(ms));
  double time_val = date->value()->Number();
  if (!std::isnan(time_val)) {
    int64_t const time_ms = static_cast<int64_t>(time_val);
    int day = isolate->date_cache()->DaysFromTime(time_ms);
    int time_within_day = isolate->date_cache()->TimeInDay(time_ms, day);
    int h = time_within_day / (60 * 60 * 1000);
    int m = (time_within_day / (60 * 1000)) % 60;
    int s = (time_within_day / 1000) % 60;
    time_val = MakeDate(day, MakeTime(h, m, s, ms->Number()));
  }
  return *JSDate::SetValue(date, TimeClip(time_val));
}

// ES6 section 20.3.4.32 Date.prototype.setUTCMinutes ( min, sec, ms )
BUILTIN(DatePrototypeSetUTCMinutes) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setUTCMinutes");
  int const argc = args.length() - 1;
  Handle<Object> min = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, min, Object::ToNumber(min));
  double time_val = date->value()->Number();
  if (!std::isnan(time_val)) {
    int64_t const time_ms = static_cast<int64_t>(time_val);
    int day = isolate->date_cache()->DaysFromTime(time_ms);
    int time_within_day = isolate->date_cache()->TimeInDay(time_ms, day);
    int h = time_within_day / (60 * 60 * 1000);
    double m = min->Number();
    double s = (time_within_day / 1000) % 60;
    double milli = time_within_day % 1000;
    if (argc >= 2) {
      Handle<Object> sec = args.at<Object>(2);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, sec, Object::ToNumber(sec));
      s = sec->Number();
      if (argc >= 3) {
        Handle<Object> ms = args.at<Object>(3);
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, ms, Object::ToNumber(ms));
        milli = ms->Number();
      }
    }
    time_val = MakeDate(day, MakeTime(h, m, s, milli));
  }
  return *JSDate::SetValue(date, TimeClip(time_val));
}

// ES6 section 20.3.4.31 Date.prototype.setUTCMonth ( month, date )
BUILTIN(DatePrototypeSetUTCMonth) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setUTCMonth");
  int const argc = args.length() - 1;
  Handle<Object> month = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, month, Object::ToNumber(month));
  double time_val = date->value()->Number();
  if (!std::isnan(time_val)) {
    int64_t const time_ms = static_cast<int64_t>(time_val);
    int days = isolate->date_cache()->DaysFromTime(time_ms);
    int time_within_day = isolate->date_cache()->TimeInDay(time_ms, days);
    int year, unused, day;
    isolate->date_cache()->YearMonthDayFromDays(days, &year, &unused, &day);
    double m = month->Number();
    double dt = day;
    if (argc >= 2) {
      Handle<Object> date = args.at<Object>(2);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, date, Object::ToNumber(date));
      dt = date->Number();
    }
    time_val = MakeDate(MakeDay(year, m, dt), time_within_day);
  }
  return *JSDate::SetValue(date, TimeClip(time_val));
}

// ES6 section 20.3.4.34 Date.prototype.setUTCSeconds ( sec, ms )
BUILTIN(DatePrototypeSetUTCSeconds) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setUTCSeconds");
  int const argc = args.length() - 1;
  Handle<Object> sec = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, sec, Object::ToNumber(sec));
  double time_val = date->value()->Number();
  if (!std::isnan(time_val)) {
    int64_t const time_ms = static_cast<int64_t>(time_val);
    int day = isolate->date_cache()->DaysFromTime(time_ms);
    int time_within_day = isolate->date_cache()->TimeInDay(time_ms, day);
    int h = time_within_day / (60 * 60 * 1000);
    double m = (time_within_day / (60 * 1000)) % 60;
    double s = sec->Number();
    double milli = time_within_day % 1000;
    if (argc >= 2) {
      Handle<Object> ms = args.at<Object>(2);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, ms, Object::ToNumber(ms));
      milli = ms->Number();
    }
    time_val = MakeDate(day, MakeTime(h, m, s, milli));
  }
  return *JSDate::SetValue(date, TimeClip(time_val));
}

// ES6 section 20.3.4.35 Date.prototype.toDateString ( )
BUILTIN(DatePrototypeToDateString) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.toDateString");
  char buffer[128];
  ToDateString(date->value()->Number(), ArrayVector(buffer),
               isolate->date_cache(), kDateOnly);
  RETURN_RESULT_OR_FAILURE(
      isolate, isolate->factory()->NewStringFromUtf8(CStrVector(buffer)));
}

// ES6 section 20.3.4.36 Date.prototype.toISOString ( )
BUILTIN(DatePrototypeToISOString) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.toISOString");
  double const time_val = date->value()->Number();
  if (std::isnan(time_val)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kInvalidTimeValue));
  }
  int64_t const time_ms = static_cast<int64_t>(time_val);
  int year, month, day, weekday, hour, min, sec, ms;
  isolate->date_cache()->BreakDownTime(time_ms, &year, &month, &day, &weekday,
                                       &hour, &min, &sec, &ms);
  char buffer[128];
  if (year >= 0 && year <= 9999) {
    SNPrintF(ArrayVector(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", year,
             month + 1, day, hour, min, sec, ms);
  } else if (year < 0) {
    SNPrintF(ArrayVector(buffer), "-%06d-%02d-%02dT%02d:%02d:%02d.%03dZ", -year,
             month + 1, day, hour, min, sec, ms);
  } else {
    SNPrintF(ArrayVector(buffer), "+%06d-%02d-%02dT%02d:%02d:%02d.%03dZ", year,
             month + 1, day, hour, min, sec, ms);
  }
  return *isolate->factory()->NewStringFromAsciiChecked(buffer);
}

// ES6 section 20.3.4.41 Date.prototype.toString ( )
BUILTIN(DatePrototypeToString) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.toString");
  char buffer[128];
  ToDateString(date->value()->Number(), ArrayVector(buffer),
               isolate->date_cache());
  RETURN_RESULT_OR_FAILURE(
      isolate, isolate->factory()->NewStringFromUtf8(CStrVector(buffer)));
}

// ES6 section 20.3.4.42 Date.prototype.toTimeString ( )
BUILTIN(DatePrototypeToTimeString) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.toTimeString");
  char buffer[128];
  ToDateString(date->value()->Number(), ArrayVector(buffer),
               isolate->date_cache(), kTimeOnly);
  RETURN_RESULT_OR_FAILURE(
      isolate, isolate->factory()->NewStringFromUtf8(CStrVector(buffer)));
}

// ES6 section 20.3.4.43 Date.prototype.toUTCString ( )
BUILTIN(DatePrototypeToUTCString) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.toUTCString");
  double const time_val = date->value()->Number();
  if (std::isnan(time_val)) {
    return *isolate->factory()->NewStringFromAsciiChecked("Invalid Date");
  }
  char buffer[128];
  int64_t time_ms = static_cast<int64_t>(time_val);
  int year, month, day, weekday, hour, min, sec, ms;
  isolate->date_cache()->BreakDownTime(time_ms, &year, &month, &day, &weekday,
                                       &hour, &min, &sec, &ms);
  SNPrintF(ArrayVector(buffer), "%s, %02d %s %4d %02d:%02d:%02d GMT",
           kShortWeekDays[weekday], day, kShortMonths[month], year, hour, min,
           sec);
  return *isolate->factory()->NewStringFromAsciiChecked(buffer);
}

// ES6 section 20.3.4.44 Date.prototype.valueOf ( )
BUILTIN(DatePrototypeValueOf) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.valueOf");
  return date->value();
}

// ES6 section 20.3.4.45 Date.prototype [ @@toPrimitive ] ( hint )
BUILTIN(DatePrototypeToPrimitive) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  CHECK_RECEIVER(JSReceiver, receiver, "Date.prototype [ @@toPrimitive ]");
  Handle<Object> hint = args.at<Object>(1);
  RETURN_RESULT_OR_FAILURE(isolate, JSDate::ToPrimitive(receiver, hint));
}

// ES6 section B.2.4.1 Date.prototype.getYear ( )
BUILTIN(DatePrototypeGetYear) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.getYear");
  double time_val = date->value()->Number();
  if (std::isnan(time_val)) return date->value();
  int64_t time_ms = static_cast<int64_t>(time_val);
  int64_t local_time_ms = isolate->date_cache()->ToLocal(time_ms);
  int days = isolate->date_cache()->DaysFromTime(local_time_ms);
  int year, month, day;
  isolate->date_cache()->YearMonthDayFromDays(days, &year, &month, &day);
  return Smi::FromInt(year - 1900);
}

// ES6 section B.2.4.2 Date.prototype.setYear ( year )
BUILTIN(DatePrototypeSetYear) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDate, date, "Date.prototype.setYear");
  Handle<Object> year = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, year, Object::ToNumber(year));
  double m = 0.0, dt = 1.0, y = year->Number();
  if (0.0 <= y && y <= 99.0) {
    y = 1900.0 + DoubleToInteger(y);
  }
  int time_within_day = 0;
  if (!std::isnan(date->value()->Number())) {
    int64_t const time_ms = static_cast<int64_t>(date->value()->Number());
    int64_t local_time_ms = isolate->date_cache()->ToLocal(time_ms);
    int const days = isolate->date_cache()->DaysFromTime(local_time_ms);
    time_within_day = isolate->date_cache()->TimeInDay(local_time_ms, days);
    int year, month, day;
    isolate->date_cache()->YearMonthDayFromDays(days, &year, &month, &day);
    m = month;
    dt = day;
  }
  double time_val = MakeDate(MakeDay(y, m, dt), time_within_day);
  return SetLocalDateValue(date, time_val);
}

// ES6 section 20.3.4.37 Date.prototype.toJSON ( key )
BUILTIN(DatePrototypeToJson) {
  HandleScope scope(isolate);
  Handle<Object> receiver = args.atOrUndefined(isolate, 0);
  Handle<JSReceiver> receiver_obj;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver_obj,
                                     Object::ToObject(isolate, receiver));
  Handle<Object> primitive;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, primitive,
      Object::ToPrimitive(receiver_obj, ToPrimitiveHint::kNumber));
  if (primitive->IsNumber() && !std::isfinite(primitive->Number())) {
    return isolate->heap()->null_value();
  } else {
    Handle<String> name =
        isolate->factory()->NewStringFromAsciiChecked("toISOString");
    Handle<Object> function;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, function,
                                       Object::GetProperty(receiver_obj, name));
    if (!function->IsCallable()) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewTypeError(MessageTemplate::kCalledNonCallable, name));
    }
    RETURN_RESULT_OR_FAILURE(
        isolate, Execution::Call(isolate, function, receiver_obj, 0, NULL));
  }
}

// static
void Builtins::Generate_DatePrototypeGetDate(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kDay);
}

// static
void Builtins::Generate_DatePrototypeGetDay(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kWeekday);
}

// static
void Builtins::Generate_DatePrototypeGetFullYear(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kYear);
}

// static
void Builtins::Generate_DatePrototypeGetHours(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kHour);
}

// static
void Builtins::Generate_DatePrototypeGetMilliseconds(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kMillisecond);
}

// static
void Builtins::Generate_DatePrototypeGetMinutes(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kMinute);
}

// static
void Builtins::Generate_DatePrototypeGetMonth(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kMonth);
}

// static
void Builtins::Generate_DatePrototypeGetSeconds(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kSecond);
}

// static
void Builtins::Generate_DatePrototypeGetTime(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kDateValue);
}

// static
void Builtins::Generate_DatePrototypeGetTimezoneOffset(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kTimezoneOffset);
}

// static
void Builtins::Generate_DatePrototypeGetUTCDate(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kDayUTC);
}

// static
void Builtins::Generate_DatePrototypeGetUTCDay(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kWeekdayUTC);
}

// static
void Builtins::Generate_DatePrototypeGetUTCFullYear(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kYearUTC);
}

// static
void Builtins::Generate_DatePrototypeGetUTCHours(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kHourUTC);
}

// static
void Builtins::Generate_DatePrototypeGetUTCMilliseconds(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kMillisecondUTC);
}

// static
void Builtins::Generate_DatePrototypeGetUTCMinutes(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kMinuteUTC);
}

// static
void Builtins::Generate_DatePrototypeGetUTCMonth(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kMonthUTC);
}

// static
void Builtins::Generate_DatePrototypeGetUTCSeconds(MacroAssembler* masm) {
  Generate_DatePrototype_GetField(masm, JSDate::kSecondUTC);
}

namespace {

bool AllowDynamicFunction(Isolate* isolate, Handle<JSFunction> target,
                          Handle<JSObject> target_global_proxy) {
  if (FLAG_allow_unsafe_function_constructor) return true;
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  Handle<Context> responsible_context = impl->LastEnteredContext();
  if (responsible_context.is_null()) {
    responsible_context = impl->MicrotaskContext();
    // TODO(jochen): Remove this.
    if (responsible_context.is_null()) {
      return true;
    }
  }
  if (*responsible_context == target->context()) return true;
  return isolate->MayAccess(responsible_context, target_global_proxy);
}

// ES6 section 19.2.1.1.1 CreateDynamicFunction
MaybeHandle<Object> CreateDynamicFunction(Isolate* isolate,
                                          BuiltinArguments args,
                                          const char* token) {
  // Compute number of arguments, ignoring the receiver.
  DCHECK_LE(1, args.length());
  int const argc = args.length() - 1;

  Handle<JSFunction> target = args.target<JSFunction>();
  Handle<JSObject> target_global_proxy(target->global_proxy(), isolate);

  if (!AllowDynamicFunction(isolate, target, target_global_proxy)) {
    isolate->CountUsage(v8::Isolate::kFunctionConstructorReturnedUndefined);
    return isolate->factory()->undefined_value();
  }

  // Build the source string.
  Handle<String> source;
  {
    IncrementalStringBuilder builder(isolate);
    builder.AppendCharacter('(');
    builder.AppendCString(token);
    builder.AppendCharacter('(');
    bool parenthesis_in_arg_string = false;
    if (argc > 1) {
      for (int i = 1; i < argc; ++i) {
        if (i > 1) builder.AppendCharacter(',');
        Handle<String> param;
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, param, Object::ToString(isolate, args.at<Object>(i)),
            Object);
        param = String::Flatten(param);
        builder.AppendString(param);
        // If the formal parameters string include ) - an illegal
        // character - it may make the combined function expression
        // compile. We avoid this problem by checking for this early on.
        DisallowHeapAllocation no_gc;  // Ensure vectors stay valid.
        String::FlatContent param_content = param->GetFlatContent();
        for (int i = 0, length = param->length(); i < length; ++i) {
          if (param_content.Get(i) == ')') {
            parenthesis_in_arg_string = true;
            break;
          }
        }
      }
      // If the formal parameters include an unbalanced block comment, the
      // function must be rejected. Since JavaScript does not allow nested
      // comments we can include a trailing block comment to catch this.
      builder.AppendCString("\n/**/");
    }
    builder.AppendCString(") {\n");
    if (argc > 0) {
      Handle<String> body;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, body, Object::ToString(isolate, args.at<Object>(argc)),
          Object);
      builder.AppendString(body);
    }
    builder.AppendCString("\n})");
    ASSIGN_RETURN_ON_EXCEPTION(isolate, source, builder.Finish(), Object);

    // The SyntaxError must be thrown after all the (observable) ToString
    // conversions are done.
    if (parenthesis_in_arg_string) {
      THROW_NEW_ERROR(isolate,
                      NewSyntaxError(MessageTemplate::kParenthesisInArgString),
                      Object);
    }
  }

  // Compile the string in the constructor and not a helper so that errors to
  // come from here.
  Handle<JSFunction> function;
  {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, function,
        CompileString(handle(target->native_context(), isolate), source,
                      ONLY_SINGLE_FUNCTION_LITERAL),
        Object);
    Handle<Object> result;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, result,
        Execution::Call(isolate, function, target_global_proxy, 0, nullptr),
        Object);
    function = Handle<JSFunction>::cast(result);
    function->shared()->set_name_should_print_as_anonymous(true);
  }

  // If new.target is equal to target then the function created
  // is already correctly setup and nothing else should be done
  // here. But if new.target is not equal to target then we are
  // have a Function builtin subclassing case and therefore the
  // function has wrong initial map. To fix that we create a new
  // function object with correct initial map.
  Handle<Object> unchecked_new_target = args.new_target();
  if (!unchecked_new_target->IsUndefined(isolate) &&
      !unchecked_new_target.is_identical_to(target)) {
    Handle<JSReceiver> new_target =
        Handle<JSReceiver>::cast(unchecked_new_target);
    Handle<Map> initial_map;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, initial_map,
        JSFunction::GetDerivedMap(isolate, target, new_target), Object);

    Handle<SharedFunctionInfo> shared_info(function->shared(), isolate);
    Handle<Map> map = Map::AsLanguageMode(
        initial_map, shared_info->language_mode(), shared_info->kind());

    Handle<Context> context(function->context(), isolate);
    function = isolate->factory()->NewFunctionFromSharedFunctionInfo(
        map, shared_info, context, NOT_TENURED);
  }
  return function;
}

}  // namespace

// ES6 section 19.2.1.1 Function ( p1, p2, ... , pn, body )
BUILTIN(FunctionConstructor) {
  HandleScope scope(isolate);
  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result, CreateDynamicFunction(isolate, args, "function"));
  return *result;
}

namespace {

Object* DoFunctionBind(Isolate* isolate, BuiltinArguments args) {
  HandleScope scope(isolate);
  DCHECK_LE(1, args.length());
  if (!args.receiver()->IsCallable()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kFunctionBind));
  }

  // Allocate the bound function with the given {this_arg} and {args}.
  Handle<JSReceiver> target = args.at<JSReceiver>(0);
  Handle<Object> this_arg = isolate->factory()->undefined_value();
  ScopedVector<Handle<Object>> argv(std::max(0, args.length() - 2));
  if (args.length() > 1) {
    this_arg = args.at<Object>(1);
    for (int i = 2; i < args.length(); ++i) {
      argv[i - 2] = args.at<Object>(i);
    }
  }
  Handle<JSBoundFunction> function;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, function,
      isolate->factory()->NewJSBoundFunction(target, this_arg, argv));

  LookupIterator length_lookup(target, isolate->factory()->length_string(),
                               target, LookupIterator::OWN);
  // Setup the "length" property based on the "length" of the {target}.
  // If the targets length is the default JSFunction accessor, we can keep the
  // accessor that's installed by default on the JSBoundFunction. It lazily
  // computes the value from the underlying internal length.
  if (!target->IsJSFunction() ||
      length_lookup.state() != LookupIterator::ACCESSOR ||
      !length_lookup.GetAccessors()->IsAccessorInfo()) {
    Handle<Object> length(Smi::FromInt(0), isolate);
    Maybe<PropertyAttributes> attributes =
        JSReceiver::GetPropertyAttributes(&length_lookup);
    if (!attributes.IsJust()) return isolate->heap()->exception();
    if (attributes.FromJust() != ABSENT) {
      Handle<Object> target_length;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, target_length,
                                         Object::GetProperty(&length_lookup));
      if (target_length->IsNumber()) {
        length = isolate->factory()->NewNumber(std::max(
            0.0, DoubleToInteger(target_length->Number()) - argv.length()));
      }
    }
    LookupIterator it(function, isolate->factory()->length_string(), function);
    DCHECK_EQ(LookupIterator::ACCESSOR, it.state());
    RETURN_FAILURE_ON_EXCEPTION(isolate,
                                JSObject::DefineOwnPropertyIgnoreAttributes(
                                    &it, length, it.property_attributes()));
  }

  // Setup the "name" property based on the "name" of the {target}.
  // If the targets name is the default JSFunction accessor, we can keep the
  // accessor that's installed by default on the JSBoundFunction. It lazily
  // computes the value from the underlying internal name.
  LookupIterator name_lookup(target, isolate->factory()->name_string(), target,
                             LookupIterator::OWN);
  if (!target->IsJSFunction() ||
      name_lookup.state() != LookupIterator::ACCESSOR ||
      !name_lookup.GetAccessors()->IsAccessorInfo()) {
    Handle<Object> target_name;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, target_name,
                                       Object::GetProperty(&name_lookup));
    Handle<String> name;
    if (target_name->IsString()) {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, name,
          Name::ToFunctionName(Handle<String>::cast(target_name)));
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, name, isolate->factory()->NewConsString(
                             isolate->factory()->bound__string(), name));
    } else {
      name = isolate->factory()->bound__string();
    }
    LookupIterator it(function, isolate->factory()->name_string());
    DCHECK_EQ(LookupIterator::ACCESSOR, it.state());
    RETURN_FAILURE_ON_EXCEPTION(isolate,
                                JSObject::DefineOwnPropertyIgnoreAttributes(
                                    &it, name, it.property_attributes()));
  }
  return *function;
}

}  // namespace

// ES6 section 19.2.3.2 Function.prototype.bind ( thisArg, ...args )
BUILTIN(FunctionPrototypeBind) { return DoFunctionBind(isolate, args); }

// TODO(verwaest): This is a temporary helper until the FastFunctionBind stub
// can tailcall to the builtin directly.
RUNTIME_FUNCTION(Runtime_FunctionBind) {
  DCHECK_EQ(2, args.length());
  Arguments* incoming = reinterpret_cast<Arguments*>(args[0]);
  // Rewrap the arguments as builtins arguments.
  int argc = incoming->length() + BuiltinArguments::kNumExtraArgsWithReceiver;
  BuiltinArguments caller_args(argc, incoming->arguments() + 1);
  return DoFunctionBind(isolate, caller_args);
}

// ES6 section 19.2.3.5 Function.prototype.toString ( )
BUILTIN(FunctionPrototypeToString) {
  HandleScope scope(isolate);
  Handle<Object> receiver = args.receiver();
  if (receiver->IsJSBoundFunction()) {
    return *JSBoundFunction::ToString(Handle<JSBoundFunction>::cast(receiver));
  } else if (receiver->IsJSFunction()) {
    return *JSFunction::ToString(Handle<JSFunction>::cast(receiver));
  }
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kNotGeneric,
                            isolate->factory()->NewStringFromAsciiChecked(
                                "Function.prototype.toString")));
}

// ES6 section 25.2.1.1 GeneratorFunction (p1, p2, ... , pn, body)
BUILTIN(GeneratorFunctionConstructor) {
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(isolate,
                           CreateDynamicFunction(isolate, args, "function*"));
}

BUILTIN(AsyncFunctionConstructor) {
  HandleScope scope(isolate);
  Handle<Object> maybe_func;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, maybe_func,
      CreateDynamicFunction(isolate, args, "async function"));
  if (!maybe_func->IsJSFunction()) return *maybe_func;

  // Do not lazily compute eval position for AsyncFunction, as they may not be
  // determined after the function is resumed.
  Handle<JSFunction> func = Handle<JSFunction>::cast(maybe_func);
  Handle<Script> script = handle(Script::cast(func->shared()->script()));
  int position = script->GetEvalPosition();
  USE(position);

  return *func;
}

// -----------------------------------------------------------------------------
// ES6 section 19.1 Object Objects

// ES6 section 19.1.3.4 Object.prototype.propertyIsEnumerable ( V )
BUILTIN(ObjectPrototypePropertyIsEnumerable) {
  HandleScope scope(isolate);
  Handle<JSReceiver> object;
  Handle<Name> name;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, name, Object::ToName(isolate, args.atOrUndefined(isolate, 1)));
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, object, JSReceiver::ToObject(isolate, args.receiver()));
  Maybe<PropertyAttributes> maybe =
      JSReceiver::GetOwnPropertyAttributes(object, name);
  if (!maybe.IsJust()) return isolate->heap()->exception();
  if (maybe.FromJust() == ABSENT) return isolate->heap()->false_value();
  return isolate->heap()->ToBoolean((maybe.FromJust() & DONT_ENUM) == 0);
}

// -----------------------------------------------------------------------------
// ES6 section 19.4 Symbol Objects

// ES6 section 19.4.1.1 Symbol ( [ description ] ) for the [[Call]] case.
BUILTIN(SymbolConstructor) {
  HandleScope scope(isolate);
  Handle<Symbol> result = isolate->factory()->NewSymbol();
  Handle<Object> description = args.atOrUndefined(isolate, 1);
  if (!description->IsUndefined(isolate)) {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, description,
                                       Object::ToString(isolate, description));
    result->set_name(*description);
  }
  return *result;
}

// ES6 section 19.4.1.1 Symbol ( [ description ] ) for the [[Construct]] case.
BUILTIN(SymbolConstructor_ConstructStub) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kNotConstructor,
                            isolate->factory()->Symbol_string()));
}

// ES6 section 19.4.3.4 Symbol.prototype [ @@toPrimitive ] ( hint )
void Builtins::Generate_SymbolPrototypeToPrimitive(
    CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(4);

  Node* result =
      assembler->ToThisValue(context, receiver, PrimitiveType::kSymbol,
                             "Symbol.prototype [ @@toPrimitive ]");
  assembler->Return(result);
}

// ES6 section 19.4.3.2 Symbol.prototype.toString ( )
void Builtins::Generate_SymbolPrototypeToString(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  Node* value = assembler->ToThisValue(
      context, receiver, PrimitiveType::kSymbol, "Symbol.prototype.toString");
  Node* result =
      assembler->CallRuntime(Runtime::kSymbolDescriptiveString, context, value);
  assembler->Return(result);
}

// ES6 section 19.4.3.3 Symbol.prototype.valueOf ( )
void Builtins::Generate_SymbolPrototypeValueOf(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  Node* result = assembler->ToThisValue(
      context, receiver, PrimitiveType::kSymbol, "Symbol.prototype.valueOf");
  assembler->Return(result);
}

// -----------------------------------------------------------------------------
// ES6 section 21.1 String Objects

// ES6 section 21.1.2.1 String.fromCharCode ( ...codeUnits )
void Builtins::Generate_StringFromCharCode(CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* code = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);

  // Check if we have exactly one argument (plus the implicit receiver), i.e.
  // if the parent frame is not an arguments adaptor frame.
  Label if_oneargument(assembler), if_notoneargument(assembler);
  Node* parent_frame_pointer = assembler->LoadParentFramePointer();
  Node* parent_frame_type =
      assembler->Load(MachineType::Pointer(), parent_frame_pointer,
                      assembler->IntPtrConstant(
                          CommonFrameConstants::kContextOrFrameTypeOffset));
  assembler->Branch(
      assembler->WordEqual(
          parent_frame_type,
          assembler->SmiConstant(Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR))),
      &if_notoneargument, &if_oneargument);

  assembler->Bind(&if_oneargument);
  {
    // Single argument case, perform fast single character string cache lookup
    // for one-byte code units, or fall back to creating a single character
    // string on the fly otherwise.
    Node* code32 = assembler->TruncateTaggedToWord32(context, code);
    Node* code16 = assembler->Word32And(
        code32, assembler->Int32Constant(String::kMaxUtf16CodeUnit));
    Node* result = assembler->StringFromCharCode(code16);
    assembler->Return(result);
  }

  assembler->Bind(&if_notoneargument);
  {
    // Determine the resulting string length.
    Node* parent_frame_length =
        assembler->Load(MachineType::Pointer(), parent_frame_pointer,
                        assembler->IntPtrConstant(
                            ArgumentsAdaptorFrameConstants::kLengthOffset));
    Node* length = assembler->SmiToWord(parent_frame_length);

    // Assume that the resulting string contains only one-byte characters.
    Node* result = assembler->AllocateSeqOneByteString(context, length);

    // Truncate all input parameters and append them to the resulting string.
    Variable var_offset(assembler, MachineType::PointerRepresentation());
    Label loop(assembler, &var_offset), done_loop(assembler);
    var_offset.Bind(assembler->IntPtrConstant(0));
    assembler->Goto(&loop);
    assembler->Bind(&loop);
    {
      // Load the current {offset}.
      Node* offset = var_offset.value();

      // Check if we're done with the string.
      assembler->GotoIf(assembler->WordEqual(offset, length), &done_loop);

      // Load the next code point and truncate it to a 16-bit value.
      Node* code = assembler->Load(
          MachineType::AnyTagged(), parent_frame_pointer,
          assembler->IntPtrAdd(
              assembler->WordShl(assembler->IntPtrSub(length, offset),
                                 assembler->IntPtrConstant(kPointerSizeLog2)),
              assembler->IntPtrConstant(
                  CommonFrameConstants::kFixedFrameSizeAboveFp -
                  kPointerSize)));
      Node* code32 = assembler->TruncateTaggedToWord32(context, code);
      Node* code16 = assembler->Word32And(
          code32, assembler->Int32Constant(String::kMaxUtf16CodeUnit));

      // Check if {code16} fits into a one-byte string.
      Label if_codeisonebyte(assembler), if_codeistwobyte(assembler);
      assembler->Branch(
          assembler->Int32LessThanOrEqual(
              code16, assembler->Int32Constant(String::kMaxOneByteCharCode)),
          &if_codeisonebyte, &if_codeistwobyte);

      assembler->Bind(&if_codeisonebyte);
      {
        // The {code16} fits into the SeqOneByteString {result}.
        assembler->StoreNoWriteBarrier(
            MachineRepresentation::kWord8, result,
            assembler->IntPtrAdd(
                assembler->IntPtrConstant(SeqOneByteString::kHeaderSize -
                                          kHeapObjectTag),
                offset),
            code16);
        var_offset.Bind(
            assembler->IntPtrAdd(offset, assembler->IntPtrConstant(1)));
        assembler->Goto(&loop);
      }

      assembler->Bind(&if_codeistwobyte);
      {
        // Allocate a SeqTwoByteString to hold the resulting string.
        Node* cresult = assembler->AllocateSeqTwoByteString(context, length);

        // Copy all characters that were previously written to the
        // SeqOneByteString in {result} over to the new {cresult}.
        Variable var_coffset(assembler, MachineType::PointerRepresentation());
        Label cloop(assembler, &var_coffset), done_cloop(assembler);
        var_coffset.Bind(assembler->IntPtrConstant(0));
        assembler->Goto(&cloop);
        assembler->Bind(&cloop);
        {
          Node* coffset = var_coffset.value();
          assembler->GotoIf(assembler->WordEqual(coffset, offset), &done_cloop);
          Node* ccode = assembler->Load(
              MachineType::Uint8(), result,
              assembler->IntPtrAdd(
                  assembler->IntPtrConstant(SeqOneByteString::kHeaderSize -
                                            kHeapObjectTag),
                  coffset));
          assembler->StoreNoWriteBarrier(
              MachineRepresentation::kWord16, cresult,
              assembler->IntPtrAdd(
                  assembler->IntPtrConstant(SeqTwoByteString::kHeaderSize -
                                            kHeapObjectTag),
                  assembler->WordShl(coffset, 1)),
              ccode);
          var_coffset.Bind(
              assembler->IntPtrAdd(coffset, assembler->IntPtrConstant(1)));
          assembler->Goto(&cloop);
        }

        // Write the pending {code16} to {offset}.
        assembler->Bind(&done_cloop);
        assembler->StoreNoWriteBarrier(
            MachineRepresentation::kWord16, cresult,
            assembler->IntPtrAdd(
                assembler->IntPtrConstant(SeqTwoByteString::kHeaderSize -
                                          kHeapObjectTag),
                assembler->WordShl(offset, 1)),
            code16);

        // Copy the remaining parameters to the SeqTwoByteString {cresult}.
        Label floop(assembler, &var_offset), done_floop(assembler);
        assembler->Goto(&floop);
        assembler->Bind(&floop);
        {
          // Compute the next {offset}.
          Node* offset = assembler->IntPtrAdd(var_offset.value(),
                                              assembler->IntPtrConstant(1));

          // Check if we're done with the string.
          assembler->GotoIf(assembler->WordEqual(offset, length), &done_floop);

          // Load the next code point and truncate it to a 16-bit value.
          Node* code = assembler->Load(
              MachineType::AnyTagged(), parent_frame_pointer,
              assembler->IntPtrAdd(
                  assembler->WordShl(
                      assembler->IntPtrSub(length, offset),
                      assembler->IntPtrConstant(kPointerSizeLog2)),
                  assembler->IntPtrConstant(
                      CommonFrameConstants::kFixedFrameSizeAboveFp -
                      kPointerSize)));
          Node* code32 = assembler->TruncateTaggedToWord32(context, code);
          Node* code16 = assembler->Word32And(
              code32, assembler->Int32Constant(String::kMaxUtf16CodeUnit));

          // Store the truncated {code} point at the next offset.
          assembler->StoreNoWriteBarrier(
              MachineRepresentation::kWord16, cresult,
              assembler->IntPtrAdd(
                  assembler->IntPtrConstant(SeqTwoByteString::kHeaderSize -
                                            kHeapObjectTag),
                  assembler->WordShl(offset, 1)),
              code16);
          var_offset.Bind(offset);
          assembler->Goto(&floop);
        }

        // Return the SeqTwoByteString.
        assembler->Bind(&done_floop);
        assembler->Return(cresult);
      }
    }

    assembler->Bind(&done_loop);
    assembler->Return(result);
  }
}

namespace {  // for String.fromCodePoint

bool IsValidCodePoint(Isolate* isolate, Handle<Object> value) {
  if (!value->IsNumber() && !Object::ToNumber(value).ToHandle(&value)) {
    return false;
  }

  if (Object::ToInteger(isolate, value).ToHandleChecked()->Number() !=
      value->Number()) {
    return false;
  }

  if (value->Number() < 0 || value->Number() > 0x10FFFF) {
    return false;
  }

  return true;
}

uc32 NextCodePoint(Isolate* isolate, BuiltinArguments args, int index) {
  Handle<Object> value = args.at<Object>(1 + index);
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, value, Object::ToNumber(value), -1);
  if (!IsValidCodePoint(isolate, value)) {
    isolate->Throw(*isolate->factory()->NewRangeError(
        MessageTemplate::kInvalidCodePoint, value));
    return -1;
  }
  return DoubleToUint32(value->Number());
}

}  // namespace

// ES6 section 21.1.2.2 String.fromCodePoint ( ...codePoints )
BUILTIN(StringFromCodePoint) {
  HandleScope scope(isolate);
  int const length = args.length() - 1;
  if (length == 0) return isolate->heap()->empty_string();
  DCHECK_LT(0, length);

  // Optimistically assume that the resulting String contains only one byte
  // characters.
  List<uint8_t> one_byte_buffer(length);
  uc32 code = 0;
  int index;
  for (index = 0; index < length; index++) {
    code = NextCodePoint(isolate, args, index);
    if (code < 0) {
      return isolate->heap()->exception();
    }
    if (code > String::kMaxOneByteCharCode) {
      break;
    }
    one_byte_buffer.Add(code);
  }

  if (index == length) {
    RETURN_RESULT_OR_FAILURE(isolate, isolate->factory()->NewStringFromOneByte(
                                          one_byte_buffer.ToConstVector()));
  }

  List<uc16> two_byte_buffer(length - index);

  while (true) {
    if (code <= unibrow::Utf16::kMaxNonSurrogateCharCode) {
      two_byte_buffer.Add(code);
    } else {
      two_byte_buffer.Add(unibrow::Utf16::LeadSurrogate(code));
      two_byte_buffer.Add(unibrow::Utf16::TrailSurrogate(code));
    }

    if (++index == length) {
      break;
    }
    code = NextCodePoint(isolate, args, index);
    if (code < 0) {
      return isolate->heap()->exception();
    }
  }

  Handle<SeqTwoByteString> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      isolate->factory()->NewRawTwoByteString(one_byte_buffer.length() +
                                              two_byte_buffer.length()));

  CopyChars(result->GetChars(), one_byte_buffer.ToConstVector().start(),
            one_byte_buffer.length());
  CopyChars(result->GetChars() + one_byte_buffer.length(),
            two_byte_buffer.ToConstVector().start(), two_byte_buffer.length());

  return *result;
}

// ES6 section 21.1.3.1 String.prototype.charAt ( pos )
void Builtins::Generate_StringPrototypeCharAt(CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* receiver = assembler->Parameter(0);
  Node* position = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);

  // Check that {receiver} is coercible to Object and convert it to a String.
  receiver =
      assembler->ToThisString(context, receiver, "String.prototype.charAt");

  // Convert the {position} to a Smi and check that it's in bounds of the
  // {receiver}.
  // TODO(bmeurer): Find an abstraction for this!
  {
    // Check if the {position} is already a Smi.
    Variable var_position(assembler, MachineRepresentation::kTagged);
    var_position.Bind(position);
    Label if_positionissmi(assembler),
        if_positionisnotsmi(assembler, Label::kDeferred);
    assembler->Branch(assembler->WordIsSmi(position), &if_positionissmi,
                      &if_positionisnotsmi);
    assembler->Bind(&if_positionisnotsmi);
    {
      // Convert the {position} to an Integer via the ToIntegerStub.
      Callable callable = CodeFactory::ToInteger(assembler->isolate());
      Node* index = assembler->CallStub(callable, context, position);

      // Check if the resulting {index} is now a Smi.
      Label if_indexissmi(assembler, Label::kDeferred),
          if_indexisnotsmi(assembler, Label::kDeferred);
      assembler->Branch(assembler->WordIsSmi(index), &if_indexissmi,
                        &if_indexisnotsmi);

      assembler->Bind(&if_indexissmi);
      {
        var_position.Bind(index);
        assembler->Goto(&if_positionissmi);
      }

      assembler->Bind(&if_indexisnotsmi);
      {
        // The ToIntegerStub canonicalizes everything in Smi range to Smi
        // representation, so any HeapNumber returned is not in Smi range.
        // The only exception here is -0.0, which we treat as 0.
        Node* index_value = assembler->LoadHeapNumberValue(index);
        Label if_indexiszero(assembler, Label::kDeferred),
            if_indexisnotzero(assembler, Label::kDeferred);
        assembler->Branch(assembler->Float64Equal(
                              index_value, assembler->Float64Constant(0.0)),
                          &if_indexiszero, &if_indexisnotzero);

        assembler->Bind(&if_indexiszero);
        {
          var_position.Bind(assembler->SmiConstant(Smi::FromInt(0)));
          assembler->Goto(&if_positionissmi);
        }

        assembler->Bind(&if_indexisnotzero);
        {
          // The {index} is some other integral Number, that is definitely
          // neither -0.0 nor in Smi range.
          assembler->Return(assembler->EmptyStringConstant());
        }
      }
    }
    assembler->Bind(&if_positionissmi);
    position = var_position.value();

    // Determine the actual length of the {receiver} String.
    Node* receiver_length =
        assembler->LoadObjectField(receiver, String::kLengthOffset);

    // Return "" if the Smi {position} is outside the bounds of the {receiver}.
    Label if_positioninbounds(assembler),
        if_positionnotinbounds(assembler, Label::kDeferred);
    assembler->Branch(assembler->SmiAboveOrEqual(position, receiver_length),
                      &if_positionnotinbounds, &if_positioninbounds);
    assembler->Bind(&if_positionnotinbounds);
    assembler->Return(assembler->EmptyStringConstant());
    assembler->Bind(&if_positioninbounds);
  }

  // Load the character code at the {position} from the {receiver}.
  Node* code = assembler->StringCharCodeAt(receiver, position);

  // And return the single character string with only that {code}.
  Node* result = assembler->StringFromCharCode(code);
  assembler->Return(result);
}

// ES6 section 21.1.3.2 String.prototype.charCodeAt ( pos )
void Builtins::Generate_StringPrototypeCharCodeAt(
    CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* receiver = assembler->Parameter(0);
  Node* position = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);

  // Check that {receiver} is coercible to Object and convert it to a String.
  receiver =
      assembler->ToThisString(context, receiver, "String.prototype.charCodeAt");

  // Convert the {position} to a Smi and check that it's in bounds of the
  // {receiver}.
  // TODO(bmeurer): Find an abstraction for this!
  {
    // Check if the {position} is already a Smi.
    Variable var_position(assembler, MachineRepresentation::kTagged);
    var_position.Bind(position);
    Label if_positionissmi(assembler),
        if_positionisnotsmi(assembler, Label::kDeferred);
    assembler->Branch(assembler->WordIsSmi(position), &if_positionissmi,
                      &if_positionisnotsmi);
    assembler->Bind(&if_positionisnotsmi);
    {
      // Convert the {position} to an Integer via the ToIntegerStub.
      Callable callable = CodeFactory::ToInteger(assembler->isolate());
      Node* index = assembler->CallStub(callable, context, position);

      // Check if the resulting {index} is now a Smi.
      Label if_indexissmi(assembler, Label::kDeferred),
          if_indexisnotsmi(assembler, Label::kDeferred);
      assembler->Branch(assembler->WordIsSmi(index), &if_indexissmi,
                        &if_indexisnotsmi);

      assembler->Bind(&if_indexissmi);
      {
        var_position.Bind(index);
        assembler->Goto(&if_positionissmi);
      }

      assembler->Bind(&if_indexisnotsmi);
      {
        // The ToIntegerStub canonicalizes everything in Smi range to Smi
        // representation, so any HeapNumber returned is not in Smi range.
        // The only exception here is -0.0, which we treat as 0.
        Node* index_value = assembler->LoadHeapNumberValue(index);
        Label if_indexiszero(assembler, Label::kDeferred),
            if_indexisnotzero(assembler, Label::kDeferred);
        assembler->Branch(assembler->Float64Equal(
                              index_value, assembler->Float64Constant(0.0)),
                          &if_indexiszero, &if_indexisnotzero);

        assembler->Bind(&if_indexiszero);
        {
          var_position.Bind(assembler->SmiConstant(Smi::FromInt(0)));
          assembler->Goto(&if_positionissmi);
        }

        assembler->Bind(&if_indexisnotzero);
        {
          // The {index} is some other integral Number, that is definitely
          // neither -0.0 nor in Smi range.
          assembler->Return(assembler->NaNConstant());
        }
      }
    }
    assembler->Bind(&if_positionissmi);
    position = var_position.value();

    // Determine the actual length of the {receiver} String.
    Node* receiver_length =
        assembler->LoadObjectField(receiver, String::kLengthOffset);

    // Return NaN if the Smi {position} is outside the bounds of the {receiver}.
    Label if_positioninbounds(assembler),
        if_positionnotinbounds(assembler, Label::kDeferred);
    assembler->Branch(assembler->SmiAboveOrEqual(position, receiver_length),
                      &if_positionnotinbounds, &if_positioninbounds);
    assembler->Bind(&if_positionnotinbounds);
    assembler->Return(assembler->NaNConstant());
    assembler->Bind(&if_positioninbounds);
  }

  // Load the character at the {position} from the {receiver}.
  Node* value = assembler->StringCharCodeAt(receiver, position);
  Node* result = assembler->SmiFromWord32(value);
  assembler->Return(result);
}

// ES6 section 21.1.3.25 String.prototype.toString ()
void Builtins::Generate_StringPrototypeToString(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  Node* result = assembler->ToThisValue(
      context, receiver, PrimitiveType::kString, "String.prototype.toString");
  assembler->Return(result);
}

// ES6 section 21.1.3.27 String.prototype.trim ()
BUILTIN(StringPrototypeTrim) {
  HandleScope scope(isolate);
  TO_THIS_STRING(string, "String.prototype.trim");
  return *String::Trim(string, String::kTrim);
}

// Non-standard WebKit extension
BUILTIN(StringPrototypeTrimLeft) {
  HandleScope scope(isolate);
  TO_THIS_STRING(string, "String.prototype.trimLeft");
  return *String::Trim(string, String::kTrimLeft);
}

// Non-standard WebKit extension
BUILTIN(StringPrototypeTrimRight) {
  HandleScope scope(isolate);
  TO_THIS_STRING(string, "String.prototype.trimRight");
  return *String::Trim(string, String::kTrimRight);
}

// ES6 section 21.1.3.28 String.prototype.valueOf ( )
void Builtins::Generate_StringPrototypeValueOf(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  Node* result = assembler->ToThisValue(
      context, receiver, PrimitiveType::kString, "String.prototype.valueOf");
  assembler->Return(result);
}

// -----------------------------------------------------------------------------
// ES6 section 21.1 ArrayBuffer Objects

// ES6 section 24.1.2.1 ArrayBuffer ( length ) for the [[Call]] case.
BUILTIN(ArrayBufferConstructor) {
  HandleScope scope(isolate);
  Handle<JSFunction> target = args.target<JSFunction>();
  DCHECK(*target == target->native_context()->array_buffer_fun() ||
         *target == target->native_context()->shared_array_buffer_fun());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kConstructorNotFunction,
                            handle(target->shared()->name(), isolate)));
}

// ES6 section 24.1.2.1 ArrayBuffer ( length ) for the [[Construct]] case.
BUILTIN(ArrayBufferConstructor_ConstructStub) {
  HandleScope scope(isolate);
  Handle<JSFunction> target = args.target<JSFunction>();
  Handle<JSReceiver> new_target = Handle<JSReceiver>::cast(args.new_target());
  Handle<Object> length = args.atOrUndefined(isolate, 1);
  DCHECK(*target == target->native_context()->array_buffer_fun() ||
         *target == target->native_context()->shared_array_buffer_fun());
  Handle<Object> number_length;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, number_length,
                                     Object::ToInteger(isolate, length));
  if (number_length->Number() < 0.0) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kInvalidArrayBufferLength));
  }
  Handle<JSObject> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, result,
                                     JSObject::New(target, new_target));
  size_t byte_length;
  if (!TryNumberToSize(isolate, *number_length, &byte_length)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kInvalidArrayBufferLength));
  }
  SharedFlag shared_flag =
      (*target == target->native_context()->array_buffer_fun())
          ? SharedFlag::kNotShared
          : SharedFlag::kShared;
  if (!JSArrayBuffer::SetupAllocatingData(Handle<JSArrayBuffer>::cast(result),
                                          isolate, byte_length, true,
                                          shared_flag)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kArrayBufferAllocationFailed));
  }
  return *result;
}

// ES6 section 24.1.4.1 get ArrayBuffer.prototype.byteLength
BUILTIN(ArrayBufferPrototypeGetByteLength) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSArrayBuffer, array_buffer,
                 "get ArrayBuffer.prototype.byteLength");

  if (array_buffer->is_shared()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "get ArrayBuffer.prototype.byteLength"),
                              args.receiver()));
  }
  // TODO(franzih): According to the ES6 spec, we should throw a TypeError
  // here if the JSArrayBuffer is detached.
  return array_buffer->byte_length();
}

// ES6 section 24.1.3.1 ArrayBuffer.isView ( arg )
BUILTIN(ArrayBufferIsView) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(2, args.length());
  Object* arg = args[1];
  return isolate->heap()->ToBoolean(arg->IsJSArrayBufferView());
}

// ES7 sharedmem 6.3.4.1 get SharedArrayBuffer.prototype.byteLength
BUILTIN(SharedArrayBufferPrototypeGetByteLength) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSArrayBuffer, array_buffer,
                 "get SharedArrayBuffer.prototype.byteLength");
  if (!array_buffer->is_shared()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "get SharedArrayBuffer.prototype.byteLength"),
                              args.receiver()));
  }
  return array_buffer->byte_length();
}

// ES6 section 26.2.1.1 Proxy ( target, handler ) for the [[Call]] case.
BUILTIN(ProxyConstructor) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate,
      NewTypeError(MessageTemplate::kConstructorNotFunction,
                   isolate->factory()->NewStringFromAsciiChecked("Proxy")));
}

// ES6 section 26.2.1.1 Proxy ( target, handler ) for the [[Construct]] case.
BUILTIN(ProxyConstructor_ConstructStub) {
  HandleScope scope(isolate);
  DCHECK(isolate->proxy_function()->IsConstructor());
  Handle<Object> target = args.atOrUndefined(isolate, 1);
  Handle<Object> handler = args.atOrUndefined(isolate, 2);
  RETURN_RESULT_OR_FAILURE(isolate, JSProxy::New(isolate, target, handler));
}

// -----------------------------------------------------------------------------
// Throwers for restricted function properties and strict arguments object
// properties

BUILTIN(RestrictedFunctionPropertiesThrower) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kRestrictedFunctionProperties));
}

BUILTIN(RestrictedStrictArgumentsPropertiesThrower) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kStrictPoisonPill));
}

// -----------------------------------------------------------------------------
//

namespace {

// Returns the holder JSObject if the function can legally be called with this
// receiver.  Returns nullptr if the call is illegal.
// TODO(dcarney): CallOptimization duplicates this logic, merge.
JSObject* GetCompatibleReceiver(Isolate* isolate, FunctionTemplateInfo* info,
                                JSObject* receiver) {
  Object* recv_type = info->signature();
  // No signature, return holder.
  if (!recv_type->IsFunctionTemplateInfo()) return receiver;
  FunctionTemplateInfo* signature = FunctionTemplateInfo::cast(recv_type);

  // Check the receiver. Fast path for receivers with no hidden prototypes.
  if (signature->IsTemplateFor(receiver)) return receiver;
  if (!receiver->map()->has_hidden_prototype()) return nullptr;
  for (PrototypeIterator iter(isolate, receiver, kStartAtPrototype,
                              PrototypeIterator::END_AT_NON_HIDDEN);
       !iter.IsAtEnd(); iter.Advance()) {
    JSObject* current = iter.GetCurrent<JSObject>();
    if (signature->IsTemplateFor(current)) return current;
  }
  return nullptr;
}

template <bool is_construct>
MUST_USE_RESULT MaybeHandle<Object> HandleApiCallHelper(
    Isolate* isolate, Handle<HeapObject> function,
    Handle<HeapObject> new_target, Handle<FunctionTemplateInfo> fun_data,
    Handle<Object> receiver, BuiltinArguments args) {
  Handle<JSObject> js_receiver;
  JSObject* raw_holder;
  if (is_construct) {
    DCHECK(args.receiver()->IsTheHole(isolate));
    if (fun_data->instance_template()->IsUndefined(isolate)) {
      v8::Local<ObjectTemplate> templ =
          ObjectTemplate::New(reinterpret_cast<v8::Isolate*>(isolate),
                              ToApiHandle<v8::FunctionTemplate>(fun_data));
      fun_data->set_instance_template(*Utils::OpenHandle(*templ));
    }
    Handle<ObjectTemplateInfo> instance_template(
        ObjectTemplateInfo::cast(fun_data->instance_template()), isolate);
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, js_receiver,
        ApiNatives::InstantiateObject(instance_template,
                                      Handle<JSReceiver>::cast(new_target)),
        Object);
    args[0] = *js_receiver;
    DCHECK_EQ(*js_receiver, *args.receiver());

    raw_holder = *js_receiver;
  } else {
    DCHECK(receiver->IsJSReceiver());

    if (!receiver->IsJSObject()) {
      // This function cannot be called with the given receiver.  Abort!
      THROW_NEW_ERROR(
          isolate, NewTypeError(MessageTemplate::kIllegalInvocation), Object);
    }

    js_receiver = Handle<JSObject>::cast(receiver);

    if (!fun_data->accept_any_receiver() &&
        js_receiver->IsAccessCheckNeeded() &&
        !isolate->MayAccess(handle(isolate->context()), js_receiver)) {
      isolate->ReportFailedAccessCheck(js_receiver);
      RETURN_EXCEPTION_IF_SCHEDULED_EXCEPTION(isolate, Object);
    }

    raw_holder = GetCompatibleReceiver(isolate, *fun_data, *js_receiver);

    if (raw_holder == nullptr) {
      // This function cannot be called with the given receiver.  Abort!
      THROW_NEW_ERROR(
          isolate, NewTypeError(MessageTemplate::kIllegalInvocation), Object);
    }
  }

  Object* raw_call_data = fun_data->call_code();
  if (!raw_call_data->IsUndefined(isolate)) {
    DCHECK(raw_call_data->IsCallHandlerInfo());
    CallHandlerInfo* call_data = CallHandlerInfo::cast(raw_call_data);
    Object* callback_obj = call_data->callback();
    v8::FunctionCallback callback =
        v8::ToCData<v8::FunctionCallback>(callback_obj);
    Object* data_obj = call_data->data();

    LOG(isolate, ApiObjectAccess("call", JSObject::cast(*js_receiver)));

    FunctionCallbackArguments custom(isolate, data_obj, *function, raw_holder,
                                     *new_target, &args[0] - 1,
                                     args.length() - 1);

    Handle<Object> result = custom.Call(callback);

    RETURN_EXCEPTION_IF_SCHEDULED_EXCEPTION(isolate, Object);
    if (result.is_null()) {
      if (is_construct) return js_receiver;
      return isolate->factory()->undefined_value();
    }
    // Rebox the result.
    result->VerifyApiCallResultType();
    if (!is_construct || result->IsJSObject()) return handle(*result, isolate);
  }

  return js_receiver;
}

}  // namespace

BUILTIN(HandleApiCall) {
  HandleScope scope(isolate);
  Handle<JSFunction> function = args.target<JSFunction>();
  Handle<Object> receiver = args.receiver();
  Handle<HeapObject> new_target = args.new_target();
  Handle<FunctionTemplateInfo> fun_data(function->shared()->get_api_func_data(),
                                        isolate);
  if (new_target->IsJSReceiver()) {
    RETURN_RESULT_OR_FAILURE(
        isolate, HandleApiCallHelper<true>(isolate, function, new_target,
                                           fun_data, receiver, args));
  } else {
    RETURN_RESULT_OR_FAILURE(
        isolate, HandleApiCallHelper<false>(isolate, function, new_target,
                                            fun_data, receiver, args));
  }
}

Handle<Code> Builtins::CallFunction(ConvertReceiverMode mode,
                                    TailCallMode tail_call_mode) {
  switch (tail_call_mode) {
    case TailCallMode::kDisallow:
      switch (mode) {
        case ConvertReceiverMode::kNullOrUndefined:
          return CallFunction_ReceiverIsNullOrUndefined();
        case ConvertReceiverMode::kNotNullOrUndefined:
          return CallFunction_ReceiverIsNotNullOrUndefined();
        case ConvertReceiverMode::kAny:
          return CallFunction_ReceiverIsAny();
      }
      break;
    case TailCallMode::kAllow:
      switch (mode) {
        case ConvertReceiverMode::kNullOrUndefined:
          return TailCallFunction_ReceiverIsNullOrUndefined();
        case ConvertReceiverMode::kNotNullOrUndefined:
          return TailCallFunction_ReceiverIsNotNullOrUndefined();
        case ConvertReceiverMode::kAny:
          return TailCallFunction_ReceiverIsAny();
      }
      break;
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

Handle<Code> Builtins::Call(ConvertReceiverMode mode,
                            TailCallMode tail_call_mode) {
  switch (tail_call_mode) {
    case TailCallMode::kDisallow:
      switch (mode) {
        case ConvertReceiverMode::kNullOrUndefined:
          return Call_ReceiverIsNullOrUndefined();
        case ConvertReceiverMode::kNotNullOrUndefined:
          return Call_ReceiverIsNotNullOrUndefined();
        case ConvertReceiverMode::kAny:
          return Call_ReceiverIsAny();
      }
      break;
    case TailCallMode::kAllow:
      switch (mode) {
        case ConvertReceiverMode::kNullOrUndefined:
          return TailCall_ReceiverIsNullOrUndefined();
        case ConvertReceiverMode::kNotNullOrUndefined:
          return TailCall_ReceiverIsNotNullOrUndefined();
        case ConvertReceiverMode::kAny:
          return TailCall_ReceiverIsAny();
      }
      break;
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

Handle<Code> Builtins::CallBoundFunction(TailCallMode tail_call_mode) {
  switch (tail_call_mode) {
    case TailCallMode::kDisallow:
      return CallBoundFunction();
    case TailCallMode::kAllow:
      return TailCallBoundFunction();
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

Handle<Code> Builtins::NonPrimitiveToPrimitive(ToPrimitiveHint hint) {
  switch (hint) {
    case ToPrimitiveHint::kDefault:
      return NonPrimitiveToPrimitive_Default();
    case ToPrimitiveHint::kNumber:
      return NonPrimitiveToPrimitive_Number();
    case ToPrimitiveHint::kString:
      return NonPrimitiveToPrimitive_String();
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

Handle<Code> Builtins::OrdinaryToPrimitive(OrdinaryToPrimitiveHint hint) {
  switch (hint) {
    case OrdinaryToPrimitiveHint::kNumber:
      return OrdinaryToPrimitive_Number();
    case OrdinaryToPrimitiveHint::kString:
      return OrdinaryToPrimitive_String();
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

Handle<Code> Builtins::InterpreterPushArgsAndCall(TailCallMode tail_call_mode,
                                                  CallableType function_type) {
  switch (tail_call_mode) {
    case TailCallMode::kDisallow:
      if (function_type == CallableType::kJSFunction) {
        return InterpreterPushArgsAndCallFunction();
      } else {
        return InterpreterPushArgsAndCall();
      }
    case TailCallMode::kAllow:
      if (function_type == CallableType::kJSFunction) {
        return InterpreterPushArgsAndTailCallFunction();
      } else {
        return InterpreterPushArgsAndTailCall();
      }
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

Handle<Code> Builtins::InterpreterPushArgsAndConstruct(
    CallableType function_type) {
  switch (function_type) {
    case CallableType::kJSFunction:
      return InterpreterPushArgsAndConstructFunction();
    case CallableType::kAny:
      return InterpreterPushArgsAndConstruct();
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

namespace {

class RelocatableArguments : public BuiltinArguments, public Relocatable {
 public:
  RelocatableArguments(Isolate* isolate, int length, Object** arguments)
      : BuiltinArguments(length, arguments), Relocatable(isolate) {}

  virtual inline void IterateInstance(ObjectVisitor* v) {
    if (length() == 0) return;
    v->VisitPointers(lowest_address(), highest_address() + 1);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(RelocatableArguments);
};

}  // namespace

MaybeHandle<Object> Builtins::InvokeApiFunction(Isolate* isolate,
                                                Handle<HeapObject> function,
                                                Handle<Object> receiver,
                                                int argc,
                                                Handle<Object> args[]) {
  DCHECK(function->IsFunctionTemplateInfo() ||
         (function->IsJSFunction() &&
          JSFunction::cast(*function)->shared()->IsApiFunction()));

  // Do proper receiver conversion for non-strict mode api functions.
  if (!receiver->IsJSReceiver()) {
    if (function->IsFunctionTemplateInfo() ||
        is_sloppy(JSFunction::cast(*function)->shared()->language_mode())) {
      ASSIGN_RETURN_ON_EXCEPTION(isolate, receiver,
                                 Object::ConvertReceiver(isolate, receiver),
                                 Object);
    }
  }

  Handle<FunctionTemplateInfo> fun_data =
      function->IsFunctionTemplateInfo()
          ? Handle<FunctionTemplateInfo>::cast(function)
          : handle(JSFunction::cast(*function)->shared()->get_api_func_data(),
                   isolate);
  Handle<HeapObject> new_target = isolate->factory()->undefined_value();
  // Construct BuiltinArguments object:
  // new target, function, arguments reversed, receiver.
  const int kBufferSize = 32;
  Object* small_argv[kBufferSize];
  Object** argv;
  const int frame_argc = argc + BuiltinArguments::kNumExtraArgsWithReceiver;
  if (frame_argc <= kBufferSize) {
    argv = small_argv;
  } else {
    argv = new Object*[frame_argc];
  }
  int cursor = frame_argc - 1;
  argv[cursor--] = *receiver;
  for (int i = 0; i < argc; ++i) {
    argv[cursor--] = *args[i];
  }
  DCHECK(cursor == BuiltinArguments::kArgcOffset);
  argv[BuiltinArguments::kArgcOffset] = Smi::FromInt(frame_argc);
  argv[BuiltinArguments::kTargetOffset] = *function;
  argv[BuiltinArguments::kNewTargetOffset] = *new_target;
  MaybeHandle<Object> result;
  {
    RelocatableArguments arguments(isolate, frame_argc, &argv[frame_argc - 1]);
    result = HandleApiCallHelper<false>(isolate, function, new_target, fun_data,
                                        receiver, arguments);
  }
  if (argv != small_argv) delete[] argv;
  return result;
}

// Helper function to handle calls to non-function objects created through the
// API. The object can be called as either a constructor (using new) or just as
// a function (without new).
MUST_USE_RESULT static Object* HandleApiCallAsFunctionOrConstructor(
    Isolate* isolate, bool is_construct_call, BuiltinArguments args) {
  Handle<Object> receiver = args.receiver();

  // Get the object called.
  JSObject* obj = JSObject::cast(*receiver);

  // Set the new target.
  HeapObject* new_target;
  if (is_construct_call) {
    // TODO(adamk): This should be passed through in args instead of
    // being patched in here. We need to set a non-undefined value
    // for v8::FunctionCallbackInfo::IsConstructCall() to get the
    // right answer.
    new_target = obj;
  } else {
    new_target = isolate->heap()->undefined_value();
  }

  // Get the invocation callback from the function descriptor that was
  // used to create the called object.
  DCHECK(obj->map()->is_callable());
  JSFunction* constructor = JSFunction::cast(obj->map()->GetConstructor());
  // TODO(ishell): turn this back to a DCHECK.
  CHECK(constructor->shared()->IsApiFunction());
  Object* handler =
      constructor->shared()->get_api_func_data()->instance_call_handler();
  DCHECK(!handler->IsUndefined(isolate));
  // TODO(ishell): remove this debugging code.
  CHECK(handler->IsCallHandlerInfo());
  CallHandlerInfo* call_data = CallHandlerInfo::cast(handler);
  Object* callback_obj = call_data->callback();
  v8::FunctionCallback callback =
      v8::ToCData<v8::FunctionCallback>(callback_obj);

  // Get the data for the call and perform the callback.
  Object* result;
  {
    HandleScope scope(isolate);
    LOG(isolate, ApiObjectAccess("call non-function", obj));

    FunctionCallbackArguments custom(isolate, call_data->data(), constructor,
                                     obj, new_target, &args[0] - 1,
                                     args.length() - 1);
    Handle<Object> result_handle = custom.Call(callback);
    if (result_handle.is_null()) {
      result = isolate->heap()->undefined_value();
    } else {
      result = *result_handle;
    }
  }
  // Check for exceptions and return result.
  RETURN_FAILURE_IF_SCHEDULED_EXCEPTION(isolate);
  return result;
}

// Handle calls to non-function objects created through the API. This delegate
// function is used when the call is a normal function call.
BUILTIN(HandleApiCallAsFunction) {
  return HandleApiCallAsFunctionOrConstructor(isolate, false, args);
}

// Handle calls to non-function objects created through the API. This delegate
// function is used when the call is a construct call.
BUILTIN(HandleApiCallAsConstructor) {
  return HandleApiCallAsFunctionOrConstructor(isolate, true, args);
}

void Builtins::Generate_LoadIC_Miss(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* name = assembler->Parameter(1);
  Node* slot = assembler->Parameter(2);
  Node* vector = assembler->Parameter(3);
  Node* context = assembler->Parameter(4);

  assembler->TailCallRuntime(Runtime::kLoadIC_Miss, context, receiver, name,
                             slot, vector);
}

void Builtins::Generate_LoadGlobalIC_Miss(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* slot = assembler->Parameter(0);
  Node* vector = assembler->Parameter(1);
  Node* context = assembler->Parameter(2);

  assembler->TailCallRuntime(Runtime::kLoadGlobalIC_Miss, context, slot,
                             vector);
}

void Builtins::Generate_LoadIC_Normal(MacroAssembler* masm) {
  LoadIC::GenerateNormal(masm);
}

void Builtins::Generate_LoadIC_Getter_ForDeopt(MacroAssembler* masm) {
  NamedLoadHandlerCompiler::GenerateLoadViaGetterForDeopt(masm);
}

void Builtins::Generate_LoadIC_Slow(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* name = assembler->Parameter(1);
  // Node* slot = assembler->Parameter(2);
  // Node* vector = assembler->Parameter(3);
  Node* context = assembler->Parameter(4);

  assembler->TailCallRuntime(Runtime::kGetProperty, context, receiver, name);
}

namespace {
void Generate_LoadGlobalIC_Slow(CodeStubAssembler* assembler, TypeofMode mode) {
  typedef compiler::Node Node;

  Node* slot = assembler->Parameter(0);
  Node* vector = assembler->Parameter(1);
  Node* context = assembler->Parameter(2);
  Node* typeof_mode = assembler->SmiConstant(Smi::FromInt(mode));

  assembler->TailCallRuntime(Runtime::kGetGlobal, context, slot, vector,
                             typeof_mode);
}
}  // anonymous namespace

void Builtins::Generate_LoadGlobalIC_SlowInsideTypeof(
    CodeStubAssembler* assembler) {
  Generate_LoadGlobalIC_Slow(assembler, INSIDE_TYPEOF);
}

void Builtins::Generate_LoadGlobalIC_SlowNotInsideTypeof(
    CodeStubAssembler* assembler) {
  Generate_LoadGlobalIC_Slow(assembler, NOT_INSIDE_TYPEOF);
}

void Builtins::Generate_KeyedLoadIC_Slow(MacroAssembler* masm) {
  KeyedLoadIC::GenerateRuntimeGetProperty(masm);
}

void Builtins::Generate_KeyedLoadIC_Miss(MacroAssembler* masm) {
  KeyedLoadIC::GenerateMiss(masm);
}

void Builtins::Generate_KeyedLoadIC_Megamorphic(MacroAssembler* masm) {
  KeyedLoadIC::GenerateMegamorphic(masm);
}

void Builtins::Generate_StoreIC_Miss(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* name = assembler->Parameter(1);
  Node* value = assembler->Parameter(2);
  Node* slot = assembler->Parameter(3);
  Node* vector = assembler->Parameter(4);
  Node* context = assembler->Parameter(5);

  assembler->TailCallRuntime(Runtime::kStoreIC_Miss, context, receiver, name,
                             value, slot, vector);
}

void Builtins::Generate_StoreIC_Normal(MacroAssembler* masm) {
  StoreIC::GenerateNormal(masm);
}

namespace {
void Generate_StoreIC_Slow(CodeStubAssembler* assembler,
                           LanguageMode language_mode) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* name = assembler->Parameter(1);
  Node* value = assembler->Parameter(2);
  // Node* slot = assembler->Parameter(3);
  // Node* vector = assembler->Parameter(4);
  Node* context = assembler->Parameter(5);
  Node* lang_mode = assembler->SmiConstant(Smi::FromInt(language_mode));

  // The slow case calls into the runtime to complete the store without causing
  // an IC miss that would otherwise cause a transition to the generic stub.
  assembler->TailCallRuntime(Runtime::kSetProperty, context, receiver, name,
                             value, lang_mode);
}
}  // anonymous namespace

void Builtins::Generate_StoreIC_SlowSloppy(CodeStubAssembler* assembler) {
  Generate_StoreIC_Slow(assembler, SLOPPY);
}

void Builtins::Generate_StoreIC_SlowStrict(CodeStubAssembler* assembler) {
  Generate_StoreIC_Slow(assembler, STRICT);
}

namespace {
// 7.1.1.1 OrdinaryToPrimitive ( O, hint )
void Generate_OrdinaryToPrimitive(CodeStubAssembler* assembler,
                                  OrdinaryToPrimitiveHint hint) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* input = assembler->Parameter(0);
  Node* context = assembler->Parameter(1);

  Variable var_result(assembler, MachineRepresentation::kTagged);
  Label return_result(assembler, &var_result);

  Handle<String> method_names[2];
  switch (hint) {
    case OrdinaryToPrimitiveHint::kNumber:
      method_names[0] = assembler->factory()->valueOf_string();
      method_names[1] = assembler->factory()->toString_string();
      break;
    case OrdinaryToPrimitiveHint::kString:
      method_names[0] = assembler->factory()->toString_string();
      method_names[1] = assembler->factory()->valueOf_string();
      break;
  }
  for (Handle<String> name : method_names) {
    // Lookup the {name} on the {input}.
    Callable callable = CodeFactory::GetProperty(assembler->isolate());
    Node* name_string = assembler->HeapConstant(name);
    Node* method = assembler->CallStub(callable, context, input, name_string);

    // Check if the {method} is callable.
    Label if_methodiscallable(assembler),
        if_methodisnotcallable(assembler, Label::kDeferred);
    assembler->GotoIf(assembler->WordIsSmi(method), &if_methodisnotcallable);
    Node* method_map = assembler->LoadMap(method);
    Node* method_bit_field = assembler->LoadMapBitField(method_map);
    assembler->Branch(
        assembler->Word32Equal(
            assembler->Word32And(method_bit_field, assembler->Int32Constant(
                                                       1 << Map::kIsCallable)),
            assembler->Int32Constant(0)),
        &if_methodisnotcallable, &if_methodiscallable);

    assembler->Bind(&if_methodiscallable);
    {
      // Call the {method} on the {input}.
      Callable callable = CodeFactory::Call(assembler->isolate());
      Node* result = assembler->CallJS(callable, context, method, input);
      var_result.Bind(result);

      // Return the {result} if it is a primitive.
      assembler->GotoIf(assembler->WordIsSmi(result), &return_result);
      Node* result_instance_type = assembler->LoadInstanceType(result);
      STATIC_ASSERT(FIRST_PRIMITIVE_TYPE == FIRST_TYPE);
      assembler->GotoIf(assembler->Int32LessThanOrEqual(
                            result_instance_type,
                            assembler->Int32Constant(LAST_PRIMITIVE_TYPE)),
                        &return_result);
    }

    // Just continue with the next {name} if the {method} is not callable.
    assembler->Goto(&if_methodisnotcallable);
    assembler->Bind(&if_methodisnotcallable);
  }

  assembler->TailCallRuntime(Runtime::kThrowCannotConvertToPrimitive, context);

  assembler->Bind(&return_result);
  assembler->Return(var_result.value());
}
}  // anonymous namespace

void Builtins::Generate_OrdinaryToPrimitive_Number(
    CodeStubAssembler* assembler) {
  Generate_OrdinaryToPrimitive(assembler, OrdinaryToPrimitiveHint::kNumber);
}

void Builtins::Generate_OrdinaryToPrimitive_String(
    CodeStubAssembler* assembler) {
  Generate_OrdinaryToPrimitive(assembler, OrdinaryToPrimitiveHint::kString);
}

namespace {
// ES6 section 7.1.1 ToPrimitive ( input [ , PreferredType ] )
void Generate_NonPrimitiveToPrimitive(CodeStubAssembler* assembler,
                                      ToPrimitiveHint hint) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Node* input = assembler->Parameter(0);
  Node* context = assembler->Parameter(1);

  // Lookup the @@toPrimitive property on the {input}.
  Callable callable = CodeFactory::GetProperty(assembler->isolate());
  Node* to_primitive_symbol =
      assembler->HeapConstant(assembler->factory()->to_primitive_symbol());
  Node* exotic_to_prim =
      assembler->CallStub(callable, context, input, to_primitive_symbol);

  // Check if {exotic_to_prim} is neither null nor undefined.
  Label ordinary_to_primitive(assembler);
  assembler->GotoIf(
      assembler->WordEqual(exotic_to_prim, assembler->NullConstant()),
      &ordinary_to_primitive);
  assembler->GotoIf(
      assembler->WordEqual(exotic_to_prim, assembler->UndefinedConstant()),
      &ordinary_to_primitive);
  {
    // Invoke the {exotic_to_prim} method on the {input} with a string
    // representation of the {hint}.
    Callable callable = CodeFactory::Call(assembler->isolate());
    Node* hint_string = assembler->HeapConstant(
        assembler->factory()->ToPrimitiveHintString(hint));
    Node* result = assembler->CallJS(callable, context, exotic_to_prim, input,
                                     hint_string);

    // Verify that the {result} is actually a primitive.
    Label if_resultisprimitive(assembler),
        if_resultisnotprimitive(assembler, Label::kDeferred);
    assembler->GotoIf(assembler->WordIsSmi(result), &if_resultisprimitive);
    Node* result_instance_type = assembler->LoadInstanceType(result);
    STATIC_ASSERT(FIRST_PRIMITIVE_TYPE == FIRST_TYPE);
    assembler->Branch(assembler->Int32LessThanOrEqual(
                          result_instance_type,
                          assembler->Int32Constant(LAST_PRIMITIVE_TYPE)),
                      &if_resultisprimitive, &if_resultisnotprimitive);

    assembler->Bind(&if_resultisprimitive);
    {
      // Just return the {result}.
      assembler->Return(result);
    }

    assembler->Bind(&if_resultisnotprimitive);
    {
      // Somehow the @@toPrimitive method on {input} didn't yield a primitive.
      assembler->TailCallRuntime(Runtime::kThrowCannotConvertToPrimitive,
                                 context);
    }
  }

  // Convert using the OrdinaryToPrimitive algorithm instead.
  assembler->Bind(&ordinary_to_primitive);
  {
    Callable callable = CodeFactory::OrdinaryToPrimitive(
        assembler->isolate(), (hint == ToPrimitiveHint::kString)
                                  ? OrdinaryToPrimitiveHint::kString
                                  : OrdinaryToPrimitiveHint::kNumber);
    assembler->TailCallStub(callable, context, input);
  }
}
}  // anonymous namespace

void Builtins::Generate_NonPrimitiveToPrimitive_Default(
    CodeStubAssembler* assembler) {
  Generate_NonPrimitiveToPrimitive(assembler, ToPrimitiveHint::kDefault);
}

void Builtins::Generate_NonPrimitiveToPrimitive_Number(
    CodeStubAssembler* assembler) {
  Generate_NonPrimitiveToPrimitive(assembler, ToPrimitiveHint::kNumber);
}

void Builtins::Generate_NonPrimitiveToPrimitive_String(
    CodeStubAssembler* assembler) {
  Generate_NonPrimitiveToPrimitive(assembler, ToPrimitiveHint::kString);
}

// ES6 section 7.1.3 ToNumber ( argument )
void Builtins::Generate_NonNumberToNumber(CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* input = assembler->Parameter(0);
  Node* context = assembler->Parameter(1);

  // We might need to loop once here due to ToPrimitive conversions.
  Variable var_input(assembler, MachineRepresentation::kTagged);
  Label loop(assembler, &var_input);
  var_input.Bind(input);
  assembler->Goto(&loop);
  assembler->Bind(&loop);
  {
    // Load the current {input} value (known to be a HeapObject).
    Node* input = var_input.value();

    // Dispatch on the {input} instance type.
    Node* input_instance_type = assembler->LoadInstanceType(input);
    Label if_inputisstring(assembler), if_inputisoddball(assembler),
        if_inputisreceiver(assembler, Label::kDeferred),
        if_inputisother(assembler, Label::kDeferred);
    assembler->GotoIf(assembler->Int32LessThan(
                          input_instance_type,
                          assembler->Int32Constant(FIRST_NONSTRING_TYPE)),
                      &if_inputisstring);
    assembler->GotoIf(
        assembler->Word32Equal(input_instance_type,
                               assembler->Int32Constant(ODDBALL_TYPE)),
        &if_inputisoddball);
    STATIC_ASSERT(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
    assembler->Branch(assembler->Int32GreaterThanOrEqual(
                          input_instance_type,
                          assembler->Int32Constant(FIRST_JS_RECEIVER_TYPE)),
                      &if_inputisreceiver, &if_inputisother);

    assembler->Bind(&if_inputisstring);
    {
      // The {input} is a String, use the fast stub to convert it to a Number.
      // TODO(bmeurer): Consider inlining the StringToNumber logic here.
      Callable callable = CodeFactory::StringToNumber(assembler->isolate());
      assembler->TailCallStub(callable, context, input);
    }

    assembler->Bind(&if_inputisoddball);
    {
      // The {input} is an Oddball, we just need to the Number value of it.
      Node* result =
          assembler->LoadObjectField(input, Oddball::kToNumberOffset);
      assembler->Return(result);
    }

    assembler->Bind(&if_inputisreceiver);
    {
      // The {input} is a JSReceiver, we need to convert it to a Primitive first
      // using the ToPrimitive type conversion, preferably yielding a Number.
      Callable callable = CodeFactory::NonPrimitiveToPrimitive(
          assembler->isolate(), ToPrimitiveHint::kNumber);
      Node* result = assembler->CallStub(callable, context, input);

      // Check if the {result} is already a Number.
      Label if_resultisnumber(assembler), if_resultisnotnumber(assembler);
      assembler->GotoIf(assembler->WordIsSmi(result), &if_resultisnumber);
      Node* result_map = assembler->LoadMap(result);
      assembler->Branch(
          assembler->WordEqual(result_map, assembler->HeapNumberMapConstant()),
          &if_resultisnumber, &if_resultisnotnumber);

      assembler->Bind(&if_resultisnumber);
      {
        // The ToPrimitive conversion already gave us a Number, so we're done.
        assembler->Return(result);
      }

      assembler->Bind(&if_resultisnotnumber);
      {
        // We now have a Primitive {result}, but it's not yet a Number.
        var_input.Bind(result);
        assembler->Goto(&loop);
      }
    }

    assembler->Bind(&if_inputisother);
    {
      // The {input} is something else (i.e. Symbol or Simd128Value), let the
      // runtime figure out the correct exception.
      // Note: We cannot tail call to the runtime here, as js-to-wasm
      // trampolines also use this code currently, and they declare all
      // outgoing parameters as untagged, while we would push a tagged
      // object here.
      Node* result = assembler->CallRuntime(Runtime::kToNumber, context, input);
      assembler->Return(result);
    }
  }
}

// ES6 section 7.1.2 ToBoolean ( argument )
void Builtins::Generate_ToBoolean(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;

  Node* value = assembler->Parameter(0);

  Label return_true(assembler), return_false(assembler);
  assembler->BranchIfToBooleanIsTrue(value, &return_true, &return_false);

  assembler->Bind(&return_true);
  assembler->Return(assembler->BooleanConstant(true));

  assembler->Bind(&return_false);
  assembler->Return(assembler->BooleanConstant(false));
}

void Builtins::Generate_KeyedStoreIC_Slow(MacroAssembler* masm) {
  ElementHandlerCompiler::GenerateStoreSlow(masm);
}

void Builtins::Generate_StoreIC_Setter_ForDeopt(MacroAssembler* masm) {
  NamedStoreHandlerCompiler::GenerateStoreViaSetterForDeopt(masm);
}

void Builtins::Generate_KeyedStoreIC_Megamorphic(MacroAssembler* masm) {
  KeyedStoreIC::GenerateMegamorphic(masm, SLOPPY);
}

void Builtins::Generate_KeyedStoreIC_Megamorphic_Strict(MacroAssembler* masm) {
  KeyedStoreIC::GenerateMegamorphic(masm, STRICT);
}

void Builtins::Generate_KeyedStoreIC_Miss(MacroAssembler* masm) {
  KeyedStoreIC::GenerateMiss(masm);
}

void Builtins::Generate_Return_DebugBreak(MacroAssembler* masm) {
  DebugCodegen::GenerateDebugBreakStub(masm,
                                       DebugCodegen::SAVE_RESULT_REGISTER);
}

void Builtins::Generate_Slot_DebugBreak(MacroAssembler* masm) {
  DebugCodegen::GenerateDebugBreakStub(masm,
                                       DebugCodegen::IGNORE_RESULT_REGISTER);
}

void Builtins::Generate_FrameDropper_LiveEdit(MacroAssembler* masm) {
  DebugCodegen::GenerateFrameDropperLiveEdit(masm);
}

Builtins::Builtins() : initialized_(false) {
  memset(builtins_, 0, sizeof(builtins_[0]) * builtin_count);
}

Builtins::~Builtins() {}

namespace {
void PostBuildProfileAndTracing(Isolate* isolate, Code* code,
                                const char* name) {
  PROFILE(isolate, CodeCreateEvent(CodeEventListener::BUILTIN_TAG,
                                   AbstractCode::cast(code), name));
#ifdef ENABLE_DISASSEMBLER
  if (FLAG_print_builtin_code) {
    CodeTracer::Scope trace_scope(isolate->GetCodeTracer());
    OFStream os(trace_scope.file());
    os << "Builtin: " << name << "\n";
    code->Disassemble(name, os);
    os << "\n";
  }
#endif
}

typedef void (*MacroAssemblerGenerator)(MacroAssembler*);
typedef void (*CodeAssemblerGenerator)(CodeStubAssembler*);

Code* BuildWithMacroAssembler(Isolate* isolate,
                              MacroAssemblerGenerator generator,
                              Code::Flags flags, const char* s_name) {
  HandleScope scope(isolate);
  const size_t buffer_size = 32 * KB;
  byte buffer[buffer_size];  // NOLINT(runtime/arrays)
  MacroAssembler masm(isolate, buffer, buffer_size, CodeObjectRequired::kYes);
  DCHECK(!masm.has_frame());
  generator(&masm);
  CodeDesc desc;
  masm.GetCode(&desc);
  Handle<Code> code =
      isolate->factory()->NewCode(desc, flags, masm.CodeObject());
  PostBuildProfileAndTracing(isolate, *code, s_name);
  return *code;
}

Code* BuildAdaptor(Isolate* isolate, Address builtin_address,
                   Builtins::ExitFrameType exit_frame_type, Code::Flags flags,
                   const char* name) {
  HandleScope scope(isolate);
  const size_t buffer_size = 32 * KB;
  byte buffer[buffer_size];  // NOLINT(runtime/arrays)
  MacroAssembler masm(isolate, buffer, buffer_size, CodeObjectRequired::kYes);
  DCHECK(!masm.has_frame());
  Builtins::Generate_Adaptor(&masm, builtin_address, exit_frame_type);
  CodeDesc desc;
  masm.GetCode(&desc);
  Handle<Code> code =
      isolate->factory()->NewCode(desc, flags, masm.CodeObject());
  PostBuildProfileAndTracing(isolate, *code, name);
  return *code;
}

// Builder for builtins implemented in TurboFan with JS linkage.
Code* BuildWithCodeStubAssemblerJS(Isolate* isolate,
                                   CodeAssemblerGenerator generator, int argc,
                                   Code::Flags flags, const char* name) {
  HandleScope scope(isolate);
  Zone zone(isolate->allocator());
  CodeStubAssembler assembler(isolate, &zone, argc, flags, name);
  generator(&assembler);
  Handle<Code> code = assembler.GenerateCode();
  PostBuildProfileAndTracing(isolate, *code, name);
  return *code;
}

// Builder for builtins implemented in TurboFan with CallStub linkage.
Code* BuildWithCodeStubAssemblerCS(Isolate* isolate,
                                   CodeAssemblerGenerator generator,
                                   CallDescriptors::Key interface_descriptor,
                                   Code::Flags flags, const char* name) {
  HandleScope scope(isolate);
  Zone zone(isolate->allocator());
  // The interface descriptor with given key must be initialized at this point
  // and this construction just queries the details from the descriptors table.
  CallInterfaceDescriptor descriptor(isolate, interface_descriptor);
  // Ensure descriptor is already initialized.
  DCHECK_NOT_NULL(descriptor.GetFunctionType());
  CodeStubAssembler assembler(isolate, &zone, descriptor, flags, name);
  generator(&assembler);
  Handle<Code> code = assembler.GenerateCode();
  PostBuildProfileAndTracing(isolate, *code, name);
  return *code;
}
}  // anonymous namespace

void Builtins::SetUp(Isolate* isolate, bool create_heap_objects) {
  DCHECK(!initialized_);

  // Create a scope for the handles in the builtins.
  HandleScope scope(isolate);

  if (create_heap_objects) {
    int index = 0;
    const Code::Flags kBuiltinFlags = Code::ComputeFlags(Code::BUILTIN);
    Code* code;
#define BUILD_CPP(Name)                                                     \
  code = BuildAdaptor(isolate, FUNCTION_ADDR(Builtin_##Name), BUILTIN_EXIT, \
                      kBuiltinFlags, #Name);                                \
  builtins_[index++] = code;
#define BUILD_API(Name)                                             \
  code = BuildAdaptor(isolate, FUNCTION_ADDR(Builtin_##Name), EXIT, \
                      kBuiltinFlags, #Name);                        \
  builtins_[index++] = code;
#define BUILD_TFJ(Name, Argc)                                          \
  code = BuildWithCodeStubAssemblerJS(isolate, &Generate_##Name, Argc, \
                                      kBuiltinFlags, #Name);           \
  builtins_[index++] = code;
#define BUILD_TFS(Name, Kind, Extra, InterfaceDescriptor)              \
  { InterfaceDescriptor##Descriptor descriptor(isolate); }             \
  code = BuildWithCodeStubAssemblerCS(                                 \
      isolate, &Generate_##Name, CallDescriptors::InterfaceDescriptor, \
      Code::ComputeFlags(Code::Kind, Extra), #Name);                   \
  builtins_[index++] = code;
#define BUILD_ASM(Name)                                                        \
  code =                                                                       \
      BuildWithMacroAssembler(isolate, Generate_##Name, kBuiltinFlags, #Name); \
  builtins_[index++] = code;
#define BUILD_ASH(Name, Kind, Extra)                                           \
  code = BuildWithMacroAssembler(                                              \
      isolate, Generate_##Name, Code::ComputeFlags(Code::Kind, Extra), #Name); \
  builtins_[index++] = code;

    BUILTIN_LIST(BUILD_CPP, BUILD_API, BUILD_TFJ, BUILD_TFS, BUILD_ASM,
                 BUILD_ASH, BUILD_ASM);

#undef BUILD_CPP
#undef BUILD_API
#undef BUILD_TFJ
#undef BUILD_TFS
#undef BUILD_ASM
#undef BUILD_ASH
    CHECK_EQ(builtin_count, index);
    for (int i = 0; i < builtin_count; i++) {
      Code::cast(builtins_[i])->set_builtin_index(i);
    }
  }

  // Mark as initialized.
  initialized_ = true;
}

void Builtins::TearDown() { initialized_ = false; }

void Builtins::IterateBuiltins(ObjectVisitor* v) {
  v->VisitPointers(&builtins_[0], &builtins_[0] + builtin_count);
}

const char* Builtins::Lookup(byte* pc) {
  // may be called during initialization (disassembler!)
  if (initialized_) {
    for (int i = 0; i < builtin_count; i++) {
      Code* entry = Code::cast(builtins_[i]);
      if (entry->contains(pc)) return name(i);
    }
  }
  return NULL;
}

const char* Builtins::name(int index) {
  switch (index) {
#define CASE(Name, ...) \
  case k##Name:         \
    return #Name;
    BUILTIN_LIST_ALL(CASE)
#undef CASE
    default:
      UNREACHABLE();
      break;
  }
  return "";
}

void Builtins::Generate_InterruptCheck(MacroAssembler* masm) {
  masm->TailCallRuntime(Runtime::kInterrupt);
}

void Builtins::Generate_StackCheck(MacroAssembler* masm) {
  masm->TailCallRuntime(Runtime::kStackGuard);
}

namespace {

void ValidateSharedTypedArray(CodeStubAssembler* a, compiler::Node* tagged,
                              compiler::Node* context,
                              compiler::Node** out_instance_type,
                              compiler::Node** out_backing_store) {
  using namespace compiler;
  CodeStubAssembler::Label is_smi(a), not_smi(a), is_typed_array(a),
      not_typed_array(a), is_shared(a), not_shared(a), is_float_or_clamped(a),
      not_float_or_clamped(a), invalid(a);

  // Fail if it is not a heap object.
  a->Branch(a->WordIsSmi(tagged), &is_smi, &not_smi);
  a->Bind(&is_smi);
  a->Goto(&invalid);

  // Fail if the array's instance type is not JSTypedArray.
  a->Bind(&not_smi);
  a->Branch(a->WordEqual(a->LoadInstanceType(tagged),
                         a->Int32Constant(JS_TYPED_ARRAY_TYPE)),
            &is_typed_array, &not_typed_array);
  a->Bind(&not_typed_array);
  a->Goto(&invalid);

  // Fail if the array's JSArrayBuffer is not shared.
  a->Bind(&is_typed_array);
  Node* array_buffer = a->LoadObjectField(tagged, JSTypedArray::kBufferOffset);
  Node* is_buffer_shared = a->BitFieldDecode<JSArrayBuffer::IsShared>(
      a->LoadObjectField(array_buffer, JSArrayBuffer::kBitFieldSlot));
  a->Branch(is_buffer_shared, &is_shared, &not_shared);
  a->Bind(&not_shared);
  a->Goto(&invalid);

  // Fail if the array's element type is float32, float64 or clamped.
  a->Bind(&is_shared);
  Node* elements_instance_type = a->LoadInstanceType(
      a->LoadObjectField(tagged, JSObject::kElementsOffset));
  STATIC_ASSERT(FIXED_INT8_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_INT16_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_INT32_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_UINT8_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_UINT16_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  STATIC_ASSERT(FIXED_UINT32_ARRAY_TYPE < FIXED_FLOAT32_ARRAY_TYPE);
  a->Branch(a->Int32LessThan(elements_instance_type,
                             a->Int32Constant(FIXED_FLOAT32_ARRAY_TYPE)),
            &not_float_or_clamped, &is_float_or_clamped);
  a->Bind(&is_float_or_clamped);
  a->Goto(&invalid);

  a->Bind(&invalid);
  a->CallRuntime(Runtime::kThrowNotIntegerSharedTypedArrayError, context,
                 tagged);
  a->Return(a->UndefinedConstant());

  a->Bind(&not_float_or_clamped);
  *out_instance_type = elements_instance_type;

  Node* backing_store =
      a->LoadObjectField(array_buffer, JSArrayBuffer::kBackingStoreOffset);
  Node* byte_offset = a->ChangeUint32ToWord(a->TruncateTaggedToWord32(
      context,
      a->LoadObjectField(tagged, JSArrayBufferView::kByteOffsetOffset)));
  *out_backing_store = a->IntPtrAdd(backing_store, byte_offset);
}

// https://tc39.github.io/ecmascript_sharedmem/shmem.html#Atomics.ValidateAtomicAccess
compiler::Node* ConvertTaggedAtomicIndexToWord32(CodeStubAssembler* a,
                                                 compiler::Node* tagged,
                                                 compiler::Node* context) {
  using namespace compiler;
  CodeStubAssembler::Variable var_result(a, MachineRepresentation::kWord32);

  Callable to_number = CodeFactory::ToNumber(a->isolate());
  Node* number_index = a->CallStub(to_number, context, tagged);
  CodeStubAssembler::Label done(a, &var_result);

  CodeStubAssembler::Label if_numberissmi(a), if_numberisnotsmi(a);
  a->Branch(a->WordIsSmi(number_index), &if_numberissmi, &if_numberisnotsmi);

  a->Bind(&if_numberissmi);
  {
    var_result.Bind(a->SmiToWord32(number_index));
    a->Goto(&done);
  }

  a->Bind(&if_numberisnotsmi);
  {
    Node* number_index_value = a->LoadHeapNumberValue(number_index);
    Node* access_index = a->TruncateFloat64ToWord32(number_index_value);
    Node* test_index = a->ChangeInt32ToFloat64(access_index);

    CodeStubAssembler::Label if_indexesareequal(a), if_indexesarenotequal(a);
    a->Branch(a->Float64Equal(number_index_value, test_index),
              &if_indexesareequal, &if_indexesarenotequal);

    a->Bind(&if_indexesareequal);
    {
      var_result.Bind(access_index);
      a->Goto(&done);
    }

    a->Bind(&if_indexesarenotequal);
    a->Return(
        a->CallRuntime(Runtime::kThrowInvalidAtomicAccessIndexError, context));
  }

  a->Bind(&done);
  return var_result.value();
}

void ValidateAtomicIndex(CodeStubAssembler* a, compiler::Node* index_word,
                         compiler::Node* array_length_word,
                         compiler::Node* context) {
  using namespace compiler;
  // Check if the index is in bounds. If not, throw RangeError.
  CodeStubAssembler::Label if_inbounds(a), if_notinbounds(a);
  a->Branch(
      a->WordOr(a->Int32LessThan(index_word, a->Int32Constant(0)),
                a->Int32GreaterThanOrEqual(index_word, array_length_word)),
      &if_notinbounds, &if_inbounds);
  a->Bind(&if_notinbounds);
  a->Return(
      a->CallRuntime(Runtime::kThrowInvalidAtomicAccessIndexError, context));
  a->Bind(&if_inbounds);
}

}  // anonymous namespace

void Builtins::Generate_AtomicsLoad(CodeStubAssembler* a) {
  using namespace compiler;
  Node* array = a->Parameter(1);
  Node* index = a->Parameter(2);
  Node* context = a->Parameter(3 + 2);

  Node* instance_type;
  Node* backing_store;
  ValidateSharedTypedArray(a, array, context, &instance_type, &backing_store);

  Node* index_word32 = ConvertTaggedAtomicIndexToWord32(a, index, context);
  Node* array_length_word32 = a->TruncateTaggedToWord32(
      context, a->LoadObjectField(array, JSTypedArray::kLengthOffset));
  ValidateAtomicIndex(a, index_word32, array_length_word32, context);
  Node* index_word = a->ChangeUint32ToWord(index_word32);

  CodeStubAssembler::Label i8(a), u8(a), i16(a), u16(a), i32(a), u32(a),
      other(a);
  int32_t case_values[] = {
      FIXED_INT8_ARRAY_TYPE,   FIXED_UINT8_ARRAY_TYPE, FIXED_INT16_ARRAY_TYPE,
      FIXED_UINT16_ARRAY_TYPE, FIXED_INT32_ARRAY_TYPE, FIXED_UINT32_ARRAY_TYPE,
  };
  CodeStubAssembler::Label* case_labels[] = {
      &i8, &u8, &i16, &u16, &i32, &u32,
  };
  a->Switch(instance_type, &other, case_values, case_labels,
            arraysize(case_labels));

  a->Bind(&i8);
  a->Return(
      a->SmiTag(a->AtomicLoad(MachineType::Int8(), backing_store, index_word)));

  a->Bind(&u8);
  a->Return(a->SmiTag(
      a->AtomicLoad(MachineType::Uint8(), backing_store, index_word)));

  a->Bind(&i16);
  a->Return(a->SmiTag(a->AtomicLoad(MachineType::Int16(), backing_store,
                                    a->WordShl(index_word, 1))));

  a->Bind(&u16);
  a->Return(a->SmiTag(a->AtomicLoad(MachineType::Uint16(), backing_store,
                                    a->WordShl(index_word, 1))));

  a->Bind(&i32);
  a->Return(a->ChangeInt32ToTagged(a->AtomicLoad(
      MachineType::Int32(), backing_store, a->WordShl(index_word, 2))));

  a->Bind(&u32);
  a->Return(a->ChangeUint32ToTagged(a->AtomicLoad(
      MachineType::Uint32(), backing_store, a->WordShl(index_word, 2))));

  // This shouldn't happen, we've already validated the type.
  a->Bind(&other);
  a->Return(a->Int32Constant(0));
}

void Builtins::Generate_AtomicsStore(CodeStubAssembler* a) {
  using namespace compiler;
  Node* array = a->Parameter(1);
  Node* index = a->Parameter(2);
  Node* value = a->Parameter(3);
  Node* context = a->Parameter(4 + 2);

  Node* instance_type;
  Node* backing_store;
  ValidateSharedTypedArray(a, array, context, &instance_type, &backing_store);

  Node* index_word32 = ConvertTaggedAtomicIndexToWord32(a, index, context);
  Node* array_length_word32 = a->TruncateTaggedToWord32(
      context, a->LoadObjectField(array, JSTypedArray::kLengthOffset));
  ValidateAtomicIndex(a, index_word32, array_length_word32, context);
  Node* index_word = a->ChangeUint32ToWord(index_word32);

  Callable to_integer = CodeFactory::ToInteger(a->isolate());
  Node* value_integer = a->CallStub(to_integer, context, value);
  Node* value_word32 = a->TruncateTaggedToWord32(context, value_integer);

  CodeStubAssembler::Label u8(a), u16(a), u32(a), other(a);
  int32_t case_values[] = {
      FIXED_INT8_ARRAY_TYPE,   FIXED_UINT8_ARRAY_TYPE, FIXED_INT16_ARRAY_TYPE,
      FIXED_UINT16_ARRAY_TYPE, FIXED_INT32_ARRAY_TYPE, FIXED_UINT32_ARRAY_TYPE,
  };
  CodeStubAssembler::Label* case_labels[] = {
      &u8, &u8, &u16, &u16, &u32, &u32,
  };
  a->Switch(instance_type, &other, case_values, case_labels,
            arraysize(case_labels));

  a->Bind(&u8);
  a->AtomicStore(MachineRepresentation::kWord8, backing_store, index_word,
                 value_word32);
  a->Return(value_integer);

  a->Bind(&u16);
  a->SmiTag(a->AtomicStore(MachineRepresentation::kWord16, backing_store,
                           a->WordShl(index_word, 1), value_word32));
  a->Return(value_integer);

  a->Bind(&u32);
  a->AtomicStore(MachineRepresentation::kWord32, backing_store,
                 a->WordShl(index_word, 2), value_word32);
  a->Return(value_integer);

  // This shouldn't happen, we've already validated the type.
  a->Bind(&other);
  a->Return(a->Int32Constant(0));
}

void Builtins::Generate_CallFunction_ReceiverIsNullOrUndefined(
    MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kNullOrUndefined,
                        TailCallMode::kDisallow);
}

void Builtins::Generate_CallFunction_ReceiverIsNotNullOrUndefined(
    MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kNotNullOrUndefined,
                        TailCallMode::kDisallow);
}

void Builtins::Generate_CallFunction_ReceiverIsAny(MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kAny,
                        TailCallMode::kDisallow);
}

void Builtins::Generate_TailCallFunction_ReceiverIsNullOrUndefined(
    MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kNullOrUndefined,
                        TailCallMode::kAllow);
}

void Builtins::Generate_TailCallFunction_ReceiverIsNotNullOrUndefined(
    MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kNotNullOrUndefined,
                        TailCallMode::kAllow);
}

void Builtins::Generate_TailCallFunction_ReceiverIsAny(MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kAny, TailCallMode::kAllow);
}

void Builtins::Generate_CallBoundFunction(MacroAssembler* masm) {
  Generate_CallBoundFunctionImpl(masm, TailCallMode::kDisallow);
}

void Builtins::Generate_TailCallBoundFunction(MacroAssembler* masm) {
  Generate_CallBoundFunctionImpl(masm, TailCallMode::kAllow);
}

void Builtins::Generate_Call_ReceiverIsNullOrUndefined(MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kNullOrUndefined,
                TailCallMode::kDisallow);
}

void Builtins::Generate_Call_ReceiverIsNotNullOrUndefined(
    MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kNotNullOrUndefined,
                TailCallMode::kDisallow);
}

void Builtins::Generate_Call_ReceiverIsAny(MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kAny, TailCallMode::kDisallow);
}

void Builtins::Generate_TailCall_ReceiverIsNullOrUndefined(
    MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kNullOrUndefined,
                TailCallMode::kAllow);
}

void Builtins::Generate_TailCall_ReceiverIsNotNullOrUndefined(
    MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kNotNullOrUndefined,
                TailCallMode::kAllow);
}

void Builtins::Generate_TailCall_ReceiverIsAny(MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kAny, TailCallMode::kAllow);
}

void Builtins::Generate_InterpreterPushArgsAndCall(MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(masm, TailCallMode::kDisallow,
                                                 CallableType::kAny);
}
void Builtins::Generate_InterpreterPushArgsAndTailCall(MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(masm, TailCallMode::kAllow,
                                                 CallableType::kAny);
}

void Builtins::Generate_InterpreterPushArgsAndCallFunction(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(masm, TailCallMode::kDisallow,
                                                 CallableType::kJSFunction);
}

void Builtins::Generate_InterpreterPushArgsAndTailCallFunction(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(masm, TailCallMode::kAllow,
                                                 CallableType::kJSFunction);
}

void Builtins::Generate_InterpreterPushArgsAndConstruct(MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndConstructImpl(masm, CallableType::kAny);
}

void Builtins::Generate_InterpreterPushArgsAndConstructFunction(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndConstructImpl(
      masm, CallableType::kJSFunction);
}

void Builtins::Generate_MathMax(MacroAssembler* masm) {
  Generate_MathMaxMin(masm, MathMaxMinKind::kMax);
}

void Builtins::Generate_MathMin(MacroAssembler* masm) {
  Generate_MathMaxMin(masm, MathMaxMinKind::kMin);
}

#define DEFINE_BUILTIN_ACCESSOR(Name, ...)                                    \
  Handle<Code> Builtins::Name() {                                             \
    Code** code_address = reinterpret_cast<Code**>(builtin_address(k##Name)); \
    return Handle<Code>(code_address);                                        \
  }
BUILTIN_LIST_ALL(DEFINE_BUILTIN_ACCESSOR)
#undef DEFINE_BUILTIN_ACCESSOR

}  // namespace internal
}  // namespace v8
