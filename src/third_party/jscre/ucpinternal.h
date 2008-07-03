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

/*************************************************
*           Unicode Property Table handler       *
*************************************************/

/* Internal header file defining the layout of the bits in each pair of 32-bit
words that form a data item in the table. */

typedef struct cnode {
  unsigned f0;
  unsigned f1;
} cnode;

/* Things for the f0 field */

#define f0_scriptmask   0xff000000  /* Mask for script field */
#define f0_scriptshift          24  /* Shift for script value */
#define f0_rangeflag    0x00f00000  /* Flag for a range item */
#define f0_charmask     0x001fffff  /* Mask for code point value */

/* Things for the f1 field */

#define f1_typemask     0xfc000000  /* Mask for char type field */
#define f1_typeshift            26  /* Shift for the type field */
#define f1_rangemask    0x0000ffff  /* Mask for a range offset */
#define f1_casemask     0x0000ffff  /* Mask for a case offset */
#define f1_caseneg      0xffff8000  /* Bits for negation */

/* The data consists of a vector of structures of type cnode. The two unsigned
32-bit integers are used as follows:

(f0) (1) The most significant byte holds the script number. The numbers are
         defined by the enum in ucp.h.

     (2) The 0x00800000 bit is set if this entry defines a range of characters.
         It is not set if this entry defines a single character

     (3) The 0x00600000 bits are spare.

     (4) The 0x001fffff bits contain the code point. No Unicode code point will
         ever be greater than 0x0010ffff, so this should be OK for ever.

(f1) (1) The 0xfc000000 bits contain the character type number. The numbers are
         defined by an enum in ucp.h.

     (2) The 0x03ff0000 bits are spare.

     (3) The 0x0000ffff bits contain EITHER the unsigned offset to the top of
         range if this entry defines a range, OR the *signed* offset to the
         character's "other case" partner if this entry defines a single
         character. There is no partner if the value is zero.

-------------------------------------------------------------------------------
| script (8) |.|.|.| codepoint (21) || type (6) |.|.| spare (8) | offset (16) |
-------------------------------------------------------------------------------
              | | |                              | |
              | | |-> spare                      | |-> spare
              | |                                |
              | |-> spare                        |-> spare
              |
              |-> range flag

The upper/lower casing information is set only for characters that come in
pairs. The non-one-to-one mappings in the Unicode data are ignored.

When searching the data, proceed as follows:

(1) Set up for a binary chop search.

(2) If the top is not greater than the bottom, the character is not in the
    table. Its type must therefore be "Cn" ("Undefined").

(3) Find the middle vector element.

(4) Extract the code point and compare. If equal, we are done.

(5) If the test character is smaller, set the top to the current point, and
    goto (2).

(6) If the current entry defines a range, compute the last character by adding
    the offset, and see if the test character is within the range. If it is,
    we are done.

(7) Otherwise, set the bottom to one element past the current point and goto
    (2).
*/

/* End of ucpinternal.h */
