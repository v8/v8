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

#ifndef V8_INCREMENTAL_MARKING_H_
#define V8_INCREMENTAL_MARKING_H_


#include "execution.h"
#include "mark-compact.h"
#include "objects.h"

namespace v8 {
namespace internal {


class IncrementalMarking : public AllStatic {
 public:
  enum State {
    STOPPED,
    MARKING,
    COMPLETE
  };

  static State state() {
    ASSERT(state_ == STOPPED || FLAG_incremental_marking);
    return state_;
  }

  static inline bool IsStopped() { return state() == STOPPED; }

  static bool WorthActivating();

  static void Start();

  static void Stop();

  static void Hurry();

  static void Finalize();

  static void MarkingComplete();

  static const intptr_t kAllocatedThreshold = 1024;
  static const intptr_t kAllocationMarkingFactor = 8;

  static void Step(intptr_t allocated);

  static inline void RestartIfNotMarking() {
    if (state_ == COMPLETE) {
      state_ = MARKING;
      if (FLAG_trace_incremental_marking) {
        PrintF("[IncrementalMarking] Restarting (new grey objects)\n");
      }
    }
  }

  static inline void RecordWrite(HeapObject* obj, Object* value) {
    if (!IsStopped() && value->IsHeapObject()) {
      MarkBit value_bit = Marking::MarkBitFrom(HeapObject::cast(value));
      if (IsWhite(value_bit)) {
        MarkBit obj_bit = Marking::MarkBitFrom(obj);
        if (IsBlack(obj_bit)) {
          BlackToGrey(obj, obj_bit);
          RestartIfNotMarking();
        }
      }
    }
  }

  static inline void RecordWriteOf(HeapObject* value) {
    if (state_ != STOPPED) {
      MarkBit value_bit = Marking::MarkBitFrom(value);
      if (IsWhite(value_bit)) {
        WhiteToGrey(value, value_bit);
        RestartIfNotMarking();
      }
    }
  }


  static inline void RecordWrites(HeapObject* obj) {
    if (!IsStopped()) {
      MarkBit obj_bit = Marking::MarkBitFrom(obj);
      if (IsBlack(obj_bit)) {
        BlackToGrey(obj, obj_bit);
        RestartIfNotMarking();
      }
    }
  }

  // Impossible markbits: 01
  static inline bool IsImpossible(MarkBit mark_bit) {
    return !mark_bit.Get() && mark_bit.Next().Get();
  }

  // Black markbits: 10 - this is required by the sweeper.
  static inline bool IsBlack(MarkBit mark_bit) {
    ASSERT(!IsImpossible(mark_bit));
    return mark_bit.Get() && !mark_bit.Next().Get();
  }

  // White markbits: 00 - this is required by the mark bit clearer.
  static inline bool IsWhite(MarkBit mark_bit) {
    ASSERT(!IsImpossible(mark_bit));
    return !mark_bit.Get();
  }

  // Grey markbits: 11
  static inline bool IsGrey(MarkBit mark_bit) {
    ASSERT(!IsImpossible(mark_bit));
    return mark_bit.Get() && mark_bit.Next().Get();
  }

  static inline void BlackToGrey(HeapObject* obj, MarkBit mark_bit) {
    ASSERT(Marking::MarkBitFrom(obj) == mark_bit);
    ASSERT(obj->Size() >= 2*kPointerSize);
    ASSERT(!IsStopped());
    ASSERT(IsBlack(mark_bit));
    mark_bit.Next().Set();
    ASSERT(IsGrey(mark_bit));

    marking_stack_.Push(obj);
    ASSERT(!marking_stack_.overflowed());
  }

  static inline void WhiteToGrey(HeapObject* obj, MarkBit mark_bit) {
    ASSERT(Marking::MarkBitFrom(obj) == mark_bit);
    ASSERT(obj->Size() >= 2*kPointerSize);
    ASSERT(!IsStopped());
    ASSERT(IsWhite(mark_bit));
    mark_bit.Set();
    mark_bit.Next().Set();
    ASSERT(IsGrey(mark_bit));

    marking_stack_.Push(obj);
    ASSERT(!marking_stack_.overflowed());
  }

  static inline void MarkBlack(MarkBit mark_bit) {
    mark_bit.Set();
    mark_bit.Next().Clear();
    ASSERT(IsBlack(mark_bit));
  }

  // Does white->black or grey->grey
  static inline void MarkBlackOrKeepGrey(MarkBit mark_bit) {
    ASSERT(!IsImpossible(mark_bit));
    if (mark_bit.Get()) return;
    mark_bit.Set();
    ASSERT(!IsWhite(mark_bit));
    ASSERT(!IsImpossible(mark_bit));
  }

  static inline const char* ColorStr(MarkBit mark_bit) {
    if (IsBlack(mark_bit)) return "black";
    if (IsWhite(mark_bit)) return "white";
    if (IsGrey(mark_bit)) return "grey";
    UNREACHABLE();
    return "???";
  }

 private:
  static State state_;
  static MarkingStack marking_stack_;
};

} }  // namespace v8::internal

#endif  // V8_INCREMENTAL_MARKING_H_
