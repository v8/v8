// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_POINTER_TABLE_H_
#define V8_SANDBOX_EXTERNAL_POINTER_TABLE_H_

#include "include/v8config.h"
#include "src/base/atomicops.h"
#include "src/base/memory.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

class Isolate;

/**
 * A table storing pointers to objects outside the V8 heap.
 *
 * When V8_ENABLE_SANDBOX, its primary use is for pointing to objects outside
 * the sandbox, as described below.
 *
 * An external pointer table provides the basic mechanisms to ensure
 * memory-safe access to objects located outside the sandbox, but referenced
 * from within it. When an external pointer table is used, objects located
 * inside the sandbox reference outside objects through indices into the table.
 *
 * Type safety can be ensured by using type-specific tags for the external
 * pointers. These tags will be ORed into the unused top bits of the pointer
 * when storing them and will be ANDed away when loading the pointer later
 * again. If a pointer of the wrong type is accessed, some of the top bits will
 * remain in place, rendering the pointer inaccessible.
 *
 * Temporal memory safety is achieved through garbage collection of the table,
 * which ensures that every entry is either an invalid pointer or a valid
 * pointer pointing to a live object.
 *
 * Spatial memory safety can, if necessary, be ensured by storing the size of a
 * referenced object together with the object itself outside the sandbox, and
 * referencing both through a single entry in the table.
 *
 * The garbage collection algorithm for the table works as follows:
 *  - The top bit of every entry is reserved for the marking bit.
 *  - Every store to an entry automatically sets the marking bit when ORing
 *    with the tag. This avoids the need for write barriers.
 *  - Every load of an entry automatically removes the marking bit when ANDing
 *    with the inverted tag.
 *  - When the GC marking visitor finds a live object with an external pointer,
 *    it marks the corresponding entry as alive through Mark(), which sets the
 *    marking bit using an atomic CAS operation.
 *  - When marking is finished, SweepAndCompact() iterates of the table once
 *    while the mutator is stopped and builds a freelist from all dead entries
 *    while also removing the marking bit from any live entry.
 *
 * The freelist is a singly-linked list, using the lower 32 bits of each entry
 * to store the index of the next free entry. When the freelist is empty and a
 * new entry is allocated, the table grows in place and the freelist is
 * re-populated from the newly added entries.
 *
 * When V8_COMPRESS_POINTERS, external pointer tables are also used to ease
 * alignment requirements in heap object fields via indirection.
 */
class V8_EXPORT_PRIVATE ExternalPointerTable {
 public:
  // Size of an ExternalPointerTable, for layout computation in IsolateData.
  // Asserted to be equal to the actual size in external-pointer-table.cc.
  static int constexpr kSize = 4 * kSystemPointerSize;

  ExternalPointerTable() = default;

  ExternalPointerTable(const ExternalPointerTable&) = delete;
  ExternalPointerTable& operator=(const ExternalPointerTable&) = delete;

  // Initializes this external pointer table by reserving the backing memory
  // and initializing the freelist.
  inline void Init(Isolate* isolate);

  // Resets this external pointer table and deletes all associated memory.
  inline void TearDown();

  // Retrieves the entry referenced by the given handle.
  //
  // This method is atomic and can be called from background threads.
  inline Address Get(ExternalPointerHandle handle,
                     ExternalPointerTag tag) const;

  // Sets the entry referenced by the given handle.
  //
  // This method is atomic and can be called from background threads.
  inline void Set(ExternalPointerHandle handle, Address value,
                  ExternalPointerTag tag);

  // Exchanges the entry referenced by the given handle with the given value,
  // returning the previous value. The same tag is applied both to decode the
  // previous value and encode the given value.
  //
  // This method is atomic and can call be called from background threads.
  inline Address Exchange(ExternalPointerHandle handle, Address value,
                          ExternalPointerTag tag);

  // Allocates a new entry in the external pointer table. The caller must
  // provide the initial value and tag.
  //
  // This method is atomic and can be called from background threads.
  inline ExternalPointerHandle AllocateAndInitializeEntry(
      Address initial_value, ExternalPointerTag tag);

  // Determines the number of entries currently on the freelist.
  // The freelist entries encode the freelist size and the next entry on the
  // list, so this routine fetches the first entry on the freelist and returns
  // the size encoded in it.
  // As entries may be allocated from background threads while this method
  // executes, its result should only be treated as an approximation of the
  // real size.
  inline uint32_t FreelistSize();

  // Marks the specified entry as alive.
  //
  // If the table is currently being compacted, this may also mark the entry
  // for compaction for which the location of the handle is required. See the
  // comments about table compaction below for more details.
  //
  // This method is atomic and can be called from background threads.
  inline void Mark(ExternalPointerHandle handle, Address handle_location);

  // Frees unmarked entries and finishes table compaction (if running).
  //
  // This method must only be called while mutator threads are stopped as it is
  // not safe to allocate table entries while the table is being swept.
  //
  // Returns the number of live entries after sweeping.
  uint32_t SweepAndCompact(Isolate* isolate);

  // Table compaction.
  //
  // The table is to some degree self-compacting: since the freelist is
  // sorted in ascending order (see SweepAndCompact()), empty slots at the start
  // of the table will usually quickly be filled. Further, empty blocks at the
  // end of the table will be decommitted to reduce memory usage. However, live
  // entries at the end of the table can prevent this decommitting and cause
  // fragmentation. The following simple algorithm is therefore used to
  // compact the table if that is deemed necessary:
  //  - At the start of the GC marking phase, determine if the table needs to be
  //    compacted. This decisiont is mostly based on the absolute and relative
  //    size of the freelist.
  //  - If compaction is needed, this algorithm attempts to shrink the table by
  //    FreelistSize/2 entries during compaction by moving all live entries out
  //    of the evacuation area (the last FreelistSize/2 entries of the table),
  //    then decommitting those blocks at the end of SweepAndCompact.
  //  - During marking, whenever a live entry inside the evacuation area is
  //    found, a new "evacuation entry" is allocated from the freelist (which is
  //    assumed to have enough free slots) and the address of the handle is
  //    written into it.
  //  - During sweeping, these evacuation entries are resolved: the content of
  //    the old entry is copied into the new entry and the handle in the object
  //    is updated to point to the new entry.
  //
  // When compacting, it is expected that the evacuation area contains few live
  // entries and that the freelist will be able to serve all evacuation entry
  // allocations. In that case, compaction is essentially free (very little
  // marking overhead, no memory overhead). However, it can happen that the
  // application allocates a large number of entries from the table during
  // marking, in which case the freelist would no longer be able to serve all
  // allocation without growing. If that situation is detected, compaction is
  // aborted during marking.
  //
  // This algorithm assumes that table entries (except for the null entry) are
  // never shared between multiple objects. Otherwise, the following could
  // happen: object A initially has handle H1 and is scanned during incremental
  // marking. Next, object B with handle H2 is scanned and marked for
  // evacuation. Afterwards, object A copies the handle H2 from object B.
  // During sweeping, only object B's handle will be updated to point to the
  // new entry while object A's handle is now dangling. If shared entries ever
  // become necessary, setting external pointer handles would have to be
  // guarded by write barriers to avoid this scenario.

  // Use heuristics to determine if table compaction is needed and if so start
  // the compaction. This is expected to be called at the start of the GC
  // marking phase.
  void StartCompactingIfNeeded();

  // Returns true if table compaction is currently running.
  bool IsCompacting() { return start_of_evacuation_area_ != 0; }

 private:
  // Required for Isolate::CheckIsolateLayout().
  friend class Isolate;

  // An external pointer table grows and shrinks in blocks of this size. This
  // is also the initial size of the table.
  static constexpr size_t kBlockSize = 16 * KB;
  static constexpr size_t kEntriesPerBlock = kBlockSize / kSystemPointerSize;

  // When the table is swept, it first sets the freelist head to this special
  // value to better catch any violation of the "don't-alloc-while-sweeping"
  // requirement (see SweepAndCompact()). This value is chosen so it points to
  // the last entry in the table, which should usually be inaccessible.
  static constexpr uint32_t kTableIsCurrentlySweepingMarker =
      (kExternalPointerTableReservationSize / kSystemPointerSize) - 1;

  // During table compaction, this value is used to indicate that table
  // compaction had to be aborted during the marking phase because the freelist
  // grew to short. See Mark() for more details.
  static constexpr uint32_t kTableCompactionAbortedMarker =
      std::numeric_limits<uint32_t>::max();

  // Outcome of external pointer table compaction to use for the
  // ExternalPointerTableCompactionOutcome histogram.
  enum class TableCompactionOutcome {
    // Table compaction was successful.
    kSuccess = 0,
    // Table compaction was partially successful: marking finished successfully,
    // but not all blocks that we wanted to free could be freed because some new
    // entries had already been allocated in them again.
    kPartialSuccess = 1,
    // Table compaction was aborted during marking because the freelist grew to
    // short.
    kAbortedDuringMarking = 2,
  };

  // Returns true if this external pointer table has been initialized.
  bool is_initialized() { return buffer_ != kNullAddress; }

  // Table capacity accesors.
  // The capacity is expressed in number of entries.
  // The capacity of the table may increase during entry allocation (if the
  // table is grown) and may decrease during sweeping (if blocks at the end are
  // free). As the former may happen concurrently, the capacity can only be
  // used reliably if either the table mutex is held or if all mutator threads
  // are currently stopped. However, it is fine to use this value to
  // sanity-check incoming ExternalPointerHandles in debug builds (there's no
  // need for actual bounds-checks because out-of-bounds accesses are guaranteed
  // to result in a harmless crash).
  uint32_t capacity() const { return base::Relaxed_Load(&capacity_); }
  void set_capacity(uint32_t new_capacity) {
    base::Relaxed_Store(&capacity_, new_capacity);
  }

  // Implementation of entry allocation. Called from AllocateAndInitializeEntry
  // and AllocateEvacuationEntry.
  //
  // If this method is used to allocate an evacuation entry, it is guaranteed to
  // return an entry before the start of the evacuation area or fail by
  // returning kNullExternalPointerHandle. In particular, it will never grow the
  // table. See the explanation of the compaction algorithm for more details.
  //
  // The caller must initialize the entry afterwards through Set(). In
  // particular, the caller is responsible for setting the mark bit of the new
  // entry.
  //
  // This method is atomic and can be called from background threads.
  inline ExternalPointerHandle AllocateInternal(bool is_evacuation_entry);

  // Allocate an entry suitable as evacuation entry during table compaction.
  //
  // This method is atomic and can be called from background threads.
  inline ExternalPointerHandle AllocateEvacuationEntry();

  // Extends the table and adds newly created entries to the freelist. Returns
  // the new freelist head. When calling this method, mutex_ must be locked.
  // If the table cannot be grown, either because it is already at its maximum
  // size or because the memory for it could not be allocated, this method will
  // fail with an OOM crash.
  uint32_t Grow();

  // Stop compacting at the end of sweeping.
  void StopCompacting();

  inline uint32_t handle_to_index(ExternalPointerHandle handle) const {
    uint32_t index = handle >> kExternalPointerIndexShift;
    DCHECK_EQ(handle, index << kExternalPointerIndexShift);
    DCHECK_LT(index, capacity());
    return index;
  }

  inline ExternalPointerHandle index_to_handle(uint32_t index) const {
    ExternalPointerHandle handle = index << kExternalPointerIndexShift;
    DCHECK_EQ(index, handle >> kExternalPointerIndexShift);
    return handle;
  }

  // Computes the address of the specified entry.
  inline Address entry_address(uint32_t index) const {
    return buffer_ + index * sizeof(Address);
  }

  // When LeakSanitizer is enabled, this method will write the untagged (raw)
  // pointer into the shadow table (located after the real table) at the given
  // index. This is necessary because LSan is unable to scan the pointers in
  // the main table due to the pointer tagging scheme (the values don't "look
  // like" pointers). So instead it can scan the pointers in the shadow table.
  inline void lsan_record_ptr(uint32_t index, Address value) {
#if defined(LEAK_SANITIZER)
    base::Memory<Address>(entry_address(index) +
                          kExternalPointerTableReservationSize) =
        value & ~kExternalPointerTagMask;
#endif  // LEAK_SANITIZER
  }

  // Loads the value at the given index. This method is non-atomic, only use it
  // when no other threads can currently access the table.
  inline Address load(uint32_t index) const {
    return base::Memory<Address>(entry_address(index));
  }

  // Stores the provided value at the given index. This method is non-atomic,
  // only use it when no other threads can currently access the table.
  inline void store(uint32_t index, Address value) {
    lsan_record_ptr(index, value);
    base::Memory<Address>(entry_address(index)) = value;
  }

  // Atomically loads the value at the given index.
  inline Address load_atomic(uint32_t index) const {
    auto addr = reinterpret_cast<base::Atomic64*>(entry_address(index));
    return base::Relaxed_Load(addr);
  }

  // Atomically stores the provided value at the given index.
  inline void store_atomic(uint32_t index, Address value) {
    lsan_record_ptr(index, value);
    auto addr = reinterpret_cast<base::Atomic64*>(entry_address(index));
    base::Relaxed_Store(addr, value);
  }

  // Atomically exchanges the value at the given index with the provided value.
  inline Address exchange_atomic(uint32_t index, Address value) {
    lsan_record_ptr(index, value);
    auto addr = reinterpret_cast<base::Atomic64*>(entry_address(index));
    return static_cast<Address>(base::Relaxed_AtomicExchange(addr, value));
  }

  static bool is_marked(Address entry) {
    return (entry & kExternalPointerMarkBit) == kExternalPointerMarkBit;
  }

  static Address set_mark_bit(Address entry) {
    return entry | kExternalPointerMarkBit;
  }

  static Address clear_mark_bit(Address entry) {
    return entry & ~kExternalPointerMarkBit;
  }

  static bool is_free(Address entry) {
    return (entry & kExternalPointerFreeEntryTag) ==
           kExternalPointerFreeEntryTag;
  }

  static uint32_t extract_next_entry_from_freelist_entry(Address entry) {
    // See make_freelist_entry below.
    return static_cast<uint32_t>(entry) & 0x00ffffff;
  }

  static uint32_t extract_freelist_size_from_freelist_entry(Address entry) {
    // See make_freelist_entry below.
    return static_cast<uint32_t>(entry >> 24) & 0x00ffffff;
  }

  static Address make_freelist_entry(uint32_t current_freelist_head,
                                     uint32_t current_freelist_size) {
    // The next freelist entry is stored in the lower 24 bits of the entry. The
    // freelist size is stored in the next 24 bits. If we ever need larger
    // tables, and therefore larger indices to encode the next free entry, we
    // can make the freelist size an approximation and drop some of the bottom
    // bits of the value when encoding it.
    // We could also keep the freelist size as an additional uint32_t member,
    // but encoding it in this way saves one atomic compare-exchange on every
    // entry allocation.
    static_assert(kMaxExternalPointers <= (1ULL << 24));
    static_assert(kExternalPointerFreeEntryTag >= (1ULL << 48));
    DCHECK_LT(current_freelist_head, kMaxExternalPointers);
    DCHECK_LT(current_freelist_size, kMaxExternalPointers);

    Address entry = current_freelist_size;
    entry <<= 24;
    entry |= current_freelist_head;
    entry |= kExternalPointerFreeEntryTag;
    return entry;
  }

  static bool is_evacuation_entry(Address entry) {
    return (entry & kEvacuationEntryTag) == kEvacuationEntryTag;
  }

  static Address extract_handle_location_from_evacuation_entry(Address entry) {
    return entry & ~kEvacuationEntryTag;
  }

  static Address make_evacuation_entry(Address handle_location) {
    return handle_location | kEvacuationEntryTag;
  }

  // The buffer backing this table. This is const after initialization. Should
  // only be accessed using the load_x() and store_x() methods, which take care
  // of atomicicy if necessary.
  Address buffer_ = kNullAddress;

  // The current capacity of this table, which is the number of usable entries.
  base::Atomic32 capacity_ = 0;

  // The index of the first entry on the freelist or zero if the list is empty.
  base::Atomic32 freelist_head_ = 0;

  // When compacting the table, this value contains the index of the first
  // entry in the evacuation area. The evacuation area is the region at the end
  // of the table from which entries are moved out of so that the underyling
  // memory pages can be freed after sweeping.
  uint32_t start_of_evacuation_area_ = 0;

  // Lock protecting the slow path for entry allocation, in particular Grow().
  // As the size of this structure must be predictable (it's part of
  // IsolateData), it cannot directly contain a Mutex and so instead contains a
  // pointer to one.
  base::Mutex* mutex_ = nullptr;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_EXTERNAL_POINTER_TABLE_H_
