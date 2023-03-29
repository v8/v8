// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARKING_H_
#define V8_HEAP_MARKING_H_

#include <cstdint>

#include "src/base/atomic-utils.h"
#include "src/common/globals.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/objects/heap-object.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

class MarkBit {
 public:
  using CellType = uint32_t;
  static_assert(sizeof(CellType) == sizeof(base::Atomic32));

  V8_ALLOW_UNUSED static inline MarkBit From(Address);
  V8_ALLOW_UNUSED static inline MarkBit From(HeapObject);

  // The function returns true if it succeeded to
  // transition the bit from 0 to 1.
  template <AccessMode mode = AccessMode::NON_ATOMIC>
  inline bool Set();

  template <AccessMode mode = AccessMode::NON_ATOMIC>
  inline bool Get();

  // The function returns true if it succeeded to
  // transition the bit from 1 to 0. Only works in non-atomic contexts.
  inline bool Clear();

#ifdef DEBUG
  bool operator==(const MarkBit& other) {
    return cell_ == other.cell_ && mask_ == other.mask_;
  }
#endif

 private:
  inline MarkBit(CellType* cell, CellType mask) : cell_(cell), mask_(mask) {}

  CellType* const cell_;
  const CellType mask_;

  friend class Bitmap;
};

template <>
inline bool MarkBit::Set<AccessMode::NON_ATOMIC>() {
  CellType old_value = *cell_;
  if ((old_value & mask_) == mask_) return false;
  *cell_ = old_value | mask_;
  return true;
}

template <>
inline bool MarkBit::Set<AccessMode::ATOMIC>() {
  return base::AsAtomic32::SetBits(cell_, mask_, mask_);
}

template <>
inline bool MarkBit::Get<AccessMode::NON_ATOMIC>() {
  return (*cell_ & mask_) != 0;
}

template <>
inline bool MarkBit::Get<AccessMode::ATOMIC>() {
  return (base::AsAtomic32::Acquire_Load(cell_) & mask_) != 0;
}

inline bool MarkBit::Clear() {
  CellType old_value = *cell_;
  *cell_ = old_value & ~mask_;
  return (old_value & mask_) == mask_;
}

// Bitmap is a sequence of cells each containing fixed number of bits.
class V8_EXPORT_PRIVATE Bitmap {
 public:
  using CellType = MarkBit::CellType;
  static constexpr uint32_t kBitsPerCell = 32;
  static_assert(kBitsPerCell == (sizeof(CellType) * kBitsPerByte));
  static constexpr uint32_t kBitsPerCellLog2 = 5;
  static constexpr uint32_t kBitIndexMask = kBitsPerCell - 1;
  static constexpr uint32_t kBytesPerCell = kBitsPerCell / kBitsPerByte;
  static constexpr uint32_t kBytesPerCellLog2 =
      kBitsPerCellLog2 - kBitsPerByteLog2;

  // The length is the number of bits in this bitmap. (+1) accounts for
  // the case where the markbits are queried for a one-word filler at the
  // end of the page.
  //
  // TODO(v8:12612): Remove the (+1) when adjusting AdvanceToNextValidObject().
  static constexpr size_t kLength =
      ((1 << kPageSizeBits) >> kTaggedSizeLog2) + 1;

  static constexpr size_t kCellsCount =
      (kLength + kBitsPerCell - 1) >> kBitsPerCellLog2;

  // The size of the bitmap in bytes is CellsCount() * kBytesPerCell.
  static constexpr size_t kSize = kCellsCount * kBytesPerCell;

  V8_INLINE static constexpr uint32_t AddressToIndex(Address address) {
    return (address & kPageAlignmentMask) >> kTaggedSizeLog2;
  }

  V8_INLINE static constexpr uint32_t IndexToCell(uint32_t index) {
    return index >> kBitsPerCellLog2;
  }

  V8_INLINE static constexpr uint32_t IndexInCell(uint32_t index) {
    return index & kBitIndexMask;
  }

  V8_INLINE static constexpr uint32_t IndexInCellMask(uint32_t index) {
    return 1u << IndexInCell(index);
  }

  // Retrieves the cell containing the provided markbit index.
  V8_INLINE static constexpr uint32_t CellAlignIndex(uint32_t index) {
    return index & ~kBitIndexMask;
  }

  V8_INLINE static Bitmap* Cast(Address addr) {
    return reinterpret_cast<Bitmap*>(addr);
  }

  // Gets the MarkBit for an `address` which may be unaligned (include the tag
  // bit).
  V8_INLINE static MarkBit MarkBitFromAddress(Address address) {
    const auto index = Bitmap::AddressToIndex(address);
    const auto mask = IndexInCellMask(index);
    MarkBit::CellType* cell =
        FromAddress(address)->cells() + IndexToCell(index);
    return MarkBit(cell, mask);
  }

  V8_INLINE MarkBit::CellType* cells() {
    return reinterpret_cast<MarkBit::CellType*>(this);
  }

  V8_INLINE const MarkBit::CellType* cells() const {
    return reinterpret_cast<const MarkBit::CellType*>(this);
  }

  V8_INLINE MarkBit MarkBitFromIndexForTesting(uint32_t index) {
    const auto mask = IndexInCellMask(index);
    MarkBit::CellType* cell = cells() + IndexToCell(index);
    return MarkBit(cell, mask);
  }

 private:
  V8_INLINE static Bitmap* FromAddress(Address address) {
    Address page_address = address & ~kPageAlignmentMask;
    return Cast(page_address + MemoryChunkLayout::kMarkingBitmapOffset);
  }
};

// static
MarkBit MarkBit::From(Address address) {
  return Bitmap::MarkBitFromAddress(address);
}

// static
MarkBit MarkBit::From(HeapObject heap_object) {
  return Bitmap::MarkBitFromAddress(heap_object.ptr());
}

template <AccessMode mode>
class ConcurrentBitmap : public Bitmap {
 public:
  void Clear();

  // Sets all bits in the range [start_index, end_index). If the access is
  // atomic, the cells at the boundary of the range are updated with atomic
  // compare and swap operation. The inner cells are updated with relaxed write.
  void SetRange(uint32_t start_index, uint32_t end_index);

  // Clears all bits in the range [start_index, end_index). If the access is
  // atomic, the cells at the boundary of the range are updated with atomic
  // compare and swap operation. The inner cells are updated with relaxed write.
  void ClearRange(uint32_t start_index, uint32_t end_index);

  // Returns true if all bits in the range [start_index, end_index) are set.
  //
  // Not safe in a concurrent context, hence lacking implementation for
  // `AccessMode::ATOMIC`.
  bool AllBitsSetInRange(uint32_t start_index, uint32_t end_index);

  // Returns true if all bits in the range [start_index, end_index) are cleared.
  //
  // Not safe in a concurrent context, hence lacking implementation for
  // `AccessMode::ATOMIC`.
  bool AllBitsClearInRange(uint32_t start_index, uint32_t end_index);

  // Returns true if all bits are cleared.
  //
  // Not safe in a concurrent context, hence lacking implementation for
  // `AccessMode::ATOMIC`.
  bool IsClean();

  // Not safe in a concurrent context, hence lacking implementation for
  // `AccessMode::ATOMIC`.
  void Print();

 private:
  // Sets bits in the given cell. The mask specifies bits to set: if a
  // bit is set in the mask then the corresponding bit is set in the cell.
  void SetBitsInCell(uint32_t cell_index, uint32_t mask);

  // Clears bits in the given cell. The mask specifies bits to clear: if a
  // bit is set in the mask then the corresponding bit is cleared in the cell.
  void ClearBitsInCell(uint32_t cell_index, uint32_t mask);

  // Clear all bits in the cell range [start_cell_index, end_cell_index). If the
  // access is atomic then *still* use a relaxed memory ordering.
  void ClearCellRangeRelaxed(uint32_t start_cell_index,
                             uint32_t end_cell_index);

  // Set all bits in the cell range [start_cell_index, end_cell_index). If the
  // access is atomic then *still* use a relaxed memory ordering.
  void SetCellRangeRelaxed(uint32_t start_cell_index, uint32_t end_cell_index);
};

template <>
inline void ConcurrentBitmap<AccessMode::ATOMIC>::ClearCellRangeRelaxed(
    uint32_t start_cell_index, uint32_t end_cell_index) {
  base::Atomic32* cell_base = reinterpret_cast<base::Atomic32*>(cells());
  for (uint32_t i = start_cell_index; i < end_cell_index; i++) {
    base::Relaxed_Store(cell_base + i, 0);
  }
}

template <>
inline void ConcurrentBitmap<AccessMode::NON_ATOMIC>::ClearCellRangeRelaxed(
    uint32_t start_cell_index, uint32_t end_cell_index) {
  for (uint32_t i = start_cell_index; i < end_cell_index; i++) {
    cells()[i] = 0;
  }
}

template <>
inline void ConcurrentBitmap<AccessMode::ATOMIC>::SetCellRangeRelaxed(
    uint32_t start_cell_index, uint32_t end_cell_index) {
  base::Atomic32* cell_base = reinterpret_cast<base::Atomic32*>(cells());
  for (uint32_t i = start_cell_index; i < end_cell_index; i++) {
    base::Relaxed_Store(cell_base + i, 0xffffffff);
  }
}

template <>
inline void ConcurrentBitmap<AccessMode::NON_ATOMIC>::SetCellRangeRelaxed(
    uint32_t start_cell_index, uint32_t end_cell_index) {
  for (uint32_t i = start_cell_index; i < end_cell_index; i++) {
    cells()[i] = 0xffffffff;
  }
}

template <AccessMode mode>
inline void ConcurrentBitmap<mode>::Clear() {
  ClearCellRangeRelaxed(0, kCellsCount);
  if (mode == AccessMode::ATOMIC) {
    // This fence prevents re-ordering of publishing stores with the mark-bit
    // setting stores.
    base::SeqCst_MemoryFence();
  }
}

template <>
inline void ConcurrentBitmap<AccessMode::NON_ATOMIC>::SetBitsInCell(
    uint32_t cell_index, uint32_t mask) {
  cells()[cell_index] |= mask;
}

template <>
inline void ConcurrentBitmap<AccessMode::ATOMIC>::SetBitsInCell(
    uint32_t cell_index, uint32_t mask) {
  base::AsAtomic32::SetBits(cells() + cell_index, mask, mask);
}

template <>
inline void ConcurrentBitmap<AccessMode::NON_ATOMIC>::ClearBitsInCell(
    uint32_t cell_index, uint32_t mask) {
  cells()[cell_index] &= ~mask;
}

template <>
inline void ConcurrentBitmap<AccessMode::ATOMIC>::ClearBitsInCell(
    uint32_t cell_index, uint32_t mask) {
  base::AsAtomic32::SetBits(cells() + cell_index, 0u, mask);
}

template <AccessMode mode>
void ConcurrentBitmap<mode>::SetRange(uint32_t start_index,
                                      uint32_t end_index) {
  if (start_index >= end_index) return;
  end_index--;

  unsigned int start_cell_index = start_index >> Bitmap::kBitsPerCellLog2;
  MarkBit::CellType start_index_mask = 1u << Bitmap::IndexInCell(start_index);

  unsigned int end_cell_index = end_index >> Bitmap::kBitsPerCellLog2;
  MarkBit::CellType end_index_mask = 1u << Bitmap::IndexInCell(end_index);

  if (start_cell_index != end_cell_index) {
    // Firstly, fill all bits from the start address to the end of the first
    // cell with 1s.
    SetBitsInCell(start_cell_index, ~(start_index_mask - 1));
    // Then fill all in between cells with 1s.
    SetCellRangeRelaxed(start_cell_index + 1, end_cell_index);
    // Finally, fill all bits until the end address in the last cell with 1s.
    SetBitsInCell(end_cell_index, end_index_mask | (end_index_mask - 1));
  } else {
    SetBitsInCell(start_cell_index,
                  end_index_mask | (end_index_mask - start_index_mask));
  }
  if (mode == AccessMode::ATOMIC) {
    // This fence prevents re-ordering of publishing stores with the mark-bit
    // setting stores.
    base::SeqCst_MemoryFence();
  }
}

template <AccessMode mode>
void ConcurrentBitmap<mode>::ClearRange(uint32_t start_index,
                                        uint32_t end_index) {
  if (start_index >= end_index) return;
  end_index--;

  unsigned int start_cell_index = start_index >> Bitmap::kBitsPerCellLog2;
  MarkBit::CellType start_index_mask = 1u << Bitmap::IndexInCell(start_index);

  unsigned int end_cell_index = end_index >> Bitmap::kBitsPerCellLog2;
  MarkBit::CellType end_index_mask = 1u << Bitmap::IndexInCell(end_index);

  if (start_cell_index != end_cell_index) {
    // Firstly, fill all bits from the start address to the end of the first
    // cell with 0s.
    ClearBitsInCell(start_cell_index, ~(start_index_mask - 1));
    // Then fill all in between cells with 0s.
    ClearCellRangeRelaxed(start_cell_index + 1, end_cell_index);
    // Finally, set all bits until the end address in the last cell with 0s.
    ClearBitsInCell(end_cell_index, end_index_mask | (end_index_mask - 1));
  } else {
    ClearBitsInCell(start_cell_index,
                    end_index_mask | (end_index_mask - start_index_mask));
  }
  if (mode == AccessMode::ATOMIC) {
    // This fence prevents re-ordering of publishing stores with the mark-bit
    // clearing stores.
    base::SeqCst_MemoryFence();
  }
}

template <>
V8_EXPORT_PRIVATE bool
ConcurrentBitmap<AccessMode::NON_ATOMIC>::AllBitsSetInRange(
    uint32_t start_index, uint32_t end_index);

template <>
V8_EXPORT_PRIVATE bool
ConcurrentBitmap<AccessMode::NON_ATOMIC>::AllBitsClearInRange(
    uint32_t start_index, uint32_t end_index);

template <>
void ConcurrentBitmap<AccessMode::NON_ATOMIC>::Print();

template <>
V8_EXPORT_PRIVATE bool ConcurrentBitmap<AccessMode::NON_ATOMIC>::IsClean();

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARKING_H_
