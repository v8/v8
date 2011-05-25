// Copyright 2011 the V8 project authors. All rights reserved.
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

#ifndef V8_IA32_MACRO_ASSEMBLER_IA32_INL_H_
#define V8_IA32_MACRO_ASSEMBLER_IA32_INL_H_

#include "macro-assembler.h"

namespace v8 {
namespace internal {

void MacroAssembler::CheckPageFlag(
    Register object,
    Register scratch,
    MemoryChunk::MemoryChunkFlags flag,
    Condition cc,
    Label* condition_met,
    Label::Distance condition_met_near) {
  ASSERT(cc == zero || cc == not_zero);
  if (scratch.is(object)) {
    and_(scratch, Immediate(~Page::kPageAlignmentMask));
  } else {
    mov(scratch, Immediate(~Page::kPageAlignmentMask));
    and_(scratch, Operand(object));
  }
  if (flag < kBitsPerByte) {
    test_b(Operand(scratch, MemoryChunk::kFlagsOffset),
           static_cast<uint8_t>(1u << flag));
  } else {
    test(Operand(scratch, MemoryChunk::kFlagsOffset), Immediate(1 << flag));
  }
  j(cc, condition_met, condition_met_near);
}


void MacroAssembler::IsBlack(Register object,
                             Register scratch0,
                             Register scratch1,
                             Label* is_black,
                             Label::Distance is_black_near) {
  HasColour(object, scratch0, scratch1,
            is_black, is_black_near,
            1, 0);  // kBlackBitPattern.
  ASSERT(strcmp(Marking::kBlackBitPattern, "10") == 0);
}


void MacroAssembler::HasColour(Register object,
                               Register bitmap_scratch,
                               Register mask_scratch,
                               Label* has_colour,
                               Label::Distance has_colour_distance,
                               int first_bit,
                               int second_bit) {
  ASSERT(!Aliasing(object, bitmap_scratch, mask_scratch, ecx));

  MarkBits(object, bitmap_scratch, mask_scratch);

  Label other_colour, word_boundary;
  test(mask_scratch, Operand(bitmap_scratch, MemoryChunk::kHeaderSize));
  j(first_bit == 1 ? zero : not_zero, &other_colour, Label::kNear);
  add(mask_scratch, Operand(mask_scratch));  // Shift left 1 by adding.
  j(zero, &word_boundary, Label::kNear);
  test(mask_scratch, Operand(bitmap_scratch, MemoryChunk::kHeaderSize));
  j(second_bit == 1 ? not_zero : zero, has_colour, has_colour_distance);
  jmp(&other_colour, Label::kNear);

  bind(&word_boundary);
  test_b(Operand(bitmap_scratch, MemoryChunk::kHeaderSize + kPointerSize), 1);

  j(second_bit == 1 ? not_zero : zero, has_colour, has_colour_distance);
  bind(&other_colour);
}


void MacroAssembler::IsDataObject(Register value,
                                  Register scratch,
                                  Label* not_data_object,
                                  Label::Distance not_data_object_distance,
                                  bool in_new_space) {
  if (in_new_space) {
    Label is_data_object;
    mov(scratch, FieldOperand(value, HeapObject::kMapOffset));
    cmp(scratch, FACTORY->heap_number_map());
    j(equal, &is_data_object, Label::kNear);
    ASSERT(kConsStringTag == 1 && kIsConsStringMask == 1);
    ASSERT(kNotStringTag == 0x80 && kIsNotStringMask == 0x80);
    // If it's a string and it's not a cons string then it's an object that
    // doesn't need scanning.
    test_b(FieldOperand(scratch, Map::kInstanceTypeOffset),
           kIsConsStringMask | kIsNotStringMask);
    // Jump if we need to mark it grey and push it.
    j(not_zero, not_data_object, not_data_object_distance);
    bind(&is_data_object);
  } else {
    mov(scratch, Operand(value));
    and_(scratch, ~Page::kPageAlignmentMask);
    test_b(Operand(scratch, MemoryChunk::kFlagsOffset),
           1 << MemoryChunk::CONTAINS_ONLY_DATA);
    // Jump if we need to mark it grey and push it.
    j(zero, not_data_object, not_data_object_distance);
  }
}


void MacroAssembler::MarkBits(Register addr_reg,
                              Register bitmap_reg,
                              Register mask_reg) {
  ASSERT(!Aliasing(addr_reg, bitmap_reg, mask_reg, ecx));
  mov(bitmap_reg, Operand(addr_reg));
  and_(bitmap_reg, ~Page::kPageAlignmentMask);
  mov(ecx, Operand(addr_reg));
  shr(ecx, Bitmap::kBitsPerCellLog2);
  and_(ecx,
       (Page::kPageAlignmentMask >> Bitmap::kBitsPerCellLog2) &
           ~(kPointerSize - 1));

  add(bitmap_reg, Operand(ecx));
  mov(ecx, Operand(addr_reg));
  shr(ecx, kPointerSizeLog2);
  and_(ecx, (1 << Bitmap::kBitsPerCellLog2) - 1);
  mov(mask_reg, Immediate(1));
  shl_cl(mask_reg);
}


void MacroAssembler::EnsureNotWhite(
    Register value,
    Register bitmap_scratch,
    Register mask_scratch,
    Label* value_is_white_and_not_data,
    Label::Distance distance,
    bool in_new_space) {
  ASSERT(!Aliasing(value, bitmap_scratch, mask_scratch, ecx));
  MarkBits(value, bitmap_scratch, mask_scratch);

  // If the value is black or grey we don't need to do anything.
  ASSERT(strcmp(Marking::kWhiteBitPattern, "00") == 0);
  ASSERT(strcmp(Marking::kBlackBitPattern, "10") == 0);
  ASSERT(strcmp(Marking::kGreyBitPattern, "11") == 0);
  ASSERT(strcmp(Marking::kImpossibleBitPattern, "01") == 0);

  Label done;

  // Since both black and grey have a 1 in the first position and white does
  // not have a 1 there we only need to check one bit.
  test(mask_scratch, Operand(bitmap_scratch, MemoryChunk::kHeaderSize));
  j(not_zero, &done, Label::kNear);

  if (FLAG_debug_code) {
    // Check for impossible bit pattern.
    Label ok;
    push(mask_scratch);
    // shl.  May overflow making the check conservative.
    add(mask_scratch, Operand(mask_scratch));
    test(mask_scratch, Operand(bitmap_scratch, MemoryChunk::kHeaderSize));
    j(zero, &ok, Label::kNear);
    int3();
    bind(&ok);
    pop(mask_scratch);
  }

  // Value is white.  We check whether it is data that doesn't need scanning.
  IsDataObject(value, ecx, value_is_white_and_not_data, distance, in_new_space);

  // Value is a data object, and it is white.  Mark it black.  Since we know
  // that the object is white we can make it black by flipping one bit.
  or_(Operand(bitmap_scratch, MemoryChunk::kHeaderSize), mask_scratch);
  bind(&done);
}

} }  // namespace v8::internal

#endif  // V8_IA32_MACRO_ASSEMBLER_IA32_INL_H_
