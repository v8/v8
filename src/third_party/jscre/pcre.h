/* This is the public header file for JavaScriptCore's variant of the PCRE
library. While this library started out as a copy of PCRE, many of the
features of PCRE have been removed. This library now supports only the
regular expression features required by the JavaScript language
specification, and has only the functions needed by JavaScriptCore and the
rest of WebKit.

           Copyright (c) 1997-2005 University of Cambridge
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

// FIXME: This file needs to be renamed to JSRegExp.h; it's no longer PCRE.

#ifndef JSRegExp_h
#define JSRegExp_h

#include "../../../public/v8.h"

typedef uint16_t UChar;

struct JSRegExp;

enum JSRegExpIgnoreCaseOption { JSRegExpDoNotIgnoreCase, JSRegExpIgnoreCase };
enum JSRegExpMultilineOption { JSRegExpSingleLine, JSRegExpMultiline };

/* jsRegExpExecute error codes */
const int JSRegExpErrorNoMatch = -1;
const int JSRegExpErrorHitLimit = -2;
const int JSRegExpErrorNoMemory = -3;
const int JSRegExpErrorInternal = -4;

typedef void* malloc_t(size_t size);
typedef void free_t(void* address);

JSRegExp* jsRegExpCompile(const UChar* pattern, int patternLength,
    JSRegExpIgnoreCaseOption, JSRegExpMultilineOption,
    unsigned* numSubpatterns, const char** errorMessage,
    malloc_t* allocate_function, free_t* free_function);

int jsRegExpExecute(const JSRegExp*,
    const UChar* subject, int subjectLength, int startOffset,
    int* offsetsVector, int offsetsVectorLength);

void jsRegExpFree(JSRegExp*);

#endif
