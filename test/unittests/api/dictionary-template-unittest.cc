// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-template.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

using DictionaryTemplateTest = v8::TestWithContext;

namespace v8 {

namespace {

v8::Local<v8::String> v8_str(v8::Isolate* isolate, const char* x) {
  return v8::String::NewFromUtf8(isolate, x).ToLocalChecked();
}

}  // namespace

TEST_F(DictionaryTemplateTest, SetPropertiesAndInstantiateWithoutValues) {
  HandleScope scope(isolate());
  constexpr std::string_view property_names[] = {"a", "b"};
  Local<DictionaryTemplate> tpl =
      DictionaryTemplate::New(isolate(), property_names);

  MaybeLocal<Value> values[2];
  Local<Object> instance = tpl->NewInstance(context(), values);
  EXPECT_FALSE(instance.IsEmpty());
  EXPECT_FALSE(
      instance->HasOwnProperty(context(), v8_str(isolate(), "a")).ToChecked());
  EXPECT_FALSE(
      instance->HasOwnProperty(context(), v8_str(isolate(), "b")).ToChecked());
}

TEST_F(DictionaryTemplateTest, SetPropertiesAndInstantiateWithSomeValues) {
  HandleScope scope(isolate());
  constexpr std::string_view property_names[] = {"a", "b"};
  Local<DictionaryTemplate> tpl =
      DictionaryTemplate::New(isolate(), property_names);

  MaybeLocal<Value> values[2] = {{}, v8_str(isolate(), "b_value")};
  Local<Object> instance = tpl->NewInstance(context(), values);
  EXPECT_FALSE(instance.IsEmpty());
  EXPECT_FALSE(
      instance->HasOwnProperty(context(), v8_str(isolate(), "a")).ToChecked());
  EXPECT_TRUE(
      instance->HasOwnProperty(context(), v8_str(isolate(), "b")).ToChecked());
}

TEST_F(DictionaryTemplateTest, SetPropertiesAndInstantiateWithAllValues) {
  HandleScope scope(isolate());
  constexpr std::string_view property_names[] = {"a", "b"};
  Local<DictionaryTemplate> tpl =
      DictionaryTemplate::New(isolate(), property_names);

  MaybeLocal<Value> values[2] = {v8_str(isolate(), "a_value"),
                                 v8_str(isolate(), "b_value")};
  Local<Object> instance = tpl->NewInstance(context(), values);
  EXPECT_FALSE(instance.IsEmpty());
  EXPECT_TRUE(
      instance->HasOwnProperty(context(), v8_str(isolate(), "a")).ToChecked());
  EXPECT_TRUE(
      instance->HasOwnProperty(context(), v8_str(isolate(), "b")).ToChecked());
}

}  // namespace v8
