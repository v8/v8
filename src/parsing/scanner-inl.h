// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_SCANNER_INL_H_
#define V8_PARSING_SCANNER_INL_H_

#include "src/parsing/scanner.h"
#include "src/unicode-cache-inl.h"

namespace v8 {
namespace internal {

V8_INLINE Token::Value Scanner::SkipWhiteSpace() {
  int start_position = source_pos();

  SkipWhiteSpaceImpl();

  // If there is an HTML comment end '-->' at the beginning of a
  // line (with only whitespace in front of it), we treat the rest
  // of the line as a comment. This is in line with the way
  // SpiderMonkey handles it.
  if (c0_ != '-' || !has_line_terminator_before_next_) {
    // Return whether or not we skipped any characters.
    if (source_pos() == start_position) {
      return Token::ILLEGAL;
    }

    return Token::WHITESPACE;
  }

  return TryToSkipHTMLCommentAndWhiteSpaces(start_position);
}

V8_INLINE void Scanner::SkipWhiteSpaceImpl() {
  while (true) {
    // We won't skip behind the end of input.
    DCHECK(!unicode_cache_->IsWhiteSpace(kEndOfInput));

    // Advance as long as character is a WhiteSpace or LineTerminator.
    // Remember if the latter is the case.
    if (unibrow::IsLineTerminator(c0_)) {
      has_line_terminator_before_next_ = true;
    } else if (!unicode_cache_->IsWhiteSpace(c0_)) {
      break;
    }
    Advance();
  }
}
}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_SCANNER_INL_H_
