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

#ifndef V8_WRITE_BARRIER_H_
#define V8_WRITE_BARRIER_H_

#include "allocation.h"
#include "checks.h"
#include "globals.h"
#include "platform.h"

namespace v8 {
namespace internal {

typedef void (*ObjectSlotCallback)(HeapObject** from, HeapObject* to);

// Used to implement the write barrier by collecting addresses of pointers
// between spaces.
class StoreBuffer : public AllStatic {
 public:
  static inline Address TopAddress();

  static void Setup();
  static void TearDown();

  // This is used by the mutator to enter addresses into the store buffer.
  static inline void Mark(Address addr);

  // This is used by the heap traversal to enter the addresses into the store
  // buffer that should still be in the store buffer after GC.  It enters
  // addresses directly into the old buffer because the GC starts by wiping the
  // old buffer and thereafter only visits each cell once so there is no need
  // to attempt to remove any dupes.  During the first part of a scavenge we
  // are using the store buffer to access the old spaces and at the same time
  // we are rebuilding the store buffer using this function.  There is, however
  // no issue of overwriting the buffer we are iterating over, because this
  // stage of the scavenge can only reduce the number of addresses in the store
  // buffer (some objects are promoted so pointers to them do not need to be in
  // the store buffer).  The later parts of the scavenge process the promotion
  // queue and they can overflow this buffer, which we must check for.
  static inline void EnterDirectlyIntoStoreBuffer(Address addr);

  // Iterates over all pointers that go from old space to new space.  It will
  // delete the store buffer as it starts so the callback should reenter
  // surviving old-to-new pointers into the store buffer to rebuild it.
  static void IteratePointersToNewSpace(ObjectSlotCallback callback);

  static const int kStoreBufferOverflowBit = 1 << 16;
  static const int kStoreBufferSize = kStoreBufferOverflowBit;
  static const int kStoreBufferLength = kStoreBufferSize / sizeof(Address);
  static const int kOldStoreBufferLength = kStoreBufferLength * 64;
  static const int kHashMapLengthLog2 = 12;
  static const int kHashMapLength = 1 << kHashMapLengthLog2;

  static void Compact();
  static void GCPrologue(GCType type, GCCallbackFlags flags);
  static void GCEpilogue(GCType type, GCCallbackFlags flags);

  static Object*** Start() { return reinterpret_cast<Object***>(old_start_); }
  static Object*** Top() { return reinterpret_cast<Object***>(old_top_); }

  enum StoreBufferMode {
    kStoreBufferFunctional,
    kStoreBufferDisabled,
    kStoreBufferBeingRebuilt
  };

  static StoreBufferMode store_buffer_mode() { return store_buffer_mode_; }
  static inline void set_store_buffer_mode(StoreBufferMode mode);
  static bool old_buffer_is_sorted() { return old_buffer_is_sorted_; }

  // Goes through the store buffer removing pointers to things that have
  // been promoted.  Rebuilds the store buffer completely if it overflowed.
  static void SortUniq();
  static void Verify();

#ifdef DEBUG
  static void Clean();
  // Slow, for asserts only.
  static bool CellIsInStoreBuffer(Address cell);
#endif

 private:
  // The store buffer is divided up into a new buffer that is constantly being
  // filled by mutator activity and an old buffer that is filled with the data
  // from the new buffer after compression.
  static Address* start_;
  static Address* limit_;

  static Address* old_start_;
  static Address* old_limit_;
  static Address* old_top_;

  static bool old_buffer_is_sorted_;
  static StoreBufferMode store_buffer_mode_;
  static bool during_gc_;
  static bool store_buffer_rebuilding_enabled_;
  static bool may_move_store_buffer_entries_;

  static VirtualMemory* virtual_memory_;
  static uintptr_t* hash_map_1_;
  static uintptr_t* hash_map_2_;

  static void CheckForFullBuffer();
  static void Uniq();
  static void ZapHashTables();
  static bool HashTablesAreZapped();

  friend class StoreBufferRebuildScope;
  friend class DontMoveStoreBufferEntriesScope;
};


class StoreBufferRebuildScope {
 public:
  StoreBufferRebuildScope() :
      stored_state_(StoreBuffer::store_buffer_rebuilding_enabled_) {
    StoreBuffer::store_buffer_rebuilding_enabled_ = true;
  }

  ~StoreBufferRebuildScope() {
    StoreBuffer::store_buffer_rebuilding_enabled_ = stored_state_;
    StoreBuffer::CheckForFullBuffer();
  }

 private:
  bool stored_state_;
};


class DontMoveStoreBufferEntriesScope {
 public:
  DontMoveStoreBufferEntriesScope() :
      stored_state_(StoreBuffer::may_move_store_buffer_entries_) {
    StoreBuffer::may_move_store_buffer_entries_ = false;
  }

  ~DontMoveStoreBufferEntriesScope() {
    StoreBuffer::may_move_store_buffer_entries_ = stored_state_;
  }

 private:
  bool stored_state_;
};

} }  // namespace v8::internal

#endif  // V8_WRITE_BARRIER_H_
