// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/heap/unified-heap-utils.h"

#include "src/api/api-inl.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

v8::Local<v8::Object> ConstructTraceableJSApiObject(
    v8::Local<v8::Context> context, void* object, const char* class_name) {
  v8::EscapableHandleScope scope(context->GetIsolate());
  v8::Local<v8::FunctionTemplate> function_t =
      v8::FunctionTemplate::New(context->GetIsolate());
  if (strlen(class_name) != 0) {
    function_t->SetClassName(
        v8::String::NewFromUtf8(v8::Isolate::GetCurrent(), class_name)
            .ToLocalChecked());
  }
  v8::Local<v8::ObjectTemplate> instance_t = function_t->InstanceTemplate();
  instance_t->SetInternalFieldCount(2);
  v8::Local<v8::Function> function =
      function_t->GetFunction(context).ToLocalChecked();
  v8::Local<v8::Object> instance =
      function->NewInstance(context).ToLocalChecked();
  instance->SetAlignedPointerInInternalField(0, object);
  instance->SetAlignedPointerInInternalField(1, object);
  CHECK(!instance.IsEmpty());
  i::Handle<i::JSReceiver> js_obj = v8::Utils::OpenHandle(*instance);
  CHECK_EQ(i::JS_API_OBJECT_TYPE, js_obj->map().instance_type());
  return scope.Escape(instance);
}

}  // namespace internal
}  // namespace v8
