#include <fcntl.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// TODO:
// - SpiderMonkey compatibility hack: "  --> something" is treated
//   as a single line comment.
// - An identifier cannot start immediately after a number.

enum Condition {
  kConditionNormal,
  kConditionDoubleQuoteString,
  kConditionSingleQuoteString,
  kConditionIdentifier,
  kConditionSingleLineComment,
  kConditionMultiLineComment,
  kConditionHtmlComment
};

#if defined(WIN32)

  typedef signed char   int8_t;
  typedef signed short  int16_t;
  typedef signed int   int32_t;

  typedef unsigned char  uint8_t;
  typedef unsigned short uint16_t;
  typedef unsigned int  uint32_t;

#else

#include <stdint.h>
#include <unistd.h>

#ifndef O_BINARY
  #define O_BINARY 0
#endif

#endif //  defined(WIN32)

#include "lexer.h"

using namespace v8::internal;

#define PUSH_TOKEN(T) { send(T); SKIP(); }
#define PUSH_LINE_TERMINATOR() { SKIP(); }
#define TERMINATE_ILLEGAL() { return 1; }

class PushScanner {

public:
  PushScanner(ExperimentalScanner* sink):
      eof_(false),
      state_(-1),
      condition_(kConditionNormal),
      limit_(NULL),
      start_(NULL),
      cursor_(NULL),
      marker_(NULL),
      real_start_(0),
      buffer_(NULL),
      buffer_end_(NULL),
      yych(0),
      yyaccept(0),
      sink_(sink) {
  }

  ~PushScanner() {
  }

  void send(Token::Value token) {
    int beg = (start_ - buffer_) + real_start_;
    int end = (cursor_ - buffer_) + real_start_;
    if (FLAG_trace_lexer) {
      printf("got %s at (%d, %d): ", Token::Name(token), beg, end);
      for (uint8_t* s = start_; s != cursor_; s++) printf("%c", (char)*s);
      printf(".\n");
    }
    sink_->Record(token, beg, end);
  }

  uint32_t push(const void *input, int input_size) {
    if (FLAG_trace_lexer) {
      printf(
        "scanner is receiving a new data batch of length %d\n"
        "scanner continues with saved state_ = %d\n",
        input_size,
        state_
      );
    }

    //  Data source is signaling end of file when batch size
    //  is less than max_fill. This is slightly annoying because
    //  max_fill is a value that can only be known after re2c does
    //  its thing. Practically though, max_fill is never bigger than
    //  the longest keyword, so given our grammar, 32 is a safe bet.

    uint8_t null[64];
    const int max_fill = 32;
    if (input_size < max_fill) { // FIXME: do something about this!!!
      eof_ = true;
      input = null;
      input_size = sizeof(null);
      memset(null, 0, sizeof(null));
    }


    //  When we get here, we have a partially
    //  consumed buffer_ which is in the following state_:
    //                                 last valid char    last valid buffer_ spot
    //                                 v           v
    //  +-------------------+-------------+---------------+-------------+----------------------+
    //  ^          ^       ^        ^       ^           ^
    //  buffer_       start_     marker_     cursor_    limit_         buffer_end_
    //
    //  We need to stretch the buffer_ and concatenate the new chunk of input to it

    size_t used = limit_ - buffer_;
    size_t needed = used + input_size;
    size_t allocated = buffer_end_ - buffer_;
    if(allocated < needed) {
      size_t limit__offset = limit_ - buffer_;
      size_t start__offset = start_ - buffer_;
      size_t marker__offset = marker_ - buffer_;
      size_t cursor__offset = cursor_ - buffer_;

      buffer_ = (uint8_t*)realloc(buffer_, needed);
      buffer_end_ = needed + buffer_;

      marker_ = marker__offset + buffer_;
      cursor_ = cursor__offset + buffer_;
      start_ = buffer_ + start__offset;
      limit_ = limit__offset + buffer_;
    }
    memcpy(limit_, input, input_size);
    limit_ += input_size;

    // The scanner start_s here
    #define YYLIMIT     limit_
    #define YYCURSOR    cursor_
    #define YYMARKER    marker_
    #define YYCTYPE     uint8_t

    #define SKIP()     { start_ = cursor_; YYSETCONDITION(kConditionNormal); goto yy0; }
    #define YYFILL(n)    { goto fill;        }

    #define YYGETSTATE()  state_
    #define YYSETSTATE(x)  { state_ = (x); }

    #define YYGETCONDITION() condition_
    #define YYSETCONDITION(x) { condition_ = (x); }

 start_:
    if (FLAG_trace_lexer) {
      printf("Starting a round; state_: %d, condition_: %d\n", state_, condition_);
    }

    /*!re2c
    re2c:indent:top   = 1;
    re2c:yych:conversion = 0;
    re2c:condenumprefix     = kCondition;
    re2c:define:YYCONDTYPE    = Condition;

    eof = "\000";
    any = [\000-\377];
    whitespace_char = [ \t\v\f\r];
    whitespace = whitespace_char+;
    identifier_start_ = [$_\\a-zA-z];
    identifier_char = [$_\\a-zA-z0-9];
    line_terminator = [\n\r]+;
    digit = [0-9];
    hex_digit = [0-9a-fA-F];
    maybe_exponent = ('e' [-+]? digit+)?;

    <Normal> "|="          { PUSH_TOKEN(Token::ASSIGN_BIT_OR); }
    <Normal> "^="          { PUSH_TOKEN(Token::ASSIGN_BIT_XOR); }
    <Normal> "&="          { PUSH_TOKEN(Token::ASSIGN_BIT_AND); }
    <Normal> "+="          { PUSH_TOKEN(Token::ASSIGN_ADD); }
    <Normal> "-="          { PUSH_TOKEN(Token::ASSIGN_SUB); }
    <Normal> "*="          { PUSH_TOKEN(Token::ASSIGN_MUL); }
    <Normal> "/="          { PUSH_TOKEN(Token::ASSIGN_DIV); }
    <Normal> "%="          { PUSH_TOKEN(Token::ASSIGN_MOD); }

    <Normal> "==="         { PUSH_TOKEN(Token::EQ_STRICT); }
    <Normal> "=="          { PUSH_TOKEN(Token::EQ); }
    <Normal> "="           { PUSH_TOKEN(Token::ASSIGN); }
    <Normal> "!=="         { PUSH_TOKEN(Token::NE_STRICT); }
    <Normal> "!="          { PUSH_TOKEN(Token::NE); }
    <Normal> "!"           { PUSH_TOKEN(Token::NOT); }

    <Normal> "//"          :=> SingleLineComment
    <Normal> "/*"          :=> MultiLineComment
    <Normal> "<!--"        :=> HtmlComment

    <Normal> ">>>="        { PUSH_TOKEN(Token::ASSIGN_SHR); }
    <Normal> "<<="         { PUSH_TOKEN(Token::ASSIGN_SHL); }
    <Normal> ">>="         { PUSH_TOKEN(Token::ASSIGN_SAR); }
    <Normal> "<="          { PUSH_TOKEN(Token::LTE); }
    <Normal> ">="          { PUSH_TOKEN(Token::GTE); }
    <Normal> "<<"          { PUSH_TOKEN(Token::SHL); }
    <Normal> ">>"          { PUSH_TOKEN(Token::SAR); }
    <Normal> "<"           { PUSH_TOKEN(Token::LT); }
    <Normal> ">"           { PUSH_TOKEN(Token::GT); }

    <Normal> '0x' hex_digit+                     { PUSH_TOKEN(Token::NUMBER); }
    <Normal> "." digit+ maybe_exponent           { PUSH_TOKEN(Token::NUMBER); }
    <Normal> digit+ ("." digit+)? maybe_exponent { PUSH_TOKEN(Token::NUMBER); }

    <Normal> "("           { PUSH_TOKEN(Token::LPAREN); }
    <Normal> ")"           { PUSH_TOKEN(Token::RPAREN); }
    <Normal> "["           { PUSH_TOKEN(Token::LBRACK); }
    <Normal> "]"           { PUSH_TOKEN(Token::RBRACK); }
    <Normal> "{"           { PUSH_TOKEN(Token::LBRACE); }
    <Normal> "}"           { PUSH_TOKEN(Token::RBRACE); }
    <Normal> ":"           { PUSH_TOKEN(Token::COLON); }
    <Normal> ";"           { PUSH_TOKEN(Token::SEMICOLON); }
    <Normal> "."           { PUSH_TOKEN(Token::PERIOD); }
    <Normal> "?"           { PUSH_TOKEN(Token::CONDITIONAL); }
    <Normal> "++"          { PUSH_TOKEN(Token::INC); }
    <Normal> "--"          { PUSH_TOKEN(Token::DEC); }

    <Normal> "||"          { PUSH_TOKEN(Token::OR); }
    <Normal> "&&"          { PUSH_TOKEN(Token::AND); }

    <Normal> "|"           { PUSH_TOKEN(Token::BIT_OR); }
    <Normal> "^"           { PUSH_TOKEN(Token::BIT_XOR); }
    <Normal> "&"           { PUSH_TOKEN(Token::BIT_AND); }
    <Normal> "+"           { PUSH_TOKEN(Token::ADD); }
    <Normal> "-"           { PUSH_TOKEN(Token::SUB); }
    <Normal> "*"           { PUSH_TOKEN(Token::MUL); }
    <Normal> "/"           { PUSH_TOKEN(Token::DIV); }
    <Normal> "%"           { PUSH_TOKEN(Token::MOD); }
    <Normal> "~"           { PUSH_TOKEN(Token::BIT_NOT); }
    <Normal> ","           { PUSH_TOKEN(Token::COMMA); }

    <Normal> line_terminator+ { PUSH_LINE_TERMINATOR(); }
    <Normal> whitespace       { SKIP(); }

    <Normal> ["]           :=> DoubleQuoteString
    <Normal> [']           :=> SingleQuoteString

    <Normal> identifier_start_    :=> Identifier

    <Normal> eof           { PUSH_TOKEN(Token::EOS); return 1; }
    <Normal> any           { TERMINATE_ILLEGAL(); }

    <DoubleQuoteString> "\\\""  { goto yy0; }
    <DoubleQuoteString> '"'     { PUSH_TOKEN(Token::STRING);}
    <DoubleQuoteString> any     { goto yy0; }

    <SingleQuoteString> "\\'"   { goto yy0; }
    <SingleQuoteString> "'"     { PUSH_TOKEN(Token::STRING);}
    <SingleQuoteString> any     { goto yy0; }

    <Identifier> identifier_char+  { goto yy0; }
    <Identifier> any               { cursor_--; PUSH_TOKEN(Token::IDENTIFIER); }

    <SingleLineComment> line_terminator { PUSH_LINE_TERMINATOR();}
    <SingleLineComment> eof             { PUSH_LINE_TERMINATOR();}
    <SingleLineComment> any             { goto yy0; }

    <MultiLineComment> [*][//]  { PUSH_LINE_TERMINATOR();}
    <MultiLineComment> eof      { TERMINATE_ILLEGAL(); }
    <MultiLineComment> any      { goto yy0; }

    <HtmlComment> eof        { TERMINATE_ILLEGAL(); }
    <HtmlComment> "-->"      { PUSH_LINE_TERMINATOR();}
    <HtmlComment> any        { goto yy0; }
    */

 fill:
    int unfinished_size = cursor_-start_;
    if (FLAG_trace_lexer) {
      printf(
        "scanner needs a refill. Exiting for now with:\n"
        "  saved fill state_ = %d\n"
        "  unfinished token size = %d\n",
        state_,
        unfinished_size
      );
      if(0 < unfinished_size && start_ < limit_) {
        printf("  unfinished token is: ");
        fwrite(start_, 1, cursor_-start_, stdout);
        putchar('\n');
      }
      putchar('\n');
    }

    if (eof_) goto start_;

    //  Once we get here, we can get rid of
    //  everything before start_ and after limit_.

    if (buffer_ < start_) {
      size_t start__offset = start_ - buffer_;
      memmove(buffer_, start_, limit_ - start_);
      marker_ -= start__offset;
      cursor_ -= start__offset;
      limit_ -= start__offset;
      start_ -= start__offset;
      real_start_ += start__offset;
    }
    return 0;
  }

 private:
  bool eof_;
  int32_t state_;
  int32_t condition_;

  uint8_t* limit_;
  uint8_t* start_;
  uint8_t* cursor_;
  uint8_t* marker_;
  int real_start_;

  uint8_t* buffer_;
  uint8_t* buffer_end_;

  uint8_t yych;
  uint32_t yyaccept;

  ExperimentalScanner* sink_;
};


ExperimentalScanner::ExperimentalScanner(const char* fname) :
    current_(0), fetched_(0) {
  file_ = fopen(fname, "rb");
  scanner_ = new PushScanner(this);
}


ExperimentalScanner::~ExperimentalScanner() {
  fclose(file_);
}


void ExperimentalScanner::FillTokens() {
  current_ = 0;
  fetched_ = 0;
  uint8_t chars[BUFFER_SIZE];
  int n = static_cast<int>(fread(&chars, 1, BUFFER_SIZE, file_));
  scanner_->push(chars, n);
}


Token::Value ExperimentalScanner::Next(int* beg_pos, int* end_pos) {
  if (current_ == fetched_) {
    FillTokens();
  }
  *beg_pos = beg_[current_];
  *end_pos = end_[current_];
  Token::Value res = token_[current_];
  if (token_[current_] != Token::Token::EOS &&
      token_[current_] != Token::ILLEGAL) {
    current_++;
  }
  return res;
}


void ExperimentalScanner::Record(Token::Value token, int beg, int end) {
  if (token == Token::EOS) end--;
  token_[fetched_] = token;
  beg_[fetched_] = beg;
  end_[fetched_] = end;
  fetched_++;
}
