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


/*!types:re2c */

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

// ----------------------------------------------------------------------
#define PUSH_EOS(T) { printf("got eos\n"); }
#define PUSH_TOKEN(T) { \
        printf("got token %s (%d)\n", tokenNames[T], T); \
         SKIP(); }
#define PUSH_STRING() { \
        printf("got string\n"); \
        size_t tokenSize = cursor-start; \
        fwrite(start, tokenSize, 1, stdout); \
        printf("\n"); \
        SKIP(); }
#define PUSH_NUMBER() { \
        printf("got number\n"); \
        size_t tokenSize = cursor-start; \
        fwrite(start, tokenSize, 1, stdout); \
        printf("\n"); \
        SKIP(); }
#define PUSH_IDENTIFIER() { \
        --cursor; \
        printf("got identifier: "); \
        size_t tokenSize = cursor-start; \
        fwrite(start, tokenSize, 1, stdout); \
        printf("\n"); \
        SKIP(); }
#define PUSH_LINE_TERMINATOR() { printf("got line terminator\n"); SKIP();}
#define TERMINATE_ILLEGAL() { return 1; }

#define TOKENS \
        TOK(EOS) \
        TOK(LPAREN) \
        TOK(RPAREN) \
        TOK(LBRACK) \
        TOK(RBRACK) \
        TOK(LBRACE) \
        TOK(RBRACE) \
        TOK(COLON) \
        TOK(SEMICOLON) \
        TOK(PERIOD) \
        TOK(CONDITIONAL) \
        TOK(INC) \
        TOK(DEC) \
        TOK(ASSIGN) \
        TOK(ASSIGN_BIT_OR) \
        TOK(ASSIGN_BIT_XOR) \
        TOK(ASSIGN_BIT_AND) \
        TOK(ASSIGN_SHL) \
        TOK(ASSIGN_SAR) \
        TOK(ASSIGN_SHR) \
        TOK(ASSIGN_ADD) \
        TOK(ASSIGN_SUB) \
        TOK(ASSIGN_MUL) \
        TOK(ASSIGN_DIV) \
        TOK(ASSIGN_MOD) \
        TOK(COMMA) \
        TOK(OR) \
        TOK(AND) \
        TOK(BIT_OR) \
        TOK(BIT_XOR) \
        TOK(BIT_AND) \
        TOK(SHL) \
        TOK(SAR) \
        TOK(ADD) \
        TOK(SUB) \
        TOK(MUL) \
        TOK(DIV) \
        TOK(MOD) \
        TOK(EQ) \
        TOK(NE) \
        TOK(EQ_STRICT) \
        TOK(NE_STRICT) \
        TOK(LT) \
        TOK(GT) \
        TOK(LTE) \
        TOK(GTE) \
        TOK(NOT) \
        TOK(BIT_NOT) \

// ----------------------------------------------------------------------
static const char *tokenNames[] =
{
    #define TOK(x) #x,
        TOKENS
    #undef TOK
};

// ----------------------------------------------------------------------
class PushScanner
{
public:

    enum Token
    {
        #define TOK(x) x,
            TOKENS
        #undef TOK
    };

private:

    bool        eof;
    int32_t     state;
    int32_t     condition;

    uint8_t     *limit;
    uint8_t     *start;
    uint8_t     *cursor;
    uint8_t     *marker;

    uint8_t     *buffer;
    uint8_t     *bufferEnd;

    uint8_t     yych;
    uint32_t    yyaccept;

public:

    // ----------------------------------------------------------------------
    PushScanner()
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
    }

    // ----------------------------------------------------------------------
    ~PushScanner()
    {
    }

    // ----------------------------------------------------------------------
    void send(
        Token token
    )
    {
        size_t tokenSize = cursor-start;
        const char *tokenName = tokenNames[token];
        printf(
            "scanner is pushing out a token of type %d (%s)",
            token,
            tokenName
        );

        if(token==EOS) putchar('\n');
        else
        {
            size_t tokenNameSize = strlen(tokenNames[token]);
            size_t padSize = 20-(20<tokenNameSize ? 20 : tokenNameSize);
            for(size_t i=0; i<padSize; ++i) putchar(' ');
            printf(" : ---->");

            fwrite(
                start,
                tokenSize,
                1,
                stdout
            );

            printf("<----\n");
        }
    }

    // ----------------------------------------------------------------------
    uint32_t push(
        const void  *input,
        ssize_t     inputSize
    )
    {
        printf(
            "scanner is receiving a new data batch of length %ld\n"
            "scanner continues with saved state = %d\n",
            inputSize,
            state
        );

        /*
         * Data source is signaling end of file when batch size
         * is less than maxFill. This is slightly annoying because
         * maxFill is a value that can only be known after re2c does
         * its thing. Practically though, maxFill is never bigger than
         * the longest keyword, so given our grammar, 32 is a safe bet.
         */
        uint8_t null[64];
        const ssize_t maxFill = 32;
        if(inputSize<maxFill) // FIXME: do something about this!!!
        {
            eof = true;
            input = null;
            inputSize = sizeof(null);
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
        size_t needed = used+inputSize;
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
        memcpy(limit, input, inputSize);
        limit += inputSize;

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

        printf("Starting a round; state: %d, condition: %d\n", state, condition);

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

        <Normal> "|="                    { PUSH_TOKEN(ASSIGN_BIT_OR); }
        <Normal> "^="                    { PUSH_TOKEN(ASSIGN_BIT_XOR); }
        <Normal> "&="                    { PUSH_TOKEN(ASSIGN_BIT_AND); }
        <Normal> "+="                    { PUSH_TOKEN(ASSIGN_ADD); }
        <Normal> "-="                    { PUSH_TOKEN(ASSIGN_SUB); }
        <Normal> "*="                    { PUSH_TOKEN(ASSIGN_MUL); }
        <Normal> "/="                    { PUSH_TOKEN(ASSIGN_DIV); }
        <Normal> "%="                    { PUSH_TOKEN(ASSIGN_MOD); }

        <Normal> "==="                   { PUSH_TOKEN(EQ_STRICT); }
        <Normal> "=="                    { PUSH_TOKEN(EQ); }
        <Normal> "="                     { PUSH_TOKEN(ASSIGN); }
        <Normal> "!=="                   { PUSH_TOKEN(NE_STRICT); }
        <Normal> "!="                    { PUSH_TOKEN(NE); }
        <Normal> "!"                     { PUSH_TOKEN(NOT); }

        <Normal> "//"                    :=> SingleLineComment
        <Normal> "/*"                    :=> MultiLineComment
        <Normal> "<!--"                  :=> HtmlComment

        <Normal> ">>>="                  { PUSH_TOKEN(ASSIGN_SHR); }
        <Normal> "<<="                   { PUSH_TOKEN(ASSIGN_SHL); }
        <Normal> ">>="                   { PUSH_TOKEN(ASSIGN_SAR); }
        <Normal> "<="                    { PUSH_TOKEN(LTE); }
        <Normal> ">="                    { PUSH_TOKEN(GTE); }
        <Normal> "<<"                    { PUSH_TOKEN(SHL); }
        <Normal> ">>"                    { PUSH_TOKEN(SAR); }
        <Normal> "<"                     { PUSH_TOKEN(LT); }
        <Normal> ">"                     { PUSH_TOKEN(GT); }

        <Normal> '0x' hex_digit+         { PUSH_NUMBER(); }
        <Normal> "." digit+ maybe_exponent { PUSH_NUMBER(); }
        <Normal> digit+ ("." digit+)? maybe_exponent { PUSH_NUMBER(); }

        <Normal> "("                     { PUSH_TOKEN(LPAREN); }
        <Normal> ")"                     { PUSH_TOKEN(RPAREN); }
        <Normal> "["                     { PUSH_TOKEN(LBRACK); }
        <Normal> "]"                     { PUSH_TOKEN(RBRACK); }
        <Normal> "{"                     { PUSH_TOKEN(LBRACE); }
        <Normal> "}"                     { PUSH_TOKEN(RBRACE); }
        <Normal> ":"                     { PUSH_TOKEN(COLON); }
        <Normal> ";"                     { PUSH_TOKEN(SEMICOLON); }
        <Normal> "."                     { PUSH_TOKEN(PERIOD); }
        <Normal> "?"                     { PUSH_TOKEN(CONDITIONAL); }
        <Normal> "++"                    { PUSH_TOKEN(INC); }
        <Normal> "--"                    { PUSH_TOKEN(DEC); }

        <Normal> "||"                    { PUSH_TOKEN(OR); }
        <Normal> "&&"                    { PUSH_TOKEN(AND); }

        <Normal> "|"                     { PUSH_TOKEN(BIT_OR); }
        <Normal> "^"                     { PUSH_TOKEN(BIT_XOR); }
        <Normal> "&"                     { PUSH_TOKEN(BIT_AND); }
        <Normal> "+"                     { PUSH_TOKEN(ADD); }
        <Normal> "-"                     { PUSH_TOKEN(SUB); }
        <Normal> "*"                     { PUSH_TOKEN(MUL); }
        <Normal> "/"                     { PUSH_TOKEN(DIV); }
        <Normal> "%"                     { PUSH_TOKEN(MOD); }
        <Normal> "~"                     { PUSH_TOKEN(BIT_NOT); }
        <Normal> ","                     { PUSH_TOKEN(COMMA); }

        <Normal> line_terminator+        { PUSH_LINE_TERMINATOR(); }
        <Normal> whitespace              { SKIP(); }

        <Normal> ["]                     :=> DoubleQuoteString
        <Normal> [']                     :=> SingleQuoteString

        <Normal> identifier_start        :=> Identifier

        <Normal> eof                     { PUSH_EOS(); return 1; }
        <Normal> any                     { TERMINATE_ILLEGAL(); }

        <DoubleQuoteString> "\\\""       { goto yy0; }
        <DoubleQuoteString> '"'          { PUSH_STRING();}
        <DoubleQuoteString> any          { goto yy0; }

        <SingleQuoteString> "\\'"        { goto yy0; }
        <SingleQuoteString> "'"          { PUSH_STRING();}
        <SingleQuoteString> any          { goto yy0; }

        <Identifier> identifier_char+    { goto yy0; }
        <Identifier> any                 { PUSH_IDENTIFIER(); }

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
        ssize_t unfinishedSize = cursor-start;
        printf(
            "scanner needs a refill. Exiting for now with:\n"
            "    saved fill state = %d\n"
            "    unfinished token size = %ld\n",
            state,
            unfinishedSize
        );

        if(0<unfinishedSize && start<limit)
        {
            printf("    unfinished token is: ");
            fwrite(start, 1, cursor-start, stdout);
            putchar('\n');
        }
        putchar('\n');

        /*
         * Once we get here, we can get rid of
         * everything before start and after limit.
         */
        if(eof==true) goto start;
        if(buffer<start)
        {
            size_t startOffset = start-buffer;
            memmove(buffer, start, limit-start);
            marker -= startOffset;
            cursor -= startOffset;
            limit -= startOffset;
            start -= startOffset;
        }
        return 0;
    }
};

// ----------------------------------------------------------------------
int main(
    int     argc,
    char    **argv
)
{
    // Parse cmd line
    int input = 0;
    if(1<argc)
    {
        input = open(argv[1], O_RDONLY | O_BINARY);
        if(input<0)
        {
            fprintf(
                stderr,
                "could not open file %s\n",
                argv[1]
            );
            exit(1);
        }
    }

    /*
     * Tokenize input file by pushing batches
     * of data one by one into the scanner.
     */
    const size_t batchSize = 256;
    uint8_t buffer[batchSize];
    PushScanner scanner;
    while(1)
    {
        ssize_t n = read(input, buffer, batchSize);
        if (scanner.push(buffer, n)) {
          printf("Scanner: illegal data\n");
          return 1;
       }
        if(n<batchSize) break;
    }
    scanner.push(0, -1);
    close(input);

    // Done
    return 0;
}
