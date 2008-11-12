// Copyright 2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#ifndef V8_BYTECODES_RE2K_H_
#define V8_BYTECODES_RE2K_H_

namespace v8 { namespace internal {

#define BYTECODE_ITERATOR(V)    \
V(BREAK,              0, 1) /* break                                        */ \
V(PUSH_CP,            1, 5) /* push_cp offset32                             */ \
V(PUSH_BT,            2, 5) /* push_bt addr32                               */ \
V(PUSH_REGISTER,      3, 2) /* push_register register_index                 */ \
V(SET_REGISTER_TO_CP, 4, 6) /* set_register_to_cp register_index offset32   */ \
V(SET_REGISTER,       5, 6) /* set_register register_index value32          */ \
V(ADVANCE_REGISTER,   6, 6) /* advance_register register_index value32      */ \
V(POP_CP,             7, 1) /* pop_cp                                       */ \
V(POP_BT,             8, 1) /* pop_bt                                       */ \
V(POP_REGISTER,       9, 2) /* pop_register register_index                  */ \
V(FAIL,              10, 1) /* fail                                         */ \
V(SUCCEED,           11, 1) /* succeed                                      */ \
V(ADVANCE_CP,        12, 5) /* advance_cp offset32                          */ \
V(GOTO,              13, 5) /* goto addr32                                  */ \
V(LOAD_CURRENT_CHAR, 14, 9) /* load offset32 addr32                         */ \
V(CHECK_CHAR,        15, 7) /* check_char uc16 addr32                       */ \
V(CHECK_NOT_CHAR,    16, 7) /* check_not_char uc16 addr32                   */ \
V(CHECK_RANGE,       17, 9) /* check_range uc16 uc16 addr32                 */ \
V(CHECK_NOT_RANGE,   18, 9) /* check_not_range uc16 uc16 addr32             */ \
V(CHECK_BACKREF,     19, 9) /* check_backref offset32 capture_idx addr32    */ \
V(CHECK_NOT_BACKREF, 20, 9) /* check_not_backref offset32 capture_idx addr32*/ \
V(LOOKUP_MAP1,       21, 11) /* l_map1 start16 bit_map_addr32 addr32        */ \
V(LOOKUP_MAP2,       22, 99) /* l_map2 start16 half_nibble_map_addr32*      */ \
V(LOOKUP_MAP8,       23, 99) /* l_map8 start16 byte_map addr32*             */ \
V(LOOKUP_HI_MAP8,    24, 99) /* l_himap8 start8 byte_map_addr32 addr32*     */ \
V(CHECK_REGISTER_LT, 25, 8) /* check_reg_lt register_index value16 addr32   */ \
V(CHECK_REGISTER_GE, 26, 8) /* check_reg_ge register_index value16 addr32   */ \

#define DECLARE_BYTECODES(name, code, length) \
  static const int BC_##name = code;
BYTECODE_ITERATOR(DECLARE_BYTECODES)
#undef DECLARE_BYTECODES
} }

#endif  // V8_BYTECODES_IA32_H_
