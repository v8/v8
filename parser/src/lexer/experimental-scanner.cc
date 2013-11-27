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

#include "experimental-scanner.h"

namespace v8 {
namespace internal {

std::set<ScannerBase*>* ScannerBase::scanners_ = NULL;

void ScannerBase::UpdateBuffersAfterGC(v8::Isolate*, GCType, GCCallbackFlags) {
  if (!scanners_) return;
  for (std::set<ScannerBase*>::const_iterator it = scanners_->begin();
       it != scanners_->end();
       ++it)
    (*it)->SetBufferBasedOnHandle();
}


template<>
const uint8_t* ExperimentalScanner<uint8_t>::GetNewBufferBasedOnHandle() const {
  String::FlatContent content = source_handle_->GetFlatContent();
  return content.ToOneByteVector().start();
}


template <>
const uint16_t* ExperimentalScanner<uint16_t>::GetNewBufferBasedOnHandle()
    const {
  String::FlatContent content = source_handle_->GetFlatContent();
  return content.ToUC16Vector().start();
}


template<>
const int8_t* ExperimentalScanner<int8_t>::GetNewBufferBasedOnHandle() const {
  String::FlatContent content = source_handle_->GetFlatContent();
  return reinterpret_cast<const int8_t*>(content.ToOneByteVector().start());
}


template<>
bool ExperimentalScanner<uint8_t>::FillLiteral(
    const TokenDesc& token, LiteralDesc* literal) {
  literal->beg_pos = token.beg_pos;
  const uint8_t* start = buffer_ + token.beg_pos;
  const uint8_t* end = buffer_ + token.end_pos;
  if (token.token == Token::STRING) {
    ++start;
    --end;
  }
  if (!token.has_escapes) {
    literal->is_ascii = true;
    literal->length = end - start;
    literal->ascii_string = Vector<const char>(
        reinterpret_cast<const char*>(start), literal->length);
    return true;
  }
  literal->buffer.Reset();
  for (const uint8_t* cursor = start; cursor != end;) {
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
  literal->is_ascii = literal->buffer.is_ascii();
  literal->length = literal->buffer.length();
  if (literal->is_ascii) {
    literal->ascii_string = literal->buffer.ascii_literal();
  } else {
    literal->utf16_string = literal->buffer.utf16_literal();
  }
  return true;
}


template<>
bool ExperimentalScanner<uint16_t>::FillLiteral(
    const TokenDesc& token, LiteralDesc* literal) {
  literal->beg_pos = token.beg_pos;
  const uint16_t* start = buffer_ + token.beg_pos;
  const uint16_t* end = buffer_ + token.end_pos;
  if (token.token == Token::STRING) {
    ++start;
    --end;
  }
  if (!token.has_escapes) {
    literal->is_ascii = false;  // FIXME: utf16 can contain only ascii chars.
    literal->length = end - start;
    literal->utf16_string = Vector<const uint16_t>(start, literal->length);
    return true;
  }
  literal->buffer.Reset();
  for (const uint16_t* cursor = start; cursor != end;) {
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
  literal->is_ascii = literal->buffer.is_ascii();
  literal->length = literal->buffer.length();
  if (literal->is_ascii) {
    literal->ascii_string = literal->buffer.ascii_literal();
  } else {
    literal->utf16_string = literal->buffer.utf16_literal();
  }
  return true;
}

template<>
bool ExperimentalScanner<int8_t>::FillLiteral(
    const TokenDesc& token, LiteralDesc* literal) {
  // FIXME: implement.
  return false;
}


}
}
