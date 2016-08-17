// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/value-serializer.h"

#include <algorithm>
#include <string>

#include "include/v8.h"
#include "src/api.h"
#include "src/base/build_config.h"
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
    EncodeTest(input_functor,
               [this, &output_functor](const std::vector<uint8_t>& data) {
                 DecodeTest(data, output_functor);
               });
  }

  template <typename InputFunctor, typename EncodedDataFunctor>
  void EncodeTest(const InputFunctor& input_functor,
                  const EncodedDataFunctor& encoded_data_functor) {
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
    encoded_data_functor(serializer.ReleaseBuffer());
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

  static std::string Utf8Value(Local<Value> value) {
    String::Utf8Value utf8(value);
    return std::string(*utf8, utf8.length());
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

TEST_F(ValueSerializerTest, RoundTripNumber) {
  RoundTripTest([this]() { return Integer::New(isolate(), 42); },
                [](Local<Value> value) {
                  ASSERT_TRUE(value->IsInt32());
                  EXPECT_EQ(42, Int32::Cast(*value)->Value());
                });
  RoundTripTest([this]() { return Integer::New(isolate(), -31337); },
                [](Local<Value> value) {
                  ASSERT_TRUE(value->IsInt32());
                  EXPECT_EQ(-31337, Int32::Cast(*value)->Value());
                });
  RoundTripTest(
      [this]() {
        return Integer::New(isolate(), std::numeric_limits<int32_t>::min());
      },
      [](Local<Value> value) {
        ASSERT_TRUE(value->IsInt32());
        EXPECT_EQ(std::numeric_limits<int32_t>::min(),
                  Int32::Cast(*value)->Value());
      });
  RoundTripTest([this]() { return Number::New(isolate(), -0.25); },
                [](Local<Value> value) {
                  ASSERT_TRUE(value->IsNumber());
                  EXPECT_EQ(-0.25, Number::Cast(*value)->Value());
                });
  RoundTripTest(
      [this]() {
        return Number::New(isolate(), std::numeric_limits<double>::quiet_NaN());
      },
      [](Local<Value> value) {
        ASSERT_TRUE(value->IsNumber());
        EXPECT_TRUE(std::isnan(Number::Cast(*value)->Value()));
      });
}

TEST_F(ValueSerializerTest, DecodeNumber) {
  // 42 zig-zag encoded (signed)
  DecodeTest({0xff, 0x09, 0x49, 0x54},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsInt32());
               EXPECT_EQ(42, Int32::Cast(*value)->Value());
             });
  // 42 varint encoded (unsigned)
  DecodeTest({0xff, 0x09, 0x55, 0x2a},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsInt32());
               EXPECT_EQ(42, Int32::Cast(*value)->Value());
             });
  // 160 zig-zag encoded (signed)
  DecodeTest({0xff, 0x09, 0x49, 0xc0, 0x02},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsInt32());
               ASSERT_EQ(160, Int32::Cast(*value)->Value());
             });
  // 160 varint encoded (unsigned)
  DecodeTest({0xff, 0x09, 0x55, 0xa0, 0x01},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsInt32());
               ASSERT_EQ(160, Int32::Cast(*value)->Value());
             });
#if defined(V8_TARGET_LITTLE_ENDIAN)
  // IEEE 754 doubles, little-endian byte order
  DecodeTest({0xff, 0x09, 0x4e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd0, 0xbf},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsNumber());
               EXPECT_EQ(-0.25, Number::Cast(*value)->Value());
             });
  // quiet NaN
  DecodeTest({0xff, 0x09, 0x4e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x7f},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsNumber());
               EXPECT_TRUE(std::isnan(Number::Cast(*value)->Value()));
             });
  // signaling NaN
  DecodeTest({0xff, 0x09, 0x4e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf4, 0x7f},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsNumber());
               EXPECT_TRUE(std::isnan(Number::Cast(*value)->Value()));
             });
#endif
  // TODO(jbroman): Equivalent test for big-endian machines.
}

// String constants (in UTF-8) used for string encoding tests.
static const char kHelloString[] = "Hello";
static const char kQuebecString[] = "\x51\x75\xC3\xA9\x62\x65\x63";
static const char kEmojiString[] = "\xF0\x9F\x91\x8A";

TEST_F(ValueSerializerTest, RoundTripString) {
  RoundTripTest([this]() { return String::Empty(isolate()); },
                [](Local<Value> value) {
                  ASSERT_TRUE(value->IsString());
                  EXPECT_EQ(0, String::Cast(*value)->Length());
                });
  // Inside ASCII.
  RoundTripTest([this]() { return StringFromUtf8(kHelloString); },
                [](Local<Value> value) {
                  ASSERT_TRUE(value->IsString());
                  EXPECT_EQ(5, String::Cast(*value)->Length());
                  EXPECT_EQ(kHelloString, Utf8Value(value));
                });
  // Inside Latin-1 (i.e. one-byte string), but not ASCII.
  RoundTripTest([this]() { return StringFromUtf8(kQuebecString); },
                [](Local<Value> value) {
                  ASSERT_TRUE(value->IsString());
                  EXPECT_EQ(6, String::Cast(*value)->Length());
                  EXPECT_EQ(kQuebecString, Utf8Value(value));
                });
  // An emoji (decodes to two 16-bit chars).
  RoundTripTest([this]() { return StringFromUtf8(kEmojiString); },
                [](Local<Value> value) {
                  ASSERT_TRUE(value->IsString());
                  EXPECT_EQ(2, String::Cast(*value)->Length());
                  EXPECT_EQ(kEmojiString, Utf8Value(value));
                });
}

TEST_F(ValueSerializerTest, DecodeString) {
  // Decoding the strings above from UTF-8.
  DecodeTest({0xff, 0x09, 0x53, 0x00},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsString());
               EXPECT_EQ(0, String::Cast(*value)->Length());
             });
  DecodeTest({0xff, 0x09, 0x53, 0x05, 'H', 'e', 'l', 'l', 'o'},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsString());
               EXPECT_EQ(5, String::Cast(*value)->Length());
               EXPECT_EQ(kHelloString, Utf8Value(value));
             });
  DecodeTest({0xff, 0x09, 0x53, 0x07, 'Q', 'u', 0xc3, 0xa9, 'b', 'e', 'c'},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsString());
               EXPECT_EQ(6, String::Cast(*value)->Length());
               EXPECT_EQ(kQuebecString, Utf8Value(value));
             });
  DecodeTest({0xff, 0x09, 0x53, 0x04, 0xf0, 0x9f, 0x91, 0x8a},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsString());
               EXPECT_EQ(2, String::Cast(*value)->Length());
               EXPECT_EQ(kEmojiString, Utf8Value(value));
             });

// And from two-byte strings (endianness dependent).
#if defined(V8_TARGET_LITTLE_ENDIAN)
  DecodeTest({0xff, 0x09, 0x63, 0x00},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsString());
               EXPECT_EQ(0, String::Cast(*value)->Length());
             });
  DecodeTest({0xff, 0x09, 0x63, 0x0a, 'H', '\0', 'e', '\0', 'l', '\0', 'l',
              '\0', 'o', '\0'},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsString());
               EXPECT_EQ(5, String::Cast(*value)->Length());
               EXPECT_EQ(kHelloString, Utf8Value(value));
             });
  DecodeTest({0xff, 0x09, 0x63, 0x0c, 'Q', '\0', 'u', '\0', 0xe9, '\0', 'b',
              '\0', 'e', '\0', 'c', '\0'},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsString());
               EXPECT_EQ(6, String::Cast(*value)->Length());
               EXPECT_EQ(kQuebecString, Utf8Value(value));
             });
  DecodeTest({0xff, 0x09, 0x63, 0x04, 0x3d, 0xd8, 0x4a, 0xdc},
             [](Local<Value> value) {
               ASSERT_TRUE(value->IsString());
               EXPECT_EQ(2, String::Cast(*value)->Length());
               EXPECT_EQ(kEmojiString, Utf8Value(value));
             });
#endif
  // TODO(jbroman): The same for big-endian systems.
}

TEST_F(ValueSerializerTest, DecodeInvalidString) {
  // UTF-8 string with too few bytes available.
  InvalidDecodeTest({0xff, 0x09, 0x53, 0x10, 'v', '8'});
#if defined(V8_TARGET_LITTLE_ENDIAN)
  // Two-byte string with too few bytes available.
  InvalidDecodeTest({0xff, 0x09, 0x63, 0x10, 'v', '\0', '8', '\0'});
  // Two-byte string with an odd byte length.
  InvalidDecodeTest({0xff, 0x09, 0x63, 0x03, 'v', '\0', '8'});
#endif
  // TODO(jbroman): The same for big-endian systems.
}

TEST_F(ValueSerializerTest, EncodeTwoByteStringUsesPadding) {
  // As long as the output has a version that Blink expects to be able to read,
  // we must respect its alignment requirements. It requires that two-byte
  // characters be aligned.
  EncodeTest(
      [this]() {
        // We need a string whose length will take two bytes to encode, so that
        // a padding byte is needed to keep the characters aligned. The string
        // must also have a two-byte character, so that it gets the two-byte
        // encoding.
        std::string string(200, ' ');
        string += kEmojiString;
        return StringFromUtf8(string.c_str());
      },
      [](const std::vector<uint8_t>& data) {
        // This is a sufficient but not necessary condition to be aligned.
        // Note that the third byte (0x00) is padding.
        const uint8_t expected_prefix[] = {0xff, 0x09, 0x00, 0x63, 0x94, 0x03};
        ASSERT_GT(data.size(), sizeof(expected_prefix) / sizeof(uint8_t));
        EXPECT_TRUE(std::equal(std::begin(expected_prefix),
                               std::end(expected_prefix), data.begin()));
      });
}

}  // namespace
}  // namespace v8
