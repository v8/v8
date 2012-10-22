// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_JSON_STRINGIFIER_H_
#define V8_JSON_STRINGIFIER_H_

#include "v8.h"
#include "v8utils.h"
#include "v8conversions.h"

namespace v8 {
namespace internal {

class BasicJsonStringifier BASE_EMBEDDED {
 public:
  explicit BasicJsonStringifier(Isolate* isolate);

  MaybeObject* Stringify(Handle<Object> object);

 private:
  static const int kInitialPartLength = 32;
  static const int kMaxPartLength = 16 * 1024;
  static const int kPartLengthGrowthFactor = 2;
  static const int kStackLimit = 8 * 1024;

  enum Result { UNCHANGED, SUCCESS, BAILOUT, CIRCULAR, STACK_OVERFLOW };

  template <bool is_ascii> void Extend();

  void ChangeEncoding();

  void ShrinkCurrentPart();

  template <bool is_ascii, typename Char>
  INLINE(void Append_(Char c));

  template <bool is_ascii, typename Char>
  INLINE(void AppendUnchecked_(Char c));

  template <bool is_ascii, typename Char>
  INLINE(void Append_(const Char* chars));

  template <bool is_ascii, typename Char>
  INLINE(void AppendUnchecked_(const Char* chars));

  INLINE(void Append(char c)) {
    if (is_ascii_) {
      Append_<true>(c);
    } else {
      Append_<false>(c);
    }
  }

  INLINE(void Append(const char* chars)) {
    if (is_ascii_) {
      Append_<true>(chars);
    } else {
      Append_<false>(chars);
    }
  }

  INLINE(Handle<Object> GetProperty(Handle<JSObject> object,
                                    Handle<String> key));

  INLINE(bool MayHaveToJsonFunction(Handle<JSObject> object));

  INLINE(Result Serialize(Handle<Object> object)) {
    return Serialize_<false>(object);
  }

  INLINE(Result SerializeDeferred(Handle<Object> object,
                           bool deferred_comma,
                           Handle<String> deferred_key)) {
    ASSERT(!deferred_key.is_null());
    return Serialize_<true>(object, deferred_comma, deferred_key);
  }

  template <bool deferred_key>
  Result Serialize_(Handle<Object> object,
                    bool comma = false,
                    Handle<String> key = Handle<String>::null());

  INLINE(void SerializeDeferredKey(bool deferred_comma,
                                   Handle<String> deferred_key)) {
    if (deferred_comma) Append(',');
    SerializeString(deferred_key);
    Append(':');
  }

  INLINE(Result SerializeSmi(Smi* object));

  INLINE(Result SerializeDouble(double number));
  INLINE(Result SerializeHeapNumber(Handle<HeapNumber> object)) {
    return SerializeDouble(object->value());
  }

  Result SerializeArray(Handle<JSArray> object);
  Result SerializeObject(Handle<JSObject> object);

  void SerializeString(Handle<String> object);

  template <bool is_ascii, typename Char>
  INLINE(void SerializeString_(Vector<const Char> vector,
                               Handle<String> string));

  template <typename Char>
  INLINE(Vector<const Char> GetCharVector(Handle<String> string));

  INLINE(Result StackPush(Handle<Object> object));
  INLINE(void StackPop());

  INLINE(Handle<String> accumulator()) {
    return Handle<String>(String::cast(accumulator_store_->value()));
  }

  INLINE(void set_accumulator(Handle<String> string)) {
    return accumulator_store_->set_value(*string);
  }

  Isolate* isolate_;
  // We use a value wrapper for the string accumulator to keep the
  // (indirect) handle to it in the outermost handle scope.
  Handle<JSValue> accumulator_store_;
  Handle<String> current_part_;
  Handle<String> tojson_symbol_;
  Handle<JSArray> stack_;
  int current_index_;
  int part_length_;
  bool is_ascii_;

  static const int kJsonQuotesCharactersPerEntry = 8;
  static const char* const JsonQuotes;
};


const char* const BasicJsonStringifier::JsonQuotes =
    "\\u0000\0 \\u0001\0 \\u0002\0 \\u0003\0 "
    "\\u0004\0 \\u0005\0 \\u0006\0 \\u0007\0 "
    "\\b\0     \\t\0     \\n\0     \\u000b\0 "
    "\\f\0     \\r\0     \\u000e\0 \\u000f\0 "
    "\\u0010\0 \\u0011\0 \\u0012\0 \\u0013\0 "
    "\\u0014\0 \\u0015\0 \\u0016\0 \\u0017\0 "
    "\\u0018\0 \\u0019\0 \\u001a\0 \\u001b\0 "
    "\\u001c\0 \\u001d\0 \\u001e\0 \\u001f\0 "
    " \0      !\0      \\\"\0     #\0      "
    "$\0      %\0      &\0      '\0      "
    "(\0      )\0      *\0      +\0      "
    ",\0      -\0      .\0      /\0      "
    "0\0      1\0      2\0      3\0      "
    "4\0      5\0      6\0      7\0      "
    "8\0      9\0      :\0      ;\0      "
    "<\0      =\0      >\0      ?\0      "
    "@\0      A\0      B\0      C\0      "
    "D\0      E\0      F\0      G\0      "
    "H\0      I\0      J\0      K\0      "
    "L\0      M\0      N\0      O\0      "
    "P\0      Q\0      R\0      S\0      "
    "T\0      U\0      V\0      W\0      "
    "X\0      Y\0      Z\0      [\0      "
    "\\\\\0     ]\0      ^\0      _\0      "
    "`\0      a\0      b\0      c\0      "
    "d\0      e\0      f\0      g\0      "
    "h\0      i\0      j\0      k\0      "
    "l\0      m\0      n\0      o\0      "
    "p\0      q\0      r\0      s\0      "
    "t\0      u\0      v\0      w\0      "
    "x\0      y\0      z\0      {\0      "
    "|\0      }\0      ~\0      \177\0      ";


BasicJsonStringifier::BasicJsonStringifier(Isolate* isolate)
    : isolate_(isolate), current_index_(0), is_ascii_(true) {
  accumulator_store_ = Handle<JSValue>::cast(
      isolate_->factory()->ToObject(isolate_->factory()->empty_string()));
  part_length_ = kInitialPartLength;
  current_part_ =
      isolate_->factory()->NewRawAsciiString(kInitialPartLength);
  tojson_symbol_ = isolate_->factory()->LookupAsciiSymbol("toJSON");
  stack_ = isolate_->factory()->NewJSArray(8);
}


MaybeObject* BasicJsonStringifier::Stringify(Handle<Object> object) {
  switch (Serialize(object)) {
    case SUCCESS:
      ShrinkCurrentPart();
      return *isolate_->factory()->NewConsString(accumulator(), current_part_);
    case UNCHANGED:
      return isolate_->heap()->undefined_value();
    case CIRCULAR:
      return isolate_->Throw(*isolate_->factory()->NewTypeError(
                 "circular_structure", HandleVector<Object>(NULL, 0)));
    case STACK_OVERFLOW:
      return isolate_->StackOverflow();
    default:
      return Smi::FromInt(0);
  }
}


template <bool is_ascii, typename Char>
void BasicJsonStringifier::Append_(Char c) {
  if (is_ascii) {
    SeqAsciiString::cast(*current_part_)->SeqAsciiStringSet(
        current_index_++, c);
  } else {
    SeqTwoByteString::cast(*current_part_)->SeqTwoByteStringSet(
        current_index_++, c);
  }
  if (current_index_ == part_length_) Extend<is_ascii>();
}


template <bool is_ascii, typename Char>
void BasicJsonStringifier::AppendUnchecked_(Char c) {
  if (is_ascii) {
    SeqAsciiString::cast(*current_part_)->SeqAsciiStringSet(
        current_index_++, c);
  } else {
    SeqTwoByteString::cast(*current_part_)->SeqTwoByteStringSet(
        current_index_++, c);
  }
  ASSERT(current_index_ < part_length_);
}


template <bool is_ascii, typename Char>
void BasicJsonStringifier::Append_(const Char* chars) {
  for ( ; *chars != '\0'; chars++) Append_<is_ascii, Char>(*chars);
}


template <bool is_ascii, typename Char>
void BasicJsonStringifier::AppendUnchecked_(const Char* chars) {
  for ( ; *chars != '\0'; chars++) AppendUnchecked_<is_ascii, Char>(*chars);
}


Handle<Object> BasicJsonStringifier::GetProperty(Handle<JSObject> object,
                                                 Handle<String> key) {
  LookupResult lookup(isolate_);
  object->LocalLookupRealNamedProperty(*key, &lookup);
  if (!lookup.IsProperty()) return isolate_->factory()->undefined_value();
  switch (lookup.type()) {
    case NORMAL: {
      Object* value = lookup.holder()->GetNormalizedProperty(&lookup);
      ASSERT(!value->IsTheHole());
      return Handle<Object>(value);
    }
    case FIELD: {
      Object* value = lookup.holder()->FastPropertyAt(lookup.GetFieldIndex());
      ASSERT(!value->IsTheHole());
      return Handle<Object>(value);
    }
    case CONSTANT_FUNCTION:
      return Handle<Object>(lookup.GetConstantFunction());
    case CALLBACKS:
    case HANDLER:
    case INTERCEPTOR:
      return Handle<Object>::null();
    case TRANSITION:
    case NONEXISTENT:
      UNREACHABLE();
      break;
  }
  return Handle<Object>::null();
}


bool BasicJsonStringifier::MayHaveToJsonFunction(Handle<JSObject> object) {
  LookupResult lookup(isolate_);
  object->LookupRealNamedProperty(*tojson_symbol_, &lookup);
  if (!lookup.IsProperty()) return false;
  Object* value;
  switch (lookup.type()) {
    case NORMAL:
      value = lookup.holder()->GetNormalizedProperty(&lookup);
      break;
    case FIELD:
      value = lookup.holder()->FastPropertyAt(lookup.GetFieldIndex());
      break;
    default:
      return true;
  }
  ASSERT(!value->IsTheHole());
  return value->IsSpecFunction();
}


BasicJsonStringifier::Result BasicJsonStringifier::StackPush(
    Handle<Object> object) {
  int length = Smi::cast(stack_->length())->value();
  if (length > kStackLimit) return STACK_OVERFLOW;
  FixedArray* elements = FixedArray::cast(stack_->elements());
  for (int i = 0; i < length; i++) {
    if (elements->get(i) == *object) {
      return CIRCULAR;
    }
  }
  stack_->EnsureSize(length + 1);
  FixedArray::cast(stack_->elements())->set(length, *object);
  stack_->set_length(Smi::FromInt(length + 1));
  return SUCCESS;
}


void BasicJsonStringifier::StackPop() {
  int length = Smi::cast(stack_->length())->value();
  stack_->set_length(Smi::FromInt(length - 1));
}


template <bool deferred_key>
BasicJsonStringifier::Result BasicJsonStringifier::Serialize_(
    Handle<Object> object, bool comma, Handle<String> key) {
  if (object->IsJSObject()) {
    // We don't deal with custom toJSON functions.
    if (MayHaveToJsonFunction(Handle<JSObject>::cast(object))) return BAILOUT;

    if (object->IsJSFunction()) {
      return UNCHANGED;
    } else if (object->IsJSArray()) {
      if (deferred_key) SerializeDeferredKey(comma, key);
      return SerializeArray(Handle<JSArray>::cast(object));
    } else if (object->IsJSValue()) {
      // JSValue with a custom prototype.
      if (object->GetPrototype()->IsJSReceiver()) return BAILOUT;
      // Unpack value wrapper and fall through.
      object = Handle<Object>(JSValue::cast(*object)->value());
    } else {
      if (deferred_key) SerializeDeferredKey(comma, key);
      return SerializeObject(Handle<JSObject>::cast(object));
    }
  }

  if (object->IsString()) {
    if (deferred_key) SerializeDeferredKey(comma, key);
    SerializeString(Handle<String>::cast(object));
    return SUCCESS;
  } else if (object->IsSmi()) {
    if (deferred_key) SerializeDeferredKey(comma, key);
    return SerializeSmi(Smi::cast(*object));
  } else if (object->IsHeapNumber()) {
    if (deferred_key) SerializeDeferredKey(comma, key);
    return SerializeHeapNumber(Handle<HeapNumber>::cast(object));
  } else if (object->IsOddball()) {
    switch (Oddball::cast(*object)->kind()) {
      case Oddball::kFalse:
        if (deferred_key) SerializeDeferredKey(comma, key);
        Append("false");
        return SUCCESS;
      case Oddball::kTrue:
        if (deferred_key) SerializeDeferredKey(comma, key);
        Append("true");
        return SUCCESS;
      case Oddball::kNull:
        if (deferred_key) SerializeDeferredKey(comma, key);
        Append("null");
        return SUCCESS;
    }
  }

  return UNCHANGED;
}


BasicJsonStringifier::Result BasicJsonStringifier::SerializeSmi(Smi* object) {
  static const int kBufferSize = 100;
  char chars[kBufferSize];
  Vector<char> buffer(chars, kBufferSize);
  Append(IntToCString(object->value(), buffer));
  return SUCCESS;
}


BasicJsonStringifier::Result BasicJsonStringifier::SerializeDouble(
    double number) {
  if (isinf(number) || isnan(number)) {
    Append("null");
    return SUCCESS;
  }
  static const int kBufferSize = 100;
  char chars[kBufferSize];
  Vector<char> buffer(chars, kBufferSize);
  Append(DoubleToCString(number, buffer));
  return SUCCESS;
}


BasicJsonStringifier::Result BasicJsonStringifier::SerializeArray(
    Handle<JSArray> object) {
  HandleScope handle_scope(isolate_);
  if (StackPush(object) == CIRCULAR) return CIRCULAR;
  int length = Smi::cast(object->length())->value();
  Append('[');
  switch (object->GetElementsKind()) {
    case FAST_SMI_ELEMENTS: {
      Handle<FixedArray> elements = Handle<FixedArray>(
          FixedArray::cast(object->elements()));
      for (int i = 0; i < length; i++) {
        if (i > 0) Append(',');
        SerializeSmi(Smi::cast(elements->get(i)));
      }
      break;
    }
    case FAST_HOLEY_SMI_ELEMENTS: {
      Handle<FixedArray> elements = Handle<FixedArray>(
          FixedArray::cast(object->elements()));
      for (int i = 0; i < length; i++) {
        if (i > 0) Append(',');
        if (elements->is_the_hole(i)) {
          Append("null");
        } else {
          SerializeSmi(Smi::cast(elements->get(i)));
        }
      }
      break;
    }
    case FAST_HOLEY_DOUBLE_ELEMENTS:
    case FAST_DOUBLE_ELEMENTS: {
      Handle<FixedDoubleArray> elements = Handle<FixedDoubleArray>(
          FixedDoubleArray::cast(object->elements()));
      for (int i = 0; i < length; i++) {
        if (i > 0) Append(',');
        SerializeDouble(elements->get_scalar(i));
      }
      break;
    }
    case FAST_HOLEY_ELEMENTS:
    case FAST_ELEMENTS: {
      Handle<FixedArray> elements = Handle<FixedArray>(
          FixedArray::cast(object->elements()));
      for (int i = 0; i < length; i++) {
        if (i > 0) Append(',');
        Result result = Serialize(Handle<Object>(elements->get(i)));
        if (result == SUCCESS) continue;
        if (result == UNCHANGED) {
          Append("null");
        } else {
          return result;
        }
      }
      break;
    }
    default:
      return BAILOUT;
  }
  Append(']');
  StackPop();
  current_part_ = handle_scope.CloseAndEscape(current_part_);
  return SUCCESS;
}


BasicJsonStringifier::Result BasicJsonStringifier::SerializeObject(
    Handle<JSObject> object) {
  HandleScope handle_scope(isolate_);
  Result stack_push = StackPush(object);
  if (stack_push != SUCCESS) return stack_push;
  if (object->IsJSGlobalProxy()) return BAILOUT;
  bool threw = false;
  Handle<FixedArray> contents =
      GetKeysInFixedArrayFor(object, LOCAL_ONLY, &threw);
  if (threw) return BAILOUT;
  Append('{');
  int length = contents->length();
  bool comma = false;
  for (int i = 0; i < length; i++) {
    Object* key = contents->get(i);
    Handle<String> key_handle;
    Handle<Object> property;
    if (key->IsString()) {
      key_handle = Handle<String>(String::cast(key));
      property = GetProperty(object, key_handle);
    } else {
      ASSERT(key->IsNumber());
      key_handle = isolate_->factory()->NumberToString(Handle<Object>(key));
      uint32_t index;
      if (key->IsSmi()) {
        property = Object::GetElement(object, Smi::cast(key)->value());
      } else if (key_handle->AsArrayIndex(&index)) {
        property = Object::GetElement(object, index);
      } else {
        property = GetProperty(object, key_handle);
      }
    }
    if (property.is_null()) return BAILOUT;
    Result result = SerializeDeferred(property, comma, key_handle);
    if (!comma && result == SUCCESS) comma = true;
    if (result >= BAILOUT) return result;
  }
  Append('}');
  StackPop();
  current_part_ = handle_scope.CloseAndEscape(current_part_);
  return SUCCESS;
}


void BasicJsonStringifier::ShrinkCurrentPart() {
  ASSERT(current_index_ < part_length_);
  if (current_index_ == 0) {
    current_part_ = isolate_->factory()->empty_string();
    return;
  }

  int string_size, allocated_string_size;
  if (is_ascii_) {
    allocated_string_size = SeqAsciiString::SizeFor(part_length_);
    string_size = SeqAsciiString::SizeFor(current_index_);
  } else {
    allocated_string_size = SeqTwoByteString::SizeFor(part_length_);
    string_size = SeqTwoByteString::SizeFor(current_index_);
  }

  int delta = allocated_string_size - string_size;
  current_part_->set_length(current_index_);

  // String sizes are pointer size aligned, so that we can use filler objects
  // that are a multiple of pointer size.
  Address end_of_string = current_part_->address() + string_size;
  isolate_->heap()->CreateFillerObjectAt(end_of_string, delta);
  if (Marking::IsBlack(Marking::MarkBitFrom(*current_part_))) {
    MemoryChunk::IncrementLiveBytesFromMutator(
        current_part_->address(), -delta);
  }
}


template <bool is_ascii>
void BasicJsonStringifier::Extend() {
  set_accumulator(
      isolate_->factory()->NewConsString(accumulator(), current_part_));
  if (part_length_ <= kMaxPartLength / kPartLengthGrowthFactor) {
    part_length_ *= kPartLengthGrowthFactor;
  }
  if (is_ascii) {
    current_part_ =
        isolate_->factory()->NewRawAsciiString(part_length_);
  } else {
    current_part_ =
        isolate_->factory()->NewRawTwoByteString(part_length_);
  }
  current_index_ = 0;
}


void BasicJsonStringifier::ChangeEncoding() {
  ShrinkCurrentPart();
  set_accumulator(
      isolate_->factory()->NewConsString(accumulator(), current_part_));
  current_part_ =
      isolate_->factory()->NewRawTwoByteString(part_length_);
  current_index_ = 0;
  is_ascii_ = false;
}


template <bool is_ascii, typename Char>
void BasicJsonStringifier::SerializeString_(Vector<const Char> vector,
                                            Handle<String> string) {
  int length = vector.length();
  if (current_index_ + (length << 3) < (part_length_ - 2)) {
    AssertNoAllocation no_allocation_scope;
    AppendUnchecked_<is_ascii, char>('"');
    for (int i = 0; i < length; i++) {
      Char c = vector[i];
      if ((c >= '#' && c <= '~' && c != '\\') ||
          (!is_ascii && ((c & 0xFF80) != 0))) {
        AppendUnchecked_<is_ascii, Char>(c);
      } else {
        AppendUnchecked_<is_ascii, char>(
            &JsonQuotes[c * kJsonQuotesCharactersPerEntry]);
      }
    }
    AppendUnchecked_<is_ascii, char>('"');
  } else {
    Append_<is_ascii, char>('"');
    String* string_location = *string;
    for (int i = 0; i < length; i++) {
      Char c = vector[i];
      if ((c >= '#' && c <= '~' && c != '\\') ||
          (!is_ascii && ((c & 0xFF80) != 0))) {
        Append_<is_ascii, Char>(c);
      } else {
        Append_<is_ascii, char>(&JsonQuotes[c * kJsonQuotesCharactersPerEntry]);
      }
      // If GC moved the string, we need to refresh the vector.
      if (*string != string_location) {
        vector = GetCharVector<Char>(string);
        string_location = *string;
      }
    }
    Append_<is_ascii, char>('"');
  }
}


template <>
Vector<const char> BasicJsonStringifier::GetCharVector(Handle<String> string) {
  String::FlatContent flat = string->GetFlatContent();
  ASSERT(flat.IsAscii());
  return flat.ToAsciiVector();
}


template <>
Vector<const uc16> BasicJsonStringifier::GetCharVector(Handle<String> string) {
  String::FlatContent flat = string->GetFlatContent();
  ASSERT(flat.IsTwoByte());
  return flat.ToUC16Vector();
}


void BasicJsonStringifier::SerializeString(Handle<String> object) {
  FlattenString(object);
  String::FlatContent flat = object->GetFlatContent();
  if (is_ascii_) {
    if (flat.IsAscii()) {
      SerializeString_<true, char>(flat.ToAsciiVector(), object);
    } else {
      ChangeEncoding();
      SerializeString(object);
    }
  } else {
    if (flat.IsAscii()) {
      SerializeString_<false, char>(flat.ToAsciiVector(), object);
    } else {
      SerializeString_<false, uc16>(flat.ToUC16Vector(), object);
    }
  }
}

} }  // namespace v8::internal

#endif  // V8_JSON_STRINGIFIER_H_
