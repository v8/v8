// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/ls/json-parser.h"
#include "src/torque/ls/json.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace torque {
namespace ls {

TEST(TestJsonPrimitives) {
  const JsonValue true_result = ParseJson("true");
  CHECK_EQ(true_result.tag, JsonValue::BOOL);
  CHECK_EQ(true_result.flag, true);

  const JsonValue false_result = ParseJson("false");
  CHECK_EQ(false_result.tag, JsonValue::BOOL);
  CHECK_EQ(false_result.flag, false);

  const JsonValue null_result = ParseJson("null");
  CHECK_EQ(null_result.tag, JsonValue::IS_NULL);

  const JsonValue number = ParseJson("42");
  CHECK_EQ(number.tag, JsonValue::NUMBER);
  CHECK_EQ(number.number, 42);
}

TEST(TestJsonStrings) {
  const JsonValue basic = ParseJson("\"basic\"");
  CHECK_EQ(basic.tag, JsonValue::STRING);
  CHECK_EQ(basic.string, "basic");

  const JsonValue singleQuote = ParseJson("\"'\"");
  CHECK_EQ(singleQuote.tag, JsonValue::STRING);
  CHECK_EQ(singleQuote.string, "'");
}

TEST(TestJsonArrays) {
  const JsonValue empty_array = ParseJson("[]");
  CHECK_EQ(empty_array.tag, JsonValue::ARRAY);
  CHECK_EQ(empty_array.array->size(), 0);

  const JsonValue number_array = ParseJson("[1, 2, 3, 4]");
  CHECK_EQ(number_array.tag, JsonValue::ARRAY);

  const JsonArray& array = *number_array.array;
  CHECK_EQ(array.size(), 4);
  CHECK_EQ(array[1].tag, JsonValue::NUMBER);
  CHECK_EQ(array[1].number, 2);

  const JsonValue string_array_object = ParseJson("[\"a\", \"b\"]");
  CHECK_EQ(string_array_object.tag, JsonValue::ARRAY);

  const JsonArray& string_array = *string_array_object.array;
  CHECK_EQ(string_array.size(), 2);
  CHECK_EQ(string_array[1].tag, JsonValue::STRING);
  CHECK_EQ(string_array[1].string, "b");
}

TEST(TestJsonObjects) {
  const JsonValue empty_object = ParseJson("{}");
  CHECK_EQ(empty_object.tag, JsonValue::OBJECT);
  CHECK_EQ(empty_object.object->size(), 0);

  const JsonValue primitive_fields = ParseJson("{ \"flag\": true, \"id\": 5}");
  CHECK_EQ(primitive_fields.tag, JsonValue::OBJECT);

  const JsonValue& flag = primitive_fields.object->at("flag");
  CHECK_EQ(flag.tag, JsonValue::BOOL);
  CHECK(flag.flag);

  const JsonValue& id = primitive_fields.object->at("id");
  CHECK_EQ(id.tag, JsonValue::NUMBER);
  CHECK_EQ(id.number, 5);

  const JsonValue& complex_fields =
      ParseJson("{ \"array\": [], \"object\": { \"name\": \"torque\" } }");
  CHECK_EQ(complex_fields.tag, JsonValue::OBJECT);

  const JsonValue& array = complex_fields.object->at("array");
  CHECK_EQ(array.tag, JsonValue::ARRAY);
  CHECK_EQ(array.array->size(), 0);

  const JsonValue& object = complex_fields.object->at("object");
  CHECK_EQ(object.tag, JsonValue::OBJECT);
  CHECK_EQ(object.object->at("name").tag, JsonValue::STRING);
  CHECK_EQ(object.object->at("name").string, "torque");
}

}  // namespace ls
}  // namespace torque
}  // namespace internal
}  // namespace v8
