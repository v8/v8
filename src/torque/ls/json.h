// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_LS_JSON_H_
#define V8_TORQUE_LS_JSON_H_

#include <map>
#include <string>
#include <vector>

#include "src/base/template-utils.h"

namespace v8 {
namespace internal {
namespace torque {
namespace ls {

struct JsonValue;

using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
  enum { OBJECT, ARRAY, STRING, NUMBER, BOOL, IS_NULL } tag;
  std::unique_ptr<JsonObject> object;
  std::unique_ptr<JsonArray> array;
  std::string string;
  double number = 0;
  bool flag = false;
};

inline JsonValue From(JsonObject object) {
  JsonValue result;
  result.tag = JsonValue::OBJECT;
  result.object = base::make_unique<JsonObject>(std::move(object));
  return result;
}

inline JsonValue From(const std::string& string) {
  JsonValue result;
  result.tag = JsonValue::STRING;
  result.string = string;
  return result;
}

inline JsonValue From(double number) {
  JsonValue result;
  result.tag = JsonValue::NUMBER;
  result.number = number;
  return result;
}

inline JsonValue From(bool b) {
  JsonValue result;
  result.tag = JsonValue::BOOL;
  result.flag = b;
  return result;
}

inline JsonValue From(JsonArray array) {
  JsonValue result;
  result.tag = JsonValue::ARRAY;
  result.array = base::make_unique<JsonArray>(std::move(array));
  return result;
}

std::string SerializeToString(const JsonValue& value);

}  // namespace ls
}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_LS_JSON_H_
