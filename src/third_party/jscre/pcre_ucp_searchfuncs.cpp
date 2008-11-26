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


/* This module contains code for searching the table of Unicode character
properties. */

#include "pcre_internal.h"

#include "ucpinternal.h"       /* Internal table details */
#include "ucptable.cpp"        /* The table itself */

namespace v8 { namespace jscre {

/*************************************************
*       Search table and return other case       *
*************************************************/

/* If the given character is a letter, and there is another case for the
letter, return the other case. Otherwise, return -1.

Arguments:
  c           the character value

Returns:      the other case or -1 if none
*/

int kjs_pcre_ucp_othercase(unsigned c)
{
    int bot = 0;
    int top = sizeof(ucp_table) / sizeof(cnode);
    int mid;
    
    /* The table is searched using a binary chop. You might think that using
     intermediate variables to hold some of the common expressions would speed
     things up, but tests with gcc 3.4.4 on Linux showed that, on the contrary, it
     makes things a lot slower. */
    
    for (;;) {
        if (top <= bot)
            return -1;
        mid = (bot + top) >> 1;
        if (c == (ucp_table[mid].f0 & f0_charmask))
            break;
        if (c < (ucp_table[mid].f0 & f0_charmask))
            top = mid;
        else {
            if ((ucp_table[mid].f0 & f0_rangeflag) && (c <= (ucp_table[mid].f0 & f0_charmask) + (ucp_table[mid].f1 & f1_rangemask)))
                break;
            bot = mid + 1;
        }
    }
    
    /* Found an entry in the table. Return -1 for a range entry. Otherwise return
     the other case if there is one, else -1. */
    
    if (ucp_table[mid].f0 & f0_rangeflag)
        return -1;
    
    int offset = ucp_table[mid].f1 & f1_casemask;
    if (offset & f1_caseneg)
        offset |= f1_caseneg;
    return !offset ? -1 : c + offset;
}

} }  // namespace v8::jscre
