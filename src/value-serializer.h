// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_VALUE_SERIALIZER_H_
#define V8_VALUE_SERIALIZER_H_

#include <cstdint>
#include <vector>

#include "include/v8.h"
#include "src/base/compiler-specific.h"
#include "src/base/macros.h"
#include "src/vector.h"

namespace v8 {
namespace internal {

class Isolate;
class Object;
class Oddball;

enum class SerializationTag : uint8_t;

/**
 * Writes V8 objects in a binary format that allows the objects to be cloned
 * according to the HTML structured clone algorithm.
 *
 * Format is based on Blink's previous serialization logic.
 */
class ValueSerializer {
 public:
  ValueSerializer();
  ~ValueSerializer();

  /*
   * Writes out a header, which includes the format version.
   */
  void WriteHeader();

  /*
   * Serializes a V8 object into the buffer.
   */
  Maybe<bool> WriteObject(Handle<Object> object) WARN_UNUSED_RESULT;

  /*
   * Returns the stored data. This serializer should not be used once the buffer
   * is released. The contents are undefined if a previous write has failed.
   */
  std::vector<uint8_t> ReleaseBuffer() { return std::move(buffer_); }

 private:
  // Writing the wire format.
  void WriteTag(SerializationTag tag);
  template <typename T>
  void WriteVarint(T value);

  // Writing V8 objects of various kinds.
  void WriteOddball(Oddball* oddball);

  std::vector<uint8_t> buffer_;

  DISALLOW_COPY_AND_ASSIGN(ValueSerializer);
};

/*
 * Deserializes values from data written with ValueSerializer, or a compatible
 * implementation.
 */
class ValueDeserializer {
 public:
  ValueDeserializer(Isolate* isolate, Vector<const uint8_t> data);
  ~ValueDeserializer();

  /*
   * Runs version detection logic, which may fail if the format is invalid.
   */
  Maybe<bool> ReadHeader() WARN_UNUSED_RESULT;

  /*
   * Deserializes a V8 object from the buffer.
   */
  MaybeHandle<Object> ReadObject() WARN_UNUSED_RESULT;

 private:
  Maybe<SerializationTag> ReadTag() WARN_UNUSED_RESULT;
  template <typename T>
  Maybe<T> ReadVarint() WARN_UNUSED_RESULT;

  Isolate* const isolate_;
  const uint8_t* position_;
  const uint8_t* const end_;
  uint32_t version_ = 0;

  DISALLOW_COPY_AND_ASSIGN(ValueDeserializer);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_VALUE_SERIALIZER_H_
