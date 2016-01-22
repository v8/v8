// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/factory.h"
#include "src/isolate-inl.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION_RETURN_TRIPLE(Runtime_ForInPrepare) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());

  if (!args[0]->IsJSReceiver()) {
    return MakeTriple(isolate->ThrowIllegalOperation(), nullptr, nullptr);
  }
  Handle<JSReceiver> receiver = args.at<JSReceiver>(0);

  Object* property_names = Runtime_GetPropertyNamesFast(
      1, Handle<Object>::cast(receiver).location(), isolate);
  if (isolate->has_pending_exception()) {
    return MakeTriple(property_names, nullptr, nullptr);
  }

  Handle<Object> cache_type(property_names, isolate);
  Handle<FixedArray> cache_array;
  int cache_length;

  if (cache_type->IsMap()) {
    Handle<Map> cache_type_map = Handle<Map>::cast(cache_type);
    int const enum_length = cache_type_map->EnumLength();
    DescriptorArray* descriptors = cache_type_map->instance_descriptors();
    if (enum_length && descriptors->HasEnumCache()) {
      cache_array = handle(descriptors->GetEnumCache(), isolate);
      cache_length = enum_length;
    } else {
      cache_array = isolate->factory()->empty_fixed_array();
      cache_length = 0;
    }
  } else {
    cache_array = Handle<FixedArray>::cast(cache_type);
    cache_length = cache_array->length();
    // Cache type of SMI one entails slow check.
    cache_type = Handle<Object>(Smi::FromInt(1), isolate);
  }

  return MakeTriple(*cache_type, *cache_array, Smi::FromInt(cache_length));
}


RUNTIME_FUNCTION(Runtime_ForInDone) {
  SealHandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_SMI_ARG_CHECKED(index, 0);
  CONVERT_SMI_ARG_CHECKED(length, 1);
  DCHECK_LE(0, index);
  DCHECK_LE(index, length);
  return isolate->heap()->ToBoolean(index == length);
}


RUNTIME_FUNCTION(Runtime_ForInFilter) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSReceiver, receiver, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, key, 1);
  // TODO(turbofan): Fast case for array indices.
  Handle<Name> name;
  if (!Object::ToName(isolate, key).ToHandle(&name)) {
    return isolate->heap()->exception();
  }
  Maybe<bool> result = JSReceiver::HasProperty(receiver, name);
  if (!result.IsJust()) return isolate->heap()->exception();
  if (result.FromJust()) return *name;
  return isolate->heap()->undefined_value();
}


RUNTIME_FUNCTION(Runtime_ForInNext) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSReceiver, receiver, 0);
  CONVERT_ARG_HANDLE_CHECKED(FixedArray, cache_array, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, cache_type, 2);
  CONVERT_SMI_ARG_CHECKED(index, 3);
  Handle<Object> key = handle(cache_array->get(index), isolate);
  // Don't need filtering if expected map still matches that of the receiver,
  // and neither for proxies.
  if (receiver->map() == *cache_type || *cache_type == Smi::FromInt(0)) {
    return *key;
  }
  // TODO(turbofan): Fast case for array indices.
  Handle<Name> name;
  if (!Object::ToName(isolate, key).ToHandle(&name)) {
    return isolate->heap()->exception();
  }
  Maybe<bool> result = JSReceiver::HasProperty(receiver, name);
  if (!result.IsJust()) return isolate->heap()->exception();
  if (result.FromJust()) return *name;
  return isolate->heap()->undefined_value();
}


RUNTIME_FUNCTION(Runtime_ForInStep) {
  SealHandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_SMI_ARG_CHECKED(index, 0);
  DCHECK_LE(0, index);
  DCHECK_LT(index, Smi::kMaxValue);
  return Smi::FromInt(index + 1);
}

}  // namespace internal
}  // namespace v8
