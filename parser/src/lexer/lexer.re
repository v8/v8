#include <fcntl.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
TODO:
- SpiderMonkey compatibility hack: "    --> something" is treated as a single line comment.
- An identifier cannot start immediately after a number.

*/



enum Condition {
    EConditionNormal,
    EConditionDoubleQuoteString,
    EConditionSingleQuoteString,
    EConditionIdentifier,
    EConditionSingleLineComment,
    EConditionMultiLineComment,
    EConditionHtmlComment
};

#if defined(WIN32)

    typedef signed char     int8_t;
    typedef signed short    int16_t;
    typedef signed int      int32_t;

    typedef unsigned char   uint8_t;
    typedef unsigned short  uint16_t;
    typedef unsigned int    uint32_t;

#else

    #include <stdint.h>
    #include <unistd.h>

    #ifndef O_BINARY
        #define O_BINARY 0
    #endif

#endif

#include "lexer.h"
using namespace v8::internal;

// ----------------------------------------------------------------------
#define PUSH_TOKEN(T) { send(T); SKIP(); }
#define PUSH_LINE_TERMINATOR() { SKIP(); }
#define TERMINATE_ILLEGAL() { return 1; }

// ----------------------------------------------------------------------
class PushScanner
{

private:

    bool        eof;
    int32_t     state;
    int32_t     condition;

    uint8_t     *limit;
    uint8_t     *start;
    uint8_t     *cursor;
    uint8_t     *marker;
    int real_start;

    uint8_t     *buffer;
    uint8_t     *bufferEnd;

    uint8_t     yych;
    uint32_t    yyaccept;

    ExperimentalScanner* sink_;

public:

    // ----------------------------------------------------------------------
    PushScanner(ExperimentalScanner* sink)
    {
        limit = 0;
        start = 0;
        state = -1;
        condition = EConditionNormal;
        cursor = 0;
        marker = 0;
        buffer = 0;
        eof = false;
        bufferEnd = 0;
        sink_ = sink;
        real_start = 0;
    }

    // ----------------------------------------------------------------------
    ~PushScanner()
    {
    }

    // ----------------------------------------------------------------------
    void send(Token::Value token) {
        int beg = (start - buffer) + real_start;
        int end = (cursor - buffer) + real_start; 
        if (FLAG_trace_lexer) {
            printf("got %s at (%d, %d): ", Token::Name(token), beg, end);
            for (uint8_t* s = start; s != cursor; s++) printf("%c", (char)*s);
            printf(".\n");
        }
        sink_->Record(token, beg, end);
    }

    // ----------------------------------------------------------------------
    uint32_t push(const void *input, int input_size) {
        if (FLAG_trace_lexer) {
            printf(
                "scanner is receiving a new data batch of length %d\n"
                "scanner continues with saved state = %d\n",
                input_size,
                state
            );
        }

        /*
         * Data source is signaling end of file when batch size
         * is less than maxFill. This is slightly annoying because
         * maxFill is a value that can only be known after re2c does
         * its thing. Practically though, maxFill is never bigger than
         * the longest keyword, so given our grammar, 32 is a safe bet.
         */
        uint8_t null[64];
        const int maxFill = 32;
        if(input_size<maxFill) // FIXME: do something about this!!!
        {
            eof = true;
            input = null;
            input_size = sizeof(null);
            memset(null, 0, sizeof(null));
        }

        /*
         * When we get here, we have a partially
         * consumed buffer which is in the following state:
         *                                                                last valid char        last valid buffer spot
         *                                                                v                      v
         * +-------------------+-------------+---------------+-------------+----------------------+
         * ^                   ^             ^               ^             ^                      ^
         * buffer              start         marker          cursor        limit                  bufferEnd
         * 
         * We need to stretch the buffer and concatenate the new chunk of input to it
         *
         */
        size_t used = limit-buffer;
        size_t needed = used+input_size;
        size_t allocated = bufferEnd-buffer;
        if(allocated<needed)
        {
            size_t limitOffset = limit-buffer;
            size_t startOffset = start-buffer;
            size_t markerOffset = marker-buffer;
            size_t cursorOffset = cursor-buffer;

                buffer = (uint8_t*)realloc(buffer, needed);
                bufferEnd = needed+buffer;

            marker = markerOffset + buffer;
            cursor = cursorOffset + buffer;
            start = buffer + startOffset;
            limit = limitOffset + buffer;
        }
        memcpy(limit, input, input_size);
        limit += input_size;

        // The scanner starts here
        #define YYLIMIT         limit
        #define YYCURSOR        cursor
        #define YYMARKER        marker
        #define YYCTYPE         uint8_t

        #define SKIP()          { start = cursor; YYSETCONDITION(EConditionNormal); goto yy0; }
        #define YYFILL(n)       { goto fill;                }

        #define YYGETSTATE()    state
        #define YYSETSTATE(x)   { state = (x);  }

        #define YYGETCONDITION() condition
        #define YYSETCONDITION(x) { condition = (x);  }

    start:

        if (FLAG_trace_lexer) {
            printf("Starting a round; state: %d, condition: %d\n", state, condition);
        }

        /*!re2c
        re2c:indent:top      = 1;
        re2c:yych:conversion = 0;
        re2c:condenumprefix          = ECondition;
        re2c:define:YYCONDTYPE       = Condition;

        eof = "\000";
        any = [\000-\377];
        whitespace_char = [ \t\v\f\r];
        whitespace = whitespace_char+;
        identifier_start = [$_\\a-zA-z];
        identifier_char = [$_\\a-zA-z0-9];
        line_terminator = [\n\r]+;
        digit = [0-9];
        hex_digit = [0-9a-fA-F];
        maybe_exponent = ('e' [-+]? digit+)?;

        <Normal> "|="                    { PUSH_TOKEN(Token::ASSIGN_BIT_OR); }
        <Normal> "^="                    { PUSH_TOKEN(Token::ASSIGN_BIT_XOR); }
        <Normal> "&="                    { PUSH_TOKEN(Token::ASSIGN_BIT_AND); }
        <Normal> "+="                    { PUSH_TOKEN(Token::ASSIGN_ADD); }
        <Normal> "-="                    { PUSH_TOKEN(Token::ASSIGN_SUB); }
        <Normal> "*="                    { PUSH_TOKEN(Token::ASSIGN_MUL); }
        <Normal> "/="                    { PUSH_TOKEN(Token::ASSIGN_DIV); }
        <Normal> "%="                    { PUSH_TOKEN(Token::ASSIGN_MOD); }

        <Normal> "==="                   { PUSH_TOKEN(Token::EQ_STRICT); }
        <Normal> "=="                    { PUSH_TOKEN(Token::EQ); }
        <Normal> "="                     { PUSH_TOKEN(Token::ASSIGN); }
        <Normal> "!=="                   { PUSH_TOKEN(Token::NE_STRICT); }
        <Normal> "!="                    { PUSH_TOKEN(Token::NE); }
        <Normal> "!"                     { PUSH_TOKEN(Token::NOT); }

        <Normal> "//"                    :=> SingleLineComment
        <Normal> "/*"                    :=> MultiLineComment
        <Normal> "<!--"                  :=> HtmlComment

        <Normal> ">>>="                  { PUSH_TOKEN(Token::ASSIGN_SHR); }
        <Normal> "<<="                   { PUSH_TOKEN(Token::ASSIGN_SHL); }
        <Normal> ">>="                   { PUSH_TOKEN(Token::ASSIGN_SAR); }
        <Normal> "<="                    { PUSH_TOKEN(Token::LTE); }
        <Normal> ">="                    { PUSH_TOKEN(Token::GTE); }
        <Normal> "<<"                    { PUSH_TOKEN(Token::SHL); }
        <Normal> ">>"                    { PUSH_TOKEN(Token::SAR); }
        <Normal> "<"                     { PUSH_TOKEN(Token::LT); }
        <Normal> ">"                     { PUSH_TOKEN(Token::GT); }

        <Normal> '0x' hex_digit+         { PUSH_TOKEN(Token::NUMBER); }
        <Normal> "." digit+ maybe_exponent { PUSH_TOKEN(Token::NUMBER); }
        <Normal> digit+ ("." digit+)? maybe_exponent { PUSH_TOKEN(Token::NUMBER); }

        <Normal> "("                     { PUSH_TOKEN(Token::LPAREN); }
        <Normal> ")"                     { PUSH_TOKEN(Token::RPAREN); }
        <Normal> "["                     { PUSH_TOKEN(Token::LBRACK); }
        <Normal> "]"                     { PUSH_TOKEN(Token::RBRACK); }
        <Normal> "{"                     { PUSH_TOKEN(Token::LBRACE); }
        <Normal> "}"                     { PUSH_TOKEN(Token::RBRACE); }
        <Normal> ":"                     { PUSH_TOKEN(Token::COLON); }
        <Normal> ";"                     { PUSH_TOKEN(Token::SEMICOLON); }
        <Normal> "."                     { PUSH_TOKEN(Token::PERIOD); }
        <Normal> "?"                     { PUSH_TOKEN(Token::CONDITIONAL); }
        <Normal> "++"                    { PUSH_TOKEN(Token::INC); }
        <Normal> "--"                    { PUSH_TOKEN(Token::DEC); }

        <Normal> "||"                    { PUSH_TOKEN(Token::OR); }
        <Normal> "&&"                    { PUSH_TOKEN(Token::AND); }

        <Normal> "|"                     { PUSH_TOKEN(Token::BIT_OR); }
        <Normal> "^"                     { PUSH_TOKEN(Token::BIT_XOR); }
        <Normal> "&"                     { PUSH_TOKEN(Token::BIT_AND); }
        <Normal> "+"                     { PUSH_TOKEN(Token::ADD); }
        <Normal> "-"                     { PUSH_TOKEN(Token::SUB); }
        <Normal> "*"                     { PUSH_TOKEN(Token::MUL); }
        <Normal> "/"                     { PUSH_TOKEN(Token::DIV); }
        <Normal> "%"                     { PUSH_TOKEN(Token::MOD); }
        <Normal> "~"                     { PUSH_TOKEN(Token::BIT_NOT); }
        <Normal> ","                     { PUSH_TOKEN(Token::COMMA); }

        <Normal> line_terminator+        { PUSH_LINE_TERMINATOR(); }
        <Normal> whitespace              { SKIP(); }

        <Normal> ["]                     :=> DoubleQuoteString
        <Normal> [']                     :=> SingleQuoteString

        <Normal> identifier_start        :=> Identifier

        <Normal> eof                     { PUSH_TOKEN(Token::EOS); return 1; }
        <Normal> any                     { TERMINATE_ILLEGAL(); }

        <DoubleQuoteString> "\\\""       { goto yy0; }
        <DoubleQuoteString> '"'          { PUSH_TOKEN(Token::STRING);}
        <DoubleQuoteString> any          { goto yy0; }

        <SingleQuoteString> "\\'"        { goto yy0; }
        <SingleQuoteString> "'"          { PUSH_TOKEN(Token::STRING);}
        <SingleQuoteString> any          { goto yy0; }

        <Identifier> identifier_char+    { goto yy0; }
        <Identifier> any                 { cursor--; PUSH_TOKEN(Token::IDENTIFIER); }

        <SingleLineComment> line_terminator { PUSH_LINE_TERMINATOR();}
        <SingleLineComment> eof          { PUSH_LINE_TERMINATOR();}
        <SingleLineComment> any          { goto yy0; }

        <MultiLineComment> [*][//]       { PUSH_LINE_TERMINATOR();}
        <MultiLineComment> eof           { TERMINATE_ILLEGAL(); }
        <MultiLineComment> any           { goto yy0; }

        <HtmlComment> eof                { TERMINATE_ILLEGAL(); }
        <HtmlComment> "-->"              { PUSH_LINE_TERMINATOR();}
        <HtmlComment> any                { goto yy0; }
        */

    fill:
        int unfinishedSize = cursor-start;
        if (FLAG_trace_lexer) {
            printf(
                "scanner needs a refill. Exiting for now with:\n"
                "    saved fill state = %d\n"
                "    unfinished token size = %d\n",
                state,
                unfinishedSize
            );
            if(0 < unfinishedSize && start < limit) {
                printf("    unfinished token is: ");
                fwrite(start, 1, cursor-start, stdout);
                putchar('\n');
            }
            putchar('\n');
        }

        /*
         * Once we get here, we can get rid of
         * everything before start and after limit.
         */
        if (eof == true) goto start;
        if (buffer < start) {
            size_t start_offset = start - buffer;
            memmove(buffer, start, limit - start);
            marker -= start_offset;
            cursor -= start_offset;
            limit -= start_offset;
            start -= start_offset;
            real_start += start_offset;
        }
        return 0;
    }
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
      token_[current_] != Token::ILLEGAL) current_++;
  return res;
}


void ExperimentalScanner::Record(Token::Value token, int beg, int end) {
  if (token == Token::EOS) end--;
  token_[fetched_] = token;
  beg_[fetched_] = beg;
  end_[fetched_] = end;
  fetched_++;
}
