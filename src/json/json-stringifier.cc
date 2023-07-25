// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/json/json-stringifier.h"

#include "src/base/strings.h"
#include "src/common/message-template.h"
#include "src/numbers/conversions.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-raw-json-inl.h"
#include "src/objects/lookup.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball-inl.h"
#include "src/objects/ordered-hash-table.h"
#include "src/objects/smi.h"
#include "src/strings/string-builder-inl.h"

namespace v8 {
namespace internal {

class JsonStringifier {
 public:
  explicit JsonStringifier(Isolate* isolate);

  ~JsonStringifier() {
    if (one_byte_ptr_ != one_byte_array_) delete[] one_byte_ptr_;
    if (two_byte_ptr_) delete[] two_byte_ptr_;
    DeleteArray(gap_);
  }

  V8_WARN_UNUSED_RESULT MaybeHandle<Object> Stringify(Handle<Object> object,
                                                      Handle<Object> replacer,
                                                      Handle<Object> gap);

 private:
  enum Result { UNCHANGED, SUCCESS, EXCEPTION };

  bool InitializeReplacer(Handle<Object> replacer);
  bool InitializeGap(Handle<Object> gap);

  V8_WARN_UNUSED_RESULT MaybeHandle<Object> ApplyToJsonFunction(
      Handle<Object> object, Handle<Object> key);
  V8_WARN_UNUSED_RESULT MaybeHandle<Object> ApplyReplacerFunction(
      Handle<Object> value, Handle<Object> key, Handle<Object> initial_holder);

  // Entry point to serialize the object.
  V8_INLINE Result SerializeObject(Handle<Object> obj) {
    return Serialize_<false>(obj, false, factory()->empty_string());
  }

  // Serialize an array element.
  // The index may serve as argument for the toJSON function.
  V8_INLINE Result SerializeElement(Isolate* isolate, Handle<Object> object,
                                    int i) {
    return Serialize_<false>(object, false,
                             Handle<Object>(Smi::FromInt(i), isolate));
  }

  // Serialize a object property.
  // The key may or may not be serialized depending on the property.
  // The key may also serve as argument for the toJSON function.
  V8_INLINE Result SerializeProperty(Handle<Object> object, bool deferred_comma,
                                     Handle<String> deferred_key) {
    DCHECK(!deferred_key.is_null());
    return Serialize_<true>(object, deferred_comma, deferred_key);
  }

  template <typename SrcChar, typename DestChar>
  V8_INLINE void Append(SrcChar c) {
    DCHECK_EQ(encoding_ == String::ONE_BYTE_ENCODING, sizeof(DestChar) == 1);
    if (sizeof(DestChar) == 1) {
      DCHECK_EQ(String::ONE_BYTE_ENCODING, encoding_);
      one_byte_ptr_[current_index_++] = c;
    } else {
      DCHECK_EQ(String::TWO_BYTE_ENCODING, encoding_);
      two_byte_ptr_[current_index_++] = c;
    }
    if V8_UNLIKELY (current_index_ == part_length_) Extend();
  }

  V8_INLINE void AppendCharacter(uint8_t c) {
    if (encoding_ == String::ONE_BYTE_ENCODING) {
      Append<uint8_t, uint8_t>(c);
    } else {
      Append<uint8_t, base::uc16>(c);
    }
  }

  template <int N>
  V8_INLINE void AppendCStringLiteral(const char (&literal)[N]) {
    // Note that the literal contains the zero char.
    const int length = N - 1;
    static_assert(length > 0);
    if (length == 1) return AppendCharacter(literal[0]);
    if (encoding_ == String::ONE_BYTE_ENCODING && CurrentPartCanFit(N)) {
      const uint8_t* chars = reinterpret_cast<const uint8_t*>(literal);
      CopyChars<uint8_t, uint8_t>(one_byte_ptr_ + current_index_, chars,
                                  length);
      current_index_ += length;
      if (current_index_ == part_length_) Extend();
      DCHECK(HasValidCurrentIndex());
      return;
    }
    return AppendCString(literal);
  }

  template <typename SrcChar>
  V8_INLINE void AppendCString(const SrcChar* s) {
    if (encoding_ == String::ONE_BYTE_ENCODING) {
      while (*s != '\0') Append<SrcChar, uint8_t>(*s++);
    } else {
      while (*s != '\0') Append<SrcChar, base::uc16>(*s++);
    }
  }

  V8_INLINE bool CurrentPartCanFit(int length) {
    return part_length_ - current_index_ > length;
  }

  // We make a rough estimate to find out if the current string can be
  // serialized without allocating a new string part. The worst case length of
  // an escaped character is 6. Shifting the remaining string length right by 3
  // is a more pessimistic estimate, but faster to calculate.
  V8_INLINE bool EscapedLengthIfCurrentPartFits(int length) {
    if (length > kMaxPartLength) return false;
    static_assert((kMaxPartLength << 3) <= String::kMaxLength);
    // This shift will not overflow because length is already less than the
    // maximum part length.
    return CurrentPartCanFit(length << 3);
  }

  // Short strings can be copied directly to {current_part_}.
  // Requires the IncrementalStringBuilder to either have two byte encoding or
  // the incoming string to have one byte representation "underneath" (The
  // one byte check requires the string to be flat).
  bool CanAppendByCopy(Handle<String> string) {
    constexpr int kMaxStringLengthForCopy = 16;
    const bool representation_ok =
        encoding_ == String::TWO_BYTE_ENCODING ||
        (string->IsFlat() &&
         String::IsOneByteRepresentationUnderneath(*string));

    return representation_ok && string->length() <= kMaxStringLengthForCopy &&
           CurrentPartCanFit(string->length());
  }

  void AppendStringByCopy(Handle<String> string) {
    DCHECK(CanAppendByCopy(string));

    {
      DisallowGarbageCollection no_gc;
      if (encoding_ == String::ONE_BYTE_ENCODING) {
        if (String::IsOneByteRepresentationUnderneath(*string)) {
          CopyChars<uint8_t, uint8_t>(
              one_byte_ptr_ + current_index_,
              string->GetCharVector<uint8_t>(no_gc).begin(), string->length());
        } else {
          ChangeEncoding();
          CopyChars<uint16_t, uint16_t>(
              two_byte_ptr_ + current_index_,
              string->GetCharVector<uint16_t>(no_gc).begin(), string->length());
        }
      } else {
        if (String::IsOneByteRepresentationUnderneath(*string)) {
          CopyChars<uint8_t, uint16_t>(
              two_byte_ptr_ + current_index_,
              string->GetCharVector<uint8_t>(no_gc).begin(), string->length());
        } else {
          CopyChars<uint16_t, uint16_t>(
              two_byte_ptr_ + current_index_,
              string->GetCharVector<uint16_t>(no_gc).begin(), string->length());
        }
      }
    }
    current_index_ += string->length();
    DCHECK(current_index_ <= part_length_);
    if (current_index_ == part_length_) Extend();
  }

  V8_NOINLINE void AppendString(Handle<String> string) {
    while (!CanAppendByCopy(string)) Extend();
    AppendStringByCopy(string);
  }

  bool HasValidCurrentIndex() const { return current_index_ < part_length_; }

  template <bool deferred_string_key>
  Result Serialize_(Handle<Object> object, bool comma, Handle<Object> key);

  V8_INLINE void SerializeDeferredKey(bool deferred_comma,
                                      Handle<Object> deferred_key);

  Result SerializeSmi(Smi object);

  Result SerializeDouble(double number);
  V8_INLINE Result SerializeHeapNumber(Handle<HeapNumber> object) {
    return SerializeDouble(object->value());
  }

  Result SerializeJSPrimitiveWrapper(Handle<JSPrimitiveWrapper> object,
                                     Handle<Object> key);

  V8_INLINE Result SerializeJSArray(Handle<JSArray> object, Handle<Object> key);
  V8_INLINE Result SerializeJSObject(Handle<JSObject> object,
                                     Handle<Object> key);

  Result SerializeJSProxy(Handle<JSProxy> object, Handle<Object> key);
  Result SerializeJSReceiverSlow(Handle<JSReceiver> object);
  Result SerializeArrayLikeSlow(Handle<JSReceiver> object, uint32_t start,
                                uint32_t length);

  void SerializeString(Handle<String> object);

  template <typename DestChar>
  class NoExtendBuilder {
   public:
    NoExtendBuilder(DestChar* start, int* current_index)
        : current_index_(current_index), start_(start), cursor_(start) {}
    ~NoExtendBuilder() {
      *current_index_ += static_cast<int>(cursor_ - start_);
    }

    V8_INLINE void Append(DestChar c) { *(cursor_++) = c; }
    V8_INLINE void AppendCString(const char* s) {
      const uint8_t* u = reinterpret_cast<const uint8_t*>(s);
      while (*u != '\0') Append(*(u++));
    }

   private:
    int* current_index_;
    DestChar* start_;
    DestChar* cursor_;
  };

  template <typename SrcChar, typename DestChar>
  V8_INLINE static void SerializeStringUnchecked_(
      base::Vector<const SrcChar> src, NoExtendBuilder<DestChar>* dest);

  template <typename SrcChar, typename DestChar>
  V8_INLINE void SerializeString_(Handle<String> string);

  template <typename Char>
  V8_INLINE static bool DoNotEscape(Char c);

  V8_INLINE void NewLine();
  V8_NOINLINE void NewLineOutline();
  V8_INLINE void Indent() { indent_++; }
  V8_INLINE void Unindent() { indent_--; }
  V8_INLINE void Separator(bool first);

  Handle<JSReceiver> CurrentHolder(Handle<Object> value,
                                   Handle<Object> inital_holder);

  Result StackPush(Handle<Object> object, Handle<Object> key);
  void StackPop();

  // Uses the current stack_ to provide a detailed error message of
  // the objects involved in the circular structure.
  Handle<String> ConstructCircularStructureErrorMessage(Handle<Object> last_key,
                                                        size_t start_index);
  // The prefix and postfix count do NOT include the starting and
  // closing lines of the error message.
  static const int kCircularErrorMessagePrefixCount = 2;
  static const int kCircularErrorMessagePostfixCount = 1;

  static const int kInitialPartLength = 2048;
  static const int kMaxPartLength = 16 * 1024;
  static const int kPartLengthGrowthFactor = 2;

  Factory* factory() { return isolate_->factory(); }

  V8_NOINLINE void Extend();
  V8_NOINLINE void ChangeEncoding();

  Isolate* isolate_;
  String::Encoding encoding_;
  Handle<FixedArray> property_list_;
  Handle<JSReceiver> replacer_function_;
  uint8_t one_byte_array_[kInitialPartLength];
  uint8_t* one_byte_ptr_;
  base::uc16* gap_;
  base::uc16* two_byte_ptr_;
  void* part_ptr_;
  int indent_;
  int part_length_;
  int current_index_;
  bool overflowed_;

  using KeyObject = std::pair<Handle<Object>, Handle<Object>>;
  std::vector<KeyObject> stack_;

  static const int kJsonEscapeTableEntrySize = 8;
  static const char* const JsonEscapeTable;
};

MaybeHandle<Object> JsonStringify(Isolate* isolate, Handle<Object> object,
                                  Handle<Object> replacer, Handle<Object> gap) {
  JsonStringifier stringifier(isolate);
  return stringifier.Stringify(object, replacer, gap);
}

// Translation table to escape Latin1 characters.
// Table entries start at a multiple of 8 and are null-terminated.
const char* const JsonStringifier::JsonEscapeTable =
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
    "|\0      }\0      ~\0      \x7F\0      "
    "\x80\0      \x81\0      \x82\0      \x83\0      "
    "\x84\0      \x85\0      \x86\0      \x87\0      "
    "\x88\0      \x89\0      \x8A\0      \x8B\0      "
    "\x8C\0      \x8D\0      \x8E\0      \x8F\0      "
    "\x90\0      \x91\0      \x92\0      \x93\0      "
    "\x94\0      \x95\0      \x96\0      \x97\0      "
    "\x98\0      \x99\0      \x9A\0      \x9B\0      "
    "\x9C\0      \x9D\0      \x9E\0      \x9F\0      "
    "\xA0\0      \xA1\0      \xA2\0      \xA3\0      "
    "\xA4\0      \xA5\0      \xA6\0      \xA7\0      "
    "\xA8\0      \xA9\0      \xAA\0      \xAB\0      "
    "\xAC\0      \xAD\0      \xAE\0      \xAF\0      "
    "\xB0\0      \xB1\0      \xB2\0      \xB3\0      "
    "\xB4\0      \xB5\0      \xB6\0      \xB7\0      "
    "\xB8\0      \xB9\0      \xBA\0      \xBB\0      "
    "\xBC\0      \xBD\0      \xBE\0      \xBF\0      "
    "\xC0\0      \xC1\0      \xC2\0      \xC3\0      "
    "\xC4\0      \xC5\0      \xC6\0      \xC7\0      "
    "\xC8\0      \xC9\0      \xCA\0      \xCB\0      "
    "\xCC\0      \xCD\0      \xCE\0      \xCF\0      "
    "\xD0\0      \xD1\0      \xD2\0      \xD3\0      "
    "\xD4\0      \xD5\0      \xD6\0      \xD7\0      "
    "\xD8\0      \xD9\0      \xDA\0      \xDB\0      "
    "\xDC\0      \xDD\0      \xDE\0      \xDF\0      "
    "\xE0\0      \xE1\0      \xE2\0      \xE3\0      "
    "\xE4\0      \xE5\0      \xE6\0      \xE7\0      "
    "\xE8\0      \xE9\0      \xEA\0      \xEB\0      "
    "\xEC\0      \xED\0      \xEE\0      \xEF\0      "
    "\xF0\0      \xF1\0      \xF2\0      \xF3\0      "
    "\xF4\0      \xF5\0      \xF6\0      \xF7\0      "
    "\xF8\0      \xF9\0      \xFA\0      \xFB\0      "
    "\xFC\0      \xFD\0      \xFE\0      \xFF\0      ";

JsonStringifier::JsonStringifier(Isolate* isolate)
    : isolate_(isolate),
      encoding_(String::ONE_BYTE_ENCODING),
      gap_(nullptr),
      two_byte_ptr_(nullptr),
      indent_(0),
      part_length_(kInitialPartLength),
      current_index_(0),
      overflowed_(false),
      stack_() {
  one_byte_ptr_ = one_byte_array_;
  part_ptr_ = one_byte_ptr_;
}

MaybeHandle<Object> JsonStringifier::Stringify(Handle<Object> object,
                                               Handle<Object> replacer,
                                               Handle<Object> gap) {
  if (!InitializeReplacer(replacer)) {
    CHECK(isolate_->has_pending_exception());
    return MaybeHandle<Object>();
  }
  if (!gap->IsUndefined(isolate_) && !InitializeGap(gap)) {
    CHECK(isolate_->has_pending_exception());
    return MaybeHandle<Object>();
  }
  Result result = SerializeObject(object);
  if (result == UNCHANGED) return factory()->undefined_value();
  if (result == SUCCESS) {
    if (encoding_ == String::ONE_BYTE_ENCODING) {
      one_byte_ptr_[current_index_++] = '\0';
      return isolate_->factory()->NewStringFromAsciiChecked(
          (char*)one_byte_ptr_);
    } else {
      return isolate_->factory()->NewStringFromTwoByte(
          base::Vector<const base::uc16>(two_byte_ptr_, current_index_));
    }
  }
  DCHECK(result == EXCEPTION);
  CHECK(isolate_->has_pending_exception());
  return MaybeHandle<Object>();
}

bool JsonStringifier::InitializeReplacer(Handle<Object> replacer) {
  DCHECK(property_list_.is_null());
  DCHECK(replacer_function_.is_null());
  Maybe<bool> is_array = Object::IsArray(replacer);
  if (is_array.IsNothing()) return false;
  if (is_array.FromJust()) {
    HandleScope handle_scope(isolate_);
    Handle<OrderedHashSet> set = factory()->NewOrderedHashSet();
    Handle<Object> length_obj;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, length_obj,
        Object::GetLengthFromArrayLike(isolate_,
                                       Handle<JSReceiver>::cast(replacer)),
        false);
    uint32_t length;
    if (!length_obj->ToUint32(&length)) length = kMaxUInt32;
    for (uint32_t i = 0; i < length; i++) {
      Handle<Object> element;
      Handle<String> key;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate_, element, Object::GetElement(isolate_, replacer, i), false);
      if (element->IsNumber() || element->IsString()) {
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate_, key, Object::ToString(isolate_, element), false);
      } else if (element->IsJSPrimitiveWrapper()) {
        Handle<Object> value(Handle<JSPrimitiveWrapper>::cast(element)->value(),
                             isolate_);
        if (value->IsNumber() || value->IsString()) {
          ASSIGN_RETURN_ON_EXCEPTION_VALUE(
              isolate_, key, Object::ToString(isolate_, element), false);
        }
      }
      if (key.is_null()) continue;
      // Object keys are internalized, so do it here.
      key = factory()->InternalizeString(key);
      MaybeHandle<OrderedHashSet> set_candidate =
          OrderedHashSet::Add(isolate_, set, key);
      if (!set_candidate.ToHandle(&set)) {
        CHECK(isolate_->has_pending_exception());
        return false;
      }
    }
    property_list_ = OrderedHashSet::ConvertToKeysArray(
        isolate_, set, GetKeysConversion::kKeepNumbers);
    property_list_ = handle_scope.CloseAndEscape(property_list_);
  } else if (replacer->IsCallable()) {
    replacer_function_ = Handle<JSReceiver>::cast(replacer);
  }
  return true;
}

bool JsonStringifier::InitializeGap(Handle<Object> gap) {
  DCHECK_NULL(gap_);
  HandleScope scope(isolate_);
  if (gap->IsJSPrimitiveWrapper()) {
    Handle<Object> value(Handle<JSPrimitiveWrapper>::cast(gap)->value(),
                         isolate_);
    if (value->IsString()) {
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate_, gap,
                                       Object::ToString(isolate_, gap), false);
    } else if (value->IsNumber()) {
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate_, gap,
                                       Object::ToNumber(isolate_, gap), false);
    }
  }

  if (gap->IsString()) {
    Handle<String> gap_string = Handle<String>::cast(gap);
    if (gap_string->length() > 0) {
      int gap_length = std::min(gap_string->length(), 10);
      gap_ = NewArray<base::uc16>(gap_length + 1);
      String::WriteToFlat(*gap_string, gap_, 0, gap_length);
      for (int i = 0; i < gap_length; i++) {
        if (gap_[i] > String::kMaxOneByteCharCode) {
          ChangeEncoding();
          break;
        }
      }
      gap_[gap_length] = '\0';
    }
  } else if (gap->IsNumber()) {
    double value = std::min(gap->Number(), 10.0);
    if (value > 0) {
      int gap_length = DoubleToInt32(value);
      gap_ = NewArray<base::uc16>(gap_length + 1);
      for (int i = 0; i < gap_length; i++) gap_[i] = ' ';
      gap_[gap_length] = '\0';
    }
  }
  return true;
}

MaybeHandle<Object> JsonStringifier::ApplyToJsonFunction(Handle<Object> object,
                                                         Handle<Object> key) {
  HandleScope scope(isolate_);

  // Retrieve toJSON function. The LookupIterator automatically handles
  // the ToObject() equivalent ("GetRoot") if {object} is a BigInt.
  Handle<Object> fun;
  LookupIterator it(isolate_, object, factory()->toJSON_string(),
                    LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  ASSIGN_RETURN_ON_EXCEPTION(isolate_, fun, Object::GetProperty(&it), Object);
  if (!fun->IsCallable()) return object;

  // Call toJSON function.
  if (key->IsSmi()) key = factory()->NumberToString(key);
  Handle<Object> argv[] = {key};
  ASSIGN_RETURN_ON_EXCEPTION(isolate_, object,
                             Execution::Call(isolate_, fun, object, 1, argv),
                             Object);
  return scope.CloseAndEscape(object);
}

MaybeHandle<Object> JsonStringifier::ApplyReplacerFunction(
    Handle<Object> value, Handle<Object> key, Handle<Object> initial_holder) {
  HandleScope scope(isolate_);
  if (key->IsSmi()) key = factory()->NumberToString(key);
  Handle<Object> argv[] = {key, value};
  Handle<JSReceiver> holder = CurrentHolder(value, initial_holder);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate_, value,
      Execution::Call(isolate_, replacer_function_, holder, 2, argv), Object);
  return scope.CloseAndEscape(value);
}

Handle<JSReceiver> JsonStringifier::CurrentHolder(
    Handle<Object> value, Handle<Object> initial_holder) {
  if (stack_.empty()) {
    Handle<JSObject> holder =
        factory()->NewJSObject(isolate_->object_function());
    JSObject::AddProperty(isolate_, holder, factory()->empty_string(),
                          initial_holder, NONE);
    return holder;
  } else {
    return Handle<JSReceiver>(JSReceiver::cast(*stack_.back().second),
                              isolate_);
  }
}

JsonStringifier::Result JsonStringifier::StackPush(Handle<Object> object,
                                                   Handle<Object> key) {
  StackLimitCheck check(isolate_);
  if (check.HasOverflowed()) {
    isolate_->StackOverflow();
    return EXCEPTION;
  }

  {
    DisallowGarbageCollection no_gc;
    Object raw_obj = *object;
    size_t size = stack_.size();
    for (size_t i = 0; i < size; ++i) {
      if (*stack_[i].second == raw_obj) {
        AllowGarbageCollection allow_to_return_error;
        Handle<String> circle_description =
            ConstructCircularStructureErrorMessage(key, i);
        Handle<Object> error = factory()->NewTypeError(
            MessageTemplate::kCircularStructure, circle_description);
        isolate_->Throw(*error);
        return EXCEPTION;
      }
    }
  }
  stack_.emplace_back(key, object);
  return SUCCESS;
}

void JsonStringifier::StackPop() { stack_.pop_back(); }

class CircularStructureMessageBuilder {
 public:
  explicit CircularStructureMessageBuilder(Isolate* isolate)
      : builder_(isolate) {}

  void AppendStartLine(Handle<Object> start_object) {
    builder_.AppendCString(kStartPrefix);
    builder_.AppendCStringLiteral("starting at object with constructor ");
    AppendConstructorName(start_object);
  }

  void AppendNormalLine(Handle<Object> key, Handle<Object> object) {
    builder_.AppendCString(kLinePrefix);
    AppendKey(key);
    builder_.AppendCStringLiteral(" -> object with constructor ");
    AppendConstructorName(object);
  }

  void AppendClosingLine(Handle<Object> closing_key) {
    builder_.AppendCString(kEndPrefix);
    AppendKey(closing_key);
    builder_.AppendCStringLiteral(" closes the circle");
  }

  void AppendEllipsis() {
    builder_.AppendCString(kLinePrefix);
    builder_.AppendCStringLiteral("...");
  }

  MaybeHandle<String> Finish() { return builder_.Finish(); }

 private:
  void AppendConstructorName(Handle<Object> object) {
    builder_.AppendCharacter('\'');
    Handle<String> constructor_name = JSReceiver::GetConstructorName(
        builder_.isolate(), Handle<JSReceiver>::cast(object));
    builder_.AppendString(constructor_name);
    builder_.AppendCharacter('\'');
  }

  // A key can either be a string, the empty string or a Smi.
  void AppendKey(Handle<Object> key) {
    if (key->IsSmi()) {
      builder_.AppendCStringLiteral("index ");
      AppendSmi(Smi::cast(*key));
      return;
    }

    CHECK(key->IsString());
    Handle<String> key_as_string = Handle<String>::cast(key);
    if (key_as_string->length() == 0) {
      builder_.AppendCStringLiteral("<anonymous>");
    } else {
      builder_.AppendCStringLiteral("property '");
      builder_.AppendString(key_as_string);
      builder_.AppendCharacter('\'');
    }
  }

  void AppendSmi(Smi smi) {
    static const int kBufferSize = 100;
    char chars[kBufferSize];
    base::Vector<char> buffer(chars, kBufferSize);
    builder_.AppendCString(IntToCString(smi.value(), buffer));
  }

  IncrementalStringBuilder builder_;
  static constexpr const char* kStartPrefix = "\n    --> ";
  static constexpr const char* kEndPrefix = "\n    --- ";
  static constexpr const char* kLinePrefix = "\n    |     ";
};

Handle<String> JsonStringifier::ConstructCircularStructureErrorMessage(
    Handle<Object> last_key, size_t start_index) {
  DCHECK(start_index < stack_.size());
  CircularStructureMessageBuilder builder(isolate_);

  // We track the index to be printed next for better readability.
  size_t index = start_index;
  const size_t stack_size = stack_.size();

  builder.AppendStartLine(stack_[index++].second);

  // Append a maximum of kCircularErrorMessagePrefixCount normal lines.
  const size_t prefix_end =
      std::min(stack_size, index + kCircularErrorMessagePrefixCount);
  for (; index < prefix_end; ++index) {
    builder.AppendNormalLine(stack_[index].first, stack_[index].second);
  }

  // If the circle consists of too many objects, we skip them and just
  // print an ellipsis.
  if (stack_size > index + kCircularErrorMessagePostfixCount) {
    builder.AppendEllipsis();
  }

  // Since we calculate the postfix lines from the back of the stack,
  // we have to ensure that lines are not printed twice.
  index = std::max(index, stack_size - kCircularErrorMessagePostfixCount);
  for (; index < stack_size; ++index) {
    builder.AppendNormalLine(stack_[index].first, stack_[index].second);
  }

  builder.AppendClosingLine(last_key);

  Handle<String> result;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate_, result, builder.Finish(),
                                   factory()->empty_string());
  return result;
}

bool MayHaveInterestingProperties(Isolate* isolate, JSReceiver object) {
  for (PrototypeIterator iter(isolate, object, kStartAtReceiver);
       !iter.IsAtEnd(); iter.Advance()) {
    if (iter.GetCurrent()->map()->may_have_interesting_properties())
      return true;
  }
  return false;
}

template <bool deferred_string_key>
JsonStringifier::Result JsonStringifier::Serialize_(Handle<Object> object,
                                                    bool comma,
                                                    Handle<Object> key) {
  StackLimitCheck interrupt_check(isolate_);
  if (interrupt_check.InterruptRequested() &&
      isolate_->stack_guard()->HandleInterrupts().IsException(isolate_)) {
    return EXCEPTION;
  }

  Handle<Object> initial_value = object;
  PtrComprCageBase cage_base(isolate_);
  if (!object->IsSmi()) {
    InstanceType instance_type =
        HeapObject::cast(*object)->map(cage_base)->instance_type();
    if ((InstanceTypeChecker::IsJSReceiver(instance_type) &&
         MayHaveInterestingProperties(isolate_, JSReceiver::cast(*object))) ||
        InstanceTypeChecker::IsBigInt(instance_type)) {
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate_, object, ApplyToJsonFunction(object, key), EXCEPTION);
    }
  }
  if (!replacer_function_.is_null()) {
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, object, ApplyReplacerFunction(object, key, initial_value),
        EXCEPTION);
  }

  if (object->IsSmi()) {
    if (deferred_string_key) SerializeDeferredKey(comma, key);
    return SerializeSmi(Smi::cast(*object));
  }

  InstanceType instance_type =
      HeapObject::cast(*object)->map(cage_base)->instance_type();
  switch (instance_type) {
    case HEAP_NUMBER_TYPE:
      if (deferred_string_key) SerializeDeferredKey(comma, key);
      return SerializeHeapNumber(Handle<HeapNumber>::cast(object));
    case BIGINT_TYPE:
      isolate_->Throw(
          *factory()->NewTypeError(MessageTemplate::kBigIntSerializeJSON));
      return EXCEPTION;
    case ODDBALL_TYPE:
      switch (Oddball::cast(*object)->kind()) {
        case Oddball::kFalse:
          if (deferred_string_key) SerializeDeferredKey(comma, key);
          AppendCStringLiteral("false");
          return SUCCESS;
        case Oddball::kTrue:
          if (deferred_string_key) SerializeDeferredKey(comma, key);
          AppendCStringLiteral("true");
          return SUCCESS;
        case Oddball::kNull:
          if (deferred_string_key) SerializeDeferredKey(comma, key);
          AppendCStringLiteral("null");
          return SUCCESS;
        default:
          return UNCHANGED;
      }
    case JS_ARRAY_TYPE:
      if (deferred_string_key) SerializeDeferredKey(comma, key);
      return SerializeJSArray(Handle<JSArray>::cast(object), key);
    case JS_PRIMITIVE_WRAPPER_TYPE:
      if (deferred_string_key) SerializeDeferredKey(comma, key);
      return SerializeJSPrimitiveWrapper(
          Handle<JSPrimitiveWrapper>::cast(object), key);
    case SYMBOL_TYPE:
      return UNCHANGED;
    case JS_RAW_JSON_TYPE:
      DCHECK(v8_flags.harmony_json_parse_with_source);
      if (deferred_string_key) SerializeDeferredKey(comma, key);
      {
        Handle<JSRawJson> raw_json_obj = Handle<JSRawJson>::cast(object);
        Handle<String> raw_json;
        if (raw_json_obj->HasInitialLayout(isolate_)) {
          // Fast path: the object returned by JSON.rawJSON has its initial map
          // intact.
          raw_json = Handle<String>::cast(handle(
              raw_json_obj->InObjectPropertyAt(JSRawJson::kRawJsonInitialIndex),
              isolate_));
        } else {
          // Slow path: perform a property get for "rawJSON". Because raw JSON
          // objects are created frozen, it is still guaranteed that there will
          // be a property named "rawJSON" that is a String. Their initial maps
          // only change due to VM-internal operations like being optimized for
          // being used as a prototype.
          raw_json = Handle<String>::cast(
              JSObject::GetProperty(isolate_, raw_json_obj,
                                    isolate_->factory()->raw_json_string())
                  .ToHandleChecked());
        }
        AppendString(raw_json);
      }
      return SUCCESS;
#if V8_ENABLE_WEBASSEMBLY
    case WASM_STRUCT_TYPE:
    case WASM_ARRAY_TYPE:
      return UNCHANGED;
#endif
    default:
      if (InstanceTypeChecker::IsString(instance_type)) {
        if (deferred_string_key) SerializeDeferredKey(comma, key);
        SerializeString(Handle<String>::cast(object));
        return SUCCESS;
      } else {
        DCHECK(object->IsJSReceiver());
        if (HeapObject::cast(*object).IsCallable(cage_base)) return UNCHANGED;
        // Go to slow path for global proxy and objects requiring access checks.
        if (deferred_string_key) SerializeDeferredKey(comma, key);
        if (InstanceTypeChecker::IsJSProxy(instance_type)) {
          return SerializeJSProxy(Handle<JSProxy>::cast(object), key);
        }
        return SerializeJSObject(Handle<JSObject>::cast(object), key);
      }
  }

  UNREACHABLE();
}

JsonStringifier::Result JsonStringifier::SerializeJSPrimitiveWrapper(
    Handle<JSPrimitiveWrapper> object, Handle<Object> key) {
  Object raw = object->value();
  if (raw.IsString()) {
    Handle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, value, Object::ToString(isolate_, object), EXCEPTION);
    SerializeString(Handle<String>::cast(value));
  } else if (raw.IsNumber()) {
    Handle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, value, Object::ToNumber(isolate_, object), EXCEPTION);
    if (value->IsSmi()) return SerializeSmi(Smi::cast(*value));
    SerializeHeapNumber(Handle<HeapNumber>::cast(value));
  } else if (raw.IsBigInt()) {
    isolate_->Throw(
        *factory()->NewTypeError(MessageTemplate::kBigIntSerializeJSON));
    return EXCEPTION;
  } else if (raw.IsBoolean()) {
    if (raw.IsTrue(isolate_)) {
      AppendCStringLiteral("true");
    } else {
      AppendCStringLiteral("false");
    }
  } else {
    // ES6 24.3.2.1 step 10.c, serialize as an ordinary JSObject.
    return SerializeJSObject(object, key);
  }
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeSmi(Smi object) {
  static const int kBufferSize = 100;
  char chars[kBufferSize];
  base::Vector<char> buffer(chars, kBufferSize);
  AppendCString(IntToCString(object.value(), buffer));
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeDouble(double number) {
  if (std::isinf(number) || std::isnan(number)) {
    AppendCStringLiteral("null");
    return SUCCESS;
  }
  static const int kBufferSize = 100;
  char chars[kBufferSize];
  base::Vector<char> buffer(chars, kBufferSize);
  AppendCString(DoubleToCString(number, buffer));
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeJSArray(
    Handle<JSArray> object, Handle<Object> key) {
  uint32_t length = 0;
  CHECK(object->length().ToArrayLength(&length));
  DCHECK(!object->IsAccessCheckNeeded());
  if (length == 0) {
    AppendCStringLiteral("[]");
    return SUCCESS;
  }

  PtrComprCageBase cage_base(isolate_);
  Result stack_push = StackPush(object, key);
  if (stack_push != SUCCESS) return stack_push;

  AppendCharacter('[');
  Indent();
  uint32_t i = 0;
  if (replacer_function_.is_null()) {
    StackLimitCheck interrupt_check(isolate_);
    const uint32_t kInterruptLength = 4000;
    uint32_t limit = std::min(length, kInterruptLength);
    const uint32_t kMaxAllowedFastPackedLength =
        std::numeric_limits<uint32_t>::max() - kInterruptLength;
    static_assert(FixedArray::kMaxLength < kMaxAllowedFastPackedLength);
    switch (object->GetElementsKind(cage_base)) {
      case PACKED_SMI_ELEMENTS: {
        Handle<FixedArray> elements(
            FixedArray::cast(object->elements(cage_base)), isolate_);
        while (true) {
          for (; i < limit; i++) {
            Separator(i == 0);
            SerializeSmi(Smi::cast(elements->get(cage_base, i)));
          }
          if (i >= length) break;
          DCHECK_LT(limit, kMaxAllowedFastPackedLength);
          limit = std::min(length, limit + kInterruptLength);
          if (interrupt_check.InterruptRequested() &&
              isolate_->stack_guard()->HandleInterrupts().IsException(
                  isolate_)) {
            return EXCEPTION;
          }
        }
        break;
      }
      case PACKED_DOUBLE_ELEMENTS: {
        Handle<FixedDoubleArray> elements(
            FixedDoubleArray::cast(object->elements(cage_base)), isolate_);
        while (true) {
          for (; i < limit; i++) {
            Separator(i == 0);
            SerializeDouble(elements->get_scalar(i));
          }
          if (i >= length) break;
          DCHECK_LT(limit, kMaxAllowedFastPackedLength);
          limit = std::min(length, limit + kInterruptLength);
          if (interrupt_check.InterruptRequested() &&
              isolate_->stack_guard()->HandleInterrupts().IsException(
                  isolate_)) {
            return EXCEPTION;
          }
        }
        break;
      }
      case PACKED_ELEMENTS: {
        HandleScope handle_scope(isolate_);
        Handle<Object> old_length(object->length(), isolate_);
        for (i = 0; i < length; i++) {
          if (object->length() != *old_length ||
              object->GetElementsKind(cage_base) != PACKED_ELEMENTS) {
            // Fall back to slow path.
            break;
          }
          Separator(i == 0);
          Result result = SerializeElement(
              isolate_,
              handle(FixedArray::cast(object->elements())->get(cage_base, i),
                     isolate_),
              i);
          if (result == UNCHANGED) {
            AppendCStringLiteral("null");
          } else if (result != SUCCESS) {
            return result;
          }
        }
        break;
      }
      default:
        break;
    }
  }
  if (i < length) {
    // Slow path for non-fast elements and fall-back in edge case.
    Result result = SerializeArrayLikeSlow(object, i, length);
    if (result != SUCCESS) return result;
  }
  Unindent();
  NewLine();
  AppendCharacter(']');
  StackPop();
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeArrayLikeSlow(
    Handle<JSReceiver> object, uint32_t start, uint32_t length) {
  // We need to write out at least two characters per array element.
  static const int kMaxSerializableArrayLength = String::kMaxLength / 2;
  if (length > kMaxSerializableArrayLength) {
    isolate_->Throw(*isolate_->factory()->NewInvalidStringLengthError());
    return EXCEPTION;
  }
  HandleScope handle_scope(isolate_);
  for (uint32_t i = start; i < length; i++) {
    Separator(i == 0);
    Handle<Object> element;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, element, JSReceiver::GetElement(isolate_, object, i),
        EXCEPTION);
    Result result = SerializeElement(isolate_, element, i);
    if (result == SUCCESS) continue;
    if (result == UNCHANGED) {
      // Detect overflow sooner for large sparse arrays.
      if (overflowed_) {
        isolate_->Throw(*isolate_->factory()->NewInvalidStringLengthError());
        return EXCEPTION;
      }
      AppendCStringLiteral("null");
    } else {
      return result;
    }
  }
  return SUCCESS;
}

namespace {
V8_INLINE bool CanFastSerializeJSObject(PtrComprCageBase cage_base,
                                        JSObject raw_object, Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  if (raw_object->map(cage_base)->IsCustomElementsReceiverMap()) return false;
  if (!raw_object->HasFastProperties(cage_base)) return false;
  auto roots = ReadOnlyRoots(isolate);
  auto elements = raw_object->elements(cage_base);
  return elements == roots.empty_fixed_array() ||
         elements == roots.empty_slow_element_dictionary();
}
}  // namespace

JsonStringifier::Result JsonStringifier::SerializeJSObject(
    Handle<JSObject> object, Handle<Object> key) {
  PtrComprCageBase cage_base(isolate_);
  HandleScope handle_scope(isolate_);

  if (!property_list_.is_null() ||
      !CanFastSerializeJSObject(cage_base, *object, isolate_)) {
    Result stack_push = StackPush(object, key);
    if (stack_push != SUCCESS) return stack_push;
    Result result = SerializeJSReceiverSlow(object);
    if (result != SUCCESS) return result;
    StackPop();
    return SUCCESS;
  }

  DCHECK(!object->IsJSGlobalProxy());
  DCHECK(!object->HasIndexedInterceptor());
  DCHECK(!object->HasNamedInterceptor());

  Handle<Map> map(object->map(cage_base), isolate_);
  if (map->NumberOfOwnDescriptors() == 0) {
    AppendCStringLiteral("{}");
    return SUCCESS;
  }

  Result stack_push = StackPush(object, key);
  if (stack_push != SUCCESS) return stack_push;
  AppendCharacter('{');
  Indent();
  bool comma = false;
  for (InternalIndex i : map->IterateOwnDescriptors()) {
    Handle<String> key_name;
    PropertyDetails details = PropertyDetails::Empty();
    {
      DisallowGarbageCollection no_gc;
      DescriptorArray descriptors = map->instance_descriptors(cage_base);
      Name name = descriptors->GetKey(i);
      // TODO(rossberg): Should this throw?
      if (!name.IsString(cage_base)) continue;
      key_name = handle(String::cast(name), isolate_);
      details = descriptors->GetDetails(i);
    }
    if (details.IsDontEnum()) continue;
    Handle<Object> property;
    if (details.location() == PropertyLocation::kField &&
        *map == object->map(cage_base)) {
      DCHECK_EQ(PropertyKind::kData, details.kind());
      FieldIndex field_index = FieldIndex::ForDetails(*map, details);
      property = JSObject::FastPropertyAt(
          isolate_, object, details.representation(), field_index);
    } else {
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate_, property,
          Object::GetPropertyOrElement(isolate_, object, key_name), EXCEPTION);
    }
    Result result = SerializeProperty(property, comma, key_name);
    if (!comma && result == SUCCESS) comma = true;
    if (result == EXCEPTION) return result;
  }
  Unindent();
  if (comma) NewLine();
  AppendCharacter('}');
  StackPop();
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeJSReceiverSlow(
    Handle<JSReceiver> object) {
  Handle<FixedArray> contents = property_list_;
  if (contents.is_null()) {
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, contents,
        KeyAccumulator::GetKeys(isolate_, object, KeyCollectionMode::kOwnOnly,
                                ENUMERABLE_STRINGS,
                                GetKeysConversion::kConvertToString),
        EXCEPTION);
  }
  AppendCharacter('{');
  Indent();
  bool comma = false;
  for (int i = 0; i < contents->length(); i++) {
    Handle<String> key(String::cast(contents->get(i)), isolate_);
    Handle<Object> property;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, property, Object::GetPropertyOrElement(isolate_, object, key),
        EXCEPTION);
    Result result = SerializeProperty(property, comma, key);
    if (!comma && result == SUCCESS) comma = true;
    if (result == EXCEPTION) return result;
  }
  Unindent();
  if (comma) NewLine();
  AppendCharacter('}');
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeJSProxy(
    Handle<JSProxy> object, Handle<Object> key) {
  HandleScope scope(isolate_);
  Result stack_push = StackPush(object, key);
  if (stack_push != SUCCESS) return stack_push;
  Maybe<bool> is_array = Object::IsArray(object);
  if (is_array.IsNothing()) return EXCEPTION;
  if (is_array.FromJust()) {
    Handle<Object> length_object;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, length_object,
        Object::GetLengthFromArrayLike(isolate_,
                                       Handle<JSReceiver>::cast(object)),
        EXCEPTION);
    uint32_t length;
    if (!length_object->ToUint32(&length)) {
      // Technically, we need to be able to handle lengths outside the
      // uint32_t range. However, we would run into string size overflow
      // if we tried to stringify such an array.
      isolate_->Throw(*isolate_->factory()->NewInvalidStringLengthError());
      return EXCEPTION;
    }
    AppendCharacter('[');
    Indent();
    Result result = SerializeArrayLikeSlow(object, 0, length);
    if (result != SUCCESS) return result;
    Unindent();
    if (length > 0) NewLine();
    AppendCharacter(']');
  } else {
    Result result = SerializeJSReceiverSlow(object);
    if (result != SUCCESS) return result;
  }
  StackPop();
  return SUCCESS;
}

template <typename SrcChar, typename DestChar>
void JsonStringifier::SerializeStringUnchecked_(
    base::Vector<const SrcChar> src, NoExtendBuilder<DestChar>* dest) {
  // Assert that base::uc16 character is not truncated down to 8 bit.
  // The <base::uc16, char> version of this method must not be called.
  DCHECK(sizeof(DestChar) >= sizeof(SrcChar));
  for (int i = 0; i < src.length(); i++) {
    SrcChar c = src[i];
    if (DoNotEscape(c)) {
      dest->Append(c);
    } else if (sizeof(SrcChar) != 1 &&
               base::IsInRange(c, static_cast<SrcChar>(0xD800),
                               static_cast<SrcChar>(0xDFFF))) {
      // The current character is a surrogate.
      if (c <= 0xDBFF) {
        // The current character is a leading surrogate.
        if (i + 1 < src.length()) {
          // There is a next character.
          SrcChar next = src[i + 1];
          if (base::IsInRange(next, static_cast<SrcChar>(0xDC00),
                              static_cast<SrcChar>(0xDFFF))) {
            // The next character is a trailing surrogate, meaning this is a
            // surrogate pair.
            dest->Append(c);
            dest->Append(next);
            i++;
          } else {
            // The next character is not a trailing surrogate. Thus, the
            // current character is a lone leading surrogate.
            dest->AppendCString("\\u");
            char* const hex = DoubleToRadixCString(c, 16);
            dest->AppendCString(hex);
            DeleteArray(hex);
          }
        } else {
          // There is no next character. Thus, the current character is a lone
          // leading surrogate.
          dest->AppendCString("\\u");
          char* const hex = DoubleToRadixCString(c, 16);
          dest->AppendCString(hex);
          DeleteArray(hex);
        }
      } else {
        // The current character is a lone trailing surrogate. (If it had been
        // preceded by a leading surrogate, we would've ended up in the other
        // branch earlier on, and the current character would've been handled
        // as part of the surrogate pair already.)
        dest->AppendCString("\\u");
        char* const hex = DoubleToRadixCString(c, 16);
        dest->AppendCString(hex);
        DeleteArray(hex);
      }
    } else {
      dest->AppendCString(&JsonEscapeTable[c * kJsonEscapeTableEntrySize]);
    }
  }
}

template <typename SrcChar, typename DestChar>
void JsonStringifier::SerializeString_(Handle<String> string) {
  int length = string->length();
  Append<uint8_t, DestChar>('"');
  // We might be able to fit the whole escaped string in the current string
  // part, or we might need to allocate.
  if V8_LIKELY (EscapedLengthIfCurrentPartFits(length)) {
    DisallowGarbageCollection no_gc;
    base::Vector<const SrcChar> vector = string->GetCharVector<SrcChar>(no_gc);
    NoExtendBuilder<DestChar> no_extend(
        reinterpret_cast<DestChar*>(part_ptr_) + current_index_,
        &current_index_);
    SerializeStringUnchecked_(vector, &no_extend);
  } else {
    FlatStringReader reader(isolate_, string);
    for (int i = 0; i < reader.length(); i++) {
      SrcChar c = reader.Get<SrcChar>(i);
      if (DoNotEscape(c)) {
        Append<SrcChar, DestChar>(c);
      } else if (sizeof(SrcChar) != 1 &&
                 base::IsInRange(c, static_cast<SrcChar>(0xD800),
                                 static_cast<SrcChar>(0xDFFF))) {
        // The current character is a surrogate.
        if (c <= 0xDBFF) {
          // The current character is a leading surrogate.
          if (i + 1 < reader.length()) {
            // There is a next character.
            SrcChar next = reader.Get<SrcChar>(i + 1);
            if (base::IsInRange(next, static_cast<SrcChar>(0xDC00),
                                static_cast<SrcChar>(0xDFFF))) {
              // The next character is a trailing surrogate, meaning this is a
              // surrogate pair.
              Append<SrcChar, DestChar>(c);
              Append<SrcChar, DestChar>(next);
              i++;
            } else {
              // The next character is not a trailing surrogate. Thus, the
              // current character is a lone leading surrogate.
              AppendCStringLiteral("\\u");
              char* const hex = DoubleToRadixCString(c, 16);
              AppendCString(hex);
              DeleteArray(hex);
            }
          } else {
            // There is no next character. Thus, the current character is a
            // lone leading surrogate.
            AppendCStringLiteral("\\u");
            char* const hex = DoubleToRadixCString(c, 16);
            AppendCString(hex);
            DeleteArray(hex);
          }
        } else {
          // The current character is a lone trailing surrogate. (If it had
          // been preceded by a leading surrogate, we would've ended up in the
          // other branch earlier on, and the current character would've been
          // handled as part of the surrogate pair already.)
          AppendCStringLiteral("\\u");
          char* const hex = DoubleToRadixCString(c, 16);
          AppendCString(hex);
          DeleteArray(hex);
        }
      } else {
        AppendCString(&JsonEscapeTable[c * kJsonEscapeTableEntrySize]);
      }
    }
  }
  Append<uint8_t, DestChar>('"');
}

template <>
bool JsonStringifier::DoNotEscape(uint8_t c) {
  // https://tc39.github.io/ecma262/#table-json-single-character-escapes
  return (c >= 0x20 && c <= 0x21) || (c >= 0x23 && c <= 0x7E && c != 0x5C);
}

template <>
bool JsonStringifier::DoNotEscape(uint16_t c) {
  // https://tc39.github.io/ecma262/#table-json-single-character-escapes
  return (c >= 0x20 && c <= 0x21) ||
         (c >= 0x23 && c != 0x5C && c != 0x7F && (c < 0xD800 || c > 0xDFFF));
}

void JsonStringifier::NewLine() {
  if (gap_ == nullptr) return;
  NewLineOutline();
}

void JsonStringifier::NewLineOutline() {
  AppendCharacter('\n');
  for (int i = 0; i < indent_; i++) AppendCString(gap_);
}

void JsonStringifier::Separator(bool first) {
  if (!first) AppendCharacter(',');
  NewLine();
}

void JsonStringifier::SerializeDeferredKey(bool deferred_comma,
                                           Handle<Object> deferred_key) {
  Separator(!deferred_comma);
  SerializeString(Handle<String>::cast(deferred_key));
  AppendCharacter(':');
  if (gap_ != nullptr) AppendCharacter(' ');
}

void JsonStringifier::SerializeString(Handle<String> object) {
  object = String::Flatten(isolate_, object);
  if (encoding_ == String::ONE_BYTE_ENCODING) {
    if (String::IsOneByteRepresentationUnderneath(*object)) {
      SerializeString_<uint8_t, uint8_t>(object);
    } else {
      ChangeEncoding();
      SerializeString(object);
    }
  } else {
    if (String::IsOneByteRepresentationUnderneath(*object)) {
      SerializeString_<uint8_t, base::uc16>(object);
    } else {
      SerializeString_<base::uc16, base::uc16>(object);
    }
  }
}

void JsonStringifier::Extend() {
  if (part_length_ >= String::kMaxLength) overflowed_ = true;
  part_length_ *= kPartLengthGrowthFactor;
  if (encoding_ == String::ONE_BYTE_ENCODING) {
    uint8_t* tmp_ptr = new uint8_t[part_length_];
    memcpy(tmp_ptr, one_byte_ptr_, current_index_);
    if (one_byte_ptr_ != one_byte_array_) delete[] one_byte_ptr_;
    one_byte_ptr_ = tmp_ptr;
    part_ptr_ = one_byte_ptr_;
  } else {
    base::uc16* tmp_ptr = new base::uc16[part_length_];
    for (int i = 0; i < current_index_; i++) {
      tmp_ptr[i] = two_byte_ptr_[i];
    }
    delete[] two_byte_ptr_;
    two_byte_ptr_ = tmp_ptr;
    part_ptr_ = two_byte_ptr_;
  }
}

void JsonStringifier::ChangeEncoding() {
  encoding_ = String::TWO_BYTE_ENCODING;
  two_byte_ptr_ = new base::uc16[part_length_];
  for (int i = 0; i < current_index_; i++) {
    two_byte_ptr_[i] = one_byte_ptr_[i];
  }
  part_ptr_ = two_byte_ptr_;
  if (one_byte_ptr_ != one_byte_array_) delete[] one_byte_ptr_;
  one_byte_ptr_ = nullptr;
}

}  // namespace internal
}  // namespace v8
