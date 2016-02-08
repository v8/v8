// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_STORE_BUFFER_H_
#define V8_STORE_BUFFER_H_

#include "src/allocation.h"
#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/globals.h"
#include "src/heap/slot-set.h"

namespace v8 {
namespace internal {

class Page;
class PagedSpace;
class StoreBuffer;

typedef void (*ObjectSlotCallback)(HeapObject** from, HeapObject* to);

// Used to implement the write barrier by collecting addresses of pointers
// between spaces.
class StoreBuffer {
 public:
  explicit StoreBuffer(Heap* heap);
  static void StoreBufferOverflow(Isolate* isolate);
  void SetUp();
  void TearDown();

  static const int kStoreBufferOverflowBit = 1 << (14 + kPointerSizeLog2);
  static const int kStoreBufferSize = kStoreBufferOverflowBit;
  static const int kStoreBufferLength = kStoreBufferSize / sizeof(Address);

  // This is used to add addresses to the store buffer non-concurrently.
  inline void Mark(Address addr);

  // Slots that do not point to the ToSpace after callback invocation will be
  // removed from the set.
  void IteratePointersToNewSpace(ObjectSlotCallback callback);

  void Verify();

  // Eliminates all stale store buffer entries from the store buffer, i.e.,
  // slots that are not part of live objects anymore. This method must be
  // called after marking, when the whole transitive closure is known and
  // must be called before sweeping when mark bits are still intact.
  void ClearInvalidStoreBufferEntries();
  void VerifyValidStoreBufferEntries();

 private:
  Heap* heap_;

  // The start and the limit of the buffer that contains store slots
  // added from the generated code.
  Address* start_;
  Address* limit_;

  base::VirtualMemory* virtual_memory_;

  // Used for synchronization of concurrent store buffer access.
  base::Mutex mutex_;

  void InsertEntriesFromBuffer();

  inline uint32_t AddressToSlotSetAndOffset(Address slot_address,
                                            SlotSet** slots);

  template <typename Callback>
  void Iterate(Callback callback);

#ifdef VERIFY_HEAP
  void VerifyPointers(LargeObjectSpace* space);
#endif
};


class LocalStoreBuffer BASE_EMBEDDED {
 public:
  LocalStoreBuffer() : top_(new Node(nullptr)) {}

  ~LocalStoreBuffer() {
    Node* current = top_;
    while (current != nullptr) {
      Node* tmp = current->next;
      delete current;
      current = tmp;
    }
  }

  inline void Record(Address addr);
  inline void Process(StoreBuffer* store_buffer);

 private:
  static const int kBufferSize = 16 * KB;

  struct Node : Malloced {
    explicit Node(Node* next_node) : next(next_node), count(0) {}

    inline bool is_full() { return count == kBufferSize; }

    Node* next;
    Address buffer[kBufferSize];
    int count;
  };

  Node* top_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_STORE_BUFFER_H_
