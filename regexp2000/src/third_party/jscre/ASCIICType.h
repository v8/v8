/*
 * Copyright (C) 2007, 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer. 
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution. 
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WTF_ASCIICType_h
#define WTF_ASCIICType_h

// The behavior of many of the functions in the <ctype.h> header is dependent
// on the current locale. But in the WebKit project, all uses of those functions
// are in code processing something that's not locale-specific. These equivalents
// for some of the <ctype.h> functions are named more explicitly, not dependent
// on the C library locale, and we should also optimize them as needed.

// All functions return false or leave the character unchanged if passed a character
// that is outside the range 0-7F. So they can be used on Unicode strings or
// characters if the intent is to do processing only if the character is ASCII.

    inline bool isASCIIAlpha(char c) { return (c | 0x20) >= 'a' && (c | 0x20) <= 'z'; }
    inline bool isASCIIAlpha(unsigned short c) { return (c | 0x20) >= 'a' && (c | 0x20) <= 'z'; }
    inline bool isASCIIAlpha(int c) { return (c | 0x20) >= 'a' && (c | 0x20) <= 'z'; }

    inline bool isASCIIAlphanumeric(char c) { return c >= '0' && c <= '9' || (c | 0x20) >= 'a' && (c | 0x20) <= 'z'; }
    inline bool isASCIIAlphanumeric(unsigned short c) { return c >= '0' && c <= '9' || (c | 0x20) >= 'a' && (c | 0x20) <= 'z'; }
    inline bool isASCIIAlphanumeric(int c) { return c >= '0' && c <= '9' || (c | 0x20) >= 'a' && (c | 0x20) <= 'z'; }

    inline bool isASCIIDigit(char c) { return (c >= '0') & (c <= '9'); }
    inline bool isASCIIDigit(unsigned short c) { return (c >= '0') & (c <= '9'); }
    inline bool isASCIIDigit(int c) { return (c >= '0') & (c <= '9'); }

    inline bool isASCIIHexDigit(char c) { return c >= '0' && c <= '9' || (c | 0x20) >= 'a' && (c | 0x20) <= 'f'; }
    inline bool isASCIIHexDigit(unsigned short c) { return c >= '0' && c <= '9' || (c | 0x20) >= 'a' && (c | 0x20) <= 'f'; }
    inline bool isASCIIHexDigit(int c) { return c >= '0' && c <= '9' || (c | 0x20) >= 'a' && (c | 0x20) <= 'f'; }

    inline bool isASCIILower(char c) { return c >= 'a' && c <= 'z'; }
    inline bool isASCIILower(unsigned short c) { return c >= 'a' && c <= 'z'; }
    inline bool isASCIILower(int c) { return c >= 'a' && c <= 'z'; }

    inline bool isASCIIUpper(char c) { return c >= 'A' && c <= 'Z'; }
    inline bool isASCIIUpper(unsigned short c) { return c >= 'A' && c <= 'Z'; }
    inline bool isASCIIUpper(int c) { return c >= 'A' && c <= 'Z'; }

    /*
        Statistics from a run of Apple's page load test for callers of isASCIISpace:

            character          count
            ---------          -----
            non-spaces         689383
        20  space              294720
        0A  \n                 89059
        09  \t                 28320
        0D  \r                 0
        0C  \f                 0
        0B  \v                 0
    */
    inline bool isASCIISpace(char c) { return c <= ' ' && (c == ' ' || (c <= 0xD && c >= 0x9)); }
    inline bool isASCIISpace(unsigned short c) { return c <= ' ' && (c == ' ' || (c <= 0xD && c >= 0x9)); }
    inline bool isASCIISpace(int c) { return c <= ' ' && (c == ' ' || (c <= 0xD && c >= 0x9)); }

    inline char toASCIILower(char c) { return c | ((c >= 'A' && c <= 'Z') << 5); }
    inline unsigned short toASCIILower(unsigned short c) { return c | ((c >= 'A' && c <= 'Z') << 5); }
    inline int toASCIILower(int c) { return c | ((c >= 'A' && c <= 'Z') << 5); }

    inline char toASCIIUpper(char c) { return static_cast<char>(c & ~((c >= 'a' && c <= 'z') << 5)); }
    inline unsigned short toASCIIUpper(unsigned short c) { return static_cast<unsigned short>(c & ~((c >= 'a' && c <= 'z') << 5)); }
    inline int toASCIIUpper(int c) { return static_cast<int>(c & ~((c >= 'a' && c <= 'z') << 5)); }

    inline int toASCIIHexValue(char c) { ASSERT(isASCIIHexDigit(c)); return c < 'A' ? c - '0' : (c - 'A' + 10) & 0xF; }
    inline int toASCIIHexValue(unsigned short c) { ASSERT(isASCIIHexDigit(c)); return c < 'A' ? c - '0' : (c - 'A' + 10) & 0xF; }
    inline int toASCIIHexValue(int c) { ASSERT(isASCIIHexDigit(c)); return c < 'A' ? c - '0' : (c - 'A' + 10) & 0xF; }

#endif
