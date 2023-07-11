// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_CODE_POINTER_TABLE_H_
#define V8_SANDBOX_CODE_POINTER_TABLE_H_

#include "include/v8config.h"
#include "src/base/atomicops.h"
#include "src/base/memory.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/sandbox/external-entity-table.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

class Isolate;
class Counters;

/**
 * The entries of a CodePointerTable.
 */
struct CodePointerTableEntry {
  // Make this entry a code pointer entry containing the given pointer.
  inline void MakeCodePointerEntry(Address value);

  // Load code pointer stored in this entry.
  // This entry must be a code pointer entry.
  inline Address GetCodePointer() const;

  // Store the given code pointer in this entry.
  // This entry must be a code pointer entry.
  inline void SetCodePointer(Address value);

  // Make this entry a freelist entry, containing the index of the next entry
  // on the freelist.
  inline void MakeFreelistEntry(uint32_t next_entry_index);

  // Get the index of the next entry on the freelist. This method may be
  // called even when the entry is not a freelist entry. However, the result
  // is only valid if this is a freelist entry. This behaviour is required
  // for efficient entry allocation, see TryAllocateEntryFromFreelist.
  inline uint32_t GetNextFreelistEntryIndex() const;

  // Mark this entry as alive during garbage collection.
  inline void Mark();

  // Unmark this entry during sweeping.
  inline void Unmark();

  // Test whether this entry is currently marked as alive.
  inline bool IsMarked() const;

 private:
  friend class CodePointerTable;

  // Freelist entries contain the index of the next free entry in their lower 32
  // bits and this tag in the upper 32 bits.
  static constexpr Address kFreeEntryTag = 0xffffffffULL << 32;

  std::atomic<Address> pointer_;
  // Currently only contains the marking bit, but will likely contain another
  // pointer (to the owning Code object) in the future.
  std::atomic<Address> marking_state_;
};

static_assert(sizeof(CodePointerTableEntry) == kCodePointerTableEntrySize);

/**
 * A table containing pointers to code.
 *
 * When the sandbox is enabled, a code pointer table (CPT) can be used to ensure
 * basic control-flow integrity in the absence of special hardware support (such
 * as landing pad instructions): by referencing code through an index into a
 * CPT, and ensuring that only valid code entrypoints are stored inside the
 * table, it is then guaranteed that any indirect control-flow transfer ends up
 * on a valid entrypoint as long as an attacker is still confined to the
 * sandbox.
 */
class V8_EXPORT_PRIVATE CodePointerTable
    : public ExternalEntityTable<CodePointerTableEntry,
                                 kCodePointerTableReservationSize> {
 public:
  // Size of a CodePointerTable, for layout computation in IsolateData.
  static int constexpr kSize = 2 * kSystemPointerSize;
  static_assert(kMaxCodePointers == kMaxCapacity);

  CodePointerTable() = default;
  CodePointerTable(const CodePointerTable&) = delete;
  CodePointerTable& operator=(const CodePointerTable&) = delete;

  // The Spaces used by a CodePointerTable.
  struct Space
      : public ExternalEntityTable<CodePointerTableEntry,
                                   kCodePointerTableReservationSize>::Space {
   private:
    friend class CodePointerTable;
  };

  // Retrieves the entry referenced by the given handle.
  //
  // This method is atomic and can be called from background threads.
  inline Address Get(CodePointerHandle handle) const;

  // Sets the entry referenced by the given handle.
  //
  // This method is atomic and can be called from background threads.
  inline void Set(CodePointerHandle handle, Address value);

  // Allocates a new entry in the table. The caller must provide the initial
  // value and tag.
  //
  // This method is atomic and can be called from background threads.
  inline CodePointerHandle AllocateAndInitializeEntry(Space* space,
                                                      Address initial_value);

  // Marks the specified entry as alive.
  //
  // This method is atomic and can be called from background threads.
  inline void Mark(Space* space, CodePointerHandle handle);

  // Frees all unmarked entries in the given space.
  //
  // This method must only be called while mutator threads are stopped as it is
  // not safe to allocate table entries while a space is being swept.
  //
  // Returns the number of live entries after sweeping.
  uint32_t Sweep(Space* space, Counters* counters);

  // The base address of this table, for use in JIT compilers.
  Address base_address() const { return base(); }

 private:
  inline uint32_t HandleToIndex(CodePointerHandle handle) const;
  inline CodePointerHandle IndexToHandle(uint32_t index) const;
};

static_assert(sizeof(CodePointerTable) == CodePointerTable::kSize);

V8_EXPORT_PRIVATE CodePointerTable* GetProcessWideCodePointerTable();

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_CODE_POINTER_TABLE_H_
