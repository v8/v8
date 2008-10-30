// Copyright 2006-2008 the V8 project authors. All rights reserved.
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

#ifndef V8_JSREGEXP_INL_H_
#define V8_JSREGEXP_INL_H_


#include "jsregexp.h"


namespace v8 {
namespace internal {


CharacterClass CharacterClass::SingletonField(uc16 value) {
  CharacterClass result(FIELD);
  result.segment_ = segment_of(value);
  result.data_.u_field = long_bit(value & kSegmentMask);
  return result;
}


CharacterClass CharacterClass::RangeField(Range range) {
  CharacterClass result;
  result.InitializeFieldFrom(Vector<Range>(&range, 1));
  return result;
}


CharacterClass CharacterClass::Union(CharacterClass* left,
                                     CharacterClass* right) {
  CharacterClass result(UNION);
  result.data_.u_union.left = left;
  result.data_.u_union.right = right;
  return result;
}


void CharacterClass::write_nibble(int index, byte value) {
  ASSERT(0 <= index && index < 16);
  data_.u_field |= static_cast<uint64_t>(value) << (4 * index);
}


byte CharacterClass::read_nibble(int index) {
  ASSERT(0 <= index && index < 16);
  return (data_.u_field >> (4 * index)) & 0xf;
}


unsigned CharacterClass::segment_of(uc16 value) {
  return value >> CharacterClass::kFieldWidth;
}


uc16 CharacterClass::segment_start(unsigned segment) {
  return segment << CharacterClass::kFieldWidth;
}



}  // namespace internal
}  // namespace v8


#endif  // V8_JSREGEXP_INL_H_
