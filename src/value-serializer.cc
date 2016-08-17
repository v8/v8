// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/value-serializer.h"

#include <type_traits>

#include "src/base/logging.h"
#include "src/factory.h"
#include "src/handles-inl.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects.h"

namespace v8 {
namespace internal {

static const uint32_t kLatestVersion = 9;

template <typename T>
static size_t BytesNeededForVarint(T value) {
  static_assert(std::is_integral<T>::value && std::is_unsigned<T>::value,
                "Only unsigned integer types can be written as varints.");
  size_t result = 0;
  do {
    result++;
    value >>= 7;
  } while (value);
  return result;
}

enum class SerializationTag : uint8_t {
  // version:uint32_t (if at beginning of data, sets version > 0)
  kVersion = 0xFF,
  // ignore
  kPadding = '\0',
  // refTableSize:uint32_t (previously used for sanity checks; safe to ignore)
  kVerifyObjectCount = '?',
  // Oddballs (no data).
  kUndefined = '_',
  kNull = '0',
  kTrue = 'T',
  kFalse = 'F',
  // Number represented as 32-bit integer, ZigZag-encoded
  // (like sint32 in protobuf)
  kInt32 = 'I',
  // Number represented as 32-bit unsigned integer, varint-encoded
  // (like uint32 in protobuf)
  kUint32 = 'U',
  // Number represented as a 64-bit double.
  // Host byte order is used (N.B. this makes the format non-portable).
  kDouble = 'N',
  // byteLength:uint32_t, then raw data
  kUtf8String = 'S',
  kTwoByteString = 'c',
};

ValueSerializer::ValueSerializer() {}

ValueSerializer::~ValueSerializer() {}

void ValueSerializer::WriteHeader() {
  WriteTag(SerializationTag::kVersion);
  WriteVarint(kLatestVersion);
}

void ValueSerializer::WriteTag(SerializationTag tag) {
  buffer_.push_back(static_cast<uint8_t>(tag));
}

template <typename T>
void ValueSerializer::WriteVarint(T value) {
  // Writes an unsigned integer as a base-128 varint.
  // The number is written, 7 bits at a time, from the least significant to the
  // most significant 7 bits. Each byte, except the last, has the MSB set.
  // See also https://developers.google.com/protocol-buffers/docs/encoding
  static_assert(std::is_integral<T>::value && std::is_unsigned<T>::value,
                "Only unsigned integer types can be written as varints.");
  uint8_t stack_buffer[sizeof(T) * 8 / 7 + 1];
  uint8_t* next_byte = &stack_buffer[0];
  do {
    *next_byte = (value & 0x7f) | 0x80;
    next_byte++;
    value >>= 7;
  } while (value);
  *(next_byte - 1) &= 0x7f;
  buffer_.insert(buffer_.end(), stack_buffer, next_byte);
}

template <typename T>
void ValueSerializer::WriteZigZag(T value) {
  // Writes a signed integer as a varint using ZigZag encoding (i.e. 0 is
  // encoded as 0, -1 as 1, 1 as 2, -2 as 3, and so on).
  // See also https://developers.google.com/protocol-buffers/docs/encoding
  // Note that this implementation relies on the right shift being arithmetic.
  static_assert(std::is_integral<T>::value && std::is_signed<T>::value,
                "Only signed integer types can be written as zigzag.");
  using UnsignedT = typename std::make_unsigned<T>::type;
  WriteVarint((static_cast<UnsignedT>(value) << 1) ^
              (value >> (8 * sizeof(T) - 1)));
}

void ValueSerializer::WriteDouble(double value) {
  // Warning: this uses host endianness.
  buffer_.insert(buffer_.end(), reinterpret_cast<const uint8_t*>(&value),
                 reinterpret_cast<const uint8_t*>(&value + 1));
}

void ValueSerializer::WriteOneByteString(Vector<const uint8_t> chars) {
  WriteVarint<uint32_t>(chars.length());
  buffer_.insert(buffer_.end(), chars.begin(), chars.end());
}

void ValueSerializer::WriteTwoByteString(Vector<const uc16> chars) {
  // Warning: this uses host endianness.
  WriteVarint<uint32_t>(chars.length() * sizeof(uc16));
  buffer_.insert(buffer_.end(), reinterpret_cast<const uint8_t*>(chars.begin()),
                 reinterpret_cast<const uint8_t*>(chars.end()));
}

uint8_t* ValueSerializer::ReserveRawBytes(size_t bytes) {
  auto old_size = buffer_.size();
  buffer_.resize(buffer_.size() + bytes);
  return &buffer_[old_size];
}

Maybe<bool> ValueSerializer::WriteObject(Handle<Object> object) {
  if (object->IsSmi()) {
    WriteSmi(Smi::cast(*object));
    return Just(true);
  }

  DCHECK(object->IsHeapObject());
  switch (HeapObject::cast(*object)->map()->instance_type()) {
    case ODDBALL_TYPE:
      WriteOddball(Oddball::cast(*object));
      return Just(true);
    case HEAP_NUMBER_TYPE:
    case MUTABLE_HEAP_NUMBER_TYPE:
      WriteHeapNumber(HeapNumber::cast(*object));
      return Just(true);
    default:
      if (object->IsString()) {
        WriteString(Handle<String>::cast(object));
        return Just(true);
      }
      UNIMPLEMENTED();
      return Nothing<bool>();
  }
}

void ValueSerializer::WriteOddball(Oddball* oddball) {
  SerializationTag tag = SerializationTag::kUndefined;
  switch (oddball->kind()) {
    case Oddball::kUndefined:
      tag = SerializationTag::kUndefined;
      break;
    case Oddball::kFalse:
      tag = SerializationTag::kFalse;
      break;
    case Oddball::kTrue:
      tag = SerializationTag::kTrue;
      break;
    case Oddball::kNull:
      tag = SerializationTag::kNull;
      break;
    default:
      UNREACHABLE();
      break;
  }
  WriteTag(tag);
}

void ValueSerializer::WriteSmi(Smi* smi) {
  static_assert(kSmiValueSize <= 32, "Expected SMI <= 32 bits.");
  WriteTag(SerializationTag::kInt32);
  WriteZigZag<int32_t>(smi->value());
}

void ValueSerializer::WriteHeapNumber(HeapNumber* number) {
  WriteTag(SerializationTag::kDouble);
  WriteDouble(number->value());
}

void ValueSerializer::WriteString(Handle<String> string) {
  string = String::Flatten(string);
  DisallowHeapAllocation no_gc;
  String::FlatContent flat = string->GetFlatContent();
  DCHECK(flat.IsFlat());
  if (flat.IsOneByte()) {
    // The existing format uses UTF-8, rather than Latin-1. As a result we must
    // to do work to encode strings that have characters outside ASCII.
    // TODO(jbroman): In a future format version, consider adding a tag for
    // Latin-1 strings, so that this can be skipped.
    WriteTag(SerializationTag::kUtf8String);
    Vector<const uint8_t> chars = flat.ToOneByteVector();
    if (String::IsAscii(chars.begin(), chars.length())) {
      WriteOneByteString(chars);
    } else {
      v8::Local<v8::String> api_string = Utils::ToLocal(string);
      uint32_t utf8_length = api_string->Utf8Length();
      WriteVarint(utf8_length);
      api_string->WriteUtf8(
          reinterpret_cast<char*>(ReserveRawBytes(utf8_length)), utf8_length,
          nullptr, v8::String::NO_NULL_TERMINATION);
    }
  } else if (flat.IsTwoByte()) {
    Vector<const uc16> chars = flat.ToUC16Vector();
    uint32_t byte_length = chars.length() * sizeof(uc16);
    // The existing reading code expects 16-byte strings to be aligned.
    if ((buffer_.size() + 1 + BytesNeededForVarint(byte_length)) & 1)
      WriteTag(SerializationTag::kPadding);
    WriteTag(SerializationTag::kTwoByteString);
    WriteTwoByteString(chars);
  } else {
    UNREACHABLE();
  }
}

ValueDeserializer::ValueDeserializer(Isolate* isolate,
                                     Vector<const uint8_t> data)
    : isolate_(isolate),
      position_(data.start()),
      end_(data.start() + data.length()) {}

ValueDeserializer::~ValueDeserializer() {}

Maybe<bool> ValueDeserializer::ReadHeader() {
  if (position_ < end_ &&
      *position_ == static_cast<uint8_t>(SerializationTag::kVersion)) {
    ReadTag().ToChecked();
    if (!ReadVarint<uint32_t>().To(&version_)) return Nothing<bool>();
    if (version_ > kLatestVersion) return Nothing<bool>();
  }
  return Just(true);
}

Maybe<SerializationTag> ValueDeserializer::ReadTag() {
  SerializationTag tag;
  do {
    if (position_ >= end_) return Nothing<SerializationTag>();
    tag = static_cast<SerializationTag>(*position_);
    position_++;
  } while (tag == SerializationTag::kPadding);
  return Just(tag);
}

template <typename T>
Maybe<T> ValueDeserializer::ReadVarint() {
  // Reads an unsigned integer as a base-128 varint.
  // The number is written, 7 bits at a time, from the least significant to the
  // most significant 7 bits. Each byte, except the last, has the MSB set.
  // If the varint is larger than T, any more significant bits are discarded.
  // See also https://developers.google.com/protocol-buffers/docs/encoding
  static_assert(std::is_integral<T>::value && std::is_unsigned<T>::value,
                "Only unsigned integer types can be read as varints.");
  T value = 0;
  unsigned shift = 0;
  bool has_another_byte;
  do {
    if (position_ >= end_) return Nothing<T>();
    uint8_t byte = *position_;
    if (V8_LIKELY(shift < sizeof(T) * 8)) {
      value |= (byte & 0x7f) << shift;
      shift += 7;
    }
    has_another_byte = byte & 0x80;
    position_++;
  } while (has_another_byte);
  return Just(value);
}

template <typename T>
Maybe<T> ValueDeserializer::ReadZigZag() {
  // Writes a signed integer as a varint using ZigZag encoding (i.e. 0 is
  // encoded as 0, -1 as 1, 1 as 2, -2 as 3, and so on).
  // See also https://developers.google.com/protocol-buffers/docs/encoding
  static_assert(std::is_integral<T>::value && std::is_signed<T>::value,
                "Only signed integer types can be read as zigzag.");
  using UnsignedT = typename std::make_unsigned<T>::type;
  UnsignedT unsigned_value;
  if (!ReadVarint<UnsignedT>().To(&unsigned_value)) return Nothing<T>();
  return Just(static_cast<T>((unsigned_value >> 1) ^
                             -static_cast<T>(unsigned_value & 1)));
}

Maybe<double> ValueDeserializer::ReadDouble() {
  // Warning: this uses host endianness.
  if (position_ > end_ - sizeof(double)) return Nothing<double>();
  double value;
  memcpy(&value, position_, sizeof(double));
  position_ += sizeof(double);
  if (std::isnan(value)) value = std::numeric_limits<double>::quiet_NaN();
  return Just(value);
}

Maybe<Vector<const uint8_t>> ValueDeserializer::ReadRawBytes(int size) {
  if (size > end_ - position_) return Nothing<Vector<const uint8_t>>();
  const uint8_t* start = position_;
  position_ += size;
  return Just(Vector<const uint8_t>(start, size));
}

MaybeHandle<Object> ValueDeserializer::ReadObject() {
  SerializationTag tag;
  if (!ReadTag().To(&tag)) return MaybeHandle<Object>();
  switch (tag) {
    case SerializationTag::kVerifyObjectCount:
      // Read the count and ignore it.
      if (ReadVarint<uint32_t>().IsNothing()) return MaybeHandle<Object>();
      return ReadObject();
    case SerializationTag::kUndefined:
      return isolate_->factory()->undefined_value();
    case SerializationTag::kNull:
      return isolate_->factory()->null_value();
    case SerializationTag::kTrue:
      return isolate_->factory()->true_value();
    case SerializationTag::kFalse:
      return isolate_->factory()->false_value();
    case SerializationTag::kInt32: {
      Maybe<int32_t> number = ReadZigZag<int32_t>();
      if (number.IsNothing()) return MaybeHandle<Object>();
      return isolate_->factory()->NewNumberFromInt(number.FromJust());
    }
    case SerializationTag::kUint32: {
      Maybe<uint32_t> number = ReadVarint<uint32_t>();
      if (number.IsNothing()) return MaybeHandle<Object>();
      return isolate_->factory()->NewNumberFromUint(number.FromJust());
    }
    case SerializationTag::kDouble: {
      Maybe<double> number = ReadDouble();
      if (number.IsNothing()) return MaybeHandle<Object>();
      return isolate_->factory()->NewNumber(number.FromJust());
    }
    case SerializationTag::kUtf8String:
      return ReadUtf8String();
    case SerializationTag::kTwoByteString:
      return ReadTwoByteString();
    default:
      return MaybeHandle<Object>();
  }
}

MaybeHandle<String> ValueDeserializer::ReadUtf8String() {
  uint32_t utf8_length;
  Vector<const uint8_t> utf8_bytes;
  if (!ReadVarint<uint32_t>().To(&utf8_length) ||
      utf8_length >
          static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
      !ReadRawBytes(utf8_length).To(&utf8_bytes))
    return MaybeHandle<String>();
  return isolate_->factory()->NewStringFromUtf8(
      Vector<const char>::cast(utf8_bytes));
}

MaybeHandle<String> ValueDeserializer::ReadTwoByteString() {
  uint32_t byte_length;
  Vector<const uint8_t> bytes;
  if (!ReadVarint<uint32_t>().To(&byte_length) ||
      byte_length >
          static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
      byte_length % sizeof(uc16) != 0 || !ReadRawBytes(byte_length).To(&bytes))
    return MaybeHandle<String>();

  // Allocate an uninitialized string so that we can do a raw memcpy into the
  // string on the heap (regardless of alignment).
  Handle<SeqTwoByteString> string;
  if (!isolate_->factory()
           ->NewRawTwoByteString(byte_length / sizeof(uc16))
           .ToHandle(&string))
    return MaybeHandle<String>();

  // Copy the bytes directly into the new string.
  // Warning: this uses host endianness.
  memcpy(string->GetChars(), bytes.begin(), bytes.length());
  return string;
}

}  // namespace internal
}  // namespace v8
