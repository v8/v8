// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/json-parser.h"

#include "src/char-predicates-inl.h"
#include "src/conversions.h"
#include "src/debug/debug.h"
#include "src/field-type.h"
#include "src/hash-seed-inl.h"
#include "src/heap/heap-inl.h"  // For string_table().
#include "src/message-template.h"
#include "src/objects-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/property-descriptor.h"
#include "src/string-hasher.h"
#include "src/transitions-inl.h"

namespace v8 {
namespace internal {

namespace {

// A vector-like data structure that uses a larger vector for allocation, and
// provides limited utility access. The original vector must not be used for the
// duration, and it may even be reallocated. This allows vector storage to be
// reused for the properties of sibling objects.
template <typename Container>
class VectorSegment {
 public:
  using value_type = typename Container::value_type;

  explicit VectorSegment(Container* container)
      : container_(*container), begin_(container->size()) {}
  ~VectorSegment() { container_.resize(begin_); }

  Vector<const value_type> GetVector() const {
    return VectorOf(container_) + begin_;
  }

  template <typename T>
  void push_back(T&& value) {
    container_.push_back(std::forward<T>(value));
  }

 private:
  Container& container_;
  const typename Container::size_type begin_;
};

constexpr JsonToken GetOneCharJsonToken(uint8_t c) {
  // clang-format off
  return
     c == '"' ? JsonToken::STRING :
     IsDecimalDigit(c) ?  JsonToken::NUMBER :
     c == '-' ? JsonToken::NEGATIVE_NUMBER :
     c == '[' ? JsonToken::LBRACK :
     c == '{' ? JsonToken::LBRACE :
     c == ']' ? JsonToken::RBRACK :
     c == '}' ? JsonToken::RBRACE :
     c == 't' ? JsonToken::TRUE_LITERAL :
     c == 'f' ? JsonToken::FALSE_LITERAL :
     c == 'n' ? JsonToken::NULL_LITERAL :
     c == ' ' ? JsonToken::WHITESPACE :
     c == '\t' ? JsonToken::WHITESPACE :
     c == '\r' ? JsonToken::WHITESPACE :
     c == '\n' ? JsonToken::WHITESPACE :
     c == ':' ? JsonToken::COLON :
     c == ',' ? JsonToken::COMMA :
     JsonToken::ILLEGAL;
  // clang-format on
}

// Table of one-character tokens, by character (0x00..0xFF only).
static const constexpr JsonToken one_char_json_tokens[256] = {
#define CALL_GET_SCAN_FLAGS(N) GetOneCharJsonToken(N),
    INT_0_TO_127_LIST(CALL_GET_SCAN_FLAGS)
#undef CALL_GET_SCAN_FLAGS
#define CALL_GET_SCAN_FLAGS(N) GetOneCharJsonToken(128 + N),
        INT_0_TO_127_LIST(CALL_GET_SCAN_FLAGS)
#undef CALL_GET_SCAN_FLAGS
};

enum class EscapeKind : uint8_t {
  kIllegal,
  kSelf,
  kBackspace,
  kTab,
  kNewLine,
  kFormFeed,
  kCarriageReturn,
  kUnicode
};

using EscapeKindField = BitField8<EscapeKind, 0, 3>;
using MayTerminateStringField = BitField8<bool, EscapeKindField::kNext, 1>;
using NumberPartField = BitField8<bool, MayTerminateStringField::kNext, 1>;

constexpr bool MayTerminateJsonString(uint8_t flags) {
  return MayTerminateStringField::decode(flags);
}

constexpr EscapeKind GetEscapeKind(uint8_t flags) {
  return EscapeKindField::decode(flags);
}

constexpr bool IsNumberPart(uint8_t flags) {
  return NumberPartField::decode(flags);
}

constexpr uint8_t GetJsonScanFlags(uint8_t c) {
  // clang-format off
  return (c == 'b' ? EscapeKindField::encode(EscapeKind::kBackspace)
          : c == 't' ? EscapeKindField::encode(EscapeKind::kTab)
          : c == 'n' ? EscapeKindField::encode(EscapeKind::kNewLine)
          : c == 'f' ? EscapeKindField::encode(EscapeKind::kFormFeed)
          : c == 'r' ? EscapeKindField::encode(EscapeKind::kCarriageReturn)
          : c == 'u' ? EscapeKindField::encode(EscapeKind::kUnicode)
          : c == '"' ? EscapeKindField::encode(EscapeKind::kSelf)
          : c == '\\' ? EscapeKindField::encode(EscapeKind::kSelf)
          : c == '/' ? EscapeKindField::encode(EscapeKind::kSelf)
          : EscapeKindField::encode(EscapeKind::kIllegal)) |
         (c < 0x20 ? MayTerminateStringField::encode(true)
          : c == '"' ? MayTerminateStringField::encode(true)
          : c == '\\' ? MayTerminateStringField::encode(true)
          : MayTerminateStringField::encode(false)) |
         NumberPartField::encode(c == '.' ||
                                 c == 'e' ||
                                 c == 'E' ||
                                 IsDecimalDigit(c) ||
                                 c == '-' ||
                                 c == '+');
  // clang-format on
}

// Table of one-character scan flags, by character (0x00..0xFF only).
static const constexpr uint8_t character_json_scan_flags[256] = {
#define CALL_GET_SCAN_FLAGS(N) GetJsonScanFlags(N),
    INT_0_TO_127_LIST(CALL_GET_SCAN_FLAGS)
#undef CALL_GET_SCAN_FLAGS
#define CALL_GET_SCAN_FLAGS(N) GetJsonScanFlags(128 + N),
        INT_0_TO_127_LIST(CALL_GET_SCAN_FLAGS)
#undef CALL_GET_SCAN_FLAGS
};

}  // namespace

MaybeHandle<Object> JsonParseInternalizer::Internalize(Isolate* isolate,
                                                       Handle<Object> object,
                                                       Handle<Object> reviver) {
  DCHECK(reviver->IsCallable());
  JsonParseInternalizer internalizer(isolate,
                                     Handle<JSReceiver>::cast(reviver));
  Handle<JSObject> holder =
      isolate->factory()->NewJSObject(isolate->object_function());
  Handle<String> name = isolate->factory()->empty_string();
  JSObject::AddProperty(isolate, holder, name, object, NONE);
  return internalizer.InternalizeJsonProperty(holder, name);
}

MaybeHandle<Object> JsonParseInternalizer::InternalizeJsonProperty(
    Handle<JSReceiver> holder, Handle<String> name) {
  HandleScope outer_scope(isolate_);
  Handle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate_, value, Object::GetPropertyOrElement(isolate_, holder, name),
      Object);
  if (value->IsJSReceiver()) {
    Handle<JSReceiver> object = Handle<JSReceiver>::cast(value);
    Maybe<bool> is_array = Object::IsArray(object);
    if (is_array.IsNothing()) return MaybeHandle<Object>();
    if (is_array.FromJust()) {
      Handle<Object> length_object;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate_, length_object,
          Object::GetLengthFromArrayLike(isolate_, object), Object);
      double length = length_object->Number();
      for (double i = 0; i < length; i++) {
        HandleScope inner_scope(isolate_);
        Handle<Object> index = isolate_->factory()->NewNumber(i);
        Handle<String> name = isolate_->factory()->NumberToString(index);
        if (!RecurseAndApply(object, name)) return MaybeHandle<Object>();
      }
    } else {
      Handle<FixedArray> contents;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate_, contents,
          KeyAccumulator::GetKeys(object, KeyCollectionMode::kOwnOnly,
                                  ENUMERABLE_STRINGS,
                                  GetKeysConversion::kConvertToString),
          Object);
      for (int i = 0; i < contents->length(); i++) {
        HandleScope inner_scope(isolate_);
        Handle<String> name(String::cast(contents->get(i)), isolate_);
        if (!RecurseAndApply(object, name)) return MaybeHandle<Object>();
      }
    }
  }
  Handle<Object> argv[] = {name, value};
  Handle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate_, result, Execution::Call(isolate_, reviver_, holder, 2, argv),
      Object);
  return outer_scope.CloseAndEscape(result);
}

bool JsonParseInternalizer::RecurseAndApply(Handle<JSReceiver> holder,
                                            Handle<String> name) {
  STACK_CHECK(isolate_, false);

  Handle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate_, result, InternalizeJsonProperty(holder, name), false);
  Maybe<bool> change_result = Nothing<bool>();
  if (result->IsUndefined(isolate_)) {
    change_result = JSReceiver::DeletePropertyOrElement(holder, name,
                                                        LanguageMode::kSloppy);
  } else {
    PropertyDescriptor desc;
    desc.set_value(result);
    desc.set_configurable(true);
    desc.set_enumerable(true);
    desc.set_writable(true);
    change_result = JSReceiver::DefineOwnProperty(isolate_, holder, name, &desc,
                                                  Just(kDontThrow));
  }
  MAYBE_RETURN(change_result, false);
  return true;
}

template <typename Char>
JsonParser<Char>::JsonParser(Isolate* isolate, Handle<String> source)
    : isolate_(isolate),
      zone_(isolate_->allocator(), ZONE_NAME),
      hash_seed_(HashSeed(isolate)),
      object_constructor_(isolate_->object_function()),
      original_source_(source),
      properties_(&zone_) {
  size_t start = 0;
  size_t length = source->length();
  if (source->IsSlicedString()) {
    SlicedString string = SlicedString::cast(*source);
    start = string.offset();
    String parent = string.parent();
    if (parent.IsThinString()) parent = ThinString::cast(parent).actual();
    source_ = handle(parent, isolate);
  } else {
    source_ = String::Flatten(isolate, source);
  }

  if (StringShape(*source_).IsExternal()) {
    chars_ =
        static_cast<const Char*>(SeqExternalString::cast(*source_)->GetChars());
    chars_may_relocate_ = false;
  } else {
    DisallowHeapAllocation no_gc;
    isolate->heap()->AddGCEpilogueCallback(UpdatePointersCallback,
                                           v8::kGCTypeAll, this);
    chars_ = SeqString::cast(*source_)->GetChars(no_gc);
    chars_may_relocate_ = true;
  }
  cursor_ = chars_ + start;
  end_ = cursor_ + length;
}

template <typename Char>
void JsonParser<Char>::ReportUnexpectedToken(JsonToken token) {
  // Some exception (for example stack overflow) is already pending.
  if (isolate_->has_pending_exception()) return;

  // Parse failed. Current character is the unexpected token.
  Factory* factory = this->factory();
  MessageTemplate message;
  Handle<Object> arg1 = Handle<Smi>(Smi::FromInt(position()), isolate());
  Handle<Object> arg2;

  switch (token) {
    case JsonToken::EOS:
      message = MessageTemplate::kJsonParseUnexpectedEOS;
      break;
    case JsonToken::NUMBER:
    case JsonToken::NEGATIVE_NUMBER:
      message = MessageTemplate::kJsonParseUnexpectedTokenNumber;
      break;
    case JsonToken::STRING:
      message = MessageTemplate::kJsonParseUnexpectedTokenString;
      break;
    default:
      message = MessageTemplate::kJsonParseUnexpectedToken;
      arg2 = arg1;
      arg1 = factory->LookupSingleCharacterStringFromCode(*cursor_);
      break;
  }

  Handle<Script> script(factory->NewScript(original_source_));
  if (isolate()->NeedsSourcePositionsForProfiling()) {
    Script::InitLineEnds(script);
  }
  // We should sent compile error event because we compile JSON object in
  // separated source file.
  isolate()->debug()->OnCompileError(script);
  MessageLocation location(script, position(), position() + 1);
  Handle<Object> error = factory->NewSyntaxError(message, arg1, arg2);
  isolate()->Throw(*error, &location);

  // Move the cursor to the end so we won't be able to proceed parsing.
  cursor_ = end_;
}

template <typename Char>
void JsonParser<Char>::ReportUnexpectedCharacter(uc32 c) {
  JsonToken token = JsonToken::ILLEGAL;
  if (c == kEndOfString) {
    token = JsonToken::EOS;
  } else if (c <= unibrow::Latin1::kMaxChar) {
    token = one_char_json_tokens[c];
  }
  return ReportUnexpectedToken(token);
}

template <typename Char>
JsonParser<Char>::~JsonParser() {
  if (StringShape(*source_).IsExternal()) {
    // Check that the string shape hasn't changed. Otherwise our GC hooks are
    // broken.
    SeqExternalString::cast(*source_);
  } else {
    // Check that the string shape hasn't changed. Otherwise our GC hooks are
    // broken.
    SeqString::cast(*source_);
    isolate()->heap()->RemoveGCEpilogueCallback(UpdatePointersCallback, this);
  }
}

template <typename Char>
MaybeHandle<Object> JsonParser<Char>::ParseJson() {
  Handle<Object> result = ParseJsonValue();
  if (!Check(JsonToken::EOS)) ReportUnexpectedToken(peek());
  if (isolate_->has_pending_exception()) return MaybeHandle<Object>();
  return result;
}

MaybeHandle<Object> InternalizeJsonProperty(Handle<JSObject> holder,
                                            Handle<String> key);

template <typename Char>
void JsonParser<Char>::SkipWhitespace() {
  next_ = JsonToken::EOS;

  cursor_ = std::find_if(cursor_, end_, [this](Char c) {
    JsonToken current = V8_LIKELY(c <= unibrow::Latin1::kMaxChar)
                            ? one_char_json_tokens[c]
                            : JsonToken::ILLEGAL;
    bool result = current != JsonToken::WHITESPACE;
    if (result) next_ = current;
    return result;
  });
}

// Parse any JSON value.
template <typename Char>
Handle<Object> JsonParser<Char>::ParseJsonValue() {
  StackLimitCheck stack_check(isolate_);

  if (V8_UNLIKELY(stack_check.InterruptRequested())) {
    if (stack_check.HasOverflowed()) {
      if (!isolate_->has_pending_exception()) isolate_->StackOverflow();
      return factory()->undefined_value();
    }

    if (isolate_->stack_guard()->HandleInterrupts()->IsException(isolate_)) {
      return factory()->undefined_value();
    }
  }

  SkipWhitespace();

  switch (peek()) {
    case JsonToken::STRING:
      Consume(JsonToken::STRING);
      return ParseJsonString(false);
    case JsonToken::NUMBER:
      return ParseJsonNumber(1, cursor_);
    case JsonToken::NEGATIVE_NUMBER:
      advance();
      if (is_at_end()) {
        ReportUnexpectedToken(JsonToken::EOS);
        return handle(Smi::FromInt(0), isolate_);
      }
      return ParseJsonNumber(-1, cursor_ - 1);
    case JsonToken::LBRACE:
      return ParseJsonObject();
    case JsonToken::LBRACK:
      return ParseJsonArray();
    case JsonToken::TRUE_LITERAL:
      ScanLiteral("true");
      return factory()->true_value();
    case JsonToken::FALSE_LITERAL:
      ScanLiteral("false");
      return factory()->false_value();
    case JsonToken::NULL_LITERAL:
      ScanLiteral("null");
      return factory()->null_value();

    case JsonToken::COLON:
    case JsonToken::COMMA:
    case JsonToken::ILLEGAL:
    case JsonToken::RBRACE:
    case JsonToken::RBRACK:
    case JsonToken::EOS:
      ReportUnexpectedCharacter(CurrentCharacter());
      return factory()->undefined_value();

    case JsonToken::WHITESPACE:
      UNREACHABLE();
  }
}

template <typename Char>
bool JsonParser<Char>::ParseElement(Handle<JSObject> json_object) {
  uint32_t index = 0;
  {
    // |cursor_| will only be updated if the key ends up being an index.
    DisallowHeapAllocation no_gc;
    const Char* cursor = cursor_;
    // Maybe an array index, try to parse it.
    if (*cursor == '0') {
      // With a leading zero, the string has to be "0" only to be an index.
      cursor++;
    } else {
      cursor = std::find_if(cursor, end_, [&index](Char c) {
        return !TryAddIndexChar(&index, c);
      });
    }

    if (V8_UNLIKELY(cursor == end_) || *cursor != '"') return false;
    cursor_ = cursor + 1;
  }

  ExpectNext(JsonToken::COLON);
  Handle<Object> value = ParseJsonValue();
  JSObject::SetOwnElementIgnoreAttributes(json_object, index, value, NONE)
      .Assert();
  return true;
}

// Parse a JSON object. Position must be right at '{'.
template <typename Char>
Handle<Object> JsonParser<Char>::ParseJsonObject() {
  HandleScope scope(isolate());
  Handle<JSObject> json_object = factory()->NewJSObject(object_constructor());
  Handle<Map> map(json_object->map(), isolate());
  int descriptor = 0;
  VectorSegment<ZoneVector<Handle<Object>>> properties(&properties_);
  Consume(JsonToken::LBRACE);

  bool transitioning = true;

  if (!Check(JsonToken::RBRACE)) {
    do {
      ExpectNext(JsonToken::STRING);
      if (is_at_end() ||
          (IsDecimalDigit(*cursor_) && ParseElement(json_object))) {
        continue;
      }

      // Try to follow existing transitions as long as possible. Once we stop
      // transitioning, no transition can be found anymore.
      DCHECK(transitioning);
      Handle<Map> target;

      // First check whether there is a single expected transition. If so, try
      // to parse it first.
      Handle<String> expected;
      {
        DisallowHeapAllocation no_gc;
        TransitionsAccessor transitions(isolate(), *map, &no_gc);
        expected = transitions.ExpectedTransitionKey();
      }
      Handle<String> key = ParseJsonString(true, expected);
      // If the expected transition hits, follow it.
      if (key.is_identical_to(expected)) {
        DisallowHeapAllocation no_gc;
        target = TransitionsAccessor(isolate(), *map, &no_gc)
                     .ExpectedTransitionTarget();
      } else {
        // If a transition was found, follow it and continue.
        transitioning = TransitionsAccessor(isolate(), map)
                            .FindTransitionToField(key)
                            .ToHandle(&target);
      }

      ExpectNext(JsonToken::COLON);

      Handle<Object> value = ParseJsonValue();

      if (transitioning) {
        PropertyDetails details =
            target->instance_descriptors()->GetDetails(descriptor);
        Representation expected_representation = details.representation();

        if (value->FitsRepresentation(expected_representation)) {
          if (expected_representation.IsHeapObject() &&
              !target->instance_descriptors()
                   ->GetFieldType(descriptor)
                   ->NowContains(value)) {
            Handle<FieldType> value_type(
                value->OptimalType(isolate(), expected_representation));
            Map::GeneralizeField(isolate(), target, descriptor,
                                 details.constness(), expected_representation,
                                 value_type);
          }
          DCHECK(target->instance_descriptors()
                     ->GetFieldType(descriptor)
                     ->NowContains(value));
          properties.push_back(value);
          map = target;
          descriptor++;
          continue;
        } else {
          transitioning = false;
        }
      }

      DCHECK(!transitioning);

      // Commit the intermediate state to the object and stop transitioning.
      CommitStateToJsonObject(json_object, map, properties.GetVector());

      JSObject::DefinePropertyOrElementIgnoreAttributes(json_object, key, value)
          .Check();
    } while (transitioning && Check(JsonToken::COMMA));

    // If we transitioned until the very end, transition the map now.
    if (transitioning) {
      CommitStateToJsonObject(json_object, map, properties.GetVector());
    } else {
      while (Check(JsonToken::COMMA)) {
        HandleScope local_scope(isolate());
        ExpectNext(JsonToken::STRING);
        if (is_at_end() ||
            (IsDecimalDigit(*cursor_) && ParseElement(json_object))) {
          continue;
        }

        Handle<String> key = ParseJsonString(true);
        ExpectNext(JsonToken::COLON);
        Handle<Object> value = ParseJsonValue();

        JSObject::DefinePropertyOrElementIgnoreAttributes(json_object, key,
                                                          value)
            .Check();
      }
    }

    Expect(JsonToken::RBRACE);
  }
  return scope.CloseAndEscape(json_object);
}

template <typename Char>
void JsonParser<Char>::CommitStateToJsonObject(
    Handle<JSObject> json_object, Handle<Map> map,
    const Vector<const Handle<Object>>& properties) {
  JSObject::AllocateStorageForMap(json_object, map);
  DCHECK(!json_object->map()->is_dictionary_map());

  DisallowHeapAllocation no_gc;
  DescriptorArray descriptors = json_object->map()->instance_descriptors();
  for (int i = 0; i < properties.length(); i++) {
    Handle<Object> value = properties[i];
    // Initializing store.
    json_object->WriteToField(i, descriptors->GetDetails(i), *value);
  }
}

class ElementKindLattice {
 private:
  enum Kind {
    SMI_ELEMENTS = 0,
    NUMBER_ELEMENTS = 1,
    OBJECT_ELEMENTS = (1 << 1) | NUMBER_ELEMENTS,
  };

 public:
  ElementKindLattice() : value_(SMI_ELEMENTS) {}

  void Update(Handle<Object> o) {
    if (o->IsSmi()) {
      return;
    } else if (o->IsHeapNumber()) {
      value_ = static_cast<Kind>(value_ | NUMBER_ELEMENTS);
    } else {
      value_ = OBJECT_ELEMENTS;
    }
  }

  ElementsKind GetElementsKind() const {
    switch (value_) {
      case SMI_ELEMENTS:
        return PACKED_SMI_ELEMENTS;
      case NUMBER_ELEMENTS:
        return PACKED_DOUBLE_ELEMENTS;
      case OBJECT_ELEMENTS:
        return PACKED_ELEMENTS;
    }
  }

 private:
  Kind value_;
};

// Parse a JSON array. Position must be right at '['.
template <typename Char>
Handle<Object> JsonParser<Char>::ParseJsonArray() {
  HandleScope scope(isolate());
  ZoneVector<Handle<Object>> elements(&zone_);
  Consume(JsonToken::LBRACK);

  ElementKindLattice lattice;

  if (!Check(JsonToken::RBRACK)) {
    do {
      Handle<Object> element = ParseJsonValue();
      elements.push_back(element);
      lattice.Update(element);
    } while (Check(JsonToken::COMMA));
    Expect(JsonToken::RBRACK);
  }

  // Allocate a fixed array with all the elements.

  Handle<Object> json_array;
  const ElementsKind kind = lattice.GetElementsKind();
  int elements_size = static_cast<int>(elements.size());

  switch (kind) {
    case PACKED_ELEMENTS:
    case PACKED_SMI_ELEMENTS: {
      Handle<FixedArray> elems = factory()->NewFixedArray(elements_size);
      for (int i = 0; i < elements_size; i++) elems->set(i, *elements[i]);
      json_array = factory()->NewJSArrayWithElements(elems, kind);
      break;
    }
    case PACKED_DOUBLE_ELEMENTS: {
      Handle<FixedDoubleArray> elems = Handle<FixedDoubleArray>::cast(
          factory()->NewFixedDoubleArray(elements_size));
      for (int i = 0; i < elements_size; i++) {
        elems->set(i, elements[i]->Number());
      }
      json_array = factory()->NewJSArrayWithElements(elems, kind);
      break;
    }
    default:
      UNREACHABLE();
  }

  return scope.CloseAndEscape(json_array);
}

template <typename Char>
void JsonParser<Char>::AdvanceToNonDecimal() {
  cursor_ =
      std::find_if(cursor_, end_, [](Char c) { return !IsDecimalDigit(c); });
}

template <typename Char>
Handle<Object> JsonParser<Char>::ParseJsonNumber(int sign, const Char* start) {
  double number;

  {
    DisallowHeapAllocation no_gc;

    if (*cursor_ == '0') {
      // Prefix zero is only allowed if it's the only digit before
      // a decimal point or exponent.
      uc32 c = NextCharacter();
      if (IsInRange(c, 0, static_cast<int32_t>(unibrow::Latin1::kMaxChar)) &&
          IsNumberPart(character_json_scan_flags[c])) {
        if (V8_UNLIKELY(IsDecimalDigit(c))) {
          AllowHeapAllocation allow_before_exception;
          ReportUnexpectedToken(JsonToken::NUMBER);
          return handle(Smi::FromInt(0), isolate_);
        }
      } else if (sign > 0) {
        return handle(Smi::FromInt(0), isolate_);
      }
    } else {
      const Char* smi_start = cursor_;
      AdvanceToNonDecimal();
      if (V8_UNLIKELY(smi_start == cursor_)) {
        AllowHeapAllocation allow_before_exception;
        ReportUnexpectedCharacter(CurrentCharacter());
        return handle(Smi::FromInt(0), isolate_);
      }
      uc32 c = CurrentCharacter();
      STATIC_ASSERT(Smi::IsValid(-999999999));
      STATIC_ASSERT(Smi::IsValid(999999999));
      const int kMaxSmiLength = 9;
      if ((cursor_ - smi_start) <= kMaxSmiLength &&
          (!IsInRange(c, 0, static_cast<int32_t>(unibrow::Latin1::kMaxChar)) ||
           !IsNumberPart(character_json_scan_flags[c]))) {
        // Smi.
        int32_t i = 0;
        for (; smi_start != cursor_; smi_start++) {
          DCHECK(IsDecimalDigit(*smi_start));
          i = (i * 10) + ((*smi_start) - '0');
        }
        // TODO(verwaest): Cache?
        return handle(Smi::FromInt(i * sign), isolate_);
      }
    }

    if (CurrentCharacter() == '.') {
      uc32 c = NextCharacter();
      if (!IsDecimalDigit(c)) {
        AllowHeapAllocation allow_before_exception;
        ReportUnexpectedCharacter(c);
        return handle(Smi::FromInt(0), isolate_);
      }
      AdvanceToNonDecimal();
    }

    if (AsciiAlphaToLower(CurrentCharacter()) == 'e') {
      uc32 c = NextCharacter();
      if (c == '-' || c == '+') c = NextCharacter();
      if (!IsDecimalDigit(c)) {
        AllowHeapAllocation allow_before_exception;
        ReportUnexpectedCharacter(c);
        return handle(Smi::FromInt(0), isolate_);
      }
      AdvanceToNonDecimal();
    }

    Vector<const uint8_t> chars;
    if (kIsOneByte) {
      chars = Vector<const uint8_t>::cast(
          Vector<const Char>(start, cursor_ - start));
    } else {
      literal_buffer_.Start();
      do {
        literal_buffer_.AddChar(*start++);
      } while (start != cursor_);
      chars = literal_buffer_.one_byte_literal();
    }

    number = StringToDouble(chars,
                            NO_FLAGS,  // Hex, octal or trailing junk.
                            std::numeric_limits<double>::quiet_NaN());
    DCHECK(!std::isnan(number));
  }

  return factory()->NewNumber(number);
}

namespace {

template <typename Char>
bool Matches(const Vector<const Char>& chars, Handle<String> string) {
  if (string.is_null()) return false;

  // Only supports internalized strings in their canonical representation (one
  // byte encoded as two-byte will return false here).
  if ((sizeof(Char) == 1) != string->IsOneByteRepresentation()) return false;
  if (chars.length() != string->length()) return false;

  DisallowHeapAllocation no_gc;
  const Char* string_data = string->GetChars<Char>(no_gc);
  return CompareChars(chars.begin(), string_data, chars.length()) == 0;
}

}  // namespace

template <typename Char>
Handle<String> JsonParser<Char>::MakeString(bool requires_internalization,
                                            int offset, int length) {
  AllowHeapAllocation allow_gc;
  DCHECK(chars_may_relocate_);
  Handle<SeqOneByteString> source = Handle<SeqOneByteString>::cast(source_);

  if (!requires_internalization && length > kMaxInternalizedStringValueLength) {
    Handle<SeqOneByteString> result =
        factory()->NewRawOneByteString(length).ToHandleChecked();
    DisallowHeapAllocation no_gc;
    uint8_t* d = result->GetChars(no_gc);
    uint8_t* s = source->GetChars(no_gc) + offset;
    MemCopy(d, s, length);
    return result;
  }

  return factory()->InternalizeOneByteString(source, offset, length);
}

template <typename Char>
template <typename LiteralChar>
Handle<String> JsonParser<Char>::MakeString(
    bool requires_internalization, const Vector<const LiteralChar>& chars) {
  AllowHeapAllocation allow_gc;
  DCHECK_IMPLIES(
      chars_may_relocate_,
      chars.begin() == literal_buffer_.literal<LiteralChar>().begin());
  if (!requires_internalization &&
      chars.length() > kMaxInternalizedStringValueLength) {
    if (sizeof(LiteralChar) == 1) {
      return factory()
          ->NewStringFromOneByte(Vector<const uint8_t>::cast(chars))
          .ToHandleChecked();
    }
    return factory()
        ->NewStringFromTwoByte(Vector<const uint16_t>::cast(chars))
        .ToHandleChecked();
  }

  SequentialStringKey<LiteralChar> key(chars, hash_seed_);
  return StringTable::LookupKey(isolate_, &key);
}

template <typename Char>
Handle<String> JsonParser<Char>::ParseJsonString(bool requires_internalization,
                                                 Handle<String> hint) {
  // First try to fast scan without buffering in case the string doesn't have
  // escaped sequences. Always buffer two-byte input strings as the scanned
  // substring can be one-byte.
  if (kIsOneByte) {
    DisallowHeapAllocation no_gc;
    const Char* start = cursor_;

    while (true) {
      cursor_ = std::find_if(cursor_, end_, [](Char c) {
        return MayTerminateJsonString(character_json_scan_flags[c]);
      });

      if (V8_UNLIKELY(is_at_end())) break;

      if (*cursor_ == '"') {
        Handle<String> result;
        Vector<const Char> chars(start, cursor_ - start);
        if (Matches(chars, hint)) {
          result = hint;
        } else if (chars_may_relocate_) {
          result = MakeString(requires_internalization,
                              static_cast<int>(start - chars_),
                              static_cast<int>(cursor_ - start));
        } else {
          result = MakeString(requires_internalization,
                              Vector<const uint8_t>::cast(chars));
        }
        advance();
        return result;
      }

      if (*cursor_ == '\\') break;

      DCHECK_LT(*cursor_, 0x20);
      AllowHeapAllocation allow_before_exception;
      ReportUnexpectedCharacter(*cursor_);
      return factory()->empty_string();
    }

    // We hit an escape sequence. Start buffering.
    // TODO(verwaest): MemCopy.
    literal_buffer_.Start();
    while (start != cursor_) {
      literal_buffer_.AddChar(*start++);
    }
  } else {
    literal_buffer_.Start();
  }

  while (true) {
    cursor_ = std::find_if(cursor_, end_, [this](Char c) {
      if (V8_UNLIKELY(c > unibrow::Latin1::kMaxChar)) {
        AddLiteralChar(c);
        return false;
      }
      if (MayTerminateJsonString(character_json_scan_flags[c])) {
        return true;
      }
      AddLiteralChar(c);
      return false;
    });

    if (V8_UNLIKELY(is_at_end())) break;

    if (*cursor_ == '"') {
      Handle<String> result;
      if (literal_buffer_.is_one_byte()) {
        Vector<const uint8_t> chars = literal_buffer_.one_byte_literal();
        result = Matches(chars, hint)
                     ? hint
                     : MakeString(requires_internalization, chars);
      } else {
        Vector<const uint16_t> chars = literal_buffer_.two_byte_literal();
        result = Matches(chars, hint)
                     ? hint
                     : MakeString(requires_internalization, chars);
      }
      advance();
      return result;
    }

    if (*cursor_ == '\\') {
      uc32 c = NextCharacter();
      if (V8_UNLIKELY(!IsInRange(
              c, 0, static_cast<int32_t>(unibrow::Latin1::kMaxChar)))) {
        ReportUnexpectedCharacter(c);
        return factory()->empty_string();
      }

      uc32 value;

      switch (GetEscapeKind(character_json_scan_flags[c])) {
        case EscapeKind::kSelf:
          value = c;
          break;

        case EscapeKind::kBackspace:
          value = '\x08';
          break;

        case EscapeKind::kTab:
          value = '\x09';
          break;

        case EscapeKind::kNewLine:
          value = '\x0A';
          break;

        case EscapeKind::kFormFeed:
          value = '\x0C';
          break;

        case EscapeKind::kCarriageReturn:
          value = '\x0D';
          break;

        case EscapeKind::kUnicode: {
          value = 0;
          for (int i = 0; i < 4; i++) {
            int digit = HexValue(NextCharacter());
            if (V8_UNLIKELY(digit < 0)) {
              ReportUnexpectedCharacter(CurrentCharacter());
              return factory()->empty_string();
            }
            value = value * 16 + digit;
          }
          break;
        }

        case EscapeKind::kIllegal:
          ReportUnexpectedCharacter(c);
          return factory()->empty_string();
      }

      AddLiteralChar(value);
      advance();
      continue;
    }

    DCHECK_LT(*cursor_, 0x20);
    ReportUnexpectedCharacter(*cursor_);
    return factory()->empty_string();
  }

  ReportUnexpectedCharacter(kEndOfString);
  return factory()->empty_string();
}

// Explicit instantiation.
template class JsonParser<uint8_t>;
template class JsonParser<uint16_t>;

}  // namespace internal
}  // namespace v8
