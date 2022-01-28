// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WEB_SNAPSHOT_WEB_SNAPSHOT_H_
#define V8_WEB_SNAPSHOT_WEB_SNAPSHOT_H_

#include <queue>

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

struct WebSnapshotData : public std::enable_shared_from_this<WebSnapshotData> {
  uint8_t* buffer = nullptr;
  size_t buffer_size = 0;
  WebSnapshotData() = default;
  WebSnapshotData(const WebSnapshotData&) = delete;
  WebSnapshotData& operator=(const WebSnapshotData&) = delete;
  ~WebSnapshotData() { free(buffer); }
};

class WebSnapshotSerializerDeserializer {
 public:
  inline bool has_error() const { return error_message_ != nullptr; }
  const char* error_message() const { return error_message_; }

  enum ValueType : uint8_t {
    FALSE_CONSTANT,
    TRUE_CONSTANT,
    NULL_CONSTANT,
    UNDEFINED_CONSTANT,
    INTEGER,
    DOUBLE,
    STRING_ID,
    ARRAY_ID,
    OBJECT_ID,
    FUNCTION_ID,
    CLASS_ID,
    REGEXP
  };

  static constexpr uint8_t kMagicNumber[4] = {'+', '+', '+', ';'};

  enum ContextType : uint8_t { FUNCTION, BLOCK };

  enum PropertyAttributesType : uint8_t { DEFAULT, CUSTOM };

  uint32_t FunctionKindToFunctionFlags(FunctionKind kind);
  FunctionKind FunctionFlagsToFunctionKind(uint32_t flags);
  bool IsFunctionOrMethod(uint32_t flags);
  bool IsConstructor(uint32_t flags);

  uint32_t GetDefaultAttributeFlags();
  uint32_t AttributesToFlags(PropertyDetails details);
  PropertyAttributes FlagsToAttributes(uint32_t flags);

  // The maximum count of items for each value type (strings, objects etc.)
  static constexpr uint32_t kMaxItemCount =
      static_cast<uint32_t>(FixedArray::kMaxLength - 1);
  // This ensures indices and lengths can be converted between uint32_t and int
  // without problems:
  STATIC_ASSERT(kMaxItemCount < std::numeric_limits<int32_t>::max());

 protected:
  explicit WebSnapshotSerializerDeserializer(Isolate* isolate)
      : isolate_(isolate) {}
  // Not virtual, on purpose (because it doesn't need to be).
  void Throw(const char* message);
  Isolate* isolate_;
  const char* error_message_ = nullptr;

 private:
  WebSnapshotSerializerDeserializer(const WebSnapshotSerializerDeserializer&) =
      delete;
  WebSnapshotSerializerDeserializer& operator=(
      const WebSnapshotSerializerDeserializer&) = delete;

  // Keep most common function kinds in the 7 least significant bits to make the
  // flags fit in 1 byte.
  using AsyncFunctionBitField = base::BitField<bool, 0, 1>;
  using GeneratorFunctionBitField = AsyncFunctionBitField::Next<bool, 1>;
  using ArrowFunctionBitField = GeneratorFunctionBitField::Next<bool, 1>;
  using MethodBitField = ArrowFunctionBitField::Next<bool, 1>;
  using StaticBitField = MethodBitField::Next<bool, 1>;
  using ClassConstructorBitField = StaticBitField::Next<bool, 1>;
  using DefaultConstructorBitField = ClassConstructorBitField::Next<bool, 1>;
  using DerivedConstructorBitField = DefaultConstructorBitField::Next<bool, 1>;

  using ReadOnlyBitField = base::BitField<bool, 0, 1>;
  using ConfigurableBitField = ReadOnlyBitField::Next<bool, 1>;
  using EnumerableBitField = ConfigurableBitField::Next<bool, 1>;
};

class V8_EXPORT WebSnapshotSerializer
    : public WebSnapshotSerializerDeserializer {
 public:
  explicit WebSnapshotSerializer(v8::Isolate* isolate);
  ~WebSnapshotSerializer();

  bool TakeSnapshot(v8::Local<v8::Context> context,
                    v8::Local<v8::PrimitiveArray> exports,
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

  uint32_t class_count() const {
    return static_cast<uint32_t>(class_ids_.size());
  }

  uint32_t array_count() const {
    return static_cast<uint32_t>(array_ids_.size());
  }

  uint32_t object_count() const {
    return static_cast<uint32_t>(object_ids_.size());
  }

 private:
  WebSnapshotSerializer(const WebSnapshotSerializer&) = delete;
  WebSnapshotSerializer& operator=(const WebSnapshotSerializer&) = delete;

  void SerializePendingItems();
  void WriteSnapshot(uint8_t*& buffer, size_t& buffer_size);

  // Returns true if the object was already in the map, false if it was added.
  bool InsertIntoIndexMap(ObjectCacheIndexMap& map, Handle<HeapObject> object,
                          uint32_t& id);

  void Discovery(Handle<Object> object);
  void DiscoverFunction(Handle<JSFunction> function);
  void DiscoverClass(Handle<JSFunction> function);
  void DiscoverContextAndPrototype(Handle<JSFunction> function);
  void DiscoverContext(Handle<Context> context);
  void DiscoverSource(Handle<JSFunction> function);
  void DiscoverArray(Handle<JSArray> array);
  void DiscoverObject(Handle<JSObject> object);

  void SerializeSource();
  void SerializeFunctionInfo(ValueSerializer* serializer,
                             Handle<JSFunction> function);

  void SerializeString(Handle<String> string, uint32_t& id);
  void SerializeMap(Handle<Map> map, uint32_t& id);

  void SerializeFunction(Handle<JSFunction> function);
  void SerializeClass(Handle<JSFunction> function);
  void SerializeContext(Handle<Context> context);
  void SerializeArray(Handle<JSArray> array);
  void SerializeObject(Handle<JSObject> object);

  void SerializeExport(Handle<JSObject> object, Handle<String> export_name);
  void WriteValue(Handle<Object> object, ValueSerializer& serializer);

  uint32_t GetFunctionId(JSFunction function);
  uint32_t GetClassId(JSFunction function);
  uint32_t GetContextId(Context context);
  uint32_t GetArrayId(JSArray array);
  uint32_t GetObjectId(JSObject object);

  ValueSerializer string_serializer_;
  ValueSerializer map_serializer_;
  ValueSerializer context_serializer_;
  ValueSerializer function_serializer_;
  ValueSerializer class_serializer_;
  ValueSerializer array_serializer_;
  ValueSerializer object_serializer_;
  ValueSerializer export_serializer_;

  // These are needed for being able to serialize items in order.
  Handle<ArrayList> contexts_;
  Handle<ArrayList> functions_;
  Handle<ArrayList> classes_;
  Handle<ArrayList> arrays_;
  Handle<ArrayList> objects_;

  // ObjectCacheIndexMap implements fast lookup item -> id.
  ObjectCacheIndexMap string_ids_;
  ObjectCacheIndexMap map_ids_;
  ObjectCacheIndexMap context_ids_;
  ObjectCacheIndexMap function_ids_;
  ObjectCacheIndexMap class_ids_;
  ObjectCacheIndexMap array_ids_;
  ObjectCacheIndexMap object_ids_;
  uint32_t export_count_ = 0;

  std::queue<Handle<Object>> discovery_queue_;

  // For constructing the minimal, "compacted", source string to cover all
  // function bodies.
  Handle<String> full_source_;
  uint32_t source_id_;
  // Ordered set of (start, end) pairs of all functions we've discovered.
  std::set<std::pair<int, int>> source_intervals_;
  // Maps function positions in the real source code into the function positions
  // in the constructed source code (which we'll include in the web snapshot).
  std::unordered_map<int, int> source_offset_to_compacted_source_offset_;
};

class V8_EXPORT WebSnapshotDeserializer
    : public WebSnapshotSerializerDeserializer {
 public:
  WebSnapshotDeserializer(v8::Isolate* v8_isolate, const uint8_t* data,
                          size_t buffer_size);
  WebSnapshotDeserializer(Isolate* isolate, Handle<Script> snapshot_as_script);
  ~WebSnapshotDeserializer();
  bool Deserialize();

  // For inspecting the state after deserializing a snapshot.
  uint32_t string_count() const { return string_count_; }
  uint32_t map_count() const { return map_count_; }
  uint32_t context_count() const { return context_count_; }
  uint32_t function_count() const { return function_count_; }
  uint32_t class_count() const { return class_count_; }
  uint32_t array_count() const { return array_count_; }
  uint32_t object_count() const { return object_count_; }

  static void UpdatePointersCallback(v8::Isolate* isolate, v8::GCType type,
                                     v8::GCCallbackFlags flags,
                                     void* deserializer) {
    reinterpret_cast<WebSnapshotDeserializer*>(deserializer)->UpdatePointers();
  }

  void UpdatePointers();

 private:
  WebSnapshotDeserializer(Isolate* isolate, Handle<Object> script_name,
                          base::Vector<const uint8_t> buffer);
  base::Vector<const uint8_t> ExtractScriptBuffer(
      Isolate* isolate, Handle<Script> snapshot_as_script);
  bool DeserializeSnapshot();
  bool DeserializeScript();

  WebSnapshotDeserializer(const WebSnapshotDeserializer&) = delete;
  WebSnapshotDeserializer& operator=(const WebSnapshotDeserializer&) = delete;

  void DeserializeStrings();
  String ReadString(bool internalize = false);
  void DeserializeMaps();
  void DeserializeContexts();
  Handle<ScopeInfo> CreateScopeInfo(uint32_t variable_count, bool has_parent,
                                    ContextType context_type);
  Handle<JSFunction> CreateJSFunction(int index, uint32_t start,
                                      uint32_t length, uint32_t parameter_count,
                                      uint32_t flags, uint32_t context_id);
  void DeserializeFunctionData(uint32_t count, uint32_t current_count);
  void DeserializeFunctions();
  void DeserializeClasses();
  void DeserializeArrays();
  void DeserializeObjects();
  void DeserializeExports();
  Object ReadValue(
      Handle<HeapObject> object_for_deferred_reference = Handle<HeapObject>(),
      uint32_t index_for_deferred_reference = 0);
  void ReadFunctionPrototype(Handle<JSFunction> function);
  bool SetFunctionPrototype(JSFunction function, JSReceiver prototype);

  HeapObject AddDeferredReference(Handle<HeapObject> container, uint32_t index,
                                  ValueType target_type,
                                  uint32_t target_object_index);
  void ProcessDeferredReferences();
  // Not virtual, on purpose (because it doesn't need to be).
  void Throw(const char* message);

  Handle<FixedArray> strings_handle_;
  FixedArray strings_;

  Handle<FixedArray> maps_handle_;
  FixedArray maps_;

  Handle<FixedArray> contexts_handle_;
  FixedArray contexts_;

  Handle<FixedArray> functions_handle_;
  FixedArray functions_;

  Handle<FixedArray> classes_handle_;
  FixedArray classes_;

  Handle<FixedArray> arrays_handle_;
  FixedArray arrays_;

  Handle<FixedArray> objects_handle_;
  FixedArray objects_;

  Handle<ArrayList> deferred_references_;

  Handle<WeakFixedArray> shared_function_infos_handle_;
  WeakFixedArray shared_function_infos_;

  Handle<ObjectHashTable> shared_function_info_table_;

  Handle<Script> script_;
  Handle<Object> script_name_;

  uint32_t string_count_ = 0;
  uint32_t map_count_ = 0;
  uint32_t context_count_ = 0;
  uint32_t function_count_ = 0;
  uint32_t current_function_count_ = 0;
  uint32_t class_count_ = 0;
  uint32_t current_class_count_ = 0;
  uint32_t array_count_ = 0;
  uint32_t current_array_count_ = 0;
  uint32_t object_count_ = 0;
  uint32_t current_object_count_ = 0;

  ValueDeserializer deserializer_;

  bool deserialized_ = false;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_WEB_SNAPSHOT_WEB_SNAPSHOT_H_
