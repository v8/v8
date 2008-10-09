/* This is JavaScriptCore's variant of the PCRE library. While this library
started out as a copy of PCRE, many of the features of PCRE have been
removed. This library now supports only the regular expression features
required by the JavaScript language specification, and has only the functions
needed by JavaScriptCore and the rest of WebKit.

                 Originally written by Philip Hazel
           Copyright (c) 1997-2006 University of Cambridge
    Copyright (C) 2002, 2004, 2006, 2007 Apple Inc. All rights reserved.
    Copyright (C) 2007 Eric Seidel <eric@webkit.org>

-----------------------------------------------------------------------------
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    * Neither the name of the University of Cambridge nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------------
*/

/* This module contains the external function jsRegExpExecute(), along with
supporting internal functions that are not used by other modules. */

#include "config.h"

#include "pcre_internal.h"

#include <string.h>
#include "ASCIICType.h"

/* Negative values for the firstchar and reqchar variables */

#define REQ_UNSET (-2)
#define REQ_NONE  (-1)

/*************************************************
*      Code parameters and static tables         *
*************************************************/

/* Maximum number of items on the nested bracket stacks at compile time. This
applies to the nesting of all kinds of parentheses. It does not limit
un-nested, non-capturing parentheses. This number can be made bigger if
necessary - it is used to dimension one int and one unsigned char vector at
compile time. */

#define BRASTACK_SIZE 200

/* Table for handling escaped characters in the range '0'-'z'. Positive returns
are simple data values; negative values are for special things like \d and so
on. Zero means further processing is needed (for things like \x), or the escape
is invalid. */

static const short escapes[] = {
     0,      0,      0,      0,      0,      0,      0,      0,   /* 0 - 7 */
     0,      0,    ':',    ';',    '<',    '=',    '>',    '?',   /* 8 - ? */
   '@',      0, -ESC_B,      0, -ESC_D,      0,      0,      0,   /* @ - G */
     0,      0,      0,      0,      0,      0,      0,      0,   /* H - O */
     0,      0,      0, -ESC_S,      0,      0,      0, -ESC_W,   /* P - W */
     0,      0,      0,    '[',   '\\',    ']',    '^',    '_',   /* X - _ */
   '`',      7, -ESC_b,      0, -ESC_d,      0,   '\f',      0,   /* ` - g */
     0,      0,      0,      0,      0,      0,   '\n',      0,   /* h - o */
     0,      0,    '\r', -ESC_s,   '\t',      0,  '\v', -ESC_w,   /* p - w */
     0,      0,      0                                            /* x - z */
};

/* Error code numbers. They are given names so that they can more easily be
tracked. */

enum ErrorCode {
    ERR0, ERR1, ERR2, ERR3, ERR4, ERR5, ERR6, ERR7, ERR8, ERR9,
    ERR10, ERR11, ERR12, ERR13, ERR14, ERR15, ERR16, ERR17
};

/* The texts of compile-time error messages. These are "char *" because they
are passed to the outside world. */

static const char* errorText(ErrorCode code)
{
    static const char errorTexts[] =
      /* 1 */
      "\\ at end of pattern\0"
      "\\c at end of pattern\0"
      "character value in \\x{...} sequence is too large\0"
      "numbers out of order in {} quantifier\0"
      /* 5 */
      "number too big in {} quantifier\0"
      "missing terminating ] for character class\0"
      "internal error: code overflow\0"
      "range out of order in character class\0"
      "nothing to repeat\0"
      /* 10 */
      "unmatched parentheses\0"
      "internal error: unexpected repeat\0"
      "unrecognized character after (?\0"
      "failed to get memory\0"
      "missing )\0"
      /* 15 */
      "reference to non-existent subpattern\0"
      "regular expression too large\0"
      "parentheses nested too deeply"
    ;

    int i = code;
    const char* text = errorTexts;
    while (i > 1)
        i -= !*text++;
    return text;
}

/* Structure for passing "static" information around between the functions
doing the compiling. */

struct CompileData {
    CompileData() {
        top_backref = 0;
        backrefMap = 0;
        req_varyopt = 0;
        needOuterBracket = false;
        numCapturingBrackets = 0;
    }
    int top_backref;            /* Maximum back reference */
    unsigned backrefMap;       /* Bitmap of low back refs */
    int req_varyopt;            /* "After variable item" flag for reqbyte */
    bool needOuterBracket;
    int numCapturingBrackets;
};

/* Definitions to allow mutual recursion */

static bool compileBracket(int, int*, unsigned char**, const UChar**, const UChar*, ErrorCode*, int, int*, int*, CompileData&);
static bool bracketIsAnchored(const unsigned char* code);
static bool bracketNeedsLineStart(const unsigned char* code, unsigned captureMap, unsigned backrefMap);
static int bracketFindFirstAssertedCharacter(const unsigned char* code, bool inassert);

/*************************************************
*            Handle escapes                      *
*************************************************/

/* This function is called when a \ has been encountered. It either returns a
positive value for a simple escape such as \n, or a negative value which
encodes one of the more complicated things such as \d. When UTF-8 is enabled,
a positive value greater than 255 may be returned. On entry, ptr is pointing at
the \. On exit, it is on the final character of the escape sequence.

Arguments:
  ptrptr         points to the pattern position pointer
  errorcodeptr   points to the errorcode variable
  bracount       number of previous extracting brackets
  options        the options bits
  isclass        true if inside a character class

Returns:         zero or positive => a data character
                 negative => a special escape sequence
                 on error, errorptr is set
*/

static int checkEscape(const UChar** ptrptr, const UChar* patternEnd, ErrorCode* errorcodeptr, int bracount, bool isclass)
{
    const UChar* ptr = *ptrptr + 1;

    /* If backslash is at the end of the pattern, it's an error. */
    if (ptr == patternEnd) {
        *errorcodeptr = ERR1;
        *ptrptr = ptr;
        return 0;
    }
    
    int c = *ptr;
    
    /* Non-alphamerics are literals. For digits or letters, do an initial lookup in
     a table. A non-zero result is something that can be returned immediately.
     Otherwise further processing may be required. */
    
    if (c < '0' || c > 'z') { /* Not alphameric */
    } else if (int escapeValue = escapes[c - '0']) {
        c = escapeValue;
        if (isclass) {
            if (-c == ESC_b)
                c = '\b'; /* \b is backslash in a class */
            else if (-c == ESC_B)
                c = 'B'; /* and \B is a capital B in a class (in browsers event though ECMAScript 15.10.2.19 says it raises an error) */
        }
    /* Escapes that need further processing, or are illegal. */
    
    } else {
        switch (c) {
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                /* Escape sequences starting with a non-zero digit are backreferences,
                 unless there are insufficient brackets, in which case they are octal
                 escape sequences. Those sequences end on the first non-octal character
                 or when we overflow 0-255, whichever comes first. */
                
                if (!isclass) {
                    const UChar* oldptr = ptr;
                    c -= '0';
                    while ((ptr + 1 < patternEnd) && isASCIIDigit(ptr[1]) && c <= bracount)
                        c = c * 10 + *(++ptr) - '0';
                    if (c <= bracount) {
                        c = -(ESC_REF + c);
                        break;
                    }
                    ptr = oldptr;      /* Put the pointer back and fall through */
                }
                
                /* Handle an octal number following \. If the first digit is 8 or 9,
                 this is not octal. */
                
                if ((c = *ptr) >= '8')
                    break;

            /* \0 always starts an octal number, but we may drop through to here with a
             larger first octal digit. */

            case '0': {
                c -= '0';
                int i;
                for (i = 1; i <= 2; ++i) {
                    if (ptr + i >= patternEnd || ptr[i] < '0' || ptr[i] > '7')
                        break;
                    int cc = c * 8 + ptr[i] - '0';
                    if (cc > 255)
                        break;
                    c = cc;
                }
                ptr += i - 1;
                break;
            }

            case 'x': {
                c = 0;
                int i;
                for (i = 1; i <= 2; ++i) {
                    if (ptr + i >= patternEnd || !isASCIIHexDigit(ptr[i])) {
                        c = 'x';
                        i = 1;
                        break;
                    }
                    int cc = ptr[i];
                    if (cc >= 'a')
                        cc -= 32;             /* Convert to upper case */
                    c = c * 16 + cc - ((cc < 'A') ? '0' : ('A' - 10));
                }
                ptr += i - 1;
                break;
            }

            case 'u': {
                c = 0;
                int i;
                for (i = 1; i <= 4; ++i) {
                    if (ptr + i >= patternEnd || !isASCIIHexDigit(ptr[i])) {
                        c = 'u';
                        i = 1;
                        break;
                    }
                    int cc = ptr[i];
                    if (cc >= 'a')
                        cc -= 32;             /* Convert to upper case */
                    c = c * 16 + cc - ((cc < 'A') ? '0' : ('A' - 10));
                }
                ptr += i - 1;
                break;
            }

            case 'c':
                if (++ptr == patternEnd) {
                    *errorcodeptr = ERR2;
                    return 0;
                }
                c = *ptr;
                
                /* A letter is upper-cased; then the 0x40 bit is flipped. This coding
                 is ASCII-specific, but then the whole concept of \cx is ASCII-specific. */
                c = toASCIIUpper(c) ^ 0x40;
                break;
            }
    }
    
    *ptrptr = ptr;
    return c;
}

/*************************************************
*            Check for counted repeat            *
*************************************************/

/* This function is called when a '{' is encountered in a place where it might
start a quantifier. It looks ahead to see if it really is a quantifier or not.
It is only a quantifier if it is one of the forms {ddd} {ddd,} or {ddd,ddd}
where the ddds are digits.

Arguments:
  p         pointer to the first char after '{'

Returns:    true or false
*/

static bool isCountedRepeat(const UChar* p, const UChar* patternEnd)
{
    if (p >= patternEnd || !isASCIIDigit(*p))
        return false;
    p++;
    while (p < patternEnd && isASCIIDigit(*p))
        p++;
    if (p < patternEnd && *p == '}')
        return true;
    
    if (p >= patternEnd || *p++ != ',')
        return false;
    if (p < patternEnd && *p == '}')
        return true;
    
    if (p >= patternEnd || !isASCIIDigit(*p))
        return false;
    p++;
    while (p < patternEnd && isASCIIDigit(*p))
        p++;
    
    return (p < patternEnd && *p == '}');
}

/*************************************************
*         Read repeat counts                     *
*************************************************/

/* Read an item of the form {n,m} and return the values. This is called only
after isCountedRepeat() has confirmed that a repeat-count quantifier exists,
so the syntax is guaranteed to be correct, but we need to check the values.

Arguments:
  p              pointer to first char after '{'
  minp           pointer to int for min
  maxp           pointer to int for max
                 returned as -1 if no max
  errorcodeptr   points to error code variable

Returns:         pointer to '}' on success;
                 current ptr on error, with errorcodeptr set non-zero
*/

static const UChar* readRepeatCounts(const UChar* p, int* minp, int* maxp, ErrorCode* errorcodeptr)
{
    int min = 0;
    int max = -1;
    
    /* Read the minimum value and do a paranoid check: a negative value indicates
     an integer overflow. */
    
    while (isASCIIDigit(*p))
        min = min * 10 + *p++ - '0';
    if (min < 0 || min > 65535) {
        *errorcodeptr = ERR5;
        return p;
    }
    
    /* Read the maximum value if there is one, and again do a paranoid on its size.
     Also, max must not be less than min. */
    
    if (*p == '}')
        max = min;
    else {
        if (*(++p) != '}') {
            max = 0;
            while (isASCIIDigit(*p))
                max = max * 10 + *p++ - '0';
            if (max < 0 || max > 65535) {
                *errorcodeptr = ERR5;
                return p;
            }
            if (max < min) {
                *errorcodeptr = ERR4;
                return p;
            }
        }
    }
    
    /* Fill in the required variables, and pass back the pointer to the terminating
     '}'. */
    
    *minp = min;
    *maxp = max;
    return p;
}

/*************************************************
*      Find first significant op code            *
*************************************************/

/* This is called by several functions that scan a compiled expression looking
for a fixed first character, or an anchoring op code etc. It skips over things
that do not influence this.

Arguments:
  code         pointer to the start of the group
Returns:       pointer to the first significant opcode
*/

static const unsigned char* firstSignificantOpcode(const unsigned char* code)
{
    while (*code == OP_BRANUMBER)
        code += 3;
    return code;
}

static const unsigned char* firstSignificantOpcodeSkippingAssertions(const unsigned char* code)
{
    while (true) {
        switch (*code) {
            case OP_ASSERT_NOT:
                advanceToEndOfBracket(code);
                code += 1 + LINK_SIZE;
                break;
            case OP_WORD_BOUNDARY:
            case OP_NOT_WORD_BOUNDARY:
                ++code;
                break;
            case OP_BRANUMBER:
                code += 3;
                break;
            default:
                return code;
        }
    }
}

/*************************************************
*           Get othercase range                  *
*************************************************/

/* This function is passed the start and end of a class range, in UTF-8 mode
with UCP support. It searches up the characters, looking for internal ranges of
characters in the "other" case. Each call returns the next one, updating the
start address.

Arguments:
  cptr        points to starting character value; updated
  d           end value
  ocptr       where to put start of othercase range
  odptr       where to put end of othercase range

Yield:        true when range returned; false when no more
*/

static bool getOthercaseRange(int* cptr, int d, int* ocptr, int* odptr)
{
    int c, othercase = 0;
    
    for (c = *cptr; c <= d; c++) {
        if ((othercase = kjs_pcre_ucp_othercase(c)) >= 0)
            break;
    }
    
    if (c > d)
        return false;
    
    *ocptr = othercase;
    int next = othercase + 1;
    
    for (++c; c <= d; c++) {
        if (kjs_pcre_ucp_othercase(c) != next)
            break;
        next++;
    }
    
    *odptr = next - 1;
    *cptr = c;
    
    return true;
}

/*************************************************
 *       Convert character value to UTF-8         *
 *************************************************/

/* This function takes an integer value in the range 0 - 0x7fffffff
 and encodes it as a UTF-8 character in 0 to 6 bytes.
 
 Arguments:
 cvalue     the character value
 buffer     pointer to buffer for result - at least 6 bytes long
 
 Returns:     number of characters placed in the buffer
 */

static int encodeUTF8(int cvalue, unsigned char *buffer)
{
    int i;
    for (i = 0; i < kjs_pcre_utf8_table1_size; i++)
        if (cvalue <= kjs_pcre_utf8_table1[i])
            break;
    buffer += i;
    for (int j = i; j > 0; j--) {
        *buffer-- = 0x80 | (cvalue & 0x3f);
        cvalue >>= 6;
    }
    *buffer = kjs_pcre_utf8_table2[i] | cvalue;
    return i + 1;
}

/*************************************************
*           Compile one branch                   *
*************************************************/

/* Scan the pattern, compiling it into the code vector.

Arguments:
  options        the option bits
  brackets       points to number of extracting brackets used
  codeptr        points to the pointer to the current code point
  ptrptr         points to the current pattern pointer
  errorcodeptr   points to error code variable
  firstbyteptr   set to initial literal character, or < 0 (REQ_UNSET, REQ_NONE)
  reqbyteptr     set to the last literal character required, else < 0
  cd             contains pointers to tables etc.

Returns:         true on success
                 false, with *errorcodeptr set non-zero on error
*/

static inline bool safelyCheckNextChar(const UChar* ptr, const UChar* patternEnd, UChar expected)
{
    return ((ptr + 1 < patternEnd) && ptr[1] == expected);
}

static bool
compileBranch(int options, int* brackets, unsigned char** codeptr,
               const UChar** ptrptr, const UChar* patternEnd, ErrorCode* errorcodeptr, int *firstbyteptr,
               int* reqbyteptr, CompileData& cd)
{
    int repeat_type, op_type;
    int repeat_min = 0, repeat_max = 0;      /* To please picky compilers */
    int bravalue = 0;
    int reqvary, tempreqvary;
    int c;
    unsigned char* code = *codeptr;
    unsigned char* tempcode;
    bool groupsetfirstbyte = false;
    const UChar* ptr = *ptrptr;
    const UChar* tempptr;
    unsigned char* previous = NULL;
    unsigned char classbits[32];
    
    bool class_utf8;
    unsigned char* class_utf8data;
    unsigned char utf8_char[6];
    
    /* Initialize no first byte, no required byte. REQ_UNSET means "no char
     matching encountered yet". It gets changed to REQ_NONE if we hit something that
     matches a non-fixed char first char; reqbyte just remains unset if we never
     find one.
     
     When we hit a repeat whose minimum is zero, we may have to adjust these values
     to take the zero repeat into account. This is implemented by setting them to
     zerofirstbyte and zeroreqbyte when such a repeat is encountered. The individual
     item types that can be repeated set these backoff variables appropriately. */
    
    int firstbyte = REQ_UNSET;
    int reqbyte = REQ_UNSET;
    int zeroreqbyte = REQ_UNSET;
    int zerofirstbyte = REQ_UNSET;
    
    /* The variable req_caseopt contains either the REQ_IGNORE_CASE value or zero,
     according to the current setting of the ignores-case flag. REQ_IGNORE_CASE is a bit
     value > 255. It is added into the firstbyte or reqbyte variables to record the
     case status of the value. This is used only for ASCII characters. */
    
    int req_caseopt = (options & IgnoreCaseOption) ? REQ_IGNORE_CASE : 0;
    
    /* Switch on next character until the end of the branch */
    
    for (;; ptr++) {
        bool negate_class;
        bool should_flip_negation; /* If a negative special such as \S is used, we should negate the whole class to properly support Unicode. */
        int class_charcount;
        int class_lastchar;
        int skipbytes;
        int subreqbyte;
        int subfirstbyte;
        int mclength;
        unsigned char mcbuffer[8];
        
        /* Next byte in the pattern */
        
        c = ptr < patternEnd ? *ptr : 0;
        
        /* Fill in length of a previous callout, except when the next thing is
         a quantifier. */
        
        bool is_quantifier = c == '*' || c == '+' || c == '?' || (c == '{' && isCountedRepeat(ptr + 1, patternEnd));
        
        switch (c) {
            /* The branch terminates at end of string, |, or ). */
                
            case 0:
                if (ptr < patternEnd)
                    goto NORMAL_CHAR;
                // End of string; fall through
            case '|':
            case ')':
                *firstbyteptr = firstbyte;
                *reqbyteptr = reqbyte;
                *codeptr = code;
                *ptrptr = ptr;
                return true;
                
            /* Handle single-character metacharacters. In multiline mode, ^ disables
             the setting of any following char as a first character. */

            case '^':
                if (options & MatchAcrossMultipleLinesOption) {
                    if (firstbyte == REQ_UNSET)
                        firstbyte = REQ_NONE;
                    *code++ = OP_BOL;
                } else
                    *code++ = OP_CIRC;
                previous = NULL;
                break;

            case '$':
                previous = NULL;
                if (options & MatchAcrossMultipleLinesOption)
                  *code++ = OP_EOL;
                else
                  *code++ = OP_DOLL;
                break;

            /* There can never be a first char if '.' is first, whatever happens about
             repeats. The value of reqbyte doesn't change either. */

            case '.':
                if (firstbyte == REQ_UNSET)
                    firstbyte = REQ_NONE;
                zerofirstbyte = firstbyte;
                zeroreqbyte = reqbyte;
                previous = code;
                *code++ = OP_NOT_NEWLINE;
                break;
                
            /* Character classes. If the included characters are all < 256, we build a
             32-byte bitmap of the permitted characters, except in the special case
             where there is only one such character. For negated classes, we build the
             map as usual, then invert it at the end. However, we use a different opcode
             so that data characters > 255 can be handled correctly.
             
             If the class contains characters outside the 0-255 range, a different
             opcode is compiled. It may optionally have a bit map for characters < 256,
             but those above are are explicitly listed afterwards. A flag byte tells
             whether the bitmap is present, and whether this is a negated class or not.
             */
                
            case '[': {
                previous = code;
                should_flip_negation = false;
                
                /* PCRE supports POSIX class stuff inside a class. Perl gives an error if
                 they are encountered at the top level, so we'll do that too. */
                
                /* If the first character is '^', set the negation flag and skip it. */

                if (ptr + 1 >= patternEnd) {
                    *errorcodeptr = ERR6;
                    return false;
                }

                if (ptr[1] == '^') {
                    negate_class = true;
                    ++ptr;
                } else
                    negate_class = false;
                
                /* Keep a count of chars with values < 256 so that we can optimize the case
                 of just a single character (as long as it's < 256). For higher valued UTF-8
                 characters, we don't yet do any optimization. */
                
                class_charcount = 0;
                class_lastchar = -1;
                
                class_utf8 = false;                       /* No chars >= 256 */
                class_utf8data = code + LINK_SIZE + 34;   /* For UTF-8 items */
                
                /* Initialize the 32-char bit map to all zeros. We have to build the
                 map in a temporary bit of store, in case the class contains only 1
                 character (< 256), because in that case the compiled code doesn't use the
                 bit map. */
                
                memset(classbits, 0, 32 * sizeof(unsigned char));
                
                /* Process characters until ] is reached. The first pass
                 through the regex checked the overall syntax, so we don't need to be very
                 strict here. At the start of the loop, c contains the first byte of the
                 character. */

                while ((++ptr < patternEnd) && (c = *ptr) != ']') {
                    /* Backslash may introduce a single character, or it may introduce one
                     of the specials, which just set a flag. Escaped items are checked for
                     validity in the pre-compiling pass. The sequence \b is a special case.
                     Inside a class (and only there) it is treated as backspace. Elsewhere
                     it marks a word boundary. Other escapes have preset maps ready to
                     or into the one we are building. We assume they have more than one
                     character in them, so set class_charcount bigger than one. */
                    
                    if (c == '\\') {
                        c = checkEscape(&ptr, patternEnd, errorcodeptr, cd.numCapturingBrackets, true);
                        if (c < 0) {
                            class_charcount += 2;     /* Greater than 1 is what matters */
                            switch (-c) {
                                case ESC_d:
                                    for (c = 0; c < 32; c++)
                                        classbits[c] |= classBitmapForChar(c + cbit_digit);
                                    continue;
                                    
                                case ESC_D:
                                    should_flip_negation = true;
                                    for (c = 0; c < 32; c++)
                                        classbits[c] |= ~classBitmapForChar(c + cbit_digit);
                                    continue;
                                    
                                case ESC_w:
                                    for (c = 0; c < 32; c++)
                                        classbits[c] |= classBitmapForChar(c + cbit_word);
                                    continue;
                                    
                                case ESC_W:
                                    should_flip_negation = true;
                                    for (c = 0; c < 32; c++)
                                        classbits[c] |= ~classBitmapForChar(c + cbit_word);
                                    continue;
                                    
                                case ESC_s:
                                    for (c = 0; c < 32; c++)
                                         classbits[c] |= classBitmapForChar(c + cbit_space);
                                    continue;
                                    
                                case ESC_S:
                                    should_flip_negation = true;
                                    for (c = 0; c < 32; c++)
                                         classbits[c] |= ~classBitmapForChar(c + cbit_space);
                                    continue;
                                    
                                    /* Unrecognized escapes are faulted if PCRE is running in its
                                     strict mode. By default, for compatibility with Perl, they are
                                     treated as literals. */
                                    
                                default:
                                    c = *ptr;              /* The final character */
                                    class_charcount -= 2;  /* Undo the default count from above */
                            }
                        }
                        
                        /* Fall through if we have a single character (c >= 0). This may be
                         > 256 in UTF-8 mode. */
                        
                    }   /* End of backslash handling */
                    
                    /* A single character may be followed by '-' to form a range. However,
                     Perl does not permit ']' to be the end of the range. A '-' character
                     here is treated as a literal. */
                    
                    if ((ptr + 2 < patternEnd) && ptr[1] == '-' && ptr[2] != ']') {
                        ptr += 2;
                        
                        int d = *ptr;
                        
                        /* The second part of a range can be a single-character escape, but
                         not any of the other escapes. Perl 5.6 treats a hyphen as a literal
                         in such circumstances. */
                        
                        if (d == '\\') {
                            const UChar* oldptr = ptr;
                            d = checkEscape(&ptr, patternEnd, errorcodeptr, cd.numCapturingBrackets, true);
                            
                            /* \X is literal X; any other special means the '-' was literal */
                            if (d < 0) {
                                ptr = oldptr - 2;
                                goto LONE_SINGLE_CHARACTER;  /* A few lines below */
                            }
                        }
                        
                        /* The check that the two values are in the correct order happens in
                         the pre-pass. Optimize one-character ranges */
                        
                        if (d == c)
                            goto LONE_SINGLE_CHARACTER;  /* A few lines below */
                        
                        /* In UTF-8 mode, if the upper limit is > 255, or > 127 for caseless
                         matching, we have to use an XCLASS with extra data items. Caseless
                         matching for characters > 127 is available only if UCP support is
                         available. */
                        
                        if ((d > 255 || ((options & IgnoreCaseOption) && d > 127))) {
                            class_utf8 = true;
                            
                            /* With UCP support, we can find the other case equivalents of
                             the relevant characters. There may be several ranges. Optimize how
                             they fit with the basic range. */
                            
                            if (options & IgnoreCaseOption) {
                                int occ, ocd;
                                int cc = c;
                                int origd = d;
                                while (getOthercaseRange(&cc, origd, &occ, &ocd)) {
                                    if (occ >= c && ocd <= d)
                                        continue;  /* Skip embedded ranges */
                                    
                                    if (occ < c  && ocd >= c - 1)        /* Extend the basic range */
                                    {                                  /* if there is overlap,   */
                                        c = occ;                           /* noting that if occ < c */
                                        continue;                          /* we can't have ocd > d  */
                                    }                                  /* because a subrange is  */
                                    if (ocd > d && occ <= d + 1)         /* always shorter than    */
                                    {                                  /* the basic range.       */
                                        d = ocd;
                                        continue;
                                    }
                                    
                                    if (occ == ocd)
                                        *class_utf8data++ = XCL_SINGLE;
                                    else {
                                        *class_utf8data++ = XCL_RANGE;
                                        class_utf8data += encodeUTF8(occ, class_utf8data);
                                    }
                                    class_utf8data += encodeUTF8(ocd, class_utf8data);
                                }
                            }
                            
                            /* Now record the original range, possibly modified for UCP caseless
                             overlapping ranges. */
                            
                            *class_utf8data++ = XCL_RANGE;
                            class_utf8data += encodeUTF8(c, class_utf8data);
                            class_utf8data += encodeUTF8(d, class_utf8data);
                            
                            /* With UCP support, we are done. Without UCP support, there is no
                             caseless matching for UTF-8 characters > 127; we can use the bit map
                             for the smaller ones. */
                            
                            continue;    /* With next character in the class */
                        }
                        
                        /* We use the bit map for all cases when not in UTF-8 mode; else
                         ranges that lie entirely within 0-127 when there is UCP support; else
                         for partial ranges without UCP support. */
                        
                        for (; c <= d; c++) {
                            classbits[c/8] |= (1 << (c&7));
                            if (options & IgnoreCaseOption) {
                                int uc = flipCase(c);
                                classbits[uc/8] |= (1 << (uc&7));
                            }
                            class_charcount++;                /* in case a one-char range */
                            class_lastchar = c;
                        }
                        
                        continue;   /* Go get the next char in the class */
                    }
                    
                    /* Handle a lone single character - we can get here for a normal
                     non-escape char, or after \ that introduces a single character or for an
                     apparent range that isn't. */
                    
                LONE_SINGLE_CHARACTER:
                    
                    /* Handle a character that cannot go in the bit map */
                    
                    if ((c > 255 || ((options & IgnoreCaseOption) && c > 127))) {
                        class_utf8 = true;
                        *class_utf8data++ = XCL_SINGLE;
                        class_utf8data += encodeUTF8(c, class_utf8data);
                        
                        if (options & IgnoreCaseOption) {
                            int othercase;
                            if ((othercase = kjs_pcre_ucp_othercase(c)) >= 0) {
                                *class_utf8data++ = XCL_SINGLE;
                                class_utf8data += encodeUTF8(othercase, class_utf8data);
                            }
                        }
                    } else {
                        /* Handle a single-byte character */
                        classbits[c/8] |= (1 << (c&7));
                        if (options & IgnoreCaseOption) {
                            c = flipCase(c);
                            classbits[c/8] |= (1 << (c&7));
                        }
                        class_charcount++;
                        class_lastchar = c;
                    }
                }
                
                /* If class_charcount is 1, we saw precisely one character whose value is
                 less than 256. In non-UTF-8 mode we can always optimize. In UTF-8 mode, we
                 can optimize the negative case only if there were no characters >= 128
                 because OP_NOT and the related opcodes like OP_NOTSTAR operate on
                 single-bytes only. This is an historical hangover. Maybe one day we can
                 tidy these opcodes to handle multi-byte characters.
                 
                 The optimization throws away the bit map. We turn the item into a
                 1-character OP_CHAR[NC] if it's positive, or OP_NOT if it's negative. Note
                 that OP_NOT does not support multibyte characters. In the positive case, it
                 can cause firstbyte to be set. Otherwise, there can be no first char if
                 this item is first, whatever repeat count may follow. In the case of
                 reqbyte, save the previous value for reinstating. */
                
                if (class_charcount == 1 && (!class_utf8 && (!negate_class || class_lastchar < 128))) {
                    zeroreqbyte = reqbyte;
                    
                    /* The OP_NOT opcode works on one-byte characters only. */
                    
                    if (negate_class) {
                        if (firstbyte == REQ_UNSET)
                            firstbyte = REQ_NONE;
                        zerofirstbyte = firstbyte;
                        *code++ = OP_NOT;
                        *code++ = class_lastchar;
                        break;
                    }
                    
                    /* For a single, positive character, get the value into c, and
                     then we can handle this with the normal one-character code. */
                    
                    c = class_lastchar;
                    goto NORMAL_CHAR;
                }       /* End of 1-char optimization */
                
                /* The general case - not the one-char optimization. If this is the first
                 thing in the branch, there can be no first char setting, whatever the
                 repeat count. Any reqbyte setting must remain unchanged after any kind of
                 repeat. */
                
                if (firstbyte == REQ_UNSET) firstbyte = REQ_NONE;
                zerofirstbyte = firstbyte;
                zeroreqbyte = reqbyte;
                
                /* If there are characters with values > 255, we have to compile an
                 extended class, with its own opcode. If there are no characters < 256,
                 we can omit the bitmap. */
                
                if (class_utf8 && !should_flip_negation) {
                    *class_utf8data++ = XCL_END;    /* Marks the end of extra data */
                    *code++ = OP_XCLASS;
                    code += LINK_SIZE;
                    *code = negate_class? XCL_NOT : 0;
                    
                    /* If the map is required, install it, and move on to the end of
                     the extra data */
                    
                    if (class_charcount > 0) {
                        *code++ |= XCL_MAP;
                        memcpy(code, classbits, 32);
                        code = class_utf8data;
                    }
                    
                    /* If the map is not required, slide down the extra data. */
                    
                    else {
                        int len = class_utf8data - (code + 33);
                        memmove(code + 1, code + 33, len);
                        code += len + 1;
                    }
                    
                    /* Now fill in the complete length of the item */
                    
                    putLinkValue(previous + 1, code - previous);
                    break;   /* End of class handling */
                }
                
                /* If there are no characters > 255, negate the 32-byte map if necessary,
                 and copy it into the code vector. If this is the first thing in the branch,
                 there can be no first char setting, whatever the repeat count. Any reqbyte
                 setting must remain unchanged after any kind of repeat. */
                
                *code++ = (negate_class == should_flip_negation) ? OP_CLASS : OP_NCLASS;
                if (negate_class)
                    for (c = 0; c < 32; c++)
                        code[c] = ~classbits[c];
                else
                    memcpy(code, classbits, 32);
                code += 32;
                break;
            }
                
            /* Various kinds of repeat; '{' is not necessarily a quantifier, but this
             has been tested above. */

            case '{':
                if (!is_quantifier)
                    goto NORMAL_CHAR;
                ptr = readRepeatCounts(ptr + 1, &repeat_min, &repeat_max, errorcodeptr);
                if (*errorcodeptr)
                    goto FAILED;
                goto REPEAT;
                
            case '*':
                repeat_min = 0;
                repeat_max = -1;
                goto REPEAT;
                
            case '+':
                repeat_min = 1;
                repeat_max = -1;
                goto REPEAT;
                
            case '?':
                repeat_min = 0;
                repeat_max = 1;
                
            REPEAT:
                if (!previous) {
                    *errorcodeptr = ERR9;
                    goto FAILED;
                }
                
                if (repeat_min == 0) {
                    firstbyte = zerofirstbyte;    /* Adjust for zero repeat */
                    reqbyte = zeroreqbyte;        /* Ditto */
                }
                
                /* Remember whether this is a variable length repeat */
                
                reqvary = (repeat_min == repeat_max) ? 0 : REQ_VARY;
                
                op_type = 0;                    /* Default single-char op codes */
                
                /* Save start of previous item, in case we have to move it up to make space
                 for an inserted OP_ONCE for the additional '+' extension. */
                /* FIXME: Probably don't need this because we don't use OP_ONCE. */
                
                tempcode = previous;
                
                /* If the next character is '+', we have a possessive quantifier. This
                 implies greediness, whatever the setting of the PCRE_UNGREEDY option.
                 If the next character is '?' this is a minimizing repeat, by default,
                 but if PCRE_UNGREEDY is set, it works the other way round. We change the
                 repeat type to the non-default. */
                
                if (safelyCheckNextChar(ptr, patternEnd, '?')) {
                    repeat_type = 1;
                    ptr++;
                } else
                    repeat_type = 0;
                
                /* If previous was a character match, abolish the item and generate a
                 repeat item instead. If a char item has a minumum of more than one, ensure
                 that it is set in reqbyte - it might not be if a sequence such as x{3} is
                 the first thing in a branch because the x will have gone into firstbyte
                 instead.  */
                
                if (*previous == OP_CHAR || *previous == OP_CHAR_IGNORING_CASE) {
                    /* Deal with UTF-8 characters that take up more than one byte. It's
                     easier to write this out separately than try to macrify it. Use c to
                     hold the length of the character in bytes, plus 0x80 to flag that it's a
                     length rather than a small character. */
                    
                    if (code[-1] & 0x80) {
                        unsigned char *lastchar = code - 1;
                        while((*lastchar & 0xc0) == 0x80)
                            lastchar--;
                        c = code - lastchar;            /* Length of UTF-8 character */
                        memcpy(utf8_char, lastchar, c); /* Save the char */
                        c |= 0x80;                      /* Flag c as a length */
                    }
                    else {
                        c = code[-1];
                        if (repeat_min > 1)
                            reqbyte = c | req_caseopt | cd.req_varyopt;
                    }
                    
                    goto OUTPUT_SINGLE_REPEAT;   /* Code shared with single character types */
                }
                
                else if (*previous == OP_ASCII_CHAR || *previous == OP_ASCII_LETTER_IGNORING_CASE) {
                    c = previous[1];
                    if (repeat_min > 1)
                        reqbyte = c | req_caseopt | cd.req_varyopt;
                    goto OUTPUT_SINGLE_REPEAT;
                }
                
                /* If previous was a single negated character ([^a] or similar), we use
                 one of the special opcodes, replacing it. The code is shared with single-
                 character repeats by setting opt_type to add a suitable offset into
                 repeat_type. OP_NOT is currently used only for single-byte chars. */
                
                else if (*previous == OP_NOT) {
                    op_type = OP_NOTSTAR - OP_STAR;  /* Use "not" opcodes */
                    c = previous[1];
                    goto OUTPUT_SINGLE_REPEAT;
                }
                
                /* If previous was a character type match (\d or similar), abolish it and
                 create a suitable repeat item. The code is shared with single-character
                 repeats by setting op_type to add a suitable offset into repeat_type. */
                
                else if (*previous <= OP_NOT_NEWLINE) {
                    op_type = OP_TYPESTAR - OP_STAR;  /* Use type opcodes */
                    c = *previous;
                    
                OUTPUT_SINGLE_REPEAT:
                    int prop_type = -1;
                    int prop_value = -1;
                    
                    unsigned char* oldcode = code;
                    code = previous;                  /* Usually overwrite previous item */
                    
                    /* If the maximum is zero then the minimum must also be zero; Perl allows
                     this case, so we do too - by simply omitting the item altogether. */
                    
                    if (repeat_max == 0)
                        goto END_REPEAT;
                    
                    /* Combine the op_type with the repeat_type */
                    
                    repeat_type += op_type;
                    
                    /* A minimum of zero is handled either as the special case * or ?, or as
                     an UPTO, with the maximum given. */
                    
                    if (repeat_min == 0) {
                        if (repeat_max == -1)
                            *code++ = OP_STAR + repeat_type;
                        else if (repeat_max == 1)
                            *code++ = OP_QUERY + repeat_type;
                        else {
                            *code++ = OP_UPTO + repeat_type;
                            put2ByteValueAndAdvance(code, repeat_max);
                        }
                    }
                    
                    /* A repeat minimum of 1 is optimized into some special cases. If the
                     maximum is unlimited, we use OP_PLUS. Otherwise, the original item it
                     left in place and, if the maximum is greater than 1, we use OP_UPTO with
                     one less than the maximum. */
                    
                    else if (repeat_min == 1) {
                        if (repeat_max == -1)
                            *code++ = OP_PLUS + repeat_type;
                        else {
                            code = oldcode;                 /* leave previous item in place */
                            if (repeat_max == 1)
                                goto END_REPEAT;
                            *code++ = OP_UPTO + repeat_type;
                            put2ByteValueAndAdvance(code, repeat_max - 1);
                        }
                    }
                    
                    /* The case {n,n} is just an EXACT, while the general case {n,m} is
                     handled as an EXACT followed by an UPTO. */
                    
                    else {
                        *code++ = OP_EXACT + op_type;  /* NB EXACT doesn't have repeat_type */
                        put2ByteValueAndAdvance(code, repeat_min);
                        
                        /* If the maximum is unlimited, insert an OP_STAR. Before doing so,
                         we have to insert the character for the previous code. For a repeated
                         Unicode property match, there are two extra bytes that define the
                         required property. In UTF-8 mode, long characters have their length in
                         c, with the 0x80 bit as a flag. */
                        
                        if (repeat_max < 0) {
                            if (c >= 128) {
                                memcpy(code, utf8_char, c & 7);
                                code += c & 7;
                            } else {
                                *code++ = c;
                                if (prop_type >= 0) {
                                    *code++ = prop_type;
                                    *code++ = prop_value;
                                }
                            }
                            *code++ = OP_STAR + repeat_type;
                        }
                        
                        /* Else insert an UPTO if the max is greater than the min, again
                         preceded by the character, for the previously inserted code. */
                        
                        else if (repeat_max != repeat_min) {
                            if (c >= 128) {
                                memcpy(code, utf8_char, c & 7);
                                code += c & 7;
                            } else
                                *code++ = c;
                            if (prop_type >= 0) {
                                *code++ = prop_type;
                                *code++ = prop_value;
                            }
                            repeat_max -= repeat_min;
                            *code++ = OP_UPTO + repeat_type;
                            put2ByteValueAndAdvance(code, repeat_max);
                        }
                    }
                    
                    /* The character or character type itself comes last in all cases. */
                    
                    if (c >= 128) {
                        memcpy(code, utf8_char, c & 7);
                        code += c & 7;
                    } else
                        *code++ = c;
                    
                    /* For a repeated Unicode property match, there are two extra bytes that
                     define the required property. */
                    
                    if (prop_type >= 0) {
                        *code++ = prop_type;
                        *code++ = prop_value;
                    }
                }
                
                /* If previous was a character class or a back reference, we put the repeat
                 stuff after it, but just skip the item if the repeat was {0,0}. */
                
                else if (*previous == OP_CLASS ||
                         *previous == OP_NCLASS ||
                         *previous == OP_XCLASS ||
                         *previous == OP_REF)
                {
                    if (repeat_max == 0) {
                        code = previous;
                        goto END_REPEAT;
                    }
                    
                    if (repeat_min == 0 && repeat_max == -1)
                        *code++ = OP_CRSTAR + repeat_type;
                    else if (repeat_min == 1 && repeat_max == -1)
                        *code++ = OP_CRPLUS + repeat_type;
                    else if (repeat_min == 0 && repeat_max == 1)
                        *code++ = OP_CRQUERY + repeat_type;
                    else {
                        *code++ = OP_CRRANGE + repeat_type;
                        put2ByteValueAndAdvance(code, repeat_min);
                        if (repeat_max == -1)
                            repeat_max = 0;  /* 2-byte encoding for max */
                        put2ByteValueAndAdvance(code, repeat_max);
                    }
                }
                
                /* If previous was a bracket group, we may have to replicate it in certain
                 cases. */
                
                else if (*previous >= OP_BRA) {
                    int ketoffset = 0;
                    int len = code - previous;
                    unsigned char* bralink = NULL;
                    
                    /* If the maximum repeat count is unlimited, find the end of the bracket
                     by scanning through from the start, and compute the offset back to it
                     from the current code pointer. There may be an OP_OPT setting following
                     the final KET, so we can't find the end just by going back from the code
                     pointer. */
                    
                    if (repeat_max == -1) {
                        const unsigned char* ket = previous;
                        advanceToEndOfBracket(ket);
                        ketoffset = code - ket;
                    }
                    
                    /* The case of a zero minimum is special because of the need to stick
                     OP_BRAZERO in front of it, and because the group appears once in the
                     data, whereas in other cases it appears the minimum number of times. For
                     this reason, it is simplest to treat this case separately, as otherwise
                     the code gets far too messy. There are several special subcases when the
                     minimum is zero. */
                    
                    if (repeat_min == 0) {
                        /* If the maximum is also zero, we just omit the group from the output
                         altogether. */
                        
                        if (repeat_max == 0) {
                            code = previous;
                            goto END_REPEAT;
                        }
                        
                        /* If the maximum is 1 or unlimited, we just have to stick in the
                         BRAZERO and do no more at this point. However, we do need to adjust
                         any OP_RECURSE calls inside the group that refer to the group itself or
                         any internal group, because the offset is from the start of the whole
                         regex. Temporarily terminate the pattern while doing this. */
                        
                        if (repeat_max <= 1) {
                            *code = OP_END;
                            memmove(previous+1, previous, len);
                            code++;
                            *previous++ = OP_BRAZERO + repeat_type;
                        }
                        
                        /* If the maximum is greater than 1 and limited, we have to replicate
                         in a nested fashion, sticking OP_BRAZERO before each set of brackets.
                         The first one has to be handled carefully because it's the original
                         copy, which has to be moved up. The remainder can be handled by code
                         that is common with the non-zero minimum case below. We have to
                         adjust the value of repeat_max, since one less copy is required. */
                        
                        else {
                            *code = OP_END;
                            memmove(previous + 2 + LINK_SIZE, previous, len);
                            code += 2 + LINK_SIZE;
                            *previous++ = OP_BRAZERO + repeat_type;
                            *previous++ = OP_BRA;
                            
                            /* We chain together the bracket offset fields that have to be
                             filled in later when the ends of the brackets are reached. */
                            
                            int offset = (!bralink) ? 0 : previous - bralink;
                            bralink = previous;
                            putLinkValueAllowZeroAndAdvance(previous, offset);
                        }
                        
                        repeat_max--;
                    }
                    
                    /* If the minimum is greater than zero, replicate the group as many
                     times as necessary, and adjust the maximum to the number of subsequent
                     copies that we need. If we set a first char from the group, and didn't
                     set a required char, copy the latter from the former. */
                    
                    else {
                        if (repeat_min > 1) {
                            if (groupsetfirstbyte && reqbyte < 0)
                                reqbyte = firstbyte;
                            for (int i = 1; i < repeat_min; i++) {
                                memcpy(code, previous, len);
                                code += len;
                            }
                        }
                        if (repeat_max > 0)
                            repeat_max -= repeat_min;
                    }
                    
                    /* This code is common to both the zero and non-zero minimum cases. If
                     the maximum is limited, it replicates the group in a nested fashion,
                     remembering the bracket starts on a stack. In the case of a zero minimum,
                     the first one was set up above. In all cases the repeat_max now specifies
                     the number of additional copies needed. */
                    
                    if (repeat_max >= 0) {
                        for (int i = repeat_max - 1; i >= 0; i--) {
                            *code++ = OP_BRAZERO + repeat_type;
                            
                            /* All but the final copy start a new nesting, maintaining the
                             chain of brackets outstanding. */
                            
                            if (i != 0) {
                                *code++ = OP_BRA;
                                int offset = (!bralink) ? 0 : code - bralink;
                                bralink = code;
                                putLinkValueAllowZeroAndAdvance(code, offset);
                            }
                            
                            memcpy(code, previous, len);
                            code += len;
                        }
                        
                        /* Now chain through the pending brackets, and fill in their length
                         fields (which are holding the chain links pro tem). */
                        
                        while (bralink) {
                            int offset = code - bralink + 1;
                            unsigned char* bra = code - offset;
                            int oldlinkoffset = getLinkValueAllowZero(bra + 1);
                            bralink = (!oldlinkoffset) ? 0 : bralink - oldlinkoffset;
                            *code++ = OP_KET;
                            putLinkValueAndAdvance(code, offset);
                            putLinkValue(bra + 1, offset);
                        }
                    }
                    
                    /* If the maximum is unlimited, set a repeater in the final copy. We
                     can't just offset backwards from the current code point, because we
                     don't know if there's been an options resetting after the ket. The
                     correct offset was computed above. */
                    
                    else
                        code[-ketoffset] = OP_KETRMAX + repeat_type;
                }
                
                /* Else there's some kind of shambles */
                
                else {
                    *errorcodeptr = ERR11;
                    goto FAILED;
                }
                
                /* In all case we no longer have a previous item. We also set the
                 "follows varying string" flag for subsequently encountered reqbytes if
                 it isn't already set and we have just passed a varying length item. */
                
            END_REPEAT:
                previous = NULL;
                cd.req_varyopt |= reqvary;
                break;
                
            /* Start of nested bracket sub-expression, or comment or lookahead or
             lookbehind or option setting or condition. First deal with special things
             that can come after a bracket; all are introduced by ?, and the appearance
             of any of them means that this is not a referencing group. They were
             checked for validity in the first pass over the string, so we don't have to
             check for syntax errors here.  */
                
            case '(':
                skipbytes = 0;
                
                if (*(++ptr) == '?') {
                    switch (*(++ptr)) {
                        case ':':                 /* Non-extracting bracket */
                            bravalue = OP_BRA;
                            ptr++;
                            break;
                            
                        case '=':                 /* Positive lookahead */
                            bravalue = OP_ASSERT;
                            ptr++;
                            break;
                            
                        case '!':                 /* Negative lookahead */
                            bravalue = OP_ASSERT_NOT;
                            ptr++;
                            break;
                            
                        /* Character after (? not specially recognized */
                            
                        default:
                            *errorcodeptr = ERR12;
                            goto FAILED;
                        }
                }
                
                /* Else we have a referencing group; adjust the opcode. If the bracket
                 number is greater than EXTRACT_BASIC_MAX, we set the opcode one higher, and
                 arrange for the true number to follow later, in an OP_BRANUMBER item. */
                
                else {
                    if (++(*brackets) > EXTRACT_BASIC_MAX) {
                        bravalue = OP_BRA + EXTRACT_BASIC_MAX + 1;
                        code[1 + LINK_SIZE] = OP_BRANUMBER;
                        put2ByteValue(code + 2 + LINK_SIZE, *brackets);
                        skipbytes = 3;
                    }
                    else
                        bravalue = OP_BRA + *brackets;
                }
                
                /* Process nested bracketed re. Assertions may not be repeated, but other
                 kinds can be. We copy code into a non-variable in order to be able
                 to pass its address because some compilers complain otherwise. Pass in a
                 new setting for the ims options if they have changed. */
                
                previous = (bravalue >= OP_BRAZERO) ? code : 0;
                *code = bravalue;
                tempcode = code;
                tempreqvary = cd.req_varyopt;     /* Save value before bracket */
                
                if (!compileBracket(
                                   options,
                                   brackets,                     /* Extracting bracket count */
                                   &tempcode,                    /* Where to put code (updated) */
                                   &ptr,                         /* Input pointer (updated) */
                                   patternEnd,
                                   errorcodeptr,                 /* Where to put an error message */
                                   skipbytes,                    /* Skip over OP_BRANUMBER */
                                   &subfirstbyte,                /* For possible first char */
                                   &subreqbyte,                  /* For possible last char */
                                   cd))                          /* Tables block */
                    goto FAILED;
                
                /* At the end of compiling, code is still pointing to the start of the
                 group, while tempcode has been updated to point past the end of the group
                 and any option resetting that may follow it. The pattern pointer (ptr)
                 is on the bracket. */
                
                /* Handle updating of the required and first characters. Update for normal
                 brackets of all kinds, and conditions with two branches (see code above).
                 If the bracket is followed by a quantifier with zero repeat, we have to
                 back off. Hence the definition of zeroreqbyte and zerofirstbyte outside the
                 main loop so that they can be accessed for the back off. */
                
                zeroreqbyte = reqbyte;
                zerofirstbyte = firstbyte;
                groupsetfirstbyte = false;
                
                if (bravalue >= OP_BRA) {
                    /* If we have not yet set a firstbyte in this branch, take it from the
                     subpattern, remembering that it was set here so that a repeat of more
                     than one can replicate it as reqbyte if necessary. If the subpattern has
                     no firstbyte, set "none" for the whole branch. In both cases, a zero
                     repeat forces firstbyte to "none". */
                    
                    if (firstbyte == REQ_UNSET) {
                        if (subfirstbyte >= 0) {
                            firstbyte = subfirstbyte;
                            groupsetfirstbyte = true;
                        }
                        else
                            firstbyte = REQ_NONE;
                        zerofirstbyte = REQ_NONE;
                    }
                    
                    /* If firstbyte was previously set, convert the subpattern's firstbyte
                     into reqbyte if there wasn't one, using the vary flag that was in
                     existence beforehand. */
                    
                    else if (subfirstbyte >= 0 && subreqbyte < 0)
                        subreqbyte = subfirstbyte | tempreqvary;
                    
                    /* If the subpattern set a required byte (or set a first byte that isn't
                     really the first byte - see above), set it. */
                    
                    if (subreqbyte >= 0)
                        reqbyte = subreqbyte;
                }
                
                /* For a forward assertion, we take the reqbyte, if set. This can be
                 helpful if the pattern that follows the assertion doesn't set a different
                 char. For example, it's useful for /(?=abcde).+/. We can't set firstbyte
                 for an assertion, however because it leads to incorrect effect for patterns
                 such as /(?=a)a.+/ when the "real" "a" would then become a reqbyte instead
                 of a firstbyte. This is overcome by a scan at the end if there's no
                 firstbyte, looking for an asserted first char. */
                
                else if (bravalue == OP_ASSERT && subreqbyte >= 0)
                    reqbyte = subreqbyte;
                
                /* Now update the main code pointer to the end of the group. */
                
                code = tempcode;
                
                /* Error if hit end of pattern */
                
                if (ptr >= patternEnd || *ptr != ')') {
                    *errorcodeptr = ERR14;
                    goto FAILED;
                }
                break;
                
            /* Check \ for being a real metacharacter; if not, fall through and handle
             it as a data character at the start of a string. Escape items are checked
             for validity in the pre-compiling pass. */
                
            case '\\':
                tempptr = ptr;
                c = checkEscape(&ptr, patternEnd, errorcodeptr, cd.numCapturingBrackets, false);
                
                /* Handle metacharacters introduced by \. For ones like \d, the ESC_ values
                 are arranged to be the negation of the corresponding OP_values. For the
                 back references, the values are ESC_REF plus the reference number. Only
                 back references and those types that consume a character may be repeated.
                 We can test for values between ESC_b and ESC_w for the latter; this may
                 have to change if any new ones are ever created. */
                
                if (c < 0) {
                    /* For metasequences that actually match a character, we disable the
                     setting of a first character if it hasn't already been set. */
                    
                    if (firstbyte == REQ_UNSET && -c > ESC_b && -c <= ESC_w)
                        firstbyte = REQ_NONE;
                    
                    /* Set values to reset to if this is followed by a zero repeat. */
                    
                    zerofirstbyte = firstbyte;
                    zeroreqbyte = reqbyte;
                    
                    /* Back references are handled specially */
                    
                    if (-c >= ESC_REF) {
                        int number = -c - ESC_REF;
                        previous = code;
                        *code++ = OP_REF;
                        put2ByteValueAndAdvance(code, number);
                    }
                    
                    /* For the rest, we can obtain the OP value by negating the escape
                     value */
                    
                    else {
                        previous = (-c > ESC_b && -c <= ESC_w) ? code : NULL;
                        *code++ = -c;
                    }
                    continue;
                }
                
                /* Fall through. */
                
                /* Handle a literal character. It is guaranteed not to be whitespace or #
                 when the extended flag is set. If we are in UTF-8 mode, it may be a
                 multi-byte literal character. */
                
                default:
            NORMAL_CHAR:
                
                previous = code;
                
                if (c < 128) {
                    mclength = 1;
                    mcbuffer[0] = c;
                    
                    if ((options & IgnoreCaseOption) && (c | 0x20) >= 'a' && (c | 0x20) <= 'z') {
                        *code++ = OP_ASCII_LETTER_IGNORING_CASE;
                        *code++ = c | 0x20;
                    } else {
                        *code++ = OP_ASCII_CHAR;
                        *code++ = c;
                    }
                } else {
                    mclength = encodeUTF8(c, mcbuffer);
                    
                    *code++ = (options & IgnoreCaseOption) ? OP_CHAR_IGNORING_CASE : OP_CHAR;
                    for (c = 0; c < mclength; c++)
                        *code++ = mcbuffer[c];
                }
                
                /* Set the first and required bytes appropriately. If no previous first
                 byte, set it from this character, but revert to none on a zero repeat.
                 Otherwise, leave the firstbyte value alone, and don't change it on a zero
                 repeat. */
                
                if (firstbyte == REQ_UNSET) {
                    zerofirstbyte = REQ_NONE;
                    zeroreqbyte = reqbyte;
                    
                    /* If the character is more than one byte long, we can set firstbyte
                     only if it is not to be matched caselessly. */
                    
                    if (mclength == 1 || req_caseopt == 0) {
                        firstbyte = mcbuffer[0] | req_caseopt;
                        if (mclength != 1)
                            reqbyte = code[-1] | cd.req_varyopt;
                    }
                    else
                        firstbyte = reqbyte = REQ_NONE;
                }
                
                /* firstbyte was previously set; we can set reqbyte only the length is
                 1 or the matching is caseful. */
                
                else {
                    zerofirstbyte = firstbyte;
                    zeroreqbyte = reqbyte;
                    if (mclength == 1 || req_caseopt == 0)
                        reqbyte = code[-1] | req_caseopt | cd.req_varyopt;
                }
                
                break;            /* End of literal character handling */
        }
    }                   /* end of big loop */
    
    /* Control never reaches here by falling through, only by a goto for all the
     error states. Pass back the position in the pattern so that it can be displayed
     to the user for diagnosing the error. */
    
FAILED:
    *ptrptr = ptr;
    return false;
}

/*************************************************
*     Compile sequence of alternatives           *
*************************************************/

/* On entry, ptr is pointing past the bracket character, but on return
it points to the closing bracket, or vertical bar, or end of string.
The code variable is pointing at the byte into which the BRA operator has been
stored. If the ims options are changed at the start (for a (?ims: group) or
during any branch, we need to insert an OP_OPT item at the start of every
following branch to ensure they get set correctly at run time, and also pass
the new options into every subsequent branch compile.

Argument:
  options        option bits, including any changes for this subpattern
  brackets       -> int containing the number of extracting brackets used
  codeptr        -> the address of the current code pointer
  ptrptr         -> the address of the current pattern pointer
  errorcodeptr   -> pointer to error code variable
  skipbytes      skip this many bytes at start (for OP_BRANUMBER)
  firstbyteptr   place to put the first required character, or a negative number
  reqbyteptr     place to put the last required character, or a negative number
  cd             points to the data block with tables pointers etc.

Returns:      true on success
*/

static bool
compileBracket(int options, int* brackets, unsigned char** codeptr,
    const UChar** ptrptr, const UChar* patternEnd, ErrorCode* errorcodeptr, int skipbytes,
    int* firstbyteptr, int* reqbyteptr, CompileData& cd)
{
    const UChar* ptr = *ptrptr;
    unsigned char* code = *codeptr;
    unsigned char* last_branch = code;
    unsigned char* start_bracket = code;
    int firstbyte = REQ_UNSET;
    int reqbyte = REQ_UNSET;
    
    /* Offset is set zero to mark that this bracket is still open */
    
    putLinkValueAllowZero(code + 1, 0);
    code += 1 + LINK_SIZE + skipbytes;
    
    /* Loop for each alternative branch */
    
    while (true) {
        /* Now compile the branch */
        
        int branchfirstbyte;
        int branchreqbyte;
        if (!compileBranch(options, brackets, &code, &ptr, patternEnd, errorcodeptr,
                            &branchfirstbyte, &branchreqbyte, cd)) {
            *ptrptr = ptr;
            return false;
        }
        
        /* If this is the first branch, the firstbyte and reqbyte values for the
         branch become the values for the regex. */
        
        if (*last_branch != OP_ALT) {
            firstbyte = branchfirstbyte;
            reqbyte = branchreqbyte;
        }
        
        /* If this is not the first branch, the first char and reqbyte have to
         match the values from all the previous branches, except that if the previous
         value for reqbyte didn't have REQ_VARY set, it can still match, and we set
         REQ_VARY for the regex. */
        
        else {
            /* If we previously had a firstbyte, but it doesn't match the new branch,
             we have to abandon the firstbyte for the regex, but if there was previously
             no reqbyte, it takes on the value of the old firstbyte. */
            
            if (firstbyte >= 0 && firstbyte != branchfirstbyte) {
                if (reqbyte < 0)
                    reqbyte = firstbyte;
                firstbyte = REQ_NONE;
            }
            
            /* If we (now or from before) have no firstbyte, a firstbyte from the
             branch becomes a reqbyte if there isn't a branch reqbyte. */
            
            if (firstbyte < 0 && branchfirstbyte >= 0 && branchreqbyte < 0)
                branchreqbyte = branchfirstbyte;
            
            /* Now ensure that the reqbytes match */
            
            if ((reqbyte & ~REQ_VARY) != (branchreqbyte & ~REQ_VARY))
                reqbyte = REQ_NONE;
            else
                reqbyte |= branchreqbyte;   /* To "or" REQ_VARY */
        }
        
        /* Reached end of expression, either ')' or end of pattern. Go back through
         the alternative branches and reverse the chain of offsets, with the field in
         the BRA item now becoming an offset to the first alternative. If there are
         no alternatives, it points to the end of the group. The length in the
         terminating ket is always the length of the whole bracketed item. If any of
         the ims options were changed inside the group, compile a resetting op-code
         following, except at the very end of the pattern. Return leaving the pointer
         at the terminating char. */
        
        if (ptr >= patternEnd || *ptr != '|') {
            int length = code - last_branch;
            do {
                int prev_length = getLinkValueAllowZero(last_branch + 1);
                putLinkValue(last_branch + 1, length);
                length = prev_length;
                last_branch -= length;
            } while (length > 0);
            
            /* Fill in the ket */
            
            *code = OP_KET;
            putLinkValue(code + 1, code - start_bracket);
            code += 1 + LINK_SIZE;
            
            /* Set values to pass back */
            
            *codeptr = code;
            *ptrptr = ptr;
            *firstbyteptr = firstbyte;
            *reqbyteptr = reqbyte;
            return true;
        }
        
        /* Another branch follows; insert an "or" node. Its length field points back
         to the previous branch while the bracket remains open. At the end the chain
         is reversed. It's done like this so that the start of the bracket has a
         zero offset until it is closed, making it possible to detect recursion. */
        
        *code = OP_ALT;
        putLinkValue(code + 1, code - last_branch);
        last_branch = code;
        code += 1 + LINK_SIZE;
        ptr++;
    }
    ASSERT_NOT_REACHED();
}

/*************************************************
*          Check for anchored expression         *
*************************************************/

/* Try to find out if this is an anchored regular expression. Consider each
alternative branch. If they all start OP_CIRC, or with a bracket
all of whose alternatives start OP_CIRC (recurse ad lib), then
it's anchored.

Arguments:
  code          points to start of expression (the bracket)
  captureMap    a bitmap of which brackets we are inside while testing; this
                 handles up to substring 31; all brackets after that share
                 the zero bit
  backrefMap    the back reference bitmap
*/

static bool branchIsAnchored(const unsigned char* code)
{
    const unsigned char* scode = firstSignificantOpcode(code);
    int op = *scode;

    /* Brackets */
    if (op >= OP_BRA || op == OP_ASSERT)
        return bracketIsAnchored(scode);

    /* Check for explicit anchoring */    
    return op == OP_CIRC;
}

static bool bracketIsAnchored(const unsigned char* code)
{
    do {
        if (!branchIsAnchored(code + 1 + LINK_SIZE))
            return false;
        code += getLinkValue(code + 1);
    } while (*code == OP_ALT);   /* Loop for each alternative */
    return true;
}

/*************************************************
*         Check for starting with ^ or .*        *
*************************************************/

/* This is called to find out if every branch starts with ^ or .* so that
"first char" processing can be done to speed things up in multiline
matching and for non-DOTALL patterns that start with .* (which must start at
the beginning or after \n)

Except when the .* appears inside capturing parentheses, and there is a
subsequent back reference to those parentheses. By keeping a bitmap of the
first 31 back references, we can catch some of the more common cases more
precisely; all the greater back references share a single bit.

Arguments:
  code          points to start of expression (the bracket)
  captureMap    a bitmap of which brackets we are inside while testing; this
                 handles up to substring 31; all brackets after that share
                 the zero bit
  backrefMap    the back reference bitmap
*/

static bool branchNeedsLineStart(const unsigned char* code, unsigned captureMap, unsigned backrefMap)
{
    const unsigned char* scode = firstSignificantOpcode(code);
    int op = *scode;
    
    /* Capturing brackets */
    if (op > OP_BRA) {
        int captureNum = op - OP_BRA;
        if (captureNum > EXTRACT_BASIC_MAX)
            captureNum = get2ByteValue(scode + 2 + LINK_SIZE);
        int bracketMask = (captureNum < 32) ? (1 << captureNum) : 1;
        return bracketNeedsLineStart(scode, captureMap | bracketMask, backrefMap);
    }
    
    /* Other brackets */
    if (op == OP_BRA || op == OP_ASSERT)
        return bracketNeedsLineStart(scode, captureMap, backrefMap);
    
    /* .* means "start at start or after \n" if it isn't in brackets that
     may be referenced. */
    
    if (op == OP_TYPESTAR || op == OP_TYPEMINSTAR)
        return scode[1] == OP_NOT_NEWLINE && !(captureMap & backrefMap);

    /* Explicit ^ */
    return op == OP_CIRC || op == OP_BOL;
}

static bool bracketNeedsLineStart(const unsigned char* code, unsigned captureMap, unsigned backrefMap)
{
    do {
        if (!branchNeedsLineStart(code + 1 + LINK_SIZE, captureMap, backrefMap))
            return false;
        code += getLinkValue(code + 1);
    } while (*code == OP_ALT);  /* Loop for each alternative */
    return true;
}

/*************************************************
*       Check for asserted fixed first char      *
*************************************************/

/* During compilation, the "first char" settings from forward assertions are
discarded, because they can cause conflicts with actual literals that follow.
However, if we end up without a first char setting for an unanchored pattern,
it is worth scanning the regex to see if there is an initial asserted first
char. If all branches start with the same asserted char, or with a bracket all
of whose alternatives start with the same asserted char (recurse ad lib), then
we return that char, otherwise -1.

Arguments:
  code       points to start of expression (the bracket)
  options    pointer to the options (used to check casing changes)
  inassert   true if in an assertion

Returns:     -1 or the fixed first char
*/

static int branchFindFirstAssertedCharacter(const unsigned char* code, bool inassert)
{
    const unsigned char* scode = firstSignificantOpcodeSkippingAssertions(code);
    int op = *scode;
    
    if (op >= OP_BRA)
        op = OP_BRA;
    
    switch (op) {
        default:
            return -1;
            
        case OP_BRA:
        case OP_ASSERT:
            return bracketFindFirstAssertedCharacter(scode, op == OP_ASSERT);

        case OP_EXACT:
            scode += 2;
            /* Fall through */

        case OP_CHAR:
        case OP_CHAR_IGNORING_CASE:
        case OP_ASCII_CHAR:
        case OP_ASCII_LETTER_IGNORING_CASE:
        case OP_PLUS:
        case OP_MINPLUS:
            if (!inassert)
                return -1;
            return scode[1];
    }
}

static int bracketFindFirstAssertedCharacter(const unsigned char* code, bool inassert)
{
    int c = -1;
    do {
        int d = branchFindFirstAssertedCharacter(code + 1 + LINK_SIZE, inassert);
        if (d < 0)
            return -1;
        if (c < 0)
            c = d;
        else if (c != d)
            return -1;
        code += getLinkValue(code + 1);
    } while (*code == OP_ALT);
    return c;
}

static inline int multiplyWithOverflowCheck(int a, int b)
{
    if (!a || !b)
        return 0;
    if (a > MAX_PATTERN_SIZE / b)
        return -1;
    return a * b;
}

static int calculateCompiledPatternLength(const UChar* pattern, int patternLength, JSRegExpIgnoreCaseOption ignoreCase,
    CompileData& cd, ErrorCode& errorcode)
{
    /* Make a pass over the pattern to compute the
     amount of store required to hold the compiled code. This does not have to be
     perfect as long as errors are overestimates. */

    if (patternLength > MAX_PATTERN_SIZE) {
        errorcode = ERR16;
        return -1;
    }

    int length = 1 + LINK_SIZE;      /* For initial BRA plus length */
    int branch_extra = 0;
    int lastitemlength = 0;
    unsigned brastackptr = 0;
    int brastack[BRASTACK_SIZE];
    unsigned char bralenstack[BRASTACK_SIZE];
    int bracount = 0;
    
    const UChar* ptr = (const UChar*)(pattern - 1);
    const UChar* patternEnd = (const UChar*)(pattern + patternLength);
    
    while (++ptr < patternEnd) {
        int minRepeats = 0, maxRepeats = 0;
        int c = *ptr;

        switch (c) {
            /* A backslashed item may be an escaped data character or it may be a
             character type. */

            case '\\':
                c = checkEscape(&ptr, patternEnd, &errorcode, cd.numCapturingBrackets, false);
                if (errorcode != 0)
                    return -1;
                
                lastitemlength = 1;     /* Default length of last item for repeats */
                
                if (c >= 0) {            /* Data character */
                    length += 2;          /* For a one-byte character */
                    
                    if (c > 127) {
                        int i;
                        for (i = 0; i < kjs_pcre_utf8_table1_size; i++)
                            if (c <= kjs_pcre_utf8_table1[i]) break;
                        length += i;
                        lastitemlength += i;
                    }
                    
                    continue;
                }
                
                /* Other escapes need one byte */
                
                length++;
                
                /* A back reference needs an additional 2 bytes, plus either one or 5
                 bytes for a repeat. We also need to keep the value of the highest
                 back reference. */
                
                if (c <= -ESC_REF) {
                    int refnum = -c - ESC_REF;
                    cd.backrefMap |= (refnum < 32) ? (1 << refnum) : 1;
                    if (refnum > cd.top_backref)
                        cd.top_backref = refnum;
                    length += 2;   /* For single back reference */
                    if (safelyCheckNextChar(ptr, patternEnd, '{') && isCountedRepeat(ptr + 2, patternEnd)) {
                        ptr = readRepeatCounts(ptr + 2, &minRepeats, &maxRepeats, &errorcode);
                        if (errorcode)
                            return -1;
                        if ((minRepeats == 0 && (maxRepeats == 1 || maxRepeats == -1)) ||
                            (minRepeats == 1 && maxRepeats == -1))
                            length++;
                        else
                            length += 5;
                        if (safelyCheckNextChar(ptr, patternEnd, '?'))
                            ptr++;
                    }
                }
                continue;
                
            case '^':     /* Single-byte metacharacters */
            case '.':
            case '$':
                length++;
                lastitemlength = 1;
                continue;
                
            case '*':            /* These repeats won't be after brackets; */
            case '+':            /* those are handled separately */
            case '?':
                length++;
                goto POSSESSIVE;
                
            /* This covers the cases of braced repeats after a single char, metachar,
             class, or back reference. */

            case '{':
                if (!isCountedRepeat(ptr + 1, patternEnd))
                    goto NORMAL_CHAR;
                ptr = readRepeatCounts(ptr + 1, &minRepeats, &maxRepeats, &errorcode);
                if (errorcode != 0)
                    return -1;
                
                /* These special cases just insert one extra opcode */
                
                if ((minRepeats == 0 && (maxRepeats == 1 || maxRepeats == -1)) ||
                    (minRepeats == 1 && maxRepeats == -1))
                    length++;
                
                /* These cases might insert additional copies of a preceding character. */
                
                else {
                    if (minRepeats != 1) {
                        length -= lastitemlength;   /* Uncount the original char or metachar */
                        if (minRepeats > 0)
                            length += 3 + lastitemlength;
                    }
                    length += lastitemlength + ((maxRepeats > 0) ? 3 : 1);
                }
                
                if (safelyCheckNextChar(ptr, patternEnd, '?'))
                    ptr++;      /* Needs no extra length */

            POSSESSIVE:                     /* Test for possessive quantifier */
                if (safelyCheckNextChar(ptr, patternEnd, '+')) {
                    ptr++;
                    length += 2 + 2 * LINK_SIZE;   /* Allow for atomic brackets */
                }
                continue;
                
            /* An alternation contains an offset to the next branch or ket. If any ims
             options changed in the previous branch(es), and/or if we are in a
             lookbehind assertion, extra space will be needed at the start of the
             branch. This is handled by branch_extra. */
                
            case '|':
                if (brastackptr == 0)
                    cd.needOuterBracket = true;
                length += 1 + LINK_SIZE + branch_extra;
                continue;
                
            /* A character class uses 33 characters provided that all the character
             values are less than 256. Otherwise, it uses a bit map for low valued
             characters, and individual items for others. Don't worry about character
             types that aren't allowed in classes - they'll get picked up during the
             compile. A character class that contains only one single-byte character
             uses 2 or 3 bytes, depending on whether it is negated or not. Notice this
             where we can. (In UTF-8 mode we can do this only for chars < 128.) */
                
            case '[': {
                int class_optcount;
                if (*(++ptr) == '^') {
                    class_optcount = 10;  /* Greater than one */
                    ptr++;
                }
                else
                    class_optcount = 0;
                
                bool class_utf8 = false;
                
                for (; ptr < patternEnd && *ptr != ']'; ++ptr) {
                    /* Check for escapes */
                    
                    if (*ptr == '\\') {
                        c = checkEscape(&ptr, patternEnd, &errorcode, cd.numCapturingBrackets, true);
                        if (errorcode != 0)
                            return -1;
                        
                        /* Handle escapes that turn into characters */
                        
                        if (c >= 0)
                            goto NON_SPECIAL_CHARACTER;
                        
                        /* Escapes that are meta-things. The normal ones just affect the
                         bit map, but Unicode properties require an XCLASS extended item. */
                        
                        else
                            class_optcount = 10;         /* \d, \s etc; make sure > 1 */
                    }
                    
                    /* Anything else increments the possible optimization count. We have to
                     detect ranges here so that we can compute the number of extra ranges for
                     caseless wide characters when UCP support is available. If there are wide
                     characters, we are going to have to use an XCLASS, even for single
                     characters. */
                    
                    else {
                        c = *ptr;
                        
                        /* Come here from handling \ above when it escapes to a char value */
                        
                    NON_SPECIAL_CHARACTER:
                        class_optcount++;
                        
                        int d = -1;
                        if (safelyCheckNextChar(ptr, patternEnd, '-')) {
                            UChar const *hyptr = ptr++;
                            if (safelyCheckNextChar(ptr, patternEnd, '\\')) {
                                ptr++;
                                d = checkEscape(&ptr, patternEnd, &errorcode, cd.numCapturingBrackets, true);
                                if (errorcode != 0)
                                    return -1;
                            }
                            else if ((ptr + 1 < patternEnd) && ptr[1] != ']')
                                d = *++ptr;
                            if (d < 0)
                                ptr = hyptr;      /* go back to hyphen as data */
                        }
                        
                        /* If d >= 0 we have a range. In UTF-8 mode, if the end is > 255, or >
                         127 for caseless matching, we will need to use an XCLASS. */
                        
                        if (d >= 0) {
                            class_optcount = 10;     /* Ensure > 1 */
                            if (d < c) {
                                errorcode = ERR8;
                                return -1;
                            }
                            
                            if ((d > 255 || (ignoreCase && d > 127))) {
                                unsigned char buffer[6];
                                if (!class_utf8)         /* Allow for XCLASS overhead */
                                {
                                    class_utf8 = true;
                                    length += LINK_SIZE + 2;
                                }
                                
                                /* If we have UCP support, find out how many extra ranges are
                                 needed to map the other case of characters within this range. We
                                 have to mimic the range optimization here, because extending the
                                 range upwards might push d over a boundary that makes it use
                                 another byte in the UTF-8 representation. */
                                
                                if (ignoreCase) {
                                    int occ, ocd;
                                    int cc = c;
                                    int origd = d;
                                    while (getOthercaseRange(&cc, origd, &occ, &ocd)) {
                                        if (occ >= c && ocd <= d)
                                            continue;   /* Skip embedded */
                                        
                                        if (occ < c  && ocd >= c - 1)  /* Extend the basic range */
                                        {                            /* if there is overlap,   */
                                            c = occ;                     /* noting that if occ < c */
                                            continue;                    /* we can't have ocd > d  */
                                        }                            /* because a subrange is  */
                                        if (ocd > d && occ <= d + 1)   /* always shorter than    */
                                        {                            /* the basic range.       */
                                            d = ocd;
                                            continue;
                                        }
                                        
                                        /* An extra item is needed */
                                        
                                        length += 1 + encodeUTF8(occ, buffer) +
                                        ((occ == ocd) ? 0 : encodeUTF8(ocd, buffer));
                                    }
                                }
                                
                                /* The length of the (possibly extended) range */
                                
                                length += 1 + encodeUTF8(c, buffer) + encodeUTF8(d, buffer);
                            }
                            
                        }
                        
                        /* We have a single character. There is nothing to be done unless we
                         are in UTF-8 mode. If the char is > 255, or 127 when caseless, we must
                         allow for an XCL_SINGLE item, doubled for caselessness if there is UCP
                         support. */
                        
                        else {
                            if ((c > 255 || (ignoreCase && c > 127))) {
                                unsigned char buffer[6];
                                class_optcount = 10;     /* Ensure > 1 */
                                if (!class_utf8)         /* Allow for XCLASS overhead */
                                {
                                    class_utf8 = true;
                                    length += LINK_SIZE + 2;
                                }
                                length += (ignoreCase ? 2 : 1) * (1 + encodeUTF8(c, buffer));
                            }
                        }
                    }
                }
                
                if (ptr >= patternEnd) {   /* Missing terminating ']' */
                    errorcode = ERR6;
                    return -1;
                }
                
                /* We can optimize when there was only one optimizable character.
                 Note that this does not detect the case of a negated single character.
                 In that case we do an incorrect length computation, but it's not a serious
                 problem because the computed length is too large rather than too small. */

                if (class_optcount == 1)
                    goto NORMAL_CHAR;

                /* Here, we handle repeats for the class opcodes. */
                {
                    length += 33;
                    
                    /* A repeat needs either 1 or 5 bytes. If it is a possessive quantifier,
                     we also need extra for wrapping the whole thing in a sub-pattern. */
                    
                    if (safelyCheckNextChar(ptr, patternEnd, '{') && isCountedRepeat(ptr + 2, patternEnd)) {
                        ptr = readRepeatCounts(ptr + 2, &minRepeats, &maxRepeats, &errorcode);
                        if (errorcode != 0)
                            return -1;
                        if ((minRepeats == 0 && (maxRepeats == 1 || maxRepeats == -1)) ||
                            (minRepeats == 1 && maxRepeats == -1))
                            length++;
                        else
                            length += 5;
                        if (safelyCheckNextChar(ptr, patternEnd, '+')) {
                            ptr++;
                            length += 2 + 2 * LINK_SIZE;
                        } else if (safelyCheckNextChar(ptr, patternEnd, '?'))
                            ptr++;
                    }
                }
                continue;
            }

            /* Brackets may be genuine groups or special things */
                
            case '(': {
                int branch_newextra = 0;
                int bracket_length = 1 + LINK_SIZE;
                bool capturing = false;
                
                /* Handle special forms of bracket, which all start (? */
                
                if (safelyCheckNextChar(ptr, patternEnd, '?')) {
                    switch (c = (ptr + 2 < patternEnd ? ptr[2] : 0)) {
                        /* Non-referencing groups and lookaheads just move the pointer on, and
                         then behave like a non-special bracket, except that they don't increment
                         the count of extracting brackets. Ditto for the "once only" bracket,
                         which is in Perl from version 5.005. */
                            
                        case ':':
                        case '=':
                        case '!':
                            ptr += 2;
                            break;
                            
                        /* Else loop checking valid options until ) is met. Anything else is an
                         error. If we are without any brackets, i.e. at top level, the settings
                         act as if specified in the options, so massage the options immediately.
                         This is for backward compatibility with Perl 5.004. */
                            
                        default:
                            errorcode = ERR12;
                            return -1;
                    }
                } else
                    capturing = 1;
                
                /* Capturing brackets must be counted so we can process escapes in a
                 Perlish way. If the number exceeds EXTRACT_BASIC_MAX we are going to need
                 an additional 3 bytes of memory per capturing bracket. */
                
                if (capturing) {
                    bracount++;
                    if (bracount > EXTRACT_BASIC_MAX)
                        bracket_length += 3;
                }
                
                /* Save length for computing whole length at end if there's a repeat that
                 requires duplication of the group. Also save the current value of
                 branch_extra, and start the new group with the new value. If non-zero, this
                 will either be 2 for a (?imsx: group, or 3 for a lookbehind assertion. */
                
                if (brastackptr >= sizeof(brastack)/sizeof(int)) {
                    errorcode = ERR17;
                    return -1;
                }
                
                bralenstack[brastackptr] = branch_extra;
                branch_extra = branch_newextra;
                
                brastack[brastackptr++] = length;
                length += bracket_length;
                continue;
            }

            /* Handle ket. Look for subsequent maxRepeats/minRepeats; for certain sets of values we
             have to replicate this bracket up to that many times. If brastackptr is
             0 this is an unmatched bracket which will generate an error, but take care
             not to try to access brastack[-1] when computing the length and restoring
             the branch_extra value. */

            case ')': {
                int duplength;
                length += 1 + LINK_SIZE;
                if (brastackptr > 0) {
                    duplength = length - brastack[--brastackptr];
                    branch_extra = bralenstack[brastackptr];
                }
                else
                    duplength = 0;
                
                /* Leave ptr at the final char; for readRepeatCounts this happens
                 automatically; for the others we need an increment. */
                
                if ((ptr + 1 < patternEnd) && (c = ptr[1]) == '{' && isCountedRepeat(ptr + 2, patternEnd)) {
                    ptr = readRepeatCounts(ptr + 2, &minRepeats, &maxRepeats, &errorcode);
                    if (errorcode)
                        return -1;
                } else if (c == '*') {
                    minRepeats = 0;
                    maxRepeats = -1;
                    ptr++;
                } else if (c == '+') {
                    minRepeats = 1;
                    maxRepeats = -1;
                    ptr++;
                } else if (c == '?') {
                    minRepeats = 0;
                    maxRepeats = 1;
                    ptr++;
                } else {
                    minRepeats = 1;
                    maxRepeats = 1;
                }
                
                /* If the minimum is zero, we have to allow for an OP_BRAZERO before the
                 group, and if the maximum is greater than zero, we have to replicate
                 maxval-1 times; each replication acquires an OP_BRAZERO plus a nesting
                 bracket set. */
                
                int repeatsLength;
                if (minRepeats == 0) {
                    length++;
                    if (maxRepeats > 0) {
                        repeatsLength = multiplyWithOverflowCheck(maxRepeats - 1, duplength + 3 + 2 * LINK_SIZE);
                        if (repeatsLength < 0) {
                            errorcode = ERR16;
                            return -1;
                        }
                        length += repeatsLength;
                        if (length > MAX_PATTERN_SIZE) {
                            errorcode = ERR16;
                            return -1;
                        }
                    }
                }
                
                /* When the minimum is greater than zero, we have to replicate up to
                 minval-1 times, with no additions required in the copies. Then, if there
                 is a limited maximum we have to replicate up to maxval-1 times allowing
                 for a BRAZERO item before each optional copy and nesting brackets for all
                 but one of the optional copies. */
                
                else {
                    repeatsLength = multiplyWithOverflowCheck(minRepeats - 1, duplength);
                    if (repeatsLength < 0) {
                        errorcode = ERR16;
                        return -1;
                    }
                    length += repeatsLength;
                    if (maxRepeats > minRepeats) { /* Need this test as maxRepeats=-1 means no limit */
                        repeatsLength = multiplyWithOverflowCheck(maxRepeats - minRepeats, duplength + 3 + 2 * LINK_SIZE);
                        if (repeatsLength < 0) {
                            errorcode = ERR16;
                            return -1;
                        }
                        length += repeatsLength - (2 + 2 * LINK_SIZE);
                    }
                    if (length > MAX_PATTERN_SIZE) {
                        errorcode = ERR16;
                        return -1;
                    }
                }
                
                /* Allow space for once brackets for "possessive quantifier" */
                
                if (safelyCheckNextChar(ptr, patternEnd, '+')) {
                    ptr++;
                    length += 2 + 2 * LINK_SIZE;
                }
                continue;
            }

            /* Non-special character. It won't be space or # in extended mode, so it is
             always a genuine character. If we are in a \Q...\E sequence, check for the
             end; if not, we have a literal. */
                
            default:
            NORMAL_CHAR:
                length += 2;          /* For a one-byte character */
                lastitemlength = 1;   /* Default length of last item for repeats */

                if (c > 127) {
                    int i;
                    for (i = 0; i < kjs_pcre_utf8_table1_size; i++)
                        if (c <= kjs_pcre_utf8_table1[i])
                            break;
                    length += i;
                    lastitemlength += i;
                }
                
                continue;
        }
    }
    
    length += 2 + LINK_SIZE;    /* For final KET and END */

    cd.numCapturingBrackets = bracount;
    return length;
}

/*************************************************
*        Compile a Regular Expression            *
*************************************************/

/* This function takes a string and returns a pointer to a block of store
holding a compiled version of the expression. The original API for this
function had no error code return variable; it is retained for backwards
compatibility. The new function is given a new name.

Arguments:
  pattern       the regular expression
  options       various option bits
  errorcodeptr  pointer to error code variable (pcre_compile2() only)
                  can be NULL if you don't want a code value
  errorptr      pointer to pointer to error text
  erroroffset   ptr offset in pattern where error was detected
  tables        pointer to character tables or NULL

Returns:        pointer to compiled data block, or NULL on error,
                with errorptr and erroroffset set
*/

static inline JSRegExp* returnError(ErrorCode errorcode, const char** errorptr)
{
    *errorptr = errorText(errorcode);
    return 0;
}

JSRegExp* jsRegExpCompile(const UChar* pattern, int patternLength,
                JSRegExpIgnoreCaseOption ignoreCase, JSRegExpMultilineOption multiline,
                unsigned* numSubpatterns, const char** errorptr,
                malloc_t* allocate_function, free_t* free_function)
{
    /* We can't pass back an error message if errorptr is NULL; I guess the best we
     can do is just return NULL, but we can set a code value if there is a code pointer. */
    if (!errorptr)
        return 0;
    *errorptr = NULL;
    
    CompileData cd;
    
    ErrorCode errorcode = ERR0;
    /* Call this once just to count the brackets. */
    calculateCompiledPatternLength(pattern, patternLength, ignoreCase, cd, errorcode);
    /* Call it again to compute the length. */
    int length = calculateCompiledPatternLength(pattern, patternLength, ignoreCase, cd, errorcode);
    if (errorcode)
        return returnError(errorcode, errorptr);
    
    if (length > MAX_PATTERN_SIZE)
        return returnError(ERR16, errorptr);
    
    size_t size = length + sizeof(JSRegExp);
    JSRegExp* re = reinterpret_cast<JSRegExp*>((*allocate_function)(size));
    
    if (!re)
        return returnError(ERR13, errorptr);
    
    re->options = (ignoreCase ? IgnoreCaseOption : 0) | (multiline ? MatchAcrossMultipleLinesOption : 0);
    
    /* The starting points of the name/number translation table and of the code are
     passed around in the compile data block. */
    
    const unsigned char* codeStart = (const unsigned char*)(re + 1);
    
    /* Set up a starting, non-extracting bracket, then compile the expression. On
     error, errorcode will be set non-zero, so we don't need to look at the result
     of the function here. */
    
    const UChar* ptr = (const UChar*)pattern;
    const UChar* patternEnd = pattern + patternLength;
    unsigned char* code = (unsigned char*)codeStart;
    int firstbyte, reqbyte;
    int bracketCount = 0;
    if (!cd.needOuterBracket)
        compileBranch(re->options, &bracketCount, &code, &ptr, patternEnd, &errorcode, &firstbyte, &reqbyte, cd);
    else {
        *code = OP_BRA;
        compileBracket(re->options, &bracketCount, &code, &ptr, patternEnd, &errorcode, 0, &firstbyte, &reqbyte, cd);
    }
    re->top_bracket = bracketCount;
    re->top_backref = cd.top_backref;
    
    /* If not reached end of pattern on success, there's an excess bracket. */
    
    if (errorcode == 0 && ptr < patternEnd)
        errorcode = ERR10;
    
    /* Fill in the terminating state and check for disastrous overflow, but
     if debugging, leave the test till after things are printed out. */
    
    *code++ = OP_END;

    ASSERT(code - codeStart <= length);
    if (code - codeStart > length)
        errorcode = ERR7;
    
    /* Give an error if there's back reference to a non-existent capturing
     subpattern. */
    
    if (re->top_backref > re->top_bracket)
        errorcode = ERR15;
    
    /* Failed to compile, or error while post-processing */
    
    if (errorcode != ERR0) {
        (*free_function)(reinterpret_cast<void*>(re));
        return returnError(errorcode, errorptr);
    }
    
    /* If the anchored option was not passed, set the flag if we can determine that
     the pattern is anchored by virtue of ^ characters or \A or anything else (such
     as starting with .* when DOTALL is set).
     
     Otherwise, if we know what the first character has to be, save it, because that
     speeds up unanchored matches no end. If not, see if we can set the
     UseMultiLineFirstByteOptimizationOption flag. This is helpful for multiline matches when all branches
     start with ^. and also when all branches start with .* for non-DOTALL matches.
     */
    
    if (cd.needOuterBracket ? bracketIsAnchored(codeStart) : branchIsAnchored(codeStart))
        re->options |= IsAnchoredOption;
    else {
        if (firstbyte < 0) {
            firstbyte = (cd.needOuterBracket
                    ? bracketFindFirstAssertedCharacter(codeStart, false)
                    : branchFindFirstAssertedCharacter(codeStart, false))
                | ((re->options & IgnoreCaseOption) ? REQ_IGNORE_CASE : 0);
        }
        if (firstbyte >= 0) {
            int ch = firstbyte & 255;
            if (ch < 127) {
                re->first_byte = ((firstbyte & REQ_IGNORE_CASE) && flipCase(ch) == ch) ? ch : firstbyte;
                re->options |= UseFirstByteOptimizationOption;
            }
        } else {
            if (cd.needOuterBracket ? bracketNeedsLineStart(codeStart, 0, cd.backrefMap) : branchNeedsLineStart(codeStart, 0, cd.backrefMap))
                re->options |= UseMultiLineFirstByteOptimizationOption;
        }
    }
    
    /* For an anchored pattern, we use the "required byte" only if it follows a
     variable length item in the regex. Remove the caseless flag for non-caseable
     bytes. */
    
    if (reqbyte >= 0 && (!(re->options & IsAnchoredOption) || (reqbyte & REQ_VARY))) {
        int ch = reqbyte & 255;
        if (ch < 127) {
            re->req_byte = ((reqbyte & REQ_IGNORE_CASE) && flipCase(ch) == ch) ? (reqbyte & ~REQ_IGNORE_CASE) : reqbyte;
            re->options |= UseRequiredByteOptimizationOption;
        }
    }
    
    if (numSubpatterns)
        *numSubpatterns = re->top_bracket;
    return re;
}

void jsRegExpFree(JSRegExp* re, free_t* free_function)
{
    (*free_function)(reinterpret_cast<void*>(re));
}
