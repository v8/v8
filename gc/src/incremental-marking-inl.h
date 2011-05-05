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

#ifndef V8_INCREMENTAL_MARKING_INL_H_
#define V8_INCREMENTAL_MARKING_INL_H_

#include "incremental-marking.h"

namespace v8 {
namespace internal {


void IncrementalMarking::RecordWrite(HeapObject* obj, Object* value) {
  if (!IsStopped() && value->IsHeapObject()) {
    MarkBit value_bit = heap_->marking()->MarkBitFrom(HeapObject::cast(value));
    if (IsWhite(value_bit)) {
      MarkBit obj_bit = heap_->marking()->MarkBitFrom(obj);
      if (IsBlack(obj_bit)) {
        BlackToGreyAndUnshift(obj, obj_bit);
        RestartIfNotMarking();
      }
    }
  }
}


void IncrementalMarking::RecordWriteOf(HeapObject* value) {
  if (state_ != STOPPED) {
    MarkBit value_bit = heap_->marking()->MarkBitFrom(value);
    if (IsWhite(value_bit)) {
      WhiteToGreyAndPush(value, value_bit);
      RestartIfNotMarking();
    }
  }
}


void IncrementalMarking::RecordWrites(HeapObject* obj) {
  if (!IsStopped()) {
    MarkBit obj_bit = heap_->marking()->MarkBitFrom(obj);
    if (IsBlack(obj_bit)) {
      BlackToGreyAndUnshift(obj, obj_bit);
      RestartIfNotMarking();
    }
  }
}


void IncrementalMarking::BlackToGreyAndUnshift(HeapObject* obj,
                                               MarkBit mark_bit) {
  ASSERT(heap_->marking()->MarkBitFrom(obj) == mark_bit);
  ASSERT(obj->Size() >= 2*kPointerSize);
  ASSERT(!IsStopped());
  ASSERT(IsBlack(mark_bit));
  mark_bit.Next().Set();
  ASSERT(IsGrey(mark_bit));

  marking_deque_.Unshift(obj);
  ASSERT(!marking_deque_.overflowed());
}


void IncrementalMarking::WhiteToGreyAndPush(HeapObject* obj, MarkBit mark_bit) {
  WhiteToGrey(obj, mark_bit);
  marking_deque_.Push(obj);
  ASSERT(!marking_deque_.overflowed());
}


void IncrementalMarking::WhiteToGrey(HeapObject* obj, MarkBit mark_bit) {
  ASSERT(heap_->marking()->MarkBitFrom(obj) == mark_bit);
  ASSERT(obj->Size() >= 2*kPointerSize);
  ASSERT(!IsStopped());
  ASSERT(IsWhite(mark_bit));
  mark_bit.Set();
  mark_bit.Next().Set();
  ASSERT(IsGrey(mark_bit));
}


IncrementalMarking::ObjectColor IncrementalMarking::Color(HeapObject* obj) {
  MarkBit mark_bit = heap_->marking()->MarkBitFrom(obj);
  if (IsBlack(mark_bit)) return BLACK_OBJECT;
  if (IsWhite(mark_bit)) return WHITE_OBJECT;
  if (IsGrey(mark_bit)) return GREY_OBJECT;
  UNREACHABLE();
  return IMPOSSIBLE_COLOR;
}


} }  // namespace v8::internal

#endif  // V8_INCREMENTAL_MARKING_INL_H_
