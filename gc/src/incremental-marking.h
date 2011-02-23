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

  static void Start();

  static void Stop();

  static void Hurry();

  static void Finalize();

  static void MarkingComplete();

  static const intptr_t kAllocatedThreshold = 1024;
  static const intptr_t kAllocationMarkingFactor = 8;

  static void Step(intptr_t allocated);

  static inline void RecordWrite(HeapObject* obj, Object* value) {
    if (!IsStopped() && value->IsHeapObject()) {
      if (IsBlack(obj) && IsWhite(HeapObject::cast(value))) {
        BlackToGrey(obj);
        if (state_ == COMPLETE) {
          state_ = MARKING;
          if (FLAG_trace_incremental_marking) {
            PrintF("[IncrementalMarking] Restarting (new grey objects)\n");
          }
        }
      }
    }
  }

  static inline void RecordWriteOf(HeapObject* value) {
    if (state_ != STOPPED) {
      if (IsWhite(value)) {
        WhiteToGrey(value);
        if (state_ == COMPLETE) {
          state_ = MARKING;
          if (FLAG_trace_incremental_marking) {
            PrintF("[IncrementalMarking] Restarting (new grey objects)\n");
          }
        }
      }
    }
  }


  static inline void RecordWrites(HeapObject* obj) {
    if (!IsStopped()) {
      if (IsBlack(obj)) {
        BlackToGrey(obj);
        if (state_ == COMPLETE) {
          state_ = MARKING;
          if (FLAG_trace_incremental_marking) {
            PrintF("[IncrementalMarking] Restarting (new grey objects)\n");
          }
        }
      }
    }
  }


  // Black markbits: 10
  static inline bool IsBlack(HeapObject* obj) {
    return Marking::IsMarked(obj->address());
  }

  // White markbits: 00
  static inline bool IsWhite(HeapObject* obj) {
    return !Marking::IsMarked(obj->address()) &&
        !Marking::IsMarked(obj->address() + kPointerSize);
  }

  // Grey markbits: 01
  static inline bool IsGrey(HeapObject* obj) {
    return Marking::IsMarked(obj->address() + kPointerSize);
  }

  static inline void BlackToGrey(HeapObject* obj) {
    ASSERT(!IsStopped());
    ASSERT(IsBlack(obj));
    Marking::ClearMark(obj->address());
    Marking::SetMark(obj->address() + kPointerSize);
    ASSERT(IsGrey(obj));

    marking_stack_.Push(obj);
    ASSERT(!marking_stack_.overflowed());
  }

  static inline void WhiteToGrey(HeapObject* obj) {
    ASSERT(!IsStopped());
    ASSERT(IsWhite(obj));
    Marking::SetMark(obj->address() + kPointerSize);
    ASSERT(IsGrey(obj));

    marking_stack_.Push(obj);
    ASSERT(!marking_stack_.overflowed());
  }

  static inline void MarkBlack(HeapObject* obj) {
    Marking::SetMark(obj->address());
    Marking::ClearMark(obj->address() + kPointerSize);
    ASSERT(IsBlack(obj));
  }

  static inline const char* ColorStr(HeapObject* obj) {
    if (IsBlack(obj)) return "black";
    if (IsWhite(obj)) return "white";
    if (IsGrey(obj)) return "grey";
    UNREACHABLE();
    return "???";
  }

 private:
  static State state_;
  static MarkingStack marking_stack_;
};

} }  // namespace v8::internal

#endif  // V8_INCREMENTAL_MARKING_H_
