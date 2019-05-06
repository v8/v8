// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_JSON_PARSER_H_
#define V8_JSON_PARSER_H_

#include "src/heap/factory.h"
#include "src/isolate.h"
#include "src/objects.h"
#include "src/parsing/literal-buffer.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

enum ParseElementResult { kElementFound, kElementNotFound };

class JsonParseInternalizer {
 public:
  static MaybeHandle<Object> Internalize(Isolate* isolate,
                                         Handle<Object> object,
                                         Handle<Object> reviver);

 private:
  JsonParseInternalizer(Isolate* isolate, Handle<JSReceiver> reviver)
      : isolate_(isolate), reviver_(reviver) {}

  MaybeHandle<Object> InternalizeJsonProperty(Handle<JSReceiver> holder,
                                              Handle<String> key);

  bool RecurseAndApply(Handle<JSReceiver> holder, Handle<String> name);

  Isolate* isolate_;
  Handle<JSReceiver> reviver_;
};

enum class JsonToken : uint8_t {
  NUMBER,
  NEGATIVE_NUMBER,
  STRING,
  LBRACE,
  RBRACE,
  LBRACK,
  RBRACK,
  TRUE_LITERAL,
  FALSE_LITERAL,
  NULL_LITERAL,
  WHITESPACE,
  COLON,
  COMMA,
  ILLEGAL,
  EOS
};

// A simple json parser.
template <typename Char>
class JsonParser final {
 public:
  using SeqString = typename CharTraits<Char>::String;
  using SeqExternalString = typename CharTraits<Char>::ExternalString;

  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> Parse(
      Isolate* isolate, Handle<String> source, Handle<Object> reviver) {
    Handle<Object> result;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                               JsonParser(isolate, source).ParseJson(), Object);
    if (reviver->IsCallable()) {
      return JsonParseInternalizer::Internalize(isolate, result, reviver);
    }
    return result;
  }

  static const int kEndOfString = -1;

 private:
  template <typename LiteralChar>
  Handle<String> MakeString(bool requires_internalization,
                            const Vector<const LiteralChar>& chars);

  Handle<String> MakeString(bool requires_internalization, int offset,
                            int length);

  JsonParser(Isolate* isolate, Handle<String> source);
  ~JsonParser();

  // Parse a string containing a single JSON value.
  MaybeHandle<Object> ParseJson();

  void advance() { ++cursor_; }

  uc32 CurrentCharacter() {
    if (V8_UNLIKELY(is_at_end())) return kEndOfString;
    return *cursor_;
  }

  uc32 NextCharacter() {
    advance();
    return CurrentCharacter();
  }

  void AdvanceToNonDecimal();

  V8_INLINE JsonToken peek() const { return next_; }

  void Consume(JsonToken token) {
    DCHECK_EQ(peek(), token);
    advance();
  }

  void Expect(JsonToken token) {
    if (V8_LIKELY(peek() == token)) {
      advance();
    } else {
      ReportUnexpectedToken(peek());
    }
  }

  void ExpectNext(JsonToken token) {
    SkipWhitespace();
    Expect(token);
  }

  bool Check(JsonToken token) {
    SkipWhitespace();
    if (next_ != token) return false;
    advance();
    return true;
  }

  template <size_t N>
  void ScanLiteral(const char (&s)[N]) {
    DCHECK(!is_at_end());
    // There's at least 1 character, we always consume a character and compare
    // the next character. The first character was compared before we jumped
    // to ScanLiteral.
    STATIC_ASSERT(N > 2);
    size_t remaining = static_cast<size_t>(end_ - cursor_);
    if (V8_LIKELY(remaining >= N - 1 &&
                  CompareChars(s + 1, cursor_ + 1, N - 2) == 0)) {
      cursor_ += N - 1;
      return;
    }

    cursor_++;
    for (size_t i = 0; i < Min(N - 2, remaining - 1); i++) {
      if (*(s + 1 + i) != *cursor_) {
        ReportUnexpectedCharacter(*cursor_);
        return;
      }
      cursor_++;
    }

    DCHECK(is_at_end());
    ReportUnexpectedToken(JsonToken::EOS);
  }

  // The JSON lexical grammar is specified in the ECMAScript 5 standard,
  // section 15.12.1.1. The only allowed whitespace characters between tokens
  // are tab, carriage-return, newline and space.
  void SkipWhitespace();

  // A JSON string (production JSONString) is subset of valid JavaScript string
  // literals. The string must only be double-quoted (not single-quoted), and
  // the only allowed backslash-escapes are ", /, \, b, f, n, r, t and
  // four-digit hex escapes (uXXXX). Any other use of backslashes is invalid.
  Handle<String> ParseJsonString(bool requires_internalization,
                                 Handle<String> expected = Handle<String>());

  Handle<String> ScanJsonString();

  // A JSON number (production JSONNumber) is a subset of the valid JavaScript
  // decimal number literals.
  // It includes an optional minus sign, must have at least one
  // digit before and after a decimal point, may not have prefixed zeros (unless
  // the integer part is zero), and may include an exponent part (e.g., "e-10").
  // Hexadecimal and octal numbers are not allowed.
  Handle<Object> ParseJsonNumber(int sign, const Char* start);

  // Parse a single JSON value from input (grammar production JSONValue).
  // A JSON value is either a (double-quoted) string literal, a number literal,
  // one of "true", "false", or "null", or an object or array literal.
  Handle<Object> ParseJsonValue();

  // Parse a JSON object literal (grammar production JSONObject).
  // An object literal is a squiggly-braced and comma separated sequence
  // (possibly empty) of key/value pairs, where the key is a JSON string
  // literal, the value is a JSON value, and the two are separated by a colon.
  // A JSON array doesn't allow numbers and identifiers as keys, like a
  // JavaScript array.
  Handle<Object> ParseJsonObject();

  // Helper for ParseJsonObject. Parses the form "123": obj, which is recorded
  // as an element, not a property. Returns false if we should retry parsing the
  // key as a non-element. (Returns true if it's an index or hits EOS).
  bool ParseElement(Handle<JSObject> json_object);

  // Parses a JSON array literal (grammar production JSONArray). An array
  // literal is a square-bracketed and comma separated sequence (possibly empty)
  // of JSON values.
  // A JSON array doesn't allow leaving out values from the sequence, nor does
  // it allow a terminal comma, like a JavaScript array does.
  Handle<Object> ParseJsonArray();

  // Mark that a parsing error has happened at the current character.
  void ReportUnexpectedCharacter(uc32 c);
  // Mark that a parsing error has happened at the current token.
  void ReportUnexpectedToken(JsonToken token);

  inline Isolate* isolate() { return isolate_; }
  inline Factory* factory() { return isolate_->factory(); }
  inline Handle<JSFunction> object_constructor() { return object_constructor_; }

  static const int kInitialSpecialStringLength = 32;
  static const int kPretenureTreshold = 100 * 1024;

  static void UpdatePointersCallback(v8::Isolate* v8_isolate, v8::GCType type,
                                     v8::GCCallbackFlags flags, void* parser) {
    reinterpret_cast<JsonParser<Char>*>(parser)->UpdatePointers();
  }

  void UpdatePointers() {
    DisallowHeapAllocation no_gc;
    const Char* chars = Handle<SeqString>::cast(source_)->GetChars(no_gc);
    if (chars_ != chars) {
      size_t position = cursor_ - chars_;
      size_t length = end_ - chars_;
      chars_ = chars;
      cursor_ = chars_ + position;
      end_ = chars_ + length;
    }
  }

 private:
  static const bool kIsOneByte = sizeof(Char) == 1;
  static const int kMaxInternalizedStringValueLength = 25;

  // Casts |c| to uc32 avoiding LiteralBuffer::AddChar(char) in one-byte-strings
  // with escapes that can result in two-byte strings.
  void AddLiteralChar(uc32 c) { literal_buffer_.AddChar(c); }

  void CommitStateToJsonObject(Handle<JSObject> json_object, Handle<Map> map,
                               const Vector<const Handle<Object>>& properties);

  bool is_at_end() const {
    DCHECK_LE(cursor_, end_);
    return cursor_ == end_;
  }

  int position() const { return static_cast<int>(cursor_ - chars_); }

  Isolate* isolate_;
  Zone zone_;
  const uint64_t hash_seed_;
  AllocationType allocation_;
  Handle<JSFunction> object_constructor_;
  const Handle<String> original_source_;
  Handle<String> source_;

  // Cached pointer to the raw chars in source. In case source is on-heap, we
  // register an UpdatePointers callback. For this reason, chars_, cursor_ and
  // end_ should never be locally cached across a possible allocation. The scope
  // in which we cache chars has to be guarded by a DisallowHeapAllocation
  // scope.
  const Char* chars_;
  const Char* cursor_;
  const Char* end_;

  JsonToken next_;
  LiteralBuffer literal_buffer_;
  // Indicates whether the bytes underneath source_ can relocate during GC.
  bool chars_may_relocate_;

  // Property handles are stored here inside ParseJsonObject.
  ZoneVector<Handle<Object>> properties_;
};

// Explicit instantiation declarations.
extern template class JsonParser<uint8_t>;
extern template class JsonParser<uint16_t>;

}  // namespace internal
}  // namespace v8

#endif  // V8_JSON_PARSER_H_
