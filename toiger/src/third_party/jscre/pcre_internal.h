/* This is JavaScriptCore's variant of the PCRE library. While this library
started out as a copy of PCRE, many of the features of PCRE have been
removed. This library now supports only the regular expression features
required by the JavaScript language specification, and has only the functions
needed by JavaScriptCore and the rest of WebKit.

                 Originally written by Philip Hazel
           Copyright (c) 1997-2006 University of Cambridge
    Copyright (C) 2002, 2004, 2006, 2007 Apple Inc. All rights reserved.

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

/* This header contains definitions that are shared between the different
modules, but which are not relevant to the exported API. This includes some
functions whose names all begin with "_pcre_". */

#ifndef PCRE_INTERNAL_H
#define PCRE_INTERNAL_H

/* Bit definitions for entries in the pcre_ctypes table. */

#define ctype_space   0x01
#define ctype_xdigit  0x08
#define ctype_word    0x10   /* alphameric or '_' */

/* Offsets for the bitmap tables in pcre_cbits. Each table contains a set
of bits for a class map. Some classes are built by combining these tables. */

#define cbit_space     0      /* \s */
#define cbit_digit    32      /* \d */
#define cbit_word     64      /* \w */
#define cbit_length   96      /* Length of the cbits table */

/* Offsets of the various tables from the base tables pointer, and
total length. */

#define lcc_offset      0
#define fcc_offset    128
#define cbits_offset  256
#define ctypes_offset (cbits_offset + cbit_length)
#define tables_length (ctypes_offset + 128)

#ifndef DFTABLES

// TODO: Hook this up to something that checks assertions.
#define ASSERT(x) do { } while(0)
#define ASSERT_NOT_REACHED() do {} while(0)

#ifdef WIN32
#pragma warning(disable: 4232)
#pragma warning(disable: 4244)
#endif

#include "pcre.h"

/* The value of LINK_SIZE determines the number of bytes used to store links as
offsets within the compiled regex. The default is 2, which allows for compiled
patterns up to 64K long. */

#define LINK_SIZE   2

/* Define DEBUG to get debugging output on stdout. */

#if 0
#define DEBUG
#endif

/* Use a macro for debugging printing, 'cause that eliminates the use of #ifdef
inline, and there are *still* stupid compilers about that don't like indented
pre-processor statements, or at least there were when I first wrote this. After
all, it had only been about 10 years then... */

#ifdef DEBUG
#define DPRINTF(p) printf p
#else
#define DPRINTF(p) /*nothing*/
#endif

namespace v8 { namespace jscre {

/* PCRE keeps offsets in its compiled code as 2-byte quantities (always stored
in big-endian order) by default. These are used, for example, to link from the
start of a subpattern to its alternatives and its end. The use of 2 bytes per
offset limits the size of the compiled regex to around 64K, which is big enough
for almost everybody. However, I received a request for an even bigger limit.
For this reason, and also to make the code easier to maintain, the storing and
loading of offsets from the byte string is now handled by the functions that are
defined here. */

/* PCRE uses some other 2-byte quantities that do not change when the size of
offsets changes. There are used for repeat counts and for other things such as
capturing parenthesis numbers in back references. */

static inline void put2ByteValue(unsigned char* opcodePtr, int value)
{
    ASSERT(value >= 0 && value <= 0xFFFF);
    opcodePtr[0] = value >> 8;
    opcodePtr[1] = value;
}

static inline int get2ByteValue(const unsigned char* opcodePtr)
{
    return (opcodePtr[0] << 8) | opcodePtr[1];
}

static inline void put2ByteValueAndAdvance(unsigned char*& opcodePtr, int value)
{
    put2ByteValue(opcodePtr, value);
    opcodePtr += 2;
}

static inline void putLinkValueAllowZero(unsigned char* opcodePtr, int value)
{
    put2ByteValue(opcodePtr, value);
}

static inline int getLinkValueAllowZero(const unsigned char* opcodePtr)
{
    return get2ByteValue(opcodePtr);
}

#define MAX_PATTERN_SIZE (1 << 16)

static inline void putLinkValue(unsigned char* opcodePtr, int value)
{
    ASSERT(value);
    putLinkValueAllowZero(opcodePtr, value);
}

static inline int getLinkValue(const unsigned char* opcodePtr)
{
    int value = getLinkValueAllowZero(opcodePtr);
    ASSERT(value);
    return value;
}

static inline void putLinkValueAndAdvance(unsigned char*& opcodePtr, int value)
{
    putLinkValue(opcodePtr, value);
    opcodePtr += LINK_SIZE;
}

static inline void putLinkValueAllowZeroAndAdvance(unsigned char*& opcodePtr, int value)
{
    putLinkValueAllowZero(opcodePtr, value);
    opcodePtr += LINK_SIZE;
}

// FIXME: These are really more of a "compiled regexp state" than "regexp options"
enum RegExpOptions {
    UseFirstByteOptimizationOption = 0x40000000,  /* first_byte is set */
    UseRequiredByteOptimizationOption = 0x20000000,  /* req_byte is set */
    UseMultiLineFirstByteOptimizationOption = 0x10000000,  /* start after \n for multiline */
    IsAnchoredOption = 0x02000000,  /* can't use partial with this regex */
    IgnoreCaseOption = 0x00000001,
    MatchAcrossMultipleLinesOption = 0x00000002
};

/* Flags added to firstbyte or reqbyte; a "non-literal" item is either a
variable-length repeat, or a anything other than literal characters. */

#define REQ_IGNORE_CASE 0x0100    /* indicates should ignore case */
#define REQ_VARY     0x0200    /* reqbyte followed non-literal item */

/* Miscellaneous definitions */

/* Flag bits and data types for the extended class (OP_XCLASS) for classes that
contain UTF-8 characters with values greater than 255. */

#define XCL_NOT    0x01    /* Flag: this is a negative class */
#define XCL_MAP    0x02    /* Flag: a 32-byte map is present */

#define XCL_END       0    /* Marks end of individual items */
#define XCL_SINGLE    1    /* Single item (one multibyte char) follows */
#define XCL_RANGE     2    /* A range (two multibyte chars) follows */

/* These are escaped items that aren't just an encoding of a particular data
value such as \n. They must have non-zero values, as check_escape() returns
their negation. Also, they must appear in the same order as in the opcode
definitions below, up to ESC_w. The final one must be
ESC_REF as subsequent values are used for \1, \2, \3, etc. There is are two
tests in the code for an escape > ESC_b and <= ESC_w to
detect the types that may be repeated. These are the types that consume
characters. If any new escapes are put in between that don't consume a
character, that code will have to change. */

enum { ESC_B = 1, ESC_b, ESC_D, ESC_d, ESC_S, ESC_s, ESC_W, ESC_w, ESC_REF };

/* Opcode table: OP_BRA must be last, as all values >= it are used for brackets
that extract substrings. Starting from 1 (i.e. after OP_END), the values up to
OP_EOD must correspond in order to the list of escapes immediately above.
Note that whenever this list is updated, the two macro definitions that follow
must also be updated to match. */

#define FOR_EACH_OPCODE(macro) \
    macro(END) \
    \
    macro(NOT_WORD_BOUNDARY) \
    macro(WORD_BOUNDARY) \
    macro(NOT_DIGIT) \
    macro(DIGIT) \
    macro(NOT_WHITESPACE) \
    macro(WHITESPACE) \
    macro(NOT_WORDCHAR) \
    macro(WORDCHAR) \
    \
    macro(NOT_NEWLINE) \
    \
    macro(CIRC) \
    macro(DOLL) \
    macro(BOL) \
    macro(EOL) \
    macro(CHAR) \
    macro(CHAR_IGNORING_CASE) \
    macro(ASCII_CHAR) \
    macro(ASCII_LETTER_IGNORING_CASE) \
    macro(NOT) \
    \
    macro(STAR) \
    macro(MINSTAR) \
    macro(PLUS) \
    macro(MINPLUS) \
    macro(QUERY) \
    macro(MINQUERY) \
    macro(UPTO) \
    macro(MINUPTO) \
    macro(EXACT) \
    \
    macro(NOTSTAR) \
    macro(NOTMINSTAR) \
    macro(NOTPLUS) \
    macro(NOTMINPLUS) \
    macro(NOTQUERY) \
    macro(NOTMINQUERY) \
    macro(NOTUPTO) \
    macro(NOTMINUPTO) \
    macro(NOTEXACT) \
    \
    macro(TYPESTAR) \
    macro(TYPEMINSTAR) \
    macro(TYPEPLUS) \
    macro(TYPEMINPLUS) \
    macro(TYPEQUERY) \
    macro(TYPEMINQUERY) \
    macro(TYPEUPTO) \
    macro(TYPEMINUPTO) \
    macro(TYPEEXACT) \
    \
    macro(CRSTAR) \
    macro(CRMINSTAR) \
    macro(CRPLUS) \
    macro(CRMINPLUS) \
    macro(CRQUERY) \
    macro(CRMINQUERY) \
    macro(CRRANGE) \
    macro(CRMINRANGE) \
    \
    macro(CLASS) \
    macro(NCLASS) \
    macro(XCLASS) \
    \
    macro(REF) \
    \
    macro(ALT) \
    macro(KET) \
    macro(KETRMAX) \
    macro(KETRMIN) \
    \
    macro(ASSERT) \
    macro(ASSERT_NOT) \
    \
    macro(BRAZERO) \
    macro(BRAMINZERO) \
    macro(BRANUMBER) \
    macro(BRA)

#define OPCODE_ENUM_VALUE(opcode) OP_##opcode,
enum { FOR_EACH_OPCODE(OPCODE_ENUM_VALUE) };

/* WARNING WARNING WARNING: There is an implicit assumption in pcre.c and
study.c that all opcodes are less than 128 in value. This makes handling UTF-8
character sequences easier. */

/* The highest extraction number before we have to start using additional
bytes. (Originally PCRE didn't have support for extraction counts higher than
this number.) The value is limited by the number of opcodes left after OP_BRA,
i.e. 255 - OP_BRA. We actually set it a bit lower to leave room for additional
opcodes. */

/* FIXME: Note that OP_BRA + 100 is > 128, so the two comments above
are in conflict! */

#define EXTRACT_BASIC_MAX  100

/* The index of names and the
code vector run on as long as necessary after the end. We store an explicit
offset to the name table so that if a regex is compiled on one host, saved, and
then run on another where the size of pointers is different, all might still
be well. For the case of compiled-on-4 and run-on-8, we include an extra
pointer that is always NULL.
*/

struct JSRegExp {
    unsigned options;

    unsigned short top_bracket;
    unsigned short top_backref;
    
    unsigned short first_byte;
    unsigned short req_byte;
};

/* Internal shared data tables. These are tables that are used by more than one
 of the exported public functions. They have to be "external" in the C sense,
 but are not part of the PCRE public API. The data for these tables is in the
 pcre_tables.c module. */

#define kjs_pcre_utf8_table1_size 6

extern const int    kjs_pcre_utf8_table1[6];
extern const int    kjs_pcre_utf8_table2[6];
extern const int    kjs_pcre_utf8_table3[6];
extern const unsigned char kjs_pcre_utf8_table4[0x40];

extern const unsigned char kjs_pcre_default_tables[tables_length];

static inline unsigned char toLowerCase(unsigned char c)
{
    static const unsigned char* lowerCaseChars = kjs_pcre_default_tables + lcc_offset;
    return lowerCaseChars[c];
}

static inline unsigned char flipCase(unsigned char c)
{
    static const unsigned char* flippedCaseChars = kjs_pcre_default_tables + fcc_offset;
    return flippedCaseChars[c];
}

static inline unsigned char classBitmapForChar(unsigned char c)
{
    static const unsigned char* charClassBitmaps = kjs_pcre_default_tables + cbits_offset;
    return charClassBitmaps[c];
}

static inline unsigned char charTypeForChar(unsigned char c)
{
    const unsigned char* charTypeMap = kjs_pcre_default_tables + ctypes_offset;
    return charTypeMap[c];
}

static inline bool isWordChar(UChar c)
{
    return c < 128 && (charTypeForChar(c) & ctype_word);
}

static inline bool isSpaceChar(UChar c)
{
    return (c < 128 && (charTypeForChar(c) & ctype_space));
}

static inline bool isNewline(UChar nl)
{
    return (nl == 0xA || nl == 0xD || nl == 0x2028 || nl == 0x2029);
}

static inline bool isBracketStartOpcode(unsigned char opcode)
{
    if (opcode >= OP_BRA)
        return true;
    switch (opcode) {
        case OP_ASSERT:
        case OP_ASSERT_NOT:
            return true;
        default:
            return false;
    }
}

static inline void advanceToEndOfBracket(const unsigned char*& opcodePtr)
{
    ASSERT(isBracketStartOpcode(*opcodePtr) || *opcodePtr == OP_ALT);
    do
        opcodePtr += getLinkValue(opcodePtr + 1);
    while (*opcodePtr == OP_ALT);
}

/* Internal shared functions. These are functions that are used in more
that one of the source files. They have to have external linkage, but
but are not part of the public API and so not exported from the library. */

extern int kjs_pcre_ucp_othercase(unsigned);
extern bool kjs_pcre_xclass(int, const unsigned char*);

} }  // namespace v8::jscre
#endif

#endif

/* End of pcre_internal.h */
