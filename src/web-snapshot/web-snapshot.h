// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WEB_SNAPSHOT_WEB_SNAPSHOT_H_
#define V8_WEB_SNAPSHOT_WEB_SNAPSHOT_H_

#include <queue>
#include <vector>

#include "src/handles/handles.h"
#include "src/objects/value-serializer.h"
#include "src/snapshot/serializer.h"  // For ObjectCacheIndexMap

namespace v8 {

class Context;
class Isolate;

template <typename T>
class Local;

namespace internal {

class Context;
class Map;
class Object;
class String;

struct WebSnapshotData {
  uint8_t* buffer = nullptr;
  size_t buffer_size = 0;
  ~WebSnapshotData() { free(buffer); }
};

class WebSnapshotSerializerDeserializer {
 public:
  bool has_error() const { return error_message_ != nullptr; }
  const char* error_message() const { return error_message_; }

  enum ValueType : uint8_t { STRING_ID, OBJECT_ID, FUNCTION_ID };

 protected:
  explicit WebSnapshotSerializerDeserializer(Isolate* isolate)
      : isolate_(isolate) {}
  void Throw(const char* message);
  Isolate* isolate_;
  const char* error_message_ = nullptr;

 private:
  WebSnapshotSerializerDeserializer(const WebSnapshotSerializerDeserializer&) =
      delete;
  WebSnapshotSerializerDeserializer& operator=(
      const WebSnapshotSerializerDeserializer&) = delete;
};

class V8_EXPORT WebSnapshotSerializer
    : public WebSnapshotSerializerDeserializer {
 public:
  explicit WebSnapshotSerializer(v8::Isolate* isolate);
  ~WebSnapshotSerializer();

  bool TakeSnapshot(v8::Local<v8::Context> context,
                    const std::vector<std::string>& exports,
                    WebSnapshotData& data_out);

  // For inspecting the state after taking a snapshot.
  uint32_t string_count() const {
    return static_cast<uint32_t>(string_ids_.size());
  }

  uint32_t map_count() const { return static_cast<uint32_t>(map_ids_.size()); }

  uint32_t context_count() const {
    return static_cast<uint32_t>(context_ids_.size());
  }

  uint32_t function_count() const {
    return static_cast<uint32_t>(function_ids_.size());
  }

  uint32_t object_count() const {
    return static_cast<uint32_t>(object_ids_.size());
  }

 private:
  WebSnapshotSerializer(const WebSnapshotSerializer&) = delete;
  WebSnapshotSerializer& operator=(const WebSnapshotSerializer&) = delete;

  void WriteSnapshot(uint8_t*& buffer, size_t& buffer_size);

  // Returns true if the object was already in the map, false if it was added.
  bool InsertIntoIndexMap(ObjectCacheIndexMap& map, Handle<HeapObject> object,
                          uint32_t& id);

  void SerializeString(Handle<String> string, uint32_t& id);
  void SerializeMap(Handle<Map> map, uint32_t& id);
  void SerializeFunction(Handle<JSFunction> function, uint32_t& id);
  void SerializeContext(Handle<Context> context, uint32_t& id);
  void SerializeObject(Handle<JSObject> object, uint32_t& id);
  void SerializePendingObject(Handle<JSObject> object);
  void SerializeExport(Handle<JSObject> object, const std::string& export_name);
  void WriteValue(Handle<Object> object, ValueSerializer& serializer);

  ValueSerializer string_serializer_;
  ValueSerializer map_serializer_;
  ValueSerializer context_serializer_;
  ValueSerializer function_serializer_;
  ValueSerializer object_serializer_;
  ValueSerializer export_serializer_;

  ObjectCacheIndexMap string_ids_;
  ObjectCacheIndexMap map_ids_;
  ObjectCacheIndexMap context_ids_;
  ObjectCacheIndexMap function_ids_;
  ObjectCacheIndexMap object_ids_;
  uint32_t export_count_ = 0;

  std::queue<Handle<JSObject>> pending_objects_;
};

class V8_EXPORT WebSnapshotDeserializer
    : public WebSnapshotSerializerDeserializer {
 public:
  explicit WebSnapshotDeserializer(v8::Isolate* v8_isolate);
  bool UseWebSnapshot(const uint8_t* data, size_t buffer_size);

  // For inspecting the state after taking a snapshot.
  size_t string_count() const { return strings_.size(); }
  size_t map_count() const { return maps_.size(); }
  size_t context_count() const { return contexts_.size(); }
  size_t function_count() const { return functions_.size(); }
  size_t object_count() const { return objects_.size(); }

 private:
  WebSnapshotDeserializer(const WebSnapshotDeserializer&) = delete;
  WebSnapshotDeserializer& operator=(const WebSnapshotDeserializer&) = delete;

  void DeserializeStrings(const uint8_t* data, size_t& ix, size_t size);
  Handle<String> ReadString(ValueDeserializer& deserializer,
                            bool internalize = false);
  void DeserializeMaps(const uint8_t* data, size_t& ix, size_t size);
  void DeserializeContexts(const uint8_t* data, size_t& ix, size_t size);
  Handle<ScopeInfo> CreateScopeInfo(uint32_t variable_count, bool has_parent);
  void DeserializeFunctions(const uint8_t* data, size_t& ix, size_t size);
  void DeserializeObjects(const uint8_t* data, size_t& ix, size_t size);
  void DeserializeExports(const uint8_t* data, size_t& ix, size_t size);
  void ReadValue(ValueDeserializer& deserializer, Handle<Object>& value,
                 Representation& representation);

  // TODO(v8:11525): Make these FixedArrays.
  std::vector<Handle<String>> strings_;
  std::vector<Handle<Map>> maps_;
  std::vector<Handle<Context>> contexts_;
  std::vector<Handle<JSFunction>> functions_;
  std::vector<Handle<JSObject>> objects_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_WEB_SNAPSHOT_WEB_SNAPSHOT_H_
