// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/web-snapshot/web-snapshot.h"

#include <limits>

#include "include/v8.h"
#include "src/api/api-inl.h"
#include "src/base/platform/wrappers.h"
#include "src/handles/handles.h"
#include "src/objects/contexts.h"
#include "src/objects/script.h"

namespace v8 {
namespace internal {

void WebSnapshotSerializerDeserializer::Throw(const char* message) {
  if (error_message_ != nullptr) {
    return;
  }
  error_message_ = message;
  if (!isolate_->has_pending_exception()) {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate_);
    v8_isolate->ThrowException(v8::Exception::Error(
        v8::String::NewFromUtf8(v8_isolate, message).ToLocalChecked()));
  }
}

WebSnapshotSerializer::WebSnapshotSerializer(v8::Isolate* isolate)
    : WebSnapshotSerializerDeserializer(
          reinterpret_cast<v8::internal::Isolate*>(isolate)),
      string_serializer_(isolate_, nullptr),
      map_serializer_(isolate_, nullptr),
      function_serializer_(isolate_, nullptr),
      object_serializer_(isolate_, nullptr),
      export_serializer_(isolate_, nullptr),
      string_ids_(isolate_->heap()),
      map_ids_(isolate_->heap()),
      function_ids_(isolate_->heap()),
      object_ids_(isolate_->heap()) {}

WebSnapshotSerializer::~WebSnapshotSerializer() {}

bool WebSnapshotSerializer::TakeSnapshot(
    v8::Local<v8::Context> context, const std::vector<std::string>& exports,
    WebSnapshotData& data_out) {
  if (string_ids_.size() > 0) {
    Throw("Web snapshot: Can't reuse WebSnapshotSerializer");
    return false;
  }
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate_);
  for (const std::string& export_name : exports) {
    v8::ScriptCompiler::Source source(
        v8::String::NewFromUtf8(v8_isolate, export_name.c_str(),
                                NewStringType::kNormal,
                                static_cast<int>(export_name.length()))
            .ToLocalChecked());
    auto script = ScriptCompiler::Compile(context, &source).ToLocalChecked();
    v8::MaybeLocal<v8::Value> script_result = script->Run(context);
    v8::Local<v8::Object> v8_object;
    if (script_result.IsEmpty() ||
        !script_result.ToLocalChecked()->ToObject(context).ToLocal(
            &v8_object)) {
      Throw("Web snapshot: Exported object not found");
      return false;
    }

    auto object = Handle<JSObject>::cast(Utils::OpenHandle(*v8_object));
    SerializeExport(object, export_name);
  }
  WriteSnapshot(data_out.buffer, data_out.buffer_size);
  return !has_error();
}

uint32_t WebSnapshotSerializer::string_count() const {
  return static_cast<uint32_t>(string_ids_.size());
}

uint32_t WebSnapshotSerializer::map_count() const {
  return static_cast<uint32_t>(map_ids_.size());
}

uint32_t WebSnapshotSerializer::function_count() const {
  return static_cast<uint32_t>(function_ids_.size());
}

uint32_t WebSnapshotSerializer::object_count() const {
  return static_cast<uint32_t>(object_ids_.size());
}

// Format (full snapshot):
// - String count
// - For each string:
//   - Serialized string
// - Shape count
// - For each shape:
//   - Serialized shape
// - Function count
// - For each function:
//   - Serialized function
// - Object count
// - For each object:
//   - Serialized object
// - Export count
// - For each export:
//   - Serialized export
void WebSnapshotSerializer::WriteSnapshot(uint8_t*& buffer,
                                          size_t& buffer_size) {
  while (!pending_objects_.empty()) {
    const Handle<JSObject>& object = pending_objects_.front();
    SerializePendingJSObject(object);
    pending_objects_.pop();
  }

  ValueSerializer total_serializer(isolate_, nullptr);
  size_t needed_size =
      string_serializer_.buffer_size_ + map_serializer_.buffer_size_ +
      function_serializer_.buffer_size_ + object_serializer_.buffer_size_ +
      export_serializer_.buffer_size_ + 4 * sizeof(uint32_t);
  if (total_serializer.ExpandBuffer(needed_size).IsNothing()) {
    Throw("Web snapshot: Out of memory");
    return;
  }

  total_serializer.WriteUint32(static_cast<uint32_t>(string_ids_.size()));
  total_serializer.WriteRawBytes(string_serializer_.buffer_,
                                 string_serializer_.buffer_size_);
  total_serializer.WriteUint32(static_cast<uint32_t>(map_ids_.size()));
  total_serializer.WriteRawBytes(map_serializer_.buffer_,
                                 map_serializer_.buffer_size_);
  total_serializer.WriteUint32(static_cast<uint32_t>(function_ids_.size()));
  total_serializer.WriteRawBytes(function_serializer_.buffer_,
                                 function_serializer_.buffer_size_);
  total_serializer.WriteUint32(static_cast<uint32_t>(object_ids_.size()));
  total_serializer.WriteRawBytes(object_serializer_.buffer_,
                                 object_serializer_.buffer_size_);
  total_serializer.WriteUint32(export_count_);
  total_serializer.WriteRawBytes(export_serializer_.buffer_,
                                 export_serializer_.buffer_size_);

  if (has_error()) {
    return;
  }

  auto result = total_serializer.Release();
  buffer = result.first;
  buffer_size = result.second;
}

bool WebSnapshotSerializer::InsertIntoIndexMap(ObjectCacheIndexMap& map,
                                               Handle<HeapObject> object,
                                               uint32_t& id) {
  if (static_cast<uint32_t>(map.size()) >=
      std::numeric_limits<uint32_t>::max()) {
    Throw("Web snapshot: Too many objects");
    return true;
  }
  int index_out;
  bool found = map.LookupOrInsert(object, &index_out);
  id = static_cast<uint32_t>(index_out);
  return found;
}

// Format:
// - Length
// - Raw bytes (data)
void WebSnapshotSerializer::SerializeString(Handle<String> string,
                                            uint32_t& id) {
  if (InsertIntoIndexMap(string_ids_, string, id)) {
    return;
  }

  // TODO(v8:11525): Always write strings as UTF-8.
  string = String::Flatten(isolate_, string);
  DisallowGarbageCollection no_gc;
  String::FlatContent flat = string->GetFlatContent(no_gc);
  DCHECK(flat.IsFlat());
  if (flat.IsOneByte()) {
    Vector<const uint8_t> chars = flat.ToOneByteVector();
    string_serializer_.WriteUint32(chars.length());
    string_serializer_.WriteRawBytes(chars.begin(),
                                     chars.length() * sizeof(uint8_t));
  } else if (flat.IsTwoByte()) {
    // TODO(v8:11525): Support two-byte strings.
    UNREACHABLE();
  } else {
    UNREACHABLE();
  }
}

// Format (serialized shape):
// - Property count
// - For each property
//   - String id (name)
void WebSnapshotSerializer::SerializeMap(Handle<Map> map, uint32_t& id) {
  if (InsertIntoIndexMap(map_ids_, map, id)) {
    return;
  }

  std::vector<uint32_t> string_ids;
  for (InternalIndex i : map->IterateOwnDescriptors()) {
    Handle<Name> key(map->instance_descriptors(kRelaxedLoad).GetKey(i),
                     isolate_);
    if (!key->IsString()) {
      Throw("Web snapshot: Key is not a string");
      return;
    }

    PropertyDetails details =
        map->instance_descriptors(kRelaxedLoad).GetDetails(i);
    if (details.IsDontEnum()) {
      Throw("Web snapshot: Non-enumerable properties not supported");
      return;
    }

    if (details.location() != kField) {
      Throw("Web snapshot: Properties which are not fields not supported");
      return;
    }

    uint32_t string_id = 0;
    SerializeString(Handle<String>::cast(key), string_id);
    string_ids.push_back(string_id);

    // TODO(v8:11525): Support property attributes.
  }
  map_serializer_.WriteUint32(static_cast<uint32_t>(string_ids.size()));
  for (auto i : string_ids) {
    map_serializer_.WriteUint32(i);
  }
}

// Format (serialized function):
// - String id (source string)
void WebSnapshotSerializer::SerializeJSFunction(Handle<JSFunction> function,
                                                uint32_t& id) {
  if (InsertIntoIndexMap(function_ids_, function, id)) {
    return;
  }

  if (!function->shared().HasSourceCode()) {
    Throw("Web snapshot: Function without source code");
    return;
  }
  // TODO(v8:11525): For inner functions, create a "substring" type, so that we
  // don't need to serialize the same content twice.
  Handle<String> full_source(
      String::cast(Script::cast(function->shared().script()).source()),
      isolate_);
  int start = function->shared().StartPosition();
  int end = function->shared().EndPosition();
  Handle<String> source =
      isolate_->factory()->NewSubString(full_source, start, end);
  uint32_t source_id = 0;
  SerializeString(source, source_id);
  function_serializer_.WriteUint32(source_id);

  // TODO(v8:11525): Serialize .prototype.
  // TODO(v8:11525): Support properties in functions.
}

void WebSnapshotSerializer::SerializeJSObject(Handle<JSObject> object,
                                              uint32_t& id) {
  DCHECK(!object->IsJSFunction());
  if (InsertIntoIndexMap(object_ids_, object, id)) {
    return;
  }
  pending_objects_.push(object);
}

// Format (serialized object):
// - Shape id
// - For each property:
//   - Serialized value
void WebSnapshotSerializer::SerializePendingJSObject(Handle<JSObject> object) {
  Handle<Map> map(object->map(), isolate_);
  uint32_t map_id = 0;
  SerializeMap(map, map_id);

  if (*map != object->map()) {
    Throw("Web snapshot: Map changed");
    return;
  }

  object_serializer_.WriteUint32(map_id);

  for (InternalIndex i : map->IterateOwnDescriptors()) {
    PropertyDetails details =
        map->instance_descriptors(kRelaxedLoad).GetDetails(i);
    FieldIndex field_index = FieldIndex::ForDescriptor(*map, i);
    Handle<Object> value =
        JSObject::FastPropertyAt(object, details.representation(), field_index);
    WriteValue(value, object_serializer_);
  }
}

// Format (serialized export):
// - String id (export name)
// - Object id (exported object)
void WebSnapshotSerializer::SerializeExport(Handle<JSObject> object,
                                            const std::string& export_name) {
  // TODO(v8:11525): Support exporting functions.
  ++export_count_;
  Handle<String> export_name_string =
      isolate_->factory()
          ->NewStringFromOneByte(Vector<const uint8_t>(
              reinterpret_cast<const uint8_t*>(export_name.c_str()),
              static_cast<int>(export_name.length())))
          .ToHandleChecked();
  uint32_t string_id = 0;
  SerializeString(export_name_string, string_id);
  uint32_t object_id = 0;
  SerializeJSObject(object, object_id);
  export_serializer_.WriteUint32(string_id);
  export_serializer_.WriteUint32(object_id);
}

// Format (serialized value):
// - Type id (ValueType enum)
// - Value or id (interpretation depends on the type)
void WebSnapshotSerializer::WriteValue(Handle<Object> object,
                                       ValueSerializer& serializer) {
  uint32_t id = 0;
  if (object->IsSmi()) {
    // TODO(v8:11525): Implement.
    UNREACHABLE();
  }

  DCHECK(object->IsHeapObject());
  switch (HeapObject::cast(*object).map().instance_type()) {
    case ODDBALL_TYPE:
      // TODO(v8:11525): Implement.
      UNREACHABLE();
    case HEAP_NUMBER_TYPE:
      // TODO(v8:11525): Implement.
      UNREACHABLE();
    case JS_FUNCTION_TYPE:
      SerializeJSFunction(Handle<JSFunction>::cast(object), id);
      serializer.WriteUint32(ValueType::FUNCTION_ID);
      serializer.WriteUint32(id);
      break;
    case JS_OBJECT_TYPE:
      SerializeJSObject(Handle<JSObject>::cast(object), id);
      serializer.WriteUint32(ValueType::OBJECT_ID);
      serializer.WriteUint32(id);
      break;
    default:
      if (object->IsString()) {
        SerializeString(Handle<String>::cast(object), id);
        serializer.WriteUint32(ValueType::STRING_ID);
        serializer.WriteUint32(id);
      } else {
        Throw("Web snapshot: Unsupported object");
      }
  }
  // TODO(v8:11525): Support more types.
}

WebSnapshotDeserializer::WebSnapshotDeserializer(v8::Isolate* isolate)
    : WebSnapshotSerializerDeserializer(
          reinterpret_cast<v8::internal::Isolate*>(isolate)) {}

bool WebSnapshotDeserializer::UseWebSnapshot(const uint8_t* data,
                                             size_t buffer_size) {
  if (strings_.size() > 0) {
    Throw("Web snapshot: Can't reuse WebSnapshotDeserializer");
    return false;
  }

  base::ElapsedTimer timer;
  if (FLAG_trace_web_snapshot) {
    timer.Start();
  }

  HandleScope scope(isolate_);
  size_t ix = 0;
  DeserializeStrings(data, ix, buffer_size);
  DeserializeMaps(data, ix, buffer_size);
  DeserializeFunctions(data, ix, buffer_size);
  DeserializeObjects(data, ix, buffer_size);
  DeserializeExports(data, ix, buffer_size);
  if (ix != buffer_size) {
    Throw("Web snapshot: Snapshot length mismatch");
    return false;
  }

  if (FLAG_trace_web_snapshot) {
    double ms = timer.Elapsed().InMillisecondsF();
    PrintF("[Deserializing snapshot (%zu bytes) took %0.3f ms]\n", buffer_size,
           ms);
  }

  // TODO(v8:11525): Add verification mode; verify the objects we just produced.
  return !has_error();
}

void WebSnapshotDeserializer::DeserializeStrings(const uint8_t* data,
                                                 size_t& ix, size_t size) {
  ValueDeserializer deserializer(isolate_, &data[ix], size - ix);
  uint32_t count;
  if (!deserializer.ReadUint32(&count)) {
    Throw("Web snapshot: Malformed string table");
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    // TODO(v8:11525): Read strings as UTF-8.
    MaybeHandle<String> maybe_string = deserializer.ReadOneByteString();
    Handle<String> string;
    if (!maybe_string.ToHandle(&string)) {
      Throw("Web snapshot: Malformed string");
      return;
    }
    strings_.emplace_back(string);
  }
  ix = deserializer.position_ - data;
}

void WebSnapshotDeserializer::DeserializeMaps(const uint8_t* data, size_t& ix,
                                              size_t size) {
  ValueDeserializer deserializer(isolate_, &data[ix], size - ix);
  uint32_t map_count;
  if (!deserializer.ReadUint32(&map_count)) {
    Throw("Web snapshot: Malformed shape table");
    return;
  }
  for (uint32_t i = 0; i < map_count; ++i) {
    uint32_t property_count;
    if (!deserializer.ReadUint32(&property_count)) {
      Throw("Web snapshot: Malformed shape");
      return;
    }
    if (property_count > kMaxNumberOfDescriptors) {
      Throw("Web snapshot: Malformed shape: too many properties");
      return;
    }

    Handle<DescriptorArray> descriptors =
        isolate_->factory()->NewDescriptorArray(0, property_count);
    for (uint32_t p = 0; p < property_count; ++p) {
      uint32_t string_id;
      if (!deserializer.ReadUint32(&string_id) ||
          string_id >= strings_.size()) {
        Throw("Web snapshot: Malformed shape");
        return;
      }
      Handle<String> key = strings_[string_id];
      if (!key->IsInternalizedString()) {
        key = isolate_->factory()->InternalizeString(key);
        strings_[string_id] = key;
      }

      // Use the "none" representation until we see the first object having this
      // map. At that point, modify the representation.
      Descriptor desc = Descriptor::DataField(
          isolate_, key, static_cast<int>(p), PropertyAttributes::NONE,
          Representation::None());
      descriptors->Append(&desc);
    }

    Handle<Map> map = isolate_->factory()->NewMap(
        JS_OBJECT_TYPE, JSObject::kHeaderSize * kTaggedSize, HOLEY_ELEMENTS, 0);
    map->InitializeDescriptors(isolate_, *descriptors);

    maps_.emplace_back(map);
  }
  ix = deserializer.position_ - data;
}

void WebSnapshotDeserializer::DeserializeFunctions(const uint8_t* data,
                                                   size_t& ix, size_t size) {
  ValueDeserializer deserializer(isolate_, &data[ix], size - ix);
  uint32_t count;
  if (!deserializer.ReadUint32(&count)) {
    Throw("Web snapshot: Malformed function table");
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t source_id;
    if (!deserializer.ReadUint32(&source_id) || source_id >= strings_.size()) {
      Throw("Web snapshot: Malformed function");
      return;
    }
    Handle<String> source = strings_[source_id];

    // See CreateDynamicFunction which builds the function in a similar way.
    IncrementalStringBuilder builder(isolate_);
    builder.AppendCString("(function anonymous");
    builder.AppendString(source);
    builder.AppendCString(")");
    MaybeHandle<String> maybe_source = builder.Finish();
    if (!maybe_source.ToHandle(&source)) {
      Throw("Web snapshot: Error when creating function");
      return;
    }
    Handle<JSFunction> function_from_string;
    if (!Compiler::GetFunctionFromString(
             handle(isolate_->context().native_context(), isolate_), source,
             ONLY_SINGLE_FUNCTION_LITERAL, kNoSourcePosition, false)
             .ToHandle(&function_from_string)) {
      Throw("Web snapshot: Invalid function source code");
      return;
    }
    Handle<Object> result;
    if (!Execution::Call(isolate_, function_from_string,
                         isolate_->factory()->undefined_value(), 0, nullptr)
             .ToHandle(&result)) {
      Throw("Web snapshot: Error when creating function");
      return;
    }
    Handle<JSFunction> function = Handle<JSFunction>::cast(result);
    functions_.emplace_back(function);
  }
  ix = deserializer.position_ - data;
}

void WebSnapshotDeserializer::DeserializeObjects(const uint8_t* data,
                                                 size_t& ix, size_t size) {
  ValueDeserializer deserializer(isolate_, &data[ix], size - ix);
  uint32_t object_count;
  if (!deserializer.ReadUint32(&object_count)) {
    Throw("Web snapshot: Malformed objects table");
    return;
  }
  for (size_t i = 0; i < object_count; ++i) {
    uint32_t map_id;
    if (!deserializer.ReadUint32(&map_id) || map_id >= maps_.size()) {
      Throw("Web snapshot: Malformed object");
      return;
    }
    Handle<Map> map = maps_[map_id];
    DescriptorArray descriptors = map->instance_descriptors(kRelaxedLoad);
    int no_properties = map->NumberOfOwnDescriptors();
    Handle<PropertyArray> property_array =
        isolate_->factory()->NewPropertyArray(no_properties);
    for (int i = 0; i < no_properties; ++i) {
      Handle<Object> value;
      uint32_t value_type;
      if (!deserializer.ReadUint32(&value_type)) {
        Throw("Web snapshot: Malformed object property");
        return;
      }
      Representation wanted_representation;
      switch (value_type) {
        case ValueType::STRING_ID: {
          uint32_t string_id;
          if (!deserializer.ReadUint32(&string_id) ||
              string_id >= strings_.size()) {
            Throw("Web snapshot: Malformed object property");
            return;
          }
          value = strings_[string_id];
          wanted_representation = Representation::Tagged();
          break;
        }
        case ValueType::OBJECT_ID:
          // TODO(v8:11525): Handle circular references.
          UNREACHABLE();
          break;
        case ValueType::FUNCTION_ID: {
          // Functions have been deserialized already.
          uint32_t function_id;
          if (!deserializer.ReadUint32(&function_id) ||
              function_id >= functions_.size()) {
            Throw("Web snapshot: Malformed object property");
            return;
          }
          value = functions_[function_id];
          wanted_representation = Representation::Tagged();
          break;
        }
        default:
          Throw("Web snapshot: Unsupported value type");
          return;
      }
      // Read the representation from the map.
      PropertyDetails details = descriptors.GetDetails(InternalIndex(i));
      CHECK_EQ(details.location(), kField);
      CHECK_EQ(kData, details.kind());
      Representation r = details.representation();
      if (r.IsNone()) {
        // Switch over to wanted_representation.
        details = details.CopyWithRepresentation(wanted_representation);
        descriptors.SetDetails(InternalIndex(i), details);
      } else if (!r.Equals(wanted_representation)) {
        // TODO(v8:11525): Support this case too.
        UNREACHABLE();
      }

      property_array->set(i, *value);
    }
    Handle<JSObject> object = isolate_->factory()->NewJSObjectFromMap(map);
    object->set_raw_properties_or_hash(*property_array);
    objects_.emplace_back(object);
  }
  ix = deserializer.position_ - data;
}

void WebSnapshotDeserializer::DeserializeExports(const uint8_t* data,
                                                 size_t& ix, size_t size) {
  ValueDeserializer deserializer(isolate_, &data[ix], size - ix);
  uint32_t count;
  if (!deserializer.ReadUint32(&count)) {
    Throw("Web snapshot: Malformed export table");
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t string_id = 0, object_id = 0;
    if (!deserializer.ReadUint32(&string_id) || string_id >= strings_.size() ||
        !deserializer.ReadUint32(&object_id) || object_id >= objects_.size()) {
      Throw("Web snapshot: Malformed export");
      return;
    }
    Handle<String> export_name = strings_[string_id];
    Handle<Object> exported_object = objects_[object_id];

    auto result = Object::SetProperty(isolate_, isolate_->global_object(),
                                      export_name, exported_object);
    if (result.is_null()) {
      Throw("Web snapshot: Setting global property failed");
      return;
    }
  }
  ix = deserializer.position_ - data;
}

}  // namespace internal
}  // namespace v8
