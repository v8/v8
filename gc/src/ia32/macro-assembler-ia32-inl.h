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

template<typename LabelType>
void MacroAssembler::HasScanOnScavenge(Register object,
                                       Register scratch,
                                       LabelType* scan_on_scavenge) {
  Move(scratch, object);
  and_(scratch, ~Page::kPageAlignmentMask);
  cmpb(Operand(scratch, MemoryChunk::kScanOnScavengeOffset), 0);
  j(not_equal, scan_on_scavenge);
}


template<typename LabelType>
void MacroAssembler::InOldSpaceIsBlack(Register object,
                                       Register scratch0,
                                       Register scratch1,
                                       LabelType* is_black) {
  HasColour(object, scratch0, scratch1,
            is_black,
            Page::kPageAlignmentMask,
            MemoryChunk::kHeaderSize,
            1, 0,    // kBlackBitPattern.
            false);  // In old space.
  ASSERT(strcmp(IncrementalMarking::kBlackBitPattern, "10") == 0);
}


template<typename LabelType>
void MacroAssembler::InNewSpaceIsBlack(Register object,
                                       Register scratch0,
                                       Register scratch1,
                                       LabelType* is_black) {
  HasColour(object, scratch0, scratch1,
            is_black,
            ~HEAP->new_space()->mask(),
            0,
            1, 0,   // kBlackBitPattern.
            true);  // In new space.
  ASSERT(strcmp(IncrementalMarking::kBlackBitPattern, "10") == 0);
}


template<typename LabelType>
void MacroAssembler::HasColour(Register object,
                               Register bitmap_scratch,
                               Register mask_scratch,
                               LabelType* has_colour,
                               uint32_t mask,
                               int header_size,
                               int first_bit,
                               int second_bit,
                               bool in_new_space) {
  ASSERT(!Aliasing(object, bitmap_scratch, mask_scratch, ecx));
  int32_t high_mask = ~mask;

  MarkBits(object, bitmap_scratch, mask_scratch, high_mask, in_new_space);

  NearLabel other_colour, word_boundary;
  test(mask_scratch, Operand(bitmap_scratch, header_size));
  j(first_bit == 1 ? zero : not_zero, &other_colour);
  add(mask_scratch, Operand(mask_scratch));  // Shift left 1 by adding.
  j(zero, &word_boundary);
  test(mask_scratch, Operand(bitmap_scratch, header_size));
  j(second_bit == 1 ? not_zero : zero, has_colour);
  jmp(&other_colour);

  bind(&word_boundary);
  test_b(Operand(bitmap_scratch, header_size + kPointerSize), 1);

  j(second_bit == 1 ? not_zero : zero, has_colour);
  bind(&other_colour);
}


template<typename LabelType>
void MacroAssembler::IsDataObject(Register value,
                                  Register scratch,
                                  LabelType* not_data_object,
                                  bool in_new_space) {
  if (in_new_space) {
    NearLabel is_data_object;
    mov(scratch, FieldOperand(value, HeapObject::kMapOffset));
    cmp(scratch, FACTORY->heap_number_map());
    j(equal, &is_data_object);
    ASSERT(kConsStringTag == 1 && kIsConsStringMask == 1);
    ASSERT(kNotStringTag == 0x80 && kIsNotStringMask == 0x80);
    // If it's a string and it's not a cons string then it's an object that
    // doesn't need scanning.
    test_b(FieldOperand(scratch, Map::kInstanceTypeOffset),
           kIsConsStringMask | kIsNotStringMask);
    // Jump if we need to mark it grey and push it.
    j(not_zero, not_data_object);
    bind(&is_data_object);
  } else {
    mov(scratch, Operand(value));
    and_(scratch, ~Page::kPageAlignmentMask);
    test_b(Operand(scratch, MemoryChunk::kFlagsOffset),
           1 << MemoryChunk::CONTAINS_ONLY_DATA);
    // Jump if we need to mark it grey and push it.
    j(zero, not_data_object);
  }
}


void MacroAssembler::MarkBits(Register addr_reg,
                              Register bitmap_reg,
                              Register mask_reg,
                              int32_t high_mask,
                              bool in_new_space) {
  ASSERT(!Aliasing(addr_reg, bitmap_reg, mask_reg, ecx));
  if (in_new_space) {
    mov(bitmap_reg, Immediate(ExternalReference::new_space_mark_bits()));
  } else {
    mov(bitmap_reg, Operand(addr_reg));
    and_(bitmap_reg, high_mask);
  }
  mov(ecx, Operand(addr_reg));
  static const int kBitsPerCellLog2 =
      Bitmap<MemoryChunk::BitmapStorageDescriptor>::kBitsPerCellLog2;
  shr(ecx, kBitsPerCellLog2);
  and_(ecx, ~((high_mask >> kBitsPerCellLog2) | (kPointerSize - 1)));

  add(bitmap_reg, Operand(ecx));
  mov(ecx, Operand(addr_reg));
  shr(ecx, kPointerSizeLog2);
  and_(ecx, (1 << kBitsPerCellLog2) - 1);
  mov(mask_reg, Immediate(1));
  shl_cl(mask_reg);
}


template<typename LabelType>
void MacroAssembler::EnsureNotWhite(
    Register value,
    Register bitmap_scratch,
    Register mask_scratch,
    LabelType* value_is_white_and_not_data,
    bool in_new_space) {
  ASSERT(!Aliasing(value, bitmap_scratch, mask_scratch, ecx));
  int32_t high_mask = in_new_space ?
      HEAP->new_space()->mask() :
      ~Page::kPageAlignmentMask;
  int header_size = in_new_space ? 0 : MemoryChunk::kHeaderSize;
  MarkBits(value, bitmap_scratch, mask_scratch, high_mask, in_new_space);

  // If the value is black or grey we don't need to do anything.
  ASSERT(strcmp(IncrementalMarking::kWhiteBitPattern, "00") == 0);
  ASSERT(strcmp(IncrementalMarking::kBlackBitPattern, "10") == 0);
  ASSERT(strcmp(IncrementalMarking::kGreyBitPattern, "11") == 0);
  ASSERT(strcmp(IncrementalMarking::kImpossibleBitPattern, "01") == 0);

  NearLabel done;

  // Since both black and grey have a 1 in the first position and white does
  // not have a 1 there we only need to check one bit.
  test(mask_scratch, Operand(bitmap_scratch, header_size));
  j(not_zero, &done);

  if (FLAG_debug_code) {
    // Check for impossible bit pattern.
    NearLabel ok;
    push(mask_scratch);
    // shl.  May overflow making the check conservative.
    add(mask_scratch, Operand(mask_scratch));
    test(mask_scratch, Operand(bitmap_scratch, header_size));
    j(zero, &ok);
    int3();
    bind(&ok);
    pop(mask_scratch);
  }

  // Value is white.  We check whether it is data that doesn't need scanning.
  IsDataObject(value, ecx, value_is_white_and_not_data, in_new_space);

  // Value is a data object, and it is white.  Mark it black.  Since we know
  // that the object is white we can make it black by flipping one bit.
  or_(Operand(bitmap_scratch, header_size), mask_scratch);
  bind(&done);
}

} }  // namespace v8::internal

#endif  // V8_IA32_MACRO_ASSEMBLER_IA32_INL_H_
