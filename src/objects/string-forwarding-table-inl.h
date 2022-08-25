// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_STRING_FORWARDING_TABLE_INL_H_
#define V8_OBJECTS_STRING_FORWARDING_TABLE_INL_H_

#include "src/base/atomicops.h"
#include "src/common/globals.h"
#include "src/objects/name-inl.h"
#include "src/objects/slots-inl.h"
#include "src/objects/slots.h"
#include "src/objects/string-forwarding-table.h"
#include "src/objects/string-inl.h"
// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class StringForwardingTable::Record final {
 public:
  String original_string(PtrComprCageBase cage_base) const {
    return String::cast(OriginalStringObject(cage_base));
  }

  String forward_string(PtrComprCageBase cage_base) const {
    return String::cast(ForwardStringObject(cage_base));
  }

  inline uint32_t raw_hash(PtrComprCageBase cage_base) const;

  Object OriginalStringObject(PtrComprCageBase cage_base) const {
    return OriginalStringSlot().Acquire_Load(cage_base);
  }

  Object ForwardStringObject(PtrComprCageBase cage_base) const {
    return ForwardStringSlot().Acquire_Load(cage_base);
  }

  void set_original_string(Object object) {
    OriginalStringSlot().Release_Store(object);
  }

  void set_forward_string(Object object) {
    ForwardStringSlot().Release_Store(object);
  }

  inline void SetInternalized(String string, String forward_to);

 private:
  OffHeapObjectSlot OriginalStringSlot() const {
    return OffHeapObjectSlot(&original_string_);
  }

  OffHeapObjectSlot ForwardStringSlot() const {
    return OffHeapObjectSlot(&forward_string_);
  }

  Tagged_t original_string_;
  Tagged_t forward_string_;

  friend class StringForwardingTable::Block;
};

uint32_t StringForwardingTable::Record::raw_hash(
    PtrComprCageBase cage_base) const {
  String internalized = forward_string(cage_base);
  uint32_t raw_hash = internalized.raw_hash_field();
  DCHECK(Name::IsHashFieldComputed(raw_hash));
  return raw_hash;
}

void StringForwardingTable::Record::SetInternalized(String string,
                                                    String forward_to) {
  set_original_string(string);
  set_forward_string(forward_to);
}

class StringForwardingTable::Block {
 public:
  static std::unique_ptr<Block> New(int capacity);
  explicit Block(int capacity);
  int capacity() const { return capacity_; }
  void* operator new(size_t size, int capacity);
  void* operator new(size_t size) = delete;
  void operator delete(void* data);

  Record* record(int index) {
    DCHECK_LT(index, capacity());
    return &elements_[index];
  }

  const Record* record(int index) const {
    DCHECK_LT(index, capacity());
    return &elements_[index];
  }

  void UpdateAfterEvacuation(PtrComprCageBase cage_base);
  void UpdateAfterEvacuation(PtrComprCageBase cage_base, int up_to_index);

 private:
  const int capacity_;
  Record elements_[1];
};

class StringForwardingTable::BlockVector {
 public:
  using Block = StringForwardingTable::Block;
  using Allocator = std::allocator<Block*>;

  explicit BlockVector(size_t capacity);
  ~BlockVector();
  size_t capacity() const { return capacity_; }

  Block* LoadBlock(size_t index, AcquireLoadTag) {
    DCHECK_LT(index, size());
    return base::AsAtomicPointer::Acquire_Load(&begin_[index]);
  }

  Block* LoadBlock(size_t index) {
    DCHECK_LT(index, size());
    return begin_[index];
  }

  void AddBlock(std::unique_ptr<Block> block) {
    DCHECK_LT(size(), capacity());
    base::AsAtomicPointer::Release_Store(&begin_[size_], block.release());
    size_++;
  }

  static std::unique_ptr<BlockVector> Grow(BlockVector* data, size_t capacity,
                                           const base::Mutex& mutex);

  size_t size() const { return size_; }

 private:
  V8_NO_UNIQUE_ADDRESS Allocator allocator_;
  const size_t capacity_;
  std::atomic<size_t> size_;
  Block** begin_;
};

int StringForwardingTable::size() const { return next_free_index_; }
bool StringForwardingTable::empty() const { return size() == 0; }

// static
uint32_t StringForwardingTable::BlockForIndex(int index,
                                              uint32_t* index_in_block) {
  DCHECK_GE(index, 0);
  DCHECK_NOT_NULL(index_in_block);
  // The block is the leftmost set bit of the index, corrected by the size
  // of the first block.
  const uint32_t block_index =
      kBitsPerInt -
      base::bits::CountLeadingZeros(
          static_cast<uint32_t>(index + kInitialBlockSize)) -
      kInitialBlockSizeHighestBit - 1;
  *index_in_block = IndexInBlock(index, block_index);
  return block_index;
}

// static
uint32_t StringForwardingTable::IndexInBlock(int index, uint32_t block_index) {
  DCHECK_GE(index, 0);
  // Clear out the leftmost set bit (the block index) to get the index within
  // the block.
  return static_cast<uint32_t>(index + kInitialBlockSize) &
         ~(1u << (block_index + kInitialBlockSizeHighestBit));
}

// static
uint32_t StringForwardingTable::CapacityForBlock(uint32_t block_index) {
  return 1u << (block_index + kInitialBlockSizeHighestBit);
}

template <typename Func>
void StringForwardingTable::IterateElements(Isolate* isolate, Func&& callback) {
  isolate->heap()->safepoint()->AssertActive();
  DCHECK_NE(isolate->heap()->gc_state(), Heap::NOT_IN_GC);

  if (empty()) return;
  BlockVector* blocks = blocks_.load(std::memory_order_relaxed);
  const uint32_t last_block_index = static_cast<uint32_t>(blocks->size() - 1);
  for (uint32_t block_index = 0; block_index < last_block_index;
       ++block_index) {
    Block* block = blocks->LoadBlock(block_index);
    for (int index = 0; index < block->capacity(); ++index) {
      Record* rec = block->record(index);
      callback(rec);
    }
  }
  // Handle last block separately, as it is not filled to capacity.
  const uint32_t max_index = IndexInBlock(size() - 1, last_block_index) + 1;
  Block* block = blocks->LoadBlock(last_block_index);
  for (uint32_t index = 0; index < max_index; ++index) {
    Record* rec = block->record(index);
    callback(rec);
  }
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_STRING_FORWARDING_TABLE_INL_H_
