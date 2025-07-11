// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_JS_DISPATCH_TABLE_H_
#define V8_SANDBOX_JS_DISPATCH_TABLE_H_

#include "include/v8config.h"
#include "src/base/atomicops.h"
#include "src/base/memory.h"
#include "src/common/globals.h"
#include "src/runtime/runtime.h"
#include "src/sandbox/external-entity-table.h"

#ifdef V8_ENABLE_LEAPTIERING

namespace v8 {
namespace internal {

class Isolate;
class Counters;
class Code;
enum class TieringBuiltin;

/**
 * The entries of a JSDispatchTable.
 *
 * An entry contains all information to call a JavaScript function in a
 * sandbox-compatible way: the entrypoint and the parameter count (~= the
 * signature of the function). The entrypoint will always point to the current
 * code of the function, thereby enabling seamless tiering.
 */
struct JSDispatchEntry {
  // We write-protect the JSDispatchTable on platforms that support it for
  // forward-edge CFI.
  static constexpr bool IsWriteProtected = true;

  inline void MakeJSDispatchEntry(Address object, Address entrypoint,
                                  uint16_t parameter_count, bool mark_as_alive);

  inline Address GetEntrypoint() const;
  inline Address GetCodePointer() const;
  inline Tagged<Code> GetCode() const;
  inline uint16_t GetParameterCount() const;

  inline void SetCodeAndEntrypointPointer(Address new_object,
                                          Address new_entrypoint);
  inline void SetEntrypointPointer(Address new_entrypoint);

  // Make this entry a freelist entry, containing the index of the next entry
  // on the freelist.
  inline void MakeFreelistEntry(uint32_t next_entry_index);

  // Returns true if this entry is a freelist entry.
  inline bool IsFreelistEntry() const;

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

  // Constants for access from generated code.
  // These are static_assert'ed to be correct in CheckFieldOffsets().
  static constexpr uintptr_t kEntrypointOffset = 0;
  static constexpr uintptr_t kCodeObjectOffset = kSystemPointerSize;
  static constexpr size_t kParameterCountSize = 2;

// On AIX and IBM i, mmap will give you back an address with the top bits set
// unlike other platforms where the top bits are unset.
// Therefore kObjectPointerOffset was introduced to ensure we account for
// the top bits being set when performing pointer operations.
#if defined(__PASE__)
  static constexpr uintptr_t kObjectPointerOffset = 0x0700000000000000;
#elif defined(_AIX)
  static constexpr uintptr_t kObjectPointerOffset = 0x0a00000000000000;
#else
  static constexpr uintptr_t kObjectPointerOffset = 0;
#endif

#if defined(V8_TARGET_ARCH_64_BIT)
  // Freelist entries contain the index of the next free entry in their lower 32
  // bits and are tagged with this tag.
  static constexpr Address kFreeEntryTag = 0xffff000000000000ull;
#ifdef V8_TARGET_BIG_ENDIAN
  // 2-byte parameter count is on the least significant side of encoded_word_.
  static constexpr int kBigEndianParamCountOffset =
      sizeof(Address) - sizeof(uint16_t);
  static constexpr uintptr_t kParameterCountOffset =
      kCodeObjectOffset + kBigEndianParamCountOffset;
#else
  static constexpr uintptr_t kParameterCountOffset = kCodeObjectOffset;
#endif  // V8_TARGET_BIG_ENDIAN
  static constexpr uint32_t kObjectPointerShift = 16;
  static constexpr uint32_t kParameterCountMask = 0xffff;
#elif defined(V8_TARGET_ARCH_32_BIT)
  static constexpr uintptr_t kParameterCountOffset =
      kCodeObjectOffset + kSystemPointerSize;
  static constexpr uint32_t kObjectPointerShift = 0;
  static constexpr uint32_t kParameterCountMask = 0x0;
#else
#error "Unsupported Architecture"
#endif

  static void CheckFieldOffsets();

 private:
  friend class JSDispatchTable;

  // The first word contains the pointer to the (executable) entrypoint.
  std::atomic<Address> entrypoint_;

  // On 64 bit architectures the second word of the entry contains (1) the
  // pointer to the code object associated with this entry, (2) the marking bit
  // of the entry in the LSB of the object pointer (which must be unused as the
  // address must be aligned), and (3) the 16-bit parameter count. The parameter
  // count is stored in the lower 16 bits and therefore the pointer is shifted
  // to the left. The final format therefore looks as follows:
  //
  // +----------------------+---------------+-------------------+
  // | Bits 63 ... 17       | Bit 16        | Bits 15 ... 0     |
  // |  HeapObject pointer  |  Marking bit  |  Parameter count  |
  // +----------------------+---------------+-------------------+
  //
  // On 32 bit architectures only the mark bit is shared with the pointer.
  //
  // +----------------------+---------------+
  // | Bits 32 ... 1        | Bit 0         |
  // |  HeapObject pointer  |  Marking bit  |
  // +----------------------+---------------+
  //
  // TODO(olivf): Find a better format that allows us to write atomically to the
  // individual parts and unify with 32 bit. For instance we could try to store
  // the code pointer in some compressd format, such that it fits into 32 bits.

  static constexpr Address kMarkingBit = 1 << kObjectPointerShift;
  std::atomic<Address> encoded_word_;

#ifdef V8_TARGET_ARCH_32_BIT
  // TODO(olivf): Investigate if we could shrink the entry size on 32bit
  // platforms to 12 bytes.
  std::atomic<uint16_t> parameter_count_;
  // 16 bits of padding
  std::atomic<uint32_t> next_free_entry_;
#endif  // V8_TARGET_ARCH_32_BIT
};

static_assert(sizeof(JSDispatchEntry) == kJSDispatchTableEntrySize);

/**
 * JSDispatchTable.
 *
 * The JSDispatchTable achieves two central goals:
 *
 * 1. It provides fine-grained forward-edge CFI for JavaScript function calls.
 * Both in the context of the V8 Sandbox and for process-wide CFI. For the
 * sandbox, this requires keeping the table outside of the sandbox and storing
 * both the function's entrypoints and its parameter count in it. That way, it
 * is guaranteed that every JSFunction call (1) lands at a valid JavaScript
 * entrypoint, and (2) uses the correct signature (~= parameter count). For
 * process-wide CFI, this table is write-protected using for example Intel
 * PKEYs. That way, even an attacker with an arbitrary, process-wide write
 * primitive cannot execute arbitrary code via JavaScript functions.
 *
 * 2. It enables cheap and fast tiering. When the JSDispatchTable is used, a
 * group of related JSFunctions (roughly those sharing the same SFI) share one
 * table entry. When the functions should tier up or down, only the entry needs
 * to be updated to point to the new code. Without such a table, every function
 * entrypoint would need to check if it needs to tier up or down, thereby
 * incurring some overhead on every function invocation.
 */
class V8_EXPORT_PRIVATE JSDispatchTable
    : public ExternalEntityTable<JSDispatchEntry,
                                 kJSDispatchTableReservationSize> {
  using Base =
      ExternalEntityTable<JSDispatchEntry, kJSDispatchTableReservationSize>;

 public:
#ifdef V8_ENABLE_SANDBOX
  static_assert(kMaxJSDispatchEntries == kMaxCapacity);
#endif  // V8_ENABLE_SANDBOX
  static_assert(!kSupportsCompaction);

  JSDispatchTable() = default;
  JSDispatchTable(const JSDispatchTable&) = delete;
  JSDispatchTable& operator=(const JSDispatchTable&) = delete;

  // The Spaces used by a JSDispatchTable.
  using Space = Base::SpaceWithBlackAllocationSupport;

  // Retrieves the entrypoint of the entry referenced by the given handle.
  inline Address GetEntrypoint(JSDispatchHandle handle);

  // Retrieves the Code stored in the entry referenced by the given handle.
  //
  // TODO(saelo): in the future, we might store either a Code or a
  // BytecodeArray in the entries. At that point, this could be changed to
  // return a Tagged<Union<Code, BytecodeArray>>.
  inline Tagged<Code> GetCode(JSDispatchHandle handle);

  // Returns the address of the Code object stored in the specified entry.
  inline Address GetCodeAddress(JSDispatchHandle handle);

  // Retrieves the parameter count of the entry referenced by the given handle.
  inline uint16_t GetParameterCount(JSDispatchHandle handle);

  // Updates the entry referenced by the given handle to the given Code and its
  // entrypoint. The code must be compatible with the specified entry. In
  // particular, the two must use the same parameter count.
  // NB: Callee must emit JS_DISPATCH_HANDLE_WRITE_BARRIER if needed!
  inline void SetCodeNoWriteBarrier(JSDispatchHandle handle,
                                    Tagged<Code> new_code);

  // Execute a tiering builtin instead of the actual code. Leaves the Code
  // pointer untouched and changes only the entrypoint.
  inline void SetTieringRequest(JSDispatchHandle handle, TieringBuiltin builtin,
                                Isolate* isolate);
  inline void SetCodeKeepTieringRequestNoWriteBarrier(JSDispatchHandle handle,
                                                      Tagged<Code> new_code);
  // Resets the entrypoint to the code's entrypoint.
  inline void ResetTieringRequest(JSDispatchHandle handle);
  // Check if and/or which tiering builtin is installed.
  inline bool IsTieringRequested(JSDispatchHandle handle);
  inline bool IsTieringRequested(JSDispatchHandle handle,
                                 TieringBuiltin builtin, Isolate* isolate);

  // Allocates a new entry in the table and initialize it.
  //
  // Note: If possible allocate dispatch handles through the factory.
  //
  // This method is atomic and can be called from background threads.
  inline JSDispatchHandle AllocateAndInitializeEntry(Space* space,
                                                     uint16_t parameter_count,
                                                     Tagged<Code> code);
  inline std::optional<JSDispatchHandle> TryAllocateAndInitializeEntry(
      Space* space, uint16_t parameter_count, Tagged<Code> code);

  // The following methods are used to pre allocate entries and then initialize
  // them later.
  void PreAllocateEntries(Space* space, int num);
  bool PreAllocatedEntryNeedsInitialization(Space* space,
                                            JSDispatchHandle handle);
  void InitializePreAllocatedEntry(Space* space, JSDispatchHandle handle,
                                   Tagged<Code> code, uint16_t parameter_count);

  // Can be used to statically predict the handles if the pre allocated entries
  // are in the overall first read only segment of the whole table.
#if V8_STATIC_DISPATCH_HANDLES_BOOL
  static JSDispatchHandle GetStaticHandleForReadOnlySegmentEntry(int index) {
    return IndexToHandle(kInternalNullEntryIndex + 1 + index);
  }
#endif  // V8_STATIC_DISPATCH_HANDLES_BOOL
  static bool InReadOnlySegment(JSDispatchHandle handle) {
    return HandleToIndex(handle) <= kEndOfReadOnlyIndex;
  }
  static int OffsetOfEntry(JSDispatchHandle handle) {
    return JSDispatchTable::HandleToIndex(handle)
           << kJSDispatchTableEntrySizeLog2;
  }

  // Marks the specified entry as alive.
  //
  // This method is atomic and can be called from background threads.
  inline void Mark(JSDispatchHandle handle);

  // Frees all unmarked entries in the given space.
  //
  // This method must only be called while mutator threads are stopped as it is
  // not safe to allocate table entries while a space is being swept.
  //
  // Returns the number of live entries after sweeping.
  template <typename Callback>
  uint32_t Sweep(Space* space, Counters* counters, Callback callback);

  // Iterate over all active entries in the given space.
  //
  // The callback function will be invoked once for every entry that is
  // currently in use, i.e. has been allocated and not yet freed, and will
  // receive the handle of that entry.
  template <typename Callback>
  void IterateActiveEntriesIn(Space* space, Callback callback);

  template <typename Callback>
  void IterateMarkedEntriesIn(Space* space, Callback callback);

  // The base address of this table, for use in JIT compilers.
  Address base_address() const { return base(); }

#ifdef DEBUG
  bool IsMarked(JSDispatchHandle handle);
#endif  // DEBUG
#if defined(DEBUG) || defined(VERIFY_HEAP)
  inline void VerifyEntry(JSDispatchHandle handle, Space* space,
                          Space* ro_space);
#endif  // defined(DEBUG) || defined(VERIFY_HEAP)

  void PrintEntry(JSDispatchHandle handle);
  void PrintCurrentTieringRequest(JSDispatchHandle handle, Isolate* isolate,
                                  std::ostream& os);

  static constexpr bool kWriteBarrierSetsEntryMarkBit = true;

 private:
  static inline bool IsCompatibleCode(Tagged<Code> code,
                                      uint16_t parameter_count);

  inline void SetCodeAndEntrypointNoWriteBarrier(JSDispatchHandle handle,
                                                 Tagged<Code> new_code,
                                                 Address entrypoint);

  static uint32_t HandleToIndex(JSDispatchHandle handle) {
    uint32_t index = handle.value() >> kJSDispatchHandleShift;
    DCHECK_EQ(handle.value(), index << kJSDispatchHandleShift);
    return index;
  }
  static JSDispatchHandle IndexToHandle(uint32_t index) {
    JSDispatchHandle handle(index << kJSDispatchHandleShift);
    DCHECK_EQ(index, handle.value() >> kJSDispatchHandleShift);
    return handle;
  }

  friend class MarkCompactCollector;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_LEAPTIERING

#endif  // V8_SANDBOX_JS_DISPATCH_TABLE_H_
