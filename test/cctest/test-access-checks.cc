// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "test/cctest/cctest.h"

namespace {

int32_t g_cross_context_int = 0;

void NamedGetter(v8::Local<v8::Name> property,
                 const v8::PropertyCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (property->Equals(context, v8_str("cross_context_int")).FromJust())
    info.GetReturnValue().Set(g_cross_context_int);
}

void NamedSetter(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                 const v8::PropertyCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (!property->Equals(context, v8_str("cross_context_int")).FromJust())
    return;
  if (value->IsInt32()) {
    g_cross_context_int = value->ToInt32(context).ToLocalChecked()->Value();
  }
  info.GetReturnValue().Set(value);
}

void NamedQuery(v8::Local<v8::Name> property,
                const v8::PropertyCallbackInfo<v8::Integer>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (!property->Equals(context, v8_str("cross_context_int")).FromJust())
    return;
  info.GetReturnValue().Set(v8::DontDelete);
}

void NamedDeleter(v8::Local<v8::Name> property,
                  const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (!property->Equals(context, v8_str("cross_context_int")).FromJust())
    return;
  info.GetReturnValue().Set(false);
}

void NamedEnumerator(const v8::PropertyCallbackInfo<v8::Array>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Array> names = v8::Array::New(isolate, 1);
  names->Set(context, 0, v8_str("cross_context_int")).FromJust();
  info.GetReturnValue().Set(names);
}

void IndexedGetter(uint32_t index,
                   const v8::PropertyCallbackInfo<v8::Value>& info) {
  if (index == 7) info.GetReturnValue().Set(g_cross_context_int);
}

void IndexedSetter(uint32_t index, v8::Local<v8::Value> value,
                   const v8::PropertyCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (index != 7) return;
  if (value->IsInt32()) {
    g_cross_context_int = value->ToInt32(context).ToLocalChecked()->Value();
  }
  info.GetReturnValue().Set(value);
}

void IndexedQuery(uint32_t index,
                  const v8::PropertyCallbackInfo<v8::Integer>& info) {
  if (index == 7) info.GetReturnValue().Set(v8::DontDelete);
}

void IndexedDeleter(uint32_t index,
                    const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  if (index == 7) info.GetReturnValue().Set(false);
}

void IndexedEnumerator(const v8::PropertyCallbackInfo<v8::Array>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Array> names = v8::Array::New(isolate, 1);
  names->Set(context, 0, v8_str("7")).FromJust();
  info.GetReturnValue().Set(names);
}

bool AccessCheck(v8::Local<v8::Context> accessing_context,
                 v8::Local<v8::Object> accessed_object,
                 v8::Local<v8::Value> data) {
  return false;
}

void GetCrossContextInt(v8::Local<v8::String> property,
                        const v8::PropertyCallbackInfo<v8::Value>& info) {
  info.GetReturnValue().Set(g_cross_context_int);
}

void SetCrossContextInt(v8::Local<v8::String> property,
                        v8::Local<v8::Value> value,
                        const v8::PropertyCallbackInfo<void>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (value->IsInt32()) {
    g_cross_context_int = value->ToInt32(context).ToLocalChecked()->Value();
  }
}

void Return42(v8::Local<v8::String> property,
              const v8::PropertyCallbackInfo<v8::Value>& info) {
  info.GetReturnValue().Set(42);
}

}  // namespace

TEST(AccessCheckWithInterceptor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> global_template =
      v8::ObjectTemplate::New(isolate);
  global_template->SetAccessCheckCallbackAndHandler(
      AccessCheck,
      v8::NamedPropertyHandlerConfiguration(
          NamedGetter, NamedSetter, NamedQuery, NamedDeleter, NamedEnumerator),
      v8::IndexedPropertyHandlerConfiguration(IndexedGetter, IndexedSetter,
                                              IndexedQuery, IndexedDeleter,
                                              IndexedEnumerator));
  global_template->SetNativeDataProperty(
      v8_str("cross_context_int"), GetCrossContextInt, SetCrossContextInt);
  global_template->SetNativeDataProperty(
      v8_str("all_can_read"), Return42, nullptr, v8::Local<v8::Value>(),
      v8::None, v8::Local<v8::AccessorSignature>(), v8::ALL_CAN_READ);

  v8::Local<v8::Context> context0 =
      v8::Context::New(isolate, nullptr, global_template);
  context0->Enter();

  // Running script in this context should work.
  CompileRunChecked(isolate, "this.foo = 42; this[23] = true;");
  ExpectInt32("this.all_can_read", 42);
  CompileRunChecked(isolate, "this.cross_context_int = 23");
  CHECK_EQ(g_cross_context_int, 23);
  ExpectInt32("this.cross_context_int", 23);

  // Create another context.
  {
    v8::HandleScope other_scope(isolate);
    v8::Local<v8::Context> context1 =
        v8::Context::New(isolate, nullptr, global_template);
    context1->Global()
        ->Set(context1, v8_str("other"), context0->Global())
        .FromJust();
    v8::Context::Scope context_scope(context1);

    {
      v8::TryCatch try_catch(isolate);
      CHECK(CompileRun(context1, "this.other.foo").IsEmpty());
    }
    {
      v8::TryCatch try_catch(isolate);
      CHECK(CompileRun(context1, "this.other[23]").IsEmpty());
    }

    // AllCanRead properties are also inaccessible.
    {
      v8::TryCatch try_catch(isolate);
      CHECK(CompileRun(context1, "this.other.all_can_read").IsEmpty());
    }

    // Intercepted properties are accessible, however.
    ExpectInt32("this.other.cross_context_int", 23);
    CompileRunChecked(isolate, "this.other.cross_context_int = 42");
    ExpectInt32("this.other[7]", 42);
    ExpectString("JSON.stringify(Object.getOwnPropertyNames(this.other))",
                 "[\"7\",\"cross_context_int\"]");
  }
}
