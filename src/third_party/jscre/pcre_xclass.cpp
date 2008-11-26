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

/* This module contains an internal function that is used to match an extended
class (one that contains characters whose values are > 255). */

#include "pcre_internal.h"

namespace v8 { namespace jscre {

/*************************************************
*       Match character against an XCLASS        *
*************************************************/

/* This function is called to match a character against an extended class that
might contain values > 255.

Arguments:
  c           the character
  data        points to the flag byte of the XCLASS data

Returns:      true if character matches, else false
*/

/* Get the next UTF-8 character, advancing the pointer. This is called when we
 know we are in UTF-8 mode. */

static inline void getUTF8CharAndAdvancePointer(int& c, const unsigned char*& subjectPtr)
{
    c = *subjectPtr++;
    if ((c & 0xc0) == 0xc0) {
        int gcaa = kjs_pcre_utf8_table4[c & 0x3f];  /* Number of additional bytes */
        int gcss = 6 * gcaa;
        c = (c & kjs_pcre_utf8_table3[gcaa]) << gcss;
        while (gcaa-- > 0) {
            gcss -= 6;
            c |= (*subjectPtr++ & 0x3f) << gcss;
        }
    }
}

bool kjs_pcre_xclass(int c, const unsigned char* data)
{
    bool negated = (*data & XCL_NOT);
    
    /* Character values < 256 are matched against a bitmap, if one is present. If
     not, we still carry on, because there may be ranges that start below 256 in the
     additional data. */
    
    if (c < 256) {
        if ((*data & XCL_MAP) != 0 && (data[1 + c/8] & (1 << (c&7))) != 0)
            return !negated;   /* char found */
    }
    
    /* First skip the bit map if present. Then match against the list of Unicode
     properties or large chars or ranges that end with a large char. We won't ever
     encounter XCL_PROP or XCL_NOTPROP when UCP support is not compiled. */
    
    if ((*data++ & XCL_MAP) != 0)
        data += 32;
    
    int t;
    while ((t = *data++) != XCL_END) {
        if (t == XCL_SINGLE) {
            int x;
            getUTF8CharAndAdvancePointer(x, data);
            if (c == x)
                return !negated;
        }
        else if (t == XCL_RANGE) {
            int x, y;
            getUTF8CharAndAdvancePointer(x, data);
            getUTF8CharAndAdvancePointer(y, data);
            if (c >= x && c <= y)
                return !negated;
        }
    }
    
    return negated;   /* char did not match */
}

} }  // namespace v8::jscre
