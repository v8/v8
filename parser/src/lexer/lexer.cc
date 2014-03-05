// Copyright 2013 the V8 project authors. All rights reserved.
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

#include "v8.h"
#include "lexer.h"
#include "char-predicates-inl.h"
#include "scanner-character-streams.h"

namespace v8 {
namespace internal {


#ifdef V8_USE_GENERATED_LEXER


void Scanner::Initialize(Utf16CharacterStream* source) {
  delete lexer_;
  lexer_ = NULL;
  switch (source->stream_type_) {
    case Utf16CharacterStream::kUtf8ToUtf16:
      {
        Utf8ToUtf16CharacterStream* stream =
            static_cast<Utf8ToUtf16CharacterStream*>(source);
        lexer_ =
            new Lexer<int8_t>(unicode_cache_, stream->data_, stream->length_);
        break;
      }
    case Utf16CharacterStream::kGenericStringUtf16:
      {
        GenericStringUtf16CharacterStream* stream =
            static_cast<GenericStringUtf16CharacterStream*>(source);
        ASSERT(stream->data_->IsFlat());
        if (stream->data_->IsOneByteRepresentation()) {
          lexer_ = new Lexer<uint8_t>(unicode_cache_, stream->data_,
              stream->start_position_, stream->end_position_);
        } else {
          lexer_ = new Lexer<uint16_t>(unicode_cache_, stream->data_,
              stream->start_position_, stream->end_position_);
        }
      }
      break;
    case Utf16CharacterStream::kExternalTwoByteStringUtf16:
      {
        ExternalTwoByteStringUtf16CharacterStream* stream =
            static_cast<ExternalTwoByteStringUtf16CharacterStream*>(source);
        ASSERT(stream->data_->IsFlat());
        ASSERT(stream->data_->IsOneByteRepresentation());
        lexer_ = new Lexer<uint16_t>(unicode_cache_, stream->data_,
            stream->start_position_, stream->end_position_);
      }
      break;
  }
  ASSERT(lexer_ != NULL);
  SyncSettings();
  lexer_->Scan();
}


Scanner::Scanner(UnicodeCache* unicode_cache)
  : unicode_cache_(unicode_cache),
    lexer_(NULL),
    harmony_numeric_literals_(false),
    harmony_modules_(false),
    harmony_scoping_(false) {
}


void Scanner::SyncSettings() {
  if (lexer_ == NULL) return;
  lexer_->SetHarmonyModules(harmony_modules_);
  lexer_->SetHarmonyScoping(harmony_scoping_);
  lexer_->SetHarmonyNumericLiterals(harmony_numeric_literals_);
}


#endif


static void UpdateLexersAfterGC(v8::Isolate* isolate,
                                GCType,
                                GCCallbackFlags) {
  reinterpret_cast<i::Isolate*>(isolate)->
      lexer_gc_handler()->UpdateLexersAfterGC();
}


void LexerGCHandler::AddLexer(LexerBase* lexer) {
  if (lexers_.empty()) {
    isolate_->heap()->AddGCEpilogueCallback(
        &i::UpdateLexersAfterGC, kGCTypeAll, true);
  }
  std::pair<LexerSet::iterator, bool> res = lexers_.insert(lexer);
  USE(res);
  ASSERT(res.second);
}


void LexerGCHandler::RemoveLexer(LexerBase* lexer) {
  LexerSet::iterator it = lexers_.find(lexer);
  ASSERT(it != lexers_.end());
  lexers_.erase(it);
  if (lexers_.empty()) {
    isolate_->heap()->RemoveGCEpilogueCallback(&i::UpdateLexersAfterGC);
  }
}


void LexerGCHandler::UpdateLexersAfterGC() {
  typedef std::set<LexerBase*>::const_iterator It;
  for (It it = lexers_.begin(); it != lexers_.end(); ++it) {
    (*it)->UpdateBufferBasedOnHandle();
  }
}


LexerBase::LexerBase(UnicodeCache* unicode_cache)
    : unicode_cache_(unicode_cache),
      has_line_terminator_before_next_(true),
      has_multiline_comment_before_next_(false),
      current_literal_(&literals_[0]),
      next_literal_(&literals_[1]),
      harmony_numeric_literals_(false),
      harmony_modules_(false),
      harmony_scoping_(false) {
}


LexerBase::~LexerBase() {}


// Returns the next token and advances input.
Token::Value LexerBase::Next() {
  has_line_terminator_before_next_ = false;
  has_multiline_comment_before_next_ = false;
  current_ = next_;
  std::swap(current_literal_, next_literal_);
  Scan();
  return current_.token;
}


template<typename Char>
Lexer<Char>::Lexer(UnicodeCache* unicode_cache,
                   const Char* source_ptr,
                   int length)
    : LexerBase(unicode_cache),
      isolate_(NULL),
      source_ptr_(source_ptr),
      start_position_(0),
      end_position_(length),
      buffer_(NULL),
      buffer_end_(NULL),
      start_(NULL),
      cursor_(NULL),
      last_octal_end_(NULL) {
  CHECK(false);  // not yet supported
}


template<typename Char>
Lexer<Char>::Lexer(UnicodeCache* unicode_cache,
                   Handle<String> source,
                   int start_position,
                   int end_position)
    : LexerBase(unicode_cache),
      isolate_(source->GetIsolate()),
      source_handle_(FlattenGetString(source)),
      source_ptr_(NULL),
      start_position_(start_position),
      end_position_(end_position),
      buffer_(NULL),
      buffer_end_(NULL),
      start_(NULL),
      cursor_(NULL),
      last_octal_end_(NULL) {
  UpdateBufferBasedOnHandle();
  current_.beg_pos = current_.end_pos = next_.beg_pos = next_.end_pos = 0;
  isolate_->lexer_gc_handler()->AddLexer(this);
  // TODO(dcarney): move this to UpdateBufferBasedOnHandle
  cursor_ = buffer_ + start_position;
  buffer_end_ = buffer_ + end_position;
  start_ = cursor_;
}


template<typename Char>
Lexer<Char>::~Lexer() {
  if (!source_handle_.is_null()) {
    isolate_->lexer_gc_handler()->RemoveLexer(this);
  }
}


template<typename Char>
void Lexer<Char>::SeekForward(int pos) {
  cursor_ = buffer_ + pos;
  start_ = cursor_;
  has_line_terminator_before_next_ = false;
  has_multiline_comment_before_next_ = false;
  Scan();  // Fills in next_.
}


template<typename Char>
bool Lexer<Char>::ScanRegExpPattern(bool seen_equal) {
  // Scan: ('/' | '/=') RegularExpressionBody '/' RegularExpressionFlags
  bool in_character_class = false;

  // Previous token is either '/' or '/=', in the second case, the
  // pattern starts at =.
  next_.beg_pos = next_.end_pos = (cursor_ - buffer_) - (seen_equal ? 1 : 0);

  // Scan regular expression body: According to ECMA-262, 3rd, 7.8.5,
  // the scanner should pass uninterpreted bodies to the RegExp
  // constructor.
  if (cursor_ >= buffer_end_) return false;

  while (*cursor_ != '/' || in_character_class) {
    if (unicode_cache_->IsLineTerminator(*cursor_)) return false;
    if (*cursor_ == '\\') {  // Escape sequence.
      ++cursor_;
      if (cursor_ >= buffer_end_ || unicode_cache_->IsLineTerminator(*cursor_))
        return false;
      ++cursor_;
      if (cursor_ >= buffer_end_) return false;
      // If the escape allows more characters, i.e., \x??, \u????, or \c?,
      // only "safe" characters are allowed (letters, digits, underscore),
      // otherwise the escape isn't valid and the invalid character has
      // its normal meaning. I.e., we can just continue scanning without
      // worrying whether the following characters are part of the escape
      // or not, since any '/', '\\' or '[' is guaranteed to not be part
      // of the escape sequence.

      // TODO(896): At some point, parse RegExps more throughly to capture
      // octal esacpes in strict mode.
    } else {  // Unescaped character.
      if (*cursor_ == '[') in_character_class = true;
      if (*cursor_ == ']') in_character_class = false;
      if (++cursor_ >= buffer_end_) return false;
    }
  }
  next_.end_pos = (cursor_ - buffer_);
  ++cursor_;  // consume '/'
  return true;
}


template<typename Char>
bool Lexer<Char>::ScanRegExpFlags() {
  next_.beg_pos = cursor_ - buffer_;
  // Scan regular expression flags.
  while (cursor_ < buffer_end_ && unicode_cache_->IsIdentifierPart(*cursor_)) {
    if (*cursor_ != '\\') {
      if (++cursor_ >= buffer_end_) break;
    } else {
      if (!ScanLiteralUnicodeEscape()) break;
      if (++cursor_ >= buffer_end_) break;
    }
  }
  next_.end_pos = cursor_ - buffer_;
  return true;
}


template<typename Char>
uc32 Lexer<Char>::ScanHexNumber(int length) {
  // We have seen \uXXXX, let's see what it is.
  uc32 x = 0;
  for (const Char* s = cursor_ - length; s != cursor_; ++s) {
    int d = HexValue(*s);
    if (d < 0) {
      return -1;
    }
    x = x * 16 + d;
  }
  return x;
}


template<typename Char>
const Char* Lexer<Char>::ScanHexNumber(
    const Char* cursor, const Char* end, uc32* result) {
  uc32 x = 0;
  for ( ; cursor < end; ++cursor) {
    int d = HexValue(*cursor);
    if (d < 0) {
      *result = -1;
      return NULL;
    }
    x = x * 16 + d;
  }
  *result = x;
  return cursor;
}


// Octal escapes of the forms '\0xx' and '\xxx' are not a part of
// ECMA-262. Other JS VMs support them.
template<typename Char>
const Char* Lexer<Char>::ScanOctalEscape(
    const Char* start, const Char* end, uc32* result) {
  uc32 x = *result - '0';
  const Char* cursor;
  for (cursor = start; cursor < end; cursor++) {
    int d = *cursor - '0';
    if (d < 0 || d > 7) break;
    int nx = x * 8 + d;
    if (nx >= 256) break;
    x = nx;
  }
  *result = x;
  return cursor;
}


template<typename Char>
bool Lexer<Char>::ScanLiteralUnicodeEscape() {
  ASSERT(cursor_ < buffer_end_);
  Char primary_char = *(cursor_);
  ASSERT(primary_char == '\\');
  if (++cursor_ >= buffer_end_) return false;
  primary_char = *(cursor_);
  int i = 1;
  if (primary_char == 'u') {
    i++;
    while (i < 6) {
      if (++cursor_ >= buffer_end_) return false;
      primary_char = *(cursor_);
      if (!IsHexDigit(primary_char)) break;
      i++;
    }
  }
  return i == 6;
}


template<typename Char>
const Char* Lexer<Char>::ScanIdentifierUnicodeEscape(
    const Char* cursor, const Char* end, uc32* result) {
  ASSERT(*cursor == '\\');
  if (++cursor >= end) return NULL;
  if (*cursor != 'u') return NULL;
  ++cursor;
  if (cursor + 4 > end) return NULL;
  cursor = ScanHexNumber(cursor, cursor + 4, result);
  return cursor;
}


template<typename Char>
const Char* Lexer<Char>::ScanEscape(
    const Char* cursor, const Char* end, LiteralBuffer* literal) {
  ASSERT(*cursor == '\\');
  if (++cursor >= end) return NULL;
  uc32 c = *cursor;
  if (++cursor > end) return NULL;
  // Skip escaped newlines.
  if (unicode_cache_->IsLineTerminator(c)) {
    uc32 peek = *cursor;
    // Allow CR+LF newlines in multiline string literals.
    if (IsCarriageReturn(c) && IsLineFeed(peek)) cursor++;
    // Allow LF+CR newlines in multiline string literals.
    if (IsLineFeed(c) && IsCarriageReturn(peek)) cursor++;
    return cursor;
  }

  switch (c) {
    case '\'':  // fall through
    case '"' :  // fall through
    case '\\': break;
    case 'b' : c = '\b'; break;
    case 'f' : c = '\f'; break;
    case 'n' : c = '\n'; break;
    case 'r' : c = '\r'; break;
    case 't' : c = '\t'; break;
    case 'u' : {
      ASSERT(cursor + 4 <= end);
      cursor = ScanHexNumber(cursor, cursor + 4, &c);
      if (cursor == NULL) return NULL;
      break;
    }
    case 'v' : c = '\v'; break;
    case 'x' : {
      ASSERT(cursor + 2 <= end);
      cursor = ScanHexNumber(cursor, cursor + 2, &c);
      if (cursor == NULL) return NULL;
      break;
    }
    case '0' :  // fall through
    case '1' :  // fall through
    case '2' :  // fall through
    case '3' :  // fall through
    case '4' :  // fall through
    case '5' :  // fall through
    case '6' :  // fall through
    case '7' :
      if (end > cursor + 2) end = cursor + 2;
      cursor = ScanOctalEscape(cursor, end, &c); break;
  }

  // According to ECMA-262, section 7.8.4, characters not covered by the
  // above cases should be illegal, but they are commonly handled as
  // non-escaped characters by JS VMs.
  literal->AddChar(c);
  return cursor;
}


template<typename Char>
LexerBase::Location Lexer<Char>::octal_position() const {
  if (!last_octal_end_)
    return Location::invalid();
  // The last octal might be an octal escape or an octal number. Whichever it
  // is, we'll find the start by just scanning back until we hit a non-octal
  // character.
  const Char* temp_cursor = last_octal_end_ - 1;
  while (temp_cursor >= buffer_ && *temp_cursor >= '0' && *temp_cursor <= '7')
    --temp_cursor;
  return Location(temp_cursor - buffer_ + 1, last_octal_end_ - buffer_);
}


template<>
const uint8_t* Lexer<uint8_t>::GetNewBufferBasedOnHandle() const {
  String::FlatContent content = source_handle_->GetFlatContent();
  return content.ToOneByteVector().start();
}


template <>
const uint16_t* Lexer<uint16_t>::GetNewBufferBasedOnHandle()
    const {
  String::FlatContent content = source_handle_->GetFlatContent();
  return content.ToUC16Vector().start();
}


template<>
const int8_t* Lexer<int8_t>::GetNewBufferBasedOnHandle() const {
  String::FlatContent content = source_handle_->GetFlatContent();
  return reinterpret_cast<const int8_t*>(content.ToOneByteVector().start());
}


template<typename Char>
void Lexer<Char>::UpdateBufferBasedOnHandle() {
  // We get a raw pointer from the Handle, but we also update it every time
  // there is a GC, so it is safe.
  DisallowHeapAllocation no_gc;
  const Char* new_buffer = GetNewBufferBasedOnHandle();
  if (new_buffer != buffer_) {
    int start_offset = start_ - buffer_;
    int cursor_offset = cursor_ - buffer_;
    int last_octal_end_offset = last_octal_end_ - buffer_;
    buffer_ = new_buffer;
    buffer_end_ = buffer_ + source_handle_->length();
    start_ = buffer_ + start_offset;
    cursor_ = buffer_ + cursor_offset;
    if (last_octal_end_ != NULL) {
      last_octal_end_ = buffer_ + last_octal_end_offset;
    }
    ResetLiterals();
  }
}


template<>
bool Lexer<uint8_t>::IsSubstringOfSource(const TokenDesc& token) {
  return !token.has_escapes;
}


template<>
bool Lexer<uint16_t>::IsSubstringOfSource(
    const TokenDesc& token) {
  if (token.has_escapes) return false;
  const uint16_t* start = buffer_ + token.beg_pos;
  const uint16_t* end = buffer_ + token.end_pos;
  for (const uint16_t* cursor = start; cursor != end; ++cursor) {
    if (*cursor >= unibrow::Latin1::kMaxChar) return true;
  }
  return false;
}


template<>
bool Lexer<int8_t>::IsSubstringOfSource(const TokenDesc& token) {
  // FIXME: implement.
  UNREACHABLE();
  return false;
}


template<>
bool Lexer<uint8_t>::FillLiteral(
    const TokenDesc& token, LiteralDesc* literal) {
  literal->beg_pos = token.beg_pos;
  const uint8_t* start = buffer_ + token.beg_pos;
  const uint8_t* end = buffer_ + token.end_pos;
  if (token.token == Token::STRING) {
    ++start;
    --end;
  }
  if (IsSubstringOfSource(token)) {
    literal->is_one_byte = true;
    literal->is_in_buffer = false;
    literal->offset = start - buffer_;
    literal->length = end - start;
    literal->one_byte_string = Vector<const uint8_t>(start, literal->length);
    return true;
  }
  return CopyToLiteralBuffer(start, end, token, literal);
}


template<>
bool Lexer<uint16_t>::FillLiteral(
    const TokenDesc& token, LiteralDesc* literal) {
  literal->beg_pos = token.beg_pos;
  const uint16_t* start = buffer_ + token.beg_pos;
  const uint16_t* end = buffer_ + token.end_pos;
  if (token.token == Token::STRING) {
    ++start;
    --end;
  }
  if (IsSubstringOfSource(token)) {
    literal->is_one_byte = false;
    literal->is_in_buffer = false;
    literal->offset = start - buffer_;
    literal->length = end - start;
    literal->two_byte_string = Vector<const uint16_t>(start, literal->length);
    return true;
  }
  return CopyToLiteralBuffer(start, end, token, literal);
}


template<>
bool Lexer<int8_t>::FillLiteral(
    const TokenDesc& token, LiteralDesc* literal) {
  // FIXME: implement.
  UNREACHABLE();
  return false;
}


template<class Char>
bool Lexer<Char>::CopyToLiteralBuffer(const Char* start,
                                                    const Char* end,
                                                    const TokenDesc& token,
                                                    LiteralDesc* literal) {
  literal->buffer.Reset();
  if (token.has_escapes) {
    for (const Char* cursor = start; cursor != end;) {
      if (*cursor != '\\') {
        literal->buffer.AddChar(*cursor++);
      } else if (token.token == Token::IDENTIFIER) {
        uc32 c;
        cursor = ScanIdentifierUnicodeEscape(cursor, end, &c);
        ASSERT(cursor != NULL);
        if (cursor == NULL) return false;
        literal->buffer.AddChar(c);
      } else {
        cursor = ScanEscape(cursor, end, &literal->buffer);
        ASSERT(cursor != NULL);
        if (cursor == NULL) return false;
      }
    }
  } else {
    for (const Char* cursor = start; cursor != end;) {
        literal->buffer.AddChar(*cursor++);
    }
  }
  literal->is_one_byte = literal->buffer.is_ascii();
  literal->is_in_buffer = true;
  literal->length = literal->buffer.length();
  if (literal->is_one_byte) {
    literal->one_byte_string =
        Vector<const uint8_t>::cast(literal->buffer.ascii_literal());
  } else {
    literal->two_byte_string = literal->buffer.utf16_literal();
  }
  return true;
}


template<class Char>
Handle<String> Lexer<Char>::InternalizeLiteral(
    LiteralDesc* literal) {
  Factory* factory = isolate_->factory();
  if (literal->is_in_buffer) {
    return literal->is_one_byte
        ? factory->InternalizeOneByteString(
            Vector<const uint8_t>::cast(literal->one_byte_string))
        : factory->InternalizeTwoByteString(literal->two_byte_string);
  }
  if (sizeof(Char) == 1) {
    SubStringKey<uint8_t> key(
        source_handle_, literal->offset, literal->length);
    return factory->InternalizeStringWithKey(&key);
  } else {
    SubStringKey<uint16_t> key(
        source_handle_, literal->offset, literal->length);
    return factory->InternalizeStringWithKey(&key);
  }
}


template<>
Handle<String> Lexer<uint8_t>::AllocateLiteral(
    LiteralDesc* literal, PretenureFlag pretenured) {
  Factory* factory = isolate_->factory();
  if (literal->is_in_buffer) {
    return literal->is_one_byte
        ? factory->NewStringFromOneByte(literal->one_byte_string, pretenured)
        : factory->NewStringFromTwoByte(literal->two_byte_string, pretenured);
  }
  int from = literal->offset;
  int length = literal->length;
  // Save the offset and the length before allocating the string as it may
  // cause a GC, invalidate the literal, and move the source.
  Handle<String> result = factory->NewRawOneByteString(length, pretenured);
  uint8_t* chars = SeqOneByteString::cast(*result)->GetChars();
  String::WriteToFlat(*source_handle_, chars, from, from + length);
  return result;
}


template<>
Handle<String> Lexer<uint16_t>::AllocateLiteral(
    LiteralDesc* literal, PretenureFlag pretenured) {
  Factory* factory = isolate_->factory();
  if (literal->is_in_buffer) {
    return literal->is_one_byte
        ? factory->NewStringFromOneByte(literal->one_byte_string, pretenured)
        : factory->NewStringFromTwoByte(literal->two_byte_string, pretenured);
  }
  // Save the offset and the length before allocating the string as it may
  // cause a GC, invalidate the literal, and move the source.
  int from = literal->offset;
  int length = literal->length;
  Handle<String> result = factory->NewRawTwoByteString(length, pretenured);
  uint16_t* chars = SeqTwoByteString::cast(*result)->GetChars();
  String::WriteToFlat(*source_handle_, chars, from, from + length);
  return result;
}


template<>
Handle<String> Lexer<int8_t>::AllocateLiteral(
    LiteralDesc* literal, PretenureFlag pretenured) {
  // FIXME: implement
  UNREACHABLE();
  return Handle<String>();
}

template class Lexer<uint8_t>;
template class Lexer<uint16_t>;
template class Lexer<int8_t>;

} }  // v8::internal
