// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/value-serializer.h"
#include "include/v8.h"
#include "src/api.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace {

class ValueSerializerTest : public TestWithIsolate {
 protected:
  ValueSerializerTest()
      : serialization_context_(Context::New(isolate())),
        deserialization_context_(Context::New(isolate())) {}

  const Local<Context>& serialization_context() {
    return serialization_context_;
  }
  const Local<Context>& deserialization_context() {
    return deserialization_context_;
  }

  template <typename InputFunctor, typename OutputFunctor>
  void RoundTripTest(const InputFunctor& input_functor,
                     const OutputFunctor& output_functor) {
    std::vector<uint8_t> data;
    {
      Context::Scope scope(serialization_context());
      TryCatch try_catch(isolate());
      // TODO(jbroman): Use the public API once it exists.
      Local<Value> input_value = input_functor();
      i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate());
      i::HandleScope handle_scope(internal_isolate);
      i::ValueSerializer serializer;
      serializer.WriteHeader();
      ASSERT_TRUE(serializer.WriteObject(Utils::OpenHandle(*input_value))
                      .FromMaybe(false));
      ASSERT_FALSE(try_catch.HasCaught());
      data = serializer.ReleaseBuffer();
    }
    DecodeTest(data, output_functor);
  }

  template <typename OutputFunctor>
  void DecodeTest(const std::vector<uint8_t>& data,
                  const OutputFunctor& output_functor) {
    Context::Scope scope(deserialization_context());
    TryCatch try_catch(isolate());
    // TODO(jbroman): Use the public API once it exists.
    i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate());
    i::HandleScope handle_scope(internal_isolate);
    i::ValueDeserializer deserializer(
        internal_isolate,
        i::Vector<const uint8_t>(&data[0], static_cast<int>(data.size())));
    ASSERT_TRUE(deserializer.ReadHeader().FromMaybe(false));
    Local<Value> result;
    ASSERT_TRUE(ToLocal<Value>(deserializer.ReadObject(), &result));
    ASSERT_FALSE(result.IsEmpty());
    ASSERT_FALSE(try_catch.HasCaught());
    ASSERT_TRUE(deserialization_context()
                    ->Global()
                    ->CreateDataProperty(deserialization_context_,
                                         StringFromUtf8("result"), result)
                    .FromMaybe(false));
    output_functor(result);
    ASSERT_FALSE(try_catch.HasCaught());
  }

  void InvalidDecodeTest(const std::vector<uint8_t>& data) {
    Context::Scope scope(deserialization_context());
    TryCatch try_catch(isolate());
    i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate());
    i::HandleScope handle_scope(internal_isolate);
    i::ValueDeserializer deserializer(
        internal_isolate,
        i::Vector<const uint8_t>(&data[0], static_cast<int>(data.size())));
    Maybe<bool> header_result = deserializer.ReadHeader();
    if (header_result.IsNothing()) return;
    ASSERT_TRUE(header_result.ToChecked());
    ASSERT_TRUE(deserializer.ReadObject().is_null());
  }

  Local<Value> EvaluateScriptForInput(const char* utf8_source) {
    Local<String> source = StringFromUtf8(utf8_source);
    Local<Script> script =
        Script::Compile(serialization_context_, source).ToLocalChecked();
    return script->Run(serialization_context_).ToLocalChecked();
  }

  bool EvaluateScriptForResultBool(const char* utf8_source) {
    Local<String> source = StringFromUtf8(utf8_source);
    Local<Script> script =
        Script::Compile(deserialization_context_, source).ToLocalChecked();
    Local<Value> value = script->Run(deserialization_context_).ToLocalChecked();
    return value->BooleanValue(deserialization_context_).FromJust();
  }

  Local<String> StringFromUtf8(const char* source) {
    return String::NewFromUtf8(isolate(), source, NewStringType::kNormal)
        .ToLocalChecked();
  }

 private:
  Local<Context> serialization_context_;
  Local<Context> deserialization_context_;

  DISALLOW_COPY_AND_ASSIGN(ValueSerializerTest);
};

TEST_F(ValueSerializerTest, DecodeInvalid) {
  // Version tag but no content.
  InvalidDecodeTest({0xff});
  // Version too large.
  InvalidDecodeTest({0xff, 0x7f, 0x5f});
  // Nonsense tag.
  InvalidDecodeTest({0xff, 0x09, 0xdd});
}

TEST_F(ValueSerializerTest, RoundTripOddball) {
  RoundTripTest([this]() { return Undefined(isolate()); },
                [](Local<Value> value) { EXPECT_TRUE(value->IsUndefined()); });
  RoundTripTest([this]() { return True(isolate()); },
                [](Local<Value> value) { EXPECT_TRUE(value->IsTrue()); });
  RoundTripTest([this]() { return False(isolate()); },
                [](Local<Value> value) { EXPECT_TRUE(value->IsFalse()); });
  RoundTripTest([this]() { return Null(isolate()); },
                [](Local<Value> value) { EXPECT_TRUE(value->IsNull()); });
}

TEST_F(ValueSerializerTest, DecodeOddball) {
  // What this code is expected to generate.
  DecodeTest({0xff, 0x09, 0x5f},
             [](Local<Value> value) { EXPECT_TRUE(value->IsUndefined()); });
  DecodeTest({0xff, 0x09, 0x54},
             [](Local<Value> value) { EXPECT_TRUE(value->IsTrue()); });
  DecodeTest({0xff, 0x09, 0x46},
             [](Local<Value> value) { EXPECT_TRUE(value->IsFalse()); });
  DecodeTest({0xff, 0x09, 0x30},
             [](Local<Value> value) { EXPECT_TRUE(value->IsNull()); });

  // What v9 of the Blink code generates.
  DecodeTest({0xff, 0x09, 0x3f, 0x00, 0x5f, 0x00},
             [](Local<Value> value) { EXPECT_TRUE(value->IsUndefined()); });
  DecodeTest({0xff, 0x09, 0x3f, 0x00, 0x54, 0x00},
             [](Local<Value> value) { EXPECT_TRUE(value->IsTrue()); });
  DecodeTest({0xff, 0x09, 0x3f, 0x00, 0x46, 0x00},
             [](Local<Value> value) { EXPECT_TRUE(value->IsFalse()); });
  DecodeTest({0xff, 0x09, 0x3f, 0x00, 0x30, 0x00},
             [](Local<Value> value) { EXPECT_TRUE(value->IsNull()); });

  // v0 (with no explicit version).
  DecodeTest({0x5f, 0x00},
             [](Local<Value> value) { EXPECT_TRUE(value->IsUndefined()); });
  DecodeTest({0x54, 0x00},
             [](Local<Value> value) { EXPECT_TRUE(value->IsTrue()); });
  DecodeTest({0x46, 0x00},
             [](Local<Value> value) { EXPECT_TRUE(value->IsFalse()); });
  DecodeTest({0x30, 0x00},
             [](Local<Value> value) { EXPECT_TRUE(value->IsNull()); });
}

}  // namespace
}  // namespace v8
