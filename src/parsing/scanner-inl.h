// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_SCANNER_INL_H_
#define V8_PARSING_SCANNER_INL_H_

#include "src/char-predicates-inl.h"
#include "src/parsing/scanner.h"
#include "src/unicode-cache-inl.h"

namespace v8 {
namespace internal {

// Make sure tokens are stored as a single byte.
STATIC_ASSERT(sizeof(Token::Value) == 1);

// Table of one-character tokens, by character (0x00..0x7F only).
// clang-format off
static const Token::Value one_char_tokens[] = {
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::LPAREN,       // 0x28
  Token::RPAREN,       // 0x29
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::COMMA,        // 0x2C
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::COLON,        // 0x3A
  Token::SEMICOLON,    // 0x3B
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::CONDITIONAL,  // 0x3F
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::LBRACK,     // 0x5B
  Token::ILLEGAL,
  Token::RBRACK,     // 0x5D
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::ILLEGAL,
  Token::LBRACE,       // 0x7B
  Token::ILLEGAL,
  Token::RBRACE,       // 0x7D
  Token::BIT_NOT,      // 0x7E
  Token::ILLEGAL
};
// clang-format on

V8_INLINE Token::Value Scanner::SkipWhiteSpace() {
  // We won't skip behind the end of input.
  DCHECK(!unicode_cache_->IsWhiteSpaceOrLineTerminator(kEndOfInput));

  // Make sure we skip at least one character.
  if (!unicode_cache_->IsWhiteSpaceOrLineTerminator(c0_)) return Token::ILLEGAL;
  if (!next().after_line_terminator && unibrow::IsLineTerminator(c0_)) {
    next().after_line_terminator = true;
  }

  // Advance as long as character is a WhiteSpace or LineTerminator.
  AdvanceUntil([=](uc32 c0) {
    if (!unicode_cache_->IsWhiteSpaceOrLineTerminator(c0)) return true;
    if (!next().after_line_terminator && unibrow::IsLineTerminator(c0)) {
      next().after_line_terminator = true;
    }
    return false;
  });

  return Token::WHITESPACE;
}

V8_INLINE Token::Value Scanner::ScanSingleToken() {
  Token::Value token;
  do {
    next().location.beg_pos = source_pos();

    if (static_cast<unsigned>(c0_) <= 0x7F) {
      Token::Value token = one_char_tokens[c0_];
      if (token != Token::ILLEGAL) {
        Advance();
        return token;
      }
    }

    switch (c0_) {
      case '"':
      case '\'':
        return ScanString();

      case '<':
        // < <= << <<= <!--
        Advance();
        if (c0_ == '=') return Select(Token::LTE);
        if (c0_ == '<') return Select('=', Token::ASSIGN_SHL, Token::SHL);
        if (c0_ == '!') {
          token = ScanHtmlComment();
          continue;
        }
        return Token::LT;

      case '>':
        // > >= >> >>= >>> >>>=
        Advance();
        if (c0_ == '=') return Select(Token::GTE);
        if (c0_ == '>') {
          // >> >>= >>> >>>=
          Advance();
          if (c0_ == '=') return Select(Token::ASSIGN_SAR);
          if (c0_ == '>') return Select('=', Token::ASSIGN_SHR, Token::SHR);
          return Token::SAR;
        }
        return Token::GT;

      case '=':
        // = == === =>
        Advance();
        if (c0_ == '=') return Select('=', Token::EQ_STRICT, Token::EQ);
        if (c0_ == '>') return Select(Token::ARROW);
        return Token::ASSIGN;

      case '!':
        // ! != !==
        Advance();
        if (c0_ == '=') return Select('=', Token::NE_STRICT, Token::NE);
        return Token::NOT;

      case '+':
        // + ++ +=
        Advance();
        if (c0_ == '+') return Select(Token::INC);
        if (c0_ == '=') return Select(Token::ASSIGN_ADD);
        return Token::ADD;

      case '-':
        // - -- --> -=
        Advance();
        if (c0_ == '-') {
          Advance();
          if (c0_ == '>' && next().after_line_terminator) {
            // For compatibility with SpiderMonkey, we skip lines that
            // start with an HTML comment end '-->'.
            token = SkipSingleHTMLComment();
            continue;
          }
          return Token::DEC;
        }
        if (c0_ == '=') return Select(Token::ASSIGN_SUB);
        return Token::SUB;

      case '*':
        // * *=
        Advance();
        if (c0_ == '*') return Select('=', Token::ASSIGN_EXP, Token::EXP);
        if (c0_ == '=') return Select(Token::ASSIGN_MUL);
        return Token::MUL;

      case '%':
        // % %=
        return Select('=', Token::ASSIGN_MOD, Token::MOD);

      case '/':
        // /  // /* /=
        Advance();
        if (c0_ == '/') {
          uc32 c = Peek();
          if (c == '#' || c == '@') {
            Advance();
            Advance();
            token = SkipSourceURLComment();
            continue;
          }
          token = SkipSingleLineComment();
          continue;
        }
        if (c0_ == '*') {
          token = SkipMultiLineComment();
          continue;
        }
        if (c0_ == '=') return Select(Token::ASSIGN_DIV);
        return Token::DIV;

      case '&':
        // & && &=
        Advance();
        if (c0_ == '&') return Select(Token::AND);
        if (c0_ == '=') return Select(Token::ASSIGN_BIT_AND);
        return Token::BIT_AND;

      case '|':
        // | || |=
        Advance();
        if (c0_ == '|') return Select(Token::OR);
        if (c0_ == '=') return Select(Token::ASSIGN_BIT_OR);
        return Token::BIT_OR;

      case '^':
        // ^ ^=
        return Select('=', Token::ASSIGN_BIT_XOR, Token::BIT_XOR);

      case '.':
        // . Number
        Advance();
        if (IsDecimalDigit(c0_)) return ScanNumber(true);
        if (c0_ == '.') {
          if (Peek() == '.') {
            Advance();
            Advance();
            return Token::ELLIPSIS;
          }
        }
        return Token::PERIOD;

      case '`':
        Advance();
        return ScanTemplateSpan();

      case '#':
        return ScanPrivateName();

      default:
        if (unicode_cache_->IsIdentifierStart(c0_) ||
            (CombineSurrogatePair() &&
             unicode_cache_->IsIdentifierStart(c0_))) {
          Token::Value token = ScanIdentifierOrKeyword();
          if (!Token::IsContextualKeyword(token)) return token;

          next().contextual_token = token;
          return Token::IDENTIFIER;
        }
        if (IsDecimalDigit(c0_)) return ScanNumber(false);
        if (c0_ == kEndOfInput) return Token::EOS;
        token = SkipWhiteSpace();
        continue;
    }
    // Continue scanning for tokens as long as we're just skipping whitespace.
  } while (token == Token::WHITESPACE);

  return token;
}

void Scanner::Scan() {
  next().literal_chars.Drop();
  next().raw_literal_chars.Drop();
  next().contextual_token = Token::UNINITIALIZED;
  next().invalid_template_escape_message = MessageTemplate::kNone;

  next().token = ScanSingleToken();
  next().location.end_pos = source_pos();

#ifdef DEBUG
  SanityCheckTokenDesc(current());
  SanityCheckTokenDesc(next());
  SanityCheckTokenDesc(next_next());
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_SCANNER_INL_H_
