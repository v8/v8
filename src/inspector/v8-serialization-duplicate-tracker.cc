// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/v8-serialization-duplicate-tracker.h"

#include "include/v8-context.h"
#include "include/v8-isolate.h"
#include "include/v8-primitive.h"
#include "src/base/logging.h"

namespace v8_inspector {

std::unique_ptr<protocol::DictionaryValue>
V8SerializationDuplicateTracker::LinkExistingOrCreate(
    v8::Local<v8::Value> v8Value, bool* isKnown) {
  std::unique_ptr<protocol::DictionaryValue> result =
      protocol::DictionaryValue::create();

  protocol::DictionaryValue* maybeKnownSerializedValue =
      FindKnownSerializedValue(v8Value);

  if (maybeKnownSerializedValue == nullptr) {
    *isKnown = false;
    // Keep reference to the serialized value, so that
    // `weakLocalObjectReference` can be set later.
    SetKnownSerializedValue(v8Value, result.get());
  } else {
    *isKnown = true;

    String16 type;
    maybeKnownSerializedValue->getString("type", &type);
    result->setString("type", type);

    int weakLocalObjectReference;
    // If `maybeKnownSerializedValue` has no `weakLocalObjectReference` yet,
    // it's need to be set.
    if (!maybeKnownSerializedValue->getInteger("weakLocalObjectReference",
                                               &weakLocalObjectReference)) {
      weakLocalObjectReference = m_counter++;
      maybeKnownSerializedValue->setInteger("weakLocalObjectReference",
                                            weakLocalObjectReference);
    }
    result->setInteger("weakLocalObjectReference", weakLocalObjectReference);
  }

  return result;
}

void V8SerializationDuplicateTracker::SetKnownSerializedValue(
    v8::Local<v8::Value> v8Value, protocol::DictionaryValue* serializedValue) {
  // Don't store the raw `serializedValue` pointer in the (in-sandbox) map,
  // but only its index into `m_serializedValues`.
  int32_t index = static_cast<int32_t>(m_serializedValues.size());
  m_serializedValues.push_back(serializedValue);
  m_v8ObjectToSerializedDictionary =
      m_v8ObjectToSerializedDictionary
          ->Set(m_context, v8Value,
                v8::Integer::New(v8::Isolate::GetCurrent(), index))
          .ToLocalChecked();
}

protocol::DictionaryValue*
V8SerializationDuplicateTracker::FindKnownSerializedValue(
    v8::Local<v8::Value> v8Value) {
  v8::Local<v8::Value> knownValue;
  if (!m_v8ObjectToSerializedDictionary->Get(m_context, v8Value)
           .ToLocal(&knownValue) ||
      !knownValue->IsInt32()) {
    return nullptr;
  }

  // The map lives in the sandbox and might have been corrupted, so the index
  // must be treated as untrusted and bounds-checked.
  int32_t index = knownValue.As<v8::Int32>()->Value();
  if (index < 0 || static_cast<size_t>(index) >= m_serializedValues.size()) {
    return nullptr;
  }
  return m_serializedValues[index];
}

V8SerializationDuplicateTracker::V8SerializationDuplicateTracker(
    v8::Local<v8::Context> context)
    : m_context(context),
      m_counter(1),
      m_v8ObjectToSerializedDictionary(
          v8::Map::New(v8::Isolate::GetCurrent())) {}
}  // namespace v8_inspector
