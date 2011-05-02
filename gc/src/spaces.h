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

#ifndef V8_SPACES_H_
#define V8_SPACES_H_

#include "list-inl.h"
#include "log.h"

namespace v8 {
namespace internal {

class Isolate;

// -----------------------------------------------------------------------------
// Heap structures:
//
// A JS heap consists of a young generation, an old generation, and a large
// object space. The young generation is divided into two semispaces. A
// scavenger implements Cheney's copying algorithm. The old generation is
// separated into a map space and an old object space. The map space contains
// all (and only) map objects, the rest of old objects go into the old space.
// The old generation is collected by a mark-sweep-compact collector.
//
// The semispaces of the young generation are contiguous.  The old and map
// spaces consists of a list of pages. A page has a page header and an object
// area.
//
// There is a separate large object space for objects larger than
// Page::kMaxHeapObjectSize, so that they do not have to move during
// collection. The large object space is paged. Pages in large object space
// may be larger than the page size.
//
// A store-buffer based write barrier is used to keep track of intergenerational
// references.  See store-buffer.h.
//
// During scavenges and mark-sweep collections we sometimes (after a store
// buffer overflow) iterate intergenerational pointers without decoding heap
// object maps so if the page belongs to old pointer space or large object
// space it is essential to guarantee that the page does not contain any
// garbage pointers to new space: every pointer aligned word which satisfies
// the Heap::InNewSpace() predicate must be a pointer to a live heap object in
// new space. Thus objects in old pointer and large object spaces should have a
// special layout (e.g. no bare integer fields). This requirement does not
// apply to map space which is iterated in a special fashion. However we still
// require pointer fields of dead maps to be cleaned.
//
// To enable lazy cleaning of old space pages we can mark chunks of the page
// as being garbage.  Garbage sections are marked with a special map.  These
// sections are skipped when scanning the page, even if we are otherwise
// scanning without regard for object boundaries.  Garbage sections are chained
// together to form a free list after a GC.  Garbage sections created outside
// of GCs by object trunctation etc. may not be in the free list chain.  Very
// small free spaces are ignored, they need only be cleaned of bogus pointers
// into new space.
//
// Each page may have up to one special garbage section.  The start of this
// section is denoted by the top field in the space.  The end of the section
// is denoted by the limit field in the space.  This special garbage section
// is not marked with a free space map in the data.  The point of this section
// is to enable linear allocation without having to constantly update the byte
// array every time the top field is updated and a new object is created.  The
// special garbage section is not in the chain of garbage sections.
//
// Since the top and limit fields are in the space, not the page, only one page
// has a special garbage section, and if the top and limit are equal then there
// is no special garbage section.

// Some assertion macros used in the debugging mode.

#define ASSERT_PAGE_ALIGNED(address)                                           \
  ASSERT((OffsetFrom(address) & Page::kPageAlignmentMask) == 0)

#define ASSERT_OBJECT_ALIGNED(address)                                         \
  ASSERT((OffsetFrom(address) & kObjectAlignmentMask) == 0)

#define ASSERT_MAP_ALIGNED(address)                                            \
  ASSERT((OffsetFrom(address) & kMapAlignmentMask) == 0)

#define ASSERT_OBJECT_SIZE(size)                                               \
  ASSERT((0 < size) && (size <= Page::kMaxHeapObjectSize))

#define ASSERT_PAGE_OFFSET(offset)                                             \
  ASSERT((Page::kObjectStartOffset <= offset)                                  \
      && (offset <= Page::kPageSize))

#define ASSERT_MAP_PAGE_INDEX(index)                                           \
  ASSERT((0 <= index) && (index <= MapSpace::kMaxMapPageIndex))


class PagedSpace;
class MemoryAllocator;
class AllocationInfo;
class Space;
class OldSpaceFreeList;


// TODO(gc): Check that this all gets inlined and register allocated on
// all platforms.
class MarkBit {
 public:
  typedef uint32_t CellType;

  inline MarkBit(CellType* cell, CellType mask, bool data_only)
      : cell_(cell), mask_(mask), data_only_(data_only) { }

  inline CellType* cell() { return cell_; }
  inline CellType mask() { return mask_; }

#ifdef DEBUG
  bool operator==(const MarkBit& other) {
    return cell_ == other.cell_ && mask_ == other.mask_;
  }
#endif

  inline void Set() { *cell_ |= mask_; }
  inline bool Get() { return (*cell_ & mask_) != 0; }
  inline void Clear() { *cell_ &= ~mask_; }

  inline bool data_only() { return data_only_; }

  inline MarkBit Next() {
    CellType new_mask = mask_ << 1;
    if (new_mask == 0) {
      return MarkBit(cell_ + 1, 1, data_only_);
    } else {
      return MarkBit(cell_, new_mask, data_only_);
    }
  }

 private:
  CellType* cell_;
  CellType mask_;
  // This boolean indicates that the object is in a data-only space with no
  // pointers.  This enables some optimizations when marking.
  // It is expected that this field is inlined and turned into control flow
  // at the place where the MarkBit object is created.
  bool data_only_;
};


// Bitmap is a sequence of cells each containing fixed number of bits.
template<typename StorageDescriptor>
class Bitmap {
 public:
  static const uint32_t kBitsPerCell = 32;
  static const uint32_t kBitsPerCellLog2 = 5;
  static const uint32_t kBitIndexMask = kBitsPerCell - 1;

  static int CellsForLength(int length) {
    return (length + kBitsPerCell - 1) >> kBitsPerCellLog2;
  }

  int CellsCount() {
    return StorageDescriptor::CellsCount(this->address());
  }

  static int SizeFor(int cells_count) {
    return sizeof(MarkBit::CellType)*cells_count;
  }

  INLINE(static uint32_t IndexToCell(uint32_t index)) {
    return index >> kBitsPerCellLog2;
  }

  INLINE(static uint32_t CellToIndex(uint32_t index)) {
    return index << kBitsPerCellLog2;
  }

  INLINE(static uint32_t CellAlignIndex(uint32_t index)) {
    return (index + kBitIndexMask) & ~kBitIndexMask;
  }

  INLINE(MarkBit::CellType* cells()) {
    return reinterpret_cast<MarkBit::CellType*>(this);
  }

  INLINE(Address address()) {
    return reinterpret_cast<Address>(this);
  }

  INLINE(static Bitmap* FromAddress(Address addr)) {
    return reinterpret_cast<Bitmap*>(addr);
  }

  inline MarkBit MarkBitFromIndex(uint32_t index, bool data_only = false) {
    MarkBit::CellType mask = 1 << (index & kBitIndexMask);
    MarkBit::CellType* cell = this->cells() + (index >> kBitsPerCellLog2);
    return MarkBit(cell, mask, data_only);
  }

  INLINE(void ClearRange(uint32_t start, uint32_t size)) {
    const uint32_t end = start + size;
    const uint32_t start_cell = start >> kBitsPerCellLog2;
    const uint32_t end_cell = end >> kBitsPerCellLog2;
    ASSERT((start & kBitIndexMask) == 0);
    ASSERT((end & kBitIndexMask) == 0);

    ASSERT(static_cast<int>(start_cell) < CellsCount());
    ASSERT(static_cast<int>(end_cell) <= CellsCount());

    for (uint32_t cell = start_cell; cell < end_cell; cell++) {
      cells()[cell] = 0;
    }
  }

  INLINE(void Clear()) {
    for (int i = 0; i < CellsCount(); i++) cells()[i] = 0;
  }

  static void PrintWord(uint32_t word, uint32_t himask = 0) {
    for (uint32_t mask = 1; mask != 0; mask <<= 1) {
      if ((mask & himask) != 0) PrintF("[");
      PrintF((mask & word) ? "1" : "0");
      if ((mask & himask) != 0) PrintF("]");
    }
  }

  class CellPrinter {
   public:
    CellPrinter() : seq_start(0), seq_type(0), seq_length(0) { }

    void Print(uint32_t pos, uint32_t cell) {
      if (cell == seq_type) {
        seq_length++;
        return;
      }

      Flush();

      if (IsSeq(cell)) {
        seq_start = pos;
        seq_length = 0;
        seq_type = cell;
        return;
      }

      PrintF("%d: ", pos);
      PrintWord(cell);
      PrintF("\n");
    }

    void Flush() {
      if (seq_length > 0) {
        PrintF("%d: %dx%d\n",
               seq_start,
               seq_type == 0 ? 0 : 1,
               seq_length * kBitsPerCell);
        seq_length = 0;
      }
    }

    static bool IsSeq(uint32_t cell) { return cell == 0 || cell == 0xFFFFFFFF; }
   private:
    uint32_t seq_start;
    uint32_t seq_type;
    uint32_t seq_length;
  };

  void Print() {
    CellPrinter printer;
    for (int i = 0; i < CellsCount(); i++) {
      printer.Print(i, cells()[i]);
    }
    printer.Flush();
    PrintF("\n");
  }

  bool IsClean() {
    for (int i = 0; i < CellsCount(); i++) {
      if (cells()[i] != 0) return false;
    }
    return true;
  }
};


// MemoryChunk represents a memory region owned by a specific space.
// It is divided into the header and the body. Chunk start is always
// 1MB aligned. Start of the body is aligned so it can accomodate
// any heap object.
class MemoryChunk {
 public:
  // Only works if the pointer is in the first kPageSize of the MemoryChunk.
  static MemoryChunk* FromAddress(Address a) {
    return reinterpret_cast<MemoryChunk*>(OffsetFrom(a) & ~kAlignmentMask);
  }

  // Only works for addresses in pointer spaces, not data or code spaces.
  static inline MemoryChunk* FromAnyPointerAddress(Address addr);

  Address address() { return reinterpret_cast<Address>(this); }

  bool is_valid() { return address() != NULL; }

  MemoryChunk* next_chunk() const { return next_chunk_; }
  MemoryChunk* prev_chunk() const { return prev_chunk_; }

  void set_next_chunk(MemoryChunk* next) { next_chunk_ = next; }
  void set_prev_chunk(MemoryChunk* prev) { prev_chunk_ = prev; }

  Space* owner() const {
    if ((reinterpret_cast<intptr_t>(owner_) & kFailureTagMask) ==
        kFailureTag) {
      return reinterpret_cast<Space*>(owner_ - kFailureTag);
    } else {
      return NULL;
    }
  }

  void set_owner(Space* space) {
    ASSERT((reinterpret_cast<intptr_t>(space) & kFailureTagMask) == 0);
    owner_ = reinterpret_cast<Address>(space) + kFailureTag;
    ASSERT((reinterpret_cast<intptr_t>(owner_) & kFailureTagMask) ==
           kFailureTag);
  }

  bool scan_on_scavenge() { return scan_on_scavenge_; }
  void set_scan_on_scavenge(bool scan) { scan_on_scavenge_ = scan; }

  int store_buffer_counter() { return store_buffer_counter_; }
  void set_store_buffer_counter(int counter) {
    store_buffer_counter_ = counter;
  }

  Address body() { return address() + kObjectStartOffset; }

  int body_size() { return size() - kObjectStartOffset; }

  bool Contains(Address addr) {
    return addr >= body() && addr < address() + size();
  }

  enum MemoryChunkFlags {
    IS_EXECUTABLE,
    WAS_SWEPT_CONSERVATIVELY,
    CONTAINS_ONLY_DATA,
    NUM_MEMORY_CHUNK_FLAGS
  };

  void SetFlag(int flag) {
    flags_ |= 1 << flag;
  }

  void ClearFlag(int flag) {
    flags_ &= ~(1 << flag);
  }

  bool IsFlagSet(int flag) {
    return (flags_ & (1 << flag)) != 0;
  }

  static const intptr_t kAlignment = (1 << kPageSizeBits);

  static const intptr_t kAlignmentMask = kAlignment - 1;

  static const size_t kHeaderSize = kPointerSize + kPointerSize + kPointerSize +
    kPointerSize + kPointerSize + kPointerSize + kPointerSize + kPointerSize;

  static const size_t kMarksBitmapLength =
    (1 << kPageSizeBits) >> (kPointerSizeLog2);

  static const size_t kMarksBitmapSize =
    (1 << kPageSizeBits) >> (kPointerSizeLog2 + kBitsPerByteLog2);

  static const int kBodyOffset =
    CODE_POINTER_ALIGN(MAP_POINTER_ALIGN(kHeaderSize + kMarksBitmapSize));

  // The start offset of the object area in a page. Aligned to both maps and
  // code alignment to be suitable for both.  Also aligned to 32 words because
  // the marking bitmap is arranged in 32 bit chunks.
  static const int kObjectStartAlignment = 32 * kPointerSize;
  static const int kObjectStartOffset = kBodyOffset - 1 +
      (kObjectStartAlignment - (kBodyOffset - 1) % kObjectStartAlignment);

  size_t size() const { return size_; }

  Executability executable() {
    return IsFlagSet(IS_EXECUTABLE) ? EXECUTABLE : NOT_EXECUTABLE;
  }

  bool ContainsOnlyData() {
    return IsFlagSet(CONTAINS_ONLY_DATA);
  }

  // ---------------------------------------------------------------------
  // Markbits support

  class BitmapStorageDescriptor {
   public:
    INLINE(static int CellsCount(Address addr)) {
      return Bitmap<BitmapStorageDescriptor>::CellsForLength(
          kMarksBitmapLength);
    }
  };

  typedef Bitmap<BitmapStorageDescriptor> MarkbitsBitmap;

  inline MarkbitsBitmap* markbits() {
    return MarkbitsBitmap::FromAddress(address() + kHeaderSize);
  }

  void PrintMarkbits() { markbits()->Print(); }

  inline uint32_t AddressToMarkbitIndex(Address addr) {
    return static_cast<uint32_t>(addr - this->address()) >> kPointerSizeLog2;
  }

  inline static uint32_t FastAddressToMarkbitIndex(Address addr) {
    const intptr_t offset =
        reinterpret_cast<intptr_t>(addr) & kAlignmentMask;

    return static_cast<uint32_t>(offset) >> kPointerSizeLog2;
  }

  inline Address MarkbitIndexToAddress(uint32_t index) {
    return this->address() + (index << kPointerSizeLog2);
  }

  void InsertAfter(MemoryChunk* other);
  void Unlink();

  inline Heap* heap() { return heap_; }

 protected:
  MemoryChunk* next_chunk_;
  MemoryChunk* prev_chunk_;
  size_t size_;
  intptr_t flags_;
  // The identity of the owning space.  This is tagged as a failure pointer, but
  // no failure can be in an object, so this can be distinguished from any entry
  // in a fixed array.
  Address owner_;
  Heap* heap_;
  // This flag indicates that the page is not being tracked by the store buffer.
  // At any point where we have to iterate over pointers to new space, we must
  // search this page for pointers to new space.
  bool scan_on_scavenge_;
  // Used by the store buffer to keep track of which pages to mark scan-on-
  // scavenge.
  int store_buffer_counter_;

  static MemoryChunk* Initialize(Heap* heap,
                                 Address base,
                                 size_t size,
                                 Executability executable,
                                 Space* owner);

  friend class MemoryAllocator;
};

STATIC_CHECK(sizeof(MemoryChunk) <= MemoryChunk::kHeaderSize);

// -----------------------------------------------------------------------------
// A page is a memory chunk of a size 1MB. Large object pages may be larger.
//
// The only way to get a page pointer is by calling factory methods:
//   Page* p = Page::FromAddress(addr); or
//   Page* p = Page::FromAllocationTop(top);
class Page : public MemoryChunk {
 public:
  // Returns the page containing a given address. The address ranges
  // from [page_addr .. page_addr + kPageSize[
  // This only works if the object is in fact in a page.  See also MemoryChunk::
  // FromAddress() and FromAnyAddress().
  INLINE(static Page* FromAddress(Address a)) {
    return reinterpret_cast<Page*>(OffsetFrom(a) & ~kPageAlignmentMask);
  }

  // Returns the page containing an allocation top. Because an allocation
  // top address can be the upper bound of the page, we need to subtract
  // it with kPointerSize first. The address ranges from
  // [page_addr + kObjectStartOffset .. page_addr + kPageSize].
  INLINE(static Page* FromAllocationTop(Address top)) {
    Page* p = FromAddress(top - kPointerSize);
    ASSERT_PAGE_OFFSET(p->Offset(top));
    return p;
  }

  // Returns the next page in the chain of pages owned by a space.
  inline Page* next_page();
  inline Page* prev_page();
  inline void set_next_page(Page* page);
  inline void set_prev_page(Page* page);

  // Returns the start address of the object area in this page.
  Address ObjectAreaStart() { return address() + kObjectStartOffset; }

  // Returns the end address (exclusive) of the object area in this page.
  Address ObjectAreaEnd() { return address() + Page::kPageSize; }

  // Checks whether an address is page aligned.
  static bool IsAlignedToPageSize(Address a) {
    return 0 == (OffsetFrom(a) & kPageAlignmentMask);
  }

  // Returns the offset of a given address to this page.
  INLINE(int Offset(Address a)) {
    int offset = static_cast<int>(a - address());
    ASSERT_PAGE_OFFSET(offset);
    return offset;
  }

  // Returns the address for a given offset to the this page.
  Address OffsetToAddress(int offset) {
    ASSERT_PAGE_OFFSET(offset);
    return address() + offset;
  }

  // ---------------------------------------------------------------------

  // Page size in bytes.  This must be a multiple of the OS page size.
  static const int kPageSize = 1 << kPageSizeBits;

  // Page size mask.
  static const intptr_t kPageAlignmentMask = (1 << kPageSizeBits) - 1;

  // Object area size in bytes.
  static const int kObjectAreaSize = kPageSize - kObjectStartOffset;

  // Maximum object size that fits in a page.
  static const int kMaxHeapObjectSize = kObjectAreaSize;

  static const int kFirstUsedCell =
    (kObjectStartOffset/kPointerSize) >> MarkbitsBitmap::kBitsPerCellLog2;

  static const int kLastUsedCell =
    ((kPageSize - kPointerSize)/kPointerSize) >>
      MarkbitsBitmap::kBitsPerCellLog2;

  inline void ClearGCFields();

  static inline Page* Initialize(Heap* heap,
                                 MemoryChunk* chunk,
                                 Executability executable,
                                 PagedSpace* owner);

  void InitializeAsAnchor(PagedSpace* owner);

  friend class MemoryAllocator;
};


STATIC_CHECK(sizeof(Page) <= MemoryChunk::kHeaderSize);


class LargePage : public MemoryChunk {
 public:
  HeapObject* GetObject() {
    return HeapObject::FromAddress(body());
  }

  inline LargePage* next_page() const {
    return static_cast<LargePage*>(next_chunk());
  }

  inline void set_next_page(LargePage* page) {
    set_next_chunk(page);
  }
 private:
  static LargePage* Initialize(Heap* heap,
                               MemoryChunk* chunk) {
    // TODO(gc) ISOLATESMERGE initialize chunk to point to heap?
    return static_cast<LargePage*>(chunk);
  }

  friend class MemoryAllocator;
};

STATIC_CHECK(sizeof(LargePage) <= MemoryChunk::kHeaderSize);

// ----------------------------------------------------------------------------
// Space is the abstract superclass for all allocation spaces.
class Space : public Malloced {
 public:
  Space(Heap* heap, AllocationSpace id, Executability executable)
      : heap_(heap), id_(id), executable_(executable) {}

  virtual ~Space() {}

  Heap* heap() const { return heap_; }

  // Does the space need executable memory?
  Executability executable() { return executable_; }

  // Identity used in error reporting.
  AllocationSpace identity() { return id_; }

  // Returns allocated size.
  virtual intptr_t Size() = 0;

  // Returns size of objects. Can differ from the allocated size
  // (e.g. see LargeObjectSpace).
  virtual intptr_t SizeOfObjects() { return Size(); }

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect the space by marking it read-only/writable.
  virtual void Protect() = 0;
  virtual void Unprotect() = 0;
#endif

#ifdef DEBUG
  virtual void Print() = 0;
#endif

  // After calling this we can allocate a certain number of bytes using only
  // linear allocation (with a LinearAllocationScope and an AlwaysAllocateScope)
  // without using freelists or causing a GC.  This is used by partial
  // snapshots.  It returns true of space was reserved or false if a GC is
  // needed.  For paged spaces the space requested must include the space wasted
  // at the end of each when allocating linearly.
  virtual bool ReserveSpace(int bytes) = 0;

 private:
  Heap* heap_;
  AllocationSpace id_;
  Executability executable_;
};


// ----------------------------------------------------------------------------
// All heap objects containing executable code (code objects) must be allocated
// from a 2 GB range of memory, so that they can call each other using 32-bit
// displacements.  This happens automatically on 32-bit platforms, where 32-bit
// displacements cover the entire 4GB virtual address space.  On 64-bit
// platforms, we support this using the CodeRange object, which reserves and
// manages a range of virtual memory.
class CodeRange {
 public:
  // Reserves a range of virtual memory, but does not commit any of it.
  // Can only be called once, at heap initialization time.
  // Returns false on failure.
  bool Setup(const size_t requested_size);

  // Frees the range of virtual memory, and frees the data structures used to
  // manage it.
  void TearDown();

  bool exists() { return code_range_ != NULL; }
  bool contains(Address address) {
    if (code_range_ == NULL) return false;
    Address start = static_cast<Address>(code_range_->address());
    return start <= address && address < start + code_range_->size();
  }

  // Allocates a chunk of memory from the large-object portion of
  // the code range.  On platforms with no separate code range, should
  // not be called.
  MUST_USE_RESULT Address AllocateRawMemory(const size_t requested,
                                            size_t* allocated);
  void FreeRawMemory(Address buf, size_t length);

 private:
  CodeRange();

  // The reserved range of virtual memory that all code objects are put in.
  VirtualMemory* code_range_;
  // Plain old data class, just a struct plus a constructor.
  class FreeBlock {
   public:
    FreeBlock(Address start_arg, size_t size_arg)
        : start(start_arg), size(size_arg) {
      ASSERT(IsAddressAligned(start, MemoryChunk::kAlignment));
      ASSERT(size >= static_cast<size_t>(Page::kPageSize));
    }
    FreeBlock(void* start_arg, size_t size_arg)
        : start(static_cast<Address>(start_arg)), size(size_arg) {
      ASSERT(IsAddressAligned(start, MemoryChunk::kAlignment));
      ASSERT(size >= static_cast<size_t>(Page::kPageSize));
    }

    Address start;
    size_t size;
  };

  // Freed blocks of memory are added to the free list.  When the allocation
  // list is exhausted, the free list is sorted and merged to make the new
  // allocation list.
  List<FreeBlock> free_list_;
  // Memory is allocated from the free blocks on the allocation list.
  // The block at current_allocation_block_index_ is the current block.
  List<FreeBlock> allocation_list_;
  int current_allocation_block_index_;

  // Finds a block on the allocation list that contains at least the
  // requested amount of memory.  If none is found, sorts and merges
  // the existing free memory blocks, and searches again.
  // If none can be found, terminates V8 with FatalProcessOutOfMemory.
  void GetNextAllocationBlock(size_t requested);
  // Compares the start addresses of two free blocks.
  static int CompareFreeBlockAddress(const FreeBlock* left,
                                     const FreeBlock* right);

  friend class Isolate;

  Isolate* isolate_;

  DISALLOW_COPY_AND_ASSIGN(CodeRange);
};


// ----------------------------------------------------------------------------
// A space acquires chunks of memory from the operating system. The memory
// allocator allocated and deallocates pages for the paged heap spaces and large
// pages for large object space.
//
// Each space has to manage it's own pages.
//
class MemoryAllocator {
 public:
  // Initializes its internal bookkeeping structures.
  // Max capacity of the total space and executable memory limit.
  bool Setup(intptr_t max_capacity, intptr_t capacity_executable);

  void TearDown();

  Page* AllocatePage(PagedSpace* owner, Executability executable);

  LargePage* AllocateLargePage(intptr_t object_size,
                                      Executability executable,
                                      Space* owner);

  void Free(MemoryChunk* chunk);

  // Returns the maximum available bytes of heaps.
  intptr_t Available() { return capacity_ < size_ ? 0 : capacity_ - size_; }

  // Returns allocated spaces in bytes.
  intptr_t Size() { return size_; }

  // Returns the maximum available executable bytes of heaps.
  intptr_t AvailableExecutable() {
    if (capacity_executable_ < size_executable_) return 0;
    return capacity_executable_ - size_executable_;
  }

  // Returns allocated executable spaces in bytes.
  intptr_t SizeExecutable() { return size_executable_; }

  // Returns maximum available bytes that the old space can have.
  intptr_t MaxAvailable() {
    return (Available() / Page::kPageSize) * Page::kObjectAreaSize;
  }

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect a block of memory by marking it read-only/writable.
  inline void Protect(Address start, size_t size);
  inline void Unprotect(Address start, size_t size,
                        Executability executable);

  // Protect/unprotect a chunk given a page in the chunk.
  inline void ProtectChunkFromPage(Page* page);
  inline void UnprotectChunkFromPage(Page* page);
#endif

#ifdef DEBUG
  // Reports statistic info of the space.
  void ReportStatistics();
#endif

  MemoryChunk* AllocateChunk(intptr_t body_size,
                             Executability executable,
                             Space* space);

  Address AllocateAlignedMemory(const size_t requested,
                                size_t alignment,
                                Executability executable,
                                size_t* allocated_size);

  Address ReserveAlignedMemory(const size_t requested,
                               size_t alignment,
                               size_t* allocated_size);

  void FreeMemory(Address addr, size_t size, Executability executable);

  // Commit a contiguous block of memory from the initial chunk.  Assumes that
  // the address is not NULL, the size is greater than zero, and that the
  // block is contained in the initial chunk.  Returns true if it succeeded
  // and false otherwise.
  bool CommitBlock(Address start, size_t size, Executability executable);

  // Uncommit a contiguous block of memory [start..(start+size)[.
  // start is not NULL, the size is greater than zero, and the
  // block is contained in the initial chunk.  Returns true if it succeeded
  // and false otherwise.
  bool UncommitBlock(Address start, size_t size);

  // Zaps a contiguous block of memory [start..(start+size)[ thus
  // filling it up with a recognizable non-NULL bit pattern.
  void ZapBlock(Address start, size_t size);

  void PerformAllocationCallback(ObjectSpace space,
                                 AllocationAction action,
                                 size_t size);

  void AddMemoryAllocationCallback(MemoryAllocationCallback callback,
                                          ObjectSpace space,
                                          AllocationAction action);

  void RemoveMemoryAllocationCallback(
      MemoryAllocationCallback callback);

  bool MemoryAllocationCallbackRegistered(
      MemoryAllocationCallback callback);


  // TODO(gc) ISOLATSE
  Isolate* isolate_;

 private:

  // Maximum space size in bytes.
  size_t capacity_;
  // Maximum subset of capacity_ that can be executable
  size_t capacity_executable_;

  // Allocated space size in bytes.
  size_t size_;
  // Allocated executable space size in bytes.
  size_t size_executable_;

  struct MemoryAllocationCallbackRegistration {
    MemoryAllocationCallbackRegistration(MemoryAllocationCallback callback,
                                         ObjectSpace space,
                                         AllocationAction action)
        : callback(callback), space(space), action(action) {
    }
    MemoryAllocationCallback callback;
    ObjectSpace space;
    AllocationAction action;
  };

  // A List of callback that are triggered when memory is allocated or free'd
  List<MemoryAllocationCallbackRegistration>
      memory_allocation_callbacks_;

  // Initializes pages in a chunk. Returns the first page address.
  // This function and GetChunkId() are provided for the mark-compact
  // collector to rebuild page headers in the from space, which is
  // used as a marking stack and its page headers are destroyed.
  Page* InitializePagesInChunk(int chunk_id, int pages_in_chunk,
                               PagedSpace* owner);
};


// -----------------------------------------------------------------------------
// Interface for heap object iterator to be implemented by all object space
// object iterators.
//
// NOTE: The space specific object iterators also implements the own next()
//       method which is used to avoid using virtual functions
//       iterating a specific space.

class ObjectIterator : public Malloced {
 public:
  virtual ~ObjectIterator() { }

  virtual HeapObject* next_object() = 0;
};


// -----------------------------------------------------------------------------
// Heap object iterator in new/old/map spaces.
//
// A HeapObjectIterator iterates objects from the bottom of the given space
// to its top or from the bottom of the given page to its top.
//
// If objects are allocated in the page during iteration the iterator may
// or may not iterate over those objects.  The caller must create a new
// iterator in order to be sure to visit these new objects.
class HeapObjectIterator: public ObjectIterator {
 public:
  // Creates a new object iterator in a given space.
  // If the size function is not given, the iterator calls the default
  // Object::Size().
  explicit HeapObjectIterator(PagedSpace* space);
  HeapObjectIterator(PagedSpace* space, HeapObjectCallback size_func);
  HeapObjectIterator(Page* page, HeapObjectCallback size_func);

  // Advance to the next object, skipping free spaces and other fillers and
  // skipping the special garbage section of which there is one per space.
  // Returns NULL when the iteration has ended.
  inline HeapObject* Next() {
    do {
      HeapObject* next_obj = FromCurrentPage();
      if (next_obj != NULL) return next_obj;
    } while (AdvanceToNextPage());
    return NULL;
  }

  virtual HeapObject* next_object() {
    return Next();
  }

 private:
  enum PageMode { kOnePageOnly, kAllPagesInSpace };

  Address cur_addr_;  // Current iteration point.
  Address cur_end_;   // End iteration point.
  HeapObjectCallback size_func_;  // Size function or NULL.
  PagedSpace* space_;
  PageMode page_mode_;

  // Fast (inlined) path of next().
  inline HeapObject* FromCurrentPage();

  // Slow path of next(), goes into the next page.  Returns false if the
  // iteration has ended.
  bool AdvanceToNextPage();

  // Initializes fields.
  inline void Initialize(PagedSpace* owner,
                         Address start,
                         Address end,
                         PageMode mode,
                         HeapObjectCallback size_func);

#ifdef DEBUG
  // Verifies whether fields have valid values.
  void Verify();
#endif
};


// -----------------------------------------------------------------------------
// A PageIterator iterates the pages in a paged space.

class PageIterator BASE_EMBEDDED {
 public:
  explicit inline PageIterator(PagedSpace* space);

  inline bool has_next();
  inline Page* next();

 private:
  PagedSpace* space_;
  Page* prev_page_;  // Previous page returned.
  // Next page that will be returned.  Cached here so that we can use this
  // iterator for operations that deallocate pages.
  Page* next_page_;
};


// -----------------------------------------------------------------------------
// A space has a circular list of pages. The next page can be accessed via
// Page::next_page() call.

// An abstraction of allocation and relocation pointers in a page-structured
// space.
class AllocationInfo {
 public:
  Address top;  // Current allocation top.
  Address limit;  // Current allocation limit.

#ifdef DEBUG
  bool VerifyPagedAllocation() {
    return (Page::FromAllocationTop(top) == Page::FromAllocationTop(limit))
        && (top <= limit);
  }
#endif
};


// An abstraction of the accounting statistics of a page-structured space.
// The 'capacity' of a space is the number of object-area bytes (ie, not
// including page bookkeeping structures) currently in the space. The 'size'
// of a space is the number of allocated bytes, the 'waste' in the space is
// the number of bytes that are not allocated and not available to
// allocation without reorganizing the space via a GC (eg, small blocks due
// to internal fragmentation, top of page areas in map space), and the bytes
// 'available' is the number of unallocated bytes that are not waste.  The
// capacity is the sum of size, waste, and available.
//
// The stats are only set by functions that ensure they stay balanced. These
// functions increase or decrease one of the non-capacity stats in
// conjunction with capacity, or else they always balance increases and
// decreases to the non-capacity stats.
class AllocationStats BASE_EMBEDDED {
 public:
  AllocationStats() { Clear(); }

  // Zero out all the allocation statistics (ie, no capacity).
  void Clear() {
    capacity_ = 0;
    size_ = 0;
    waste_ = 0;
  }

  // Reset the allocation statistics (ie, available = capacity with no
  // wasted or allocated bytes).
  void Reset() {
    size_ = 0;
    waste_ = 0;
  }

  // Accessors for the allocation statistics.
  intptr_t Capacity() { return capacity_; }
  intptr_t Size() { return size_; }
  intptr_t Waste() { return waste_; }

  // Grow the space by adding available bytes.  They are initially marked as
  // being in use (part of the size), but will normally be immediately freed,
  // putting them on the free list and removing them from size_.
  void ExpandSpace(int size_in_bytes) {
    capacity_ += size_in_bytes;
    size_ += size_in_bytes;
    ASSERT(size_ >= 0);
  }

  // Allocate from available bytes (available -> size).
  void AllocateBytes(intptr_t size_in_bytes) {
    size_ += size_in_bytes;
    ASSERT(size_ >= 0);
  }

  // Free allocated bytes, making them available (size -> available).
  void DeallocateBytes(intptr_t size_in_bytes) {
    size_ -= size_in_bytes;
    ASSERT(size_ >= 0);
  }

  // Waste free bytes (available -> waste).
  void WasteBytes(int size_in_bytes) {
    size_ -= size_in_bytes;
    waste_ += size_in_bytes;
    ASSERT(size_ >= 0);
  }

 private:
  intptr_t capacity_;
  intptr_t size_;
  intptr_t waste_;
};


// -----------------------------------------------------------------------------
// Free lists for old object spaces
//
// Free-list nodes are free blocks in the heap.  They look like heap objects
// (free-list node pointers have the heap object tag, and they have a map like
// a heap object).  They have a size and a next pointer.  The next pointer is
// the raw address of the next free list node (or NULL).
class FreeListNode: public HeapObject {
 public:
  // Obtain a free-list node from a raw address.  This is not a cast because
  // it does not check nor require that the first word at the address is a map
  // pointer.
  static FreeListNode* FromAddress(Address address) {
    return reinterpret_cast<FreeListNode*>(HeapObject::FromAddress(address));
  }

  static inline bool IsFreeListNode(HeapObject* object);

  // Set the size in bytes, which can be read with HeapObject::Size().  This
  // function also writes a map to the first word of the block so that it
  // looks like a heap object to the garbage collector and heap iteration
  // functions.
  void set_size(int size_in_bytes);

  // Accessors for the next field.
  inline FreeListNode* next();
  inline FreeListNode** next_address();
  inline void set_next(FreeListNode* next);

  inline void Zap();

 private:
  static const int kNextOffset = POINTER_SIZE_ALIGN(FreeSpace::kHeaderSize);

  DISALLOW_IMPLICIT_CONSTRUCTORS(FreeListNode);
};


// The free list for the old space.  The free list is organized in such a way
// as to encourage objects allocated around the same time to be near each
// other.  The normal way to allocate is intended to be by bumping a 'top'
// pointer until it hits a 'limit' pointer.  When the limit is hit we need to
// find a new space to allocate from.  This is done with the free list, which
// is divided up into rough categories to cut down on waste.  Having finer
// categories would scatter allocation more.

// The old space free list is organized in categories.
// 1-31 words:  Such small free areas are discarded for efficiency reasons.
//     They can be reclaimed by the compactor.  However the distance between top
//     and limit may be this small.
// 32-255 words: There is a list of spaces this large.  It is used for top and
//     limit when the object we need to allocate is 1-31 words in size.  These
//     spaces are called small.
// 256-2047 words: There is a list of spaces this large.  It is used for top and
//     limit when the object we need to allocate is 32-255 words in size.  These
//     spaces are called medium.
// 1048-16383 words: There is a list of spaces this large.  It is used for top
//     and limit when the object we need to allocate is 256-2047 words in size.
//     These spaces are call large.
// At least 16384 words.  This list is for objects of 2048 words or larger.
//     Empty pages are added to this list.  These spaces are called huge.
class OldSpaceFreeList BASE_EMBEDDED {
 public:
  explicit OldSpaceFreeList(PagedSpace* owner);

  // Clear the free list.
  void Reset();

  // Return the number of bytes available on the free list.
  intptr_t available() { return available_; }

  // Place a node on the free list.  The block of size 'size_in_bytes'
  // starting at 'start' is placed on the free list.  The return value is the
  // number of bytes that have been lost due to internal fragmentation by
  // freeing the block.  Bookkeeping information will be written to the block,
  // ie, its contents will be destroyed.  The start address should be word
  // aligned, and the size should be a non-zero multiple of the word size.
  int Free(Address start, int size_in_bytes);

  // Allocate a block of size 'size_in_bytes' from the free list.  The block
  // is unitialized.  A failure is returned if no block is available.  The
  // number of bytes lost to fragmentation is returned in the output parameter
  // 'wasted_bytes'.  The size should be a non-zero multiple of the word size.
  MUST_USE_RESULT HeapObject* Allocate(int size_in_bytes);

  void MarkNodes();

#ifdef DEBUG
  void Zap();
  static intptr_t SumFreeList(FreeListNode* node);
  static int FreeListLength(FreeListNode* cur);
  intptr_t SumFreeLists();
  bool IsVeryLong();
#endif

 private:
  // The size range of blocks, in bytes.
  static const int kMinBlockSize = 3 * kPointerSize;
  static const int kMaxBlockSize = Page::kMaxHeapObjectSize;

  PagedSpace* owner_;

  // Total available bytes in all blocks on this free list.
  int available_;

  static const int kSmallListMin = 0x20 * kPointerSize;
  static const int kSmallListMax = 0xff * kPointerSize;
  static const int kMediumListMax = 0x7ff * kPointerSize;
  static const int kLargeListMax = 0x3fff * kPointerSize;
  static const int kSmallAllocationMax = kSmallListMin - kPointerSize;
  static const int kMediumAllocationMax = kSmallListMax;
  static const int kLargeAllocationMax = kMediumListMax;
  FreeListNode* small_list_;
  FreeListNode* medium_list_;
  FreeListNode* large_list_;
  FreeListNode* huge_list_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(OldSpaceFreeList);
};


class PagedSpace : public Space {
 public:
  // Creates a space with a maximum capacity, and an id.
  PagedSpace(Heap* heap,
             intptr_t max_capacity,
             AllocationSpace id,
             Executability executable);

  virtual ~PagedSpace() {}

  // Set up the space using the given address range of virtual memory (from
  // the memory allocator's initial chunk) if possible.  If the block of
  // addresses is not big enough to contain a single page-aligned page, a
  // fresh chunk will be allocated.
  bool Setup();

  // Returns true if the space has been successfully set up and not
  // subsequently torn down.
  bool HasBeenSetup();

  // Cleans up the space, frees all pages in this space except those belonging
  // to the initial chunk, uncommits addresses in the initial chunk.
  void TearDown();

  // Checks whether an object/address is in this space.
  inline bool Contains(Address a);
  bool Contains(HeapObject* o) { return Contains(o->address()); }

  // Given an address occupied by a live object, return that object if it is
  // in this space, or Failure::Exception() if it is not. The implementation
  // iterates over objects in the page containing the address, the cost is
  // linear in the number of objects in the page. It may be slow.
  MUST_USE_RESULT MaybeObject* FindObject(Address addr);

  // Prepares for a mark-compact GC.
  virtual void PrepareForMarkCompact(bool will_compact);

  // Current capacity without growing (Size() + Available()).
  intptr_t Capacity() { return accounting_stats_.Capacity(); }

  // Total amount of memory committed for this space.  For paged
  // spaces this equals the capacity.
  intptr_t CommittedMemory() { return Capacity(); }

  // Sets the capacity, the available space and the wasted space to zero.
  // The stats are rebuilt during sweeping by adding each page to the
  // capacity and the size when it is encountered.  As free spaces are
  // discovered during the sweeping they are subtracted from the size and added
  // to the available and wasted totals.
  void ClearStats() { accounting_stats_.Clear(); }

  // Available bytes without growing.  These are the bytes on the free list.
  // The bytes in the linear allocation area are not included in this total
  // because updating the stats would slow down allocation.  New pages are
  // immediately added to the free list so they show up here.
  intptr_t Available() { return free_list_.available(); }

  // Allocated bytes in this space.  Garbage bytes that were not found due to
  // lazy sweeping are counted as being allocated!  The bytes in the current
  // linear allocation area (between top and limit) are also counted here.
  virtual intptr_t Size() { return accounting_stats_.Size(); }

  // As size, but the bytes in the current linear allocation area are not
  // included.
  virtual intptr_t SizeOfObjects() { return Size() - (limit() - top()); }

  // Wasted bytes in this space.  These are just the bytes that were thrown away
  // due to being too small to use for allocation.  They do not include the
  // free bytes that were not found at all due to lazy sweeping.
  virtual intptr_t Waste() { return accounting_stats_.Waste(); }

  // Returns the allocation pointer in this space.
  Address top() { return allocation_info_.top; }
  Address limit() { return allocation_info_.limit; }

  // Allocate the requested number of bytes in the space if possible, return a
  // failure object if not.
  MUST_USE_RESULT inline MaybeObject* AllocateRaw(int size_in_bytes);

  virtual bool ReserveSpace(int bytes);

  // Give a block of memory to the space's free list.  It might be added to
  // the free list or accounted as waste.
  // If add_to_freelist is false then just accounting stats are updated and
  // no attempt to add area to free list is made.
  void Free(Address start, int size_in_bytes) {
    int wasted = free_list_.Free(start, size_in_bytes);
    accounting_stats_.DeallocateBytes(size_in_bytes - wasted);
  }

  // Set space allocation info.
  void SetTop(Address top, Address limit) {
    ASSERT(top == limit ||
           Page::FromAddress(top) == Page::FromAddress(limit - 1));
    allocation_info_.top = top;
    allocation_info_.limit = limit;
  }

  void Allocate(int bytes) {
    accounting_stats_.AllocateBytes(bytes);
  }

  void IncreaseCapacity(int size) {
    accounting_stats_.ExpandSpace(size);
  }

  // Releases half of unused pages.
  void Shrink();

  // Ensures that the capacity is at least 'capacity'. Returns false on failure.
  bool EnsureCapacity(int capacity);

  // The dummy page that anchors the linked list of pages.
  Page *anchor() { return &anchor_; }

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect the space by marking it read-only/writable.
  void Protect();
  void Unprotect();
#endif

#ifdef DEBUG
  // Print meta info and objects in this space.
  virtual void Print();

  // Verify integrity of this space.
  virtual void Verify(ObjectVisitor* visitor);

  // Reports statistics for the space
  void ReportStatistics();

  // Overridden by subclasses to verify space-specific object
  // properties (e.g., only maps or free-list nodes are in map space).
  virtual void VerifyObject(HeapObject* obj) {}

  // Report code object related statistics
  void CollectCodeStatistics();
  static void ReportCodeStatistics();
  static void ResetCodeStatistics();
#endif

  bool was_swept_conservatively() { return was_swept_conservatively_; }
  void set_was_swept_conservatively(bool b) { was_swept_conservatively_ = b; }

 protected:
  // Maximum capacity of this space.
  intptr_t max_capacity_;

  // Accounting information for this space.
  AllocationStats accounting_stats_;

  // The dummy page that anchors the double linked list of pages.
  Page anchor_;

  // The space's free list.
  OldSpaceFreeList free_list_;

  // Normal allocation information.
  AllocationInfo allocation_info_;

  // Bytes of each page that cannot be allocated.  Possibly non-zero
  // for pages in spaces with only fixed-size objects.  Always zero
  // for pages in spaces with variable sized objects (those pages are
  // padded with free-list nodes).
  int page_extra_;

  bool was_swept_conservatively_;

  // Sets allocation pointer.  If the allocation pointer already pointed to a
  // non-zero-length area then that area may be returned to the free list.
  void SetAllocationInfo(Address start, Address end);

  // Expands the space by allocating a fixed number of pages. Returns false if
  // it cannot allocate requested number of pages from OS.
  bool Expand();

  // Generic fast case allocation function that tries linear allocation at the
  // address denoted by top in allocation_info_.
  inline HeapObject* AllocateLinearly(AllocationInfo* alloc_info,
                                      int size_in_bytes);

  // Slow path of AllocateRaw.  This function is space-dependent.
  MUST_USE_RESULT virtual HeapObject* SlowAllocateRaw(int size_in_bytes);

#ifdef DEBUG
  // Returns the number of total pages in this space.
  int CountTotalPages();
#endif

  friend class PageIterator;
};


#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
class NumberAndSizeInfo BASE_EMBEDDED {
 public:
  NumberAndSizeInfo() : number_(0), bytes_(0) {}

  int number() const { return number_; }
  void increment_number(int num) { number_ += num; }

  int bytes() const { return bytes_; }
  void increment_bytes(int size) { bytes_ += size; }

  void clear() {
    number_ = 0;
    bytes_ = 0;
  }

 private:
  int number_;
  int bytes_;
};


// HistogramInfo class for recording a single "bar" of a histogram.  This
// class is used for collecting statistics to print to stdout (when compiled
// with DEBUG) or to the log file (when compiled with
// ENABLE_LOGGING_AND_PROFILING).
class HistogramInfo: public NumberAndSizeInfo {
 public:
  HistogramInfo() : NumberAndSizeInfo() {}

  const char* name() { return name_; }
  void set_name(const char* name) { name_ = name; }

 private:
  const char* name_;
};
#endif


class NewSpacePage : public MemoryChunk {
 public:
  inline NewSpacePage* next_page() const {
    return static_cast<NewSpacePage*>(next_chunk());
  }

  inline void set_next_page(NewSpacePage* page) {
    set_next_chunk(page);
  }
 private:
  // Finds the NewSpacePage containg the given address.
  static NewSpacePage* FromAddress(Address address_in_page) {
    Address page_start =
        reinterpret_cast<Address>(reinterpret_cast<uintptr_t>(address_in_page) &
                                  ~Page::kPageAlignmentMask);
    return reinterpret_cast<NewSpacePage*>(page_start);
  }

  static NewSpacePage* Initialize(Heap* heap, Address start);

  friend class SemiSpace;
  friend class SemiSpaceIterator;
};


// -----------------------------------------------------------------------------
// SemiSpace in young generation
//
// A semispace is a contiguous chunk of memory holding page-like memory
// chunks. The mark-compact collector  uses the memory of the first page in
// the from space as a marking stack when tracing live objects.

class SemiSpace : public Space {
 public:
  // Constructor.
  explicit SemiSpace(Heap* heap) : Space(heap, NEW_SPACE, NOT_EXECUTABLE) {
    start_ = NULL;
    age_mark_ = NULL;
  }

  // Sets up the semispace using the given chunk.
  bool Setup(Address start, int initial_capacity, int maximum_capacity);

  // Tear down the space.  Heap memory was not allocated by the space, so it
  // is not deallocated here.
  void TearDown();

  // True if the space has been set up but not torn down.
  bool HasBeenSetup() { return start_ != NULL; }

  // Grow the size of the semispace by committing extra virtual memory.
  // Assumes that the caller has checked that the semispace has not reached
  // its maximum capacity (and thus there is space available in the reserved
  // address range to grow).
  bool Grow();

  // Grow the semispace to the new capacity.  The new capacity
  // requested must be larger than the current capacity.
  bool GrowTo(int new_capacity);

  // Shrinks the semispace to the new capacity.  The new capacity
  // requested must be more than the amount of used memory in the
  // semispace and less than the current capacity.
  bool ShrinkTo(int new_capacity);

  // Returns the start address of the space.
  Address low() {
    return NewSpacePage::FromAddress(start_)->body();
  }

  // Returns one past the end address of the space.
  Address high() {
    // TODO(gc): Change when there is more than one page.
    return current_page_->body() + current_page_->body_size();
  }

  // Age mark accessors.
  Address age_mark() { return age_mark_; }
  void set_age_mark(Address mark) { age_mark_ = mark; }

  // True if the address is in the address range of this semispace (not
  // necessarily below the allocation pointer).
  bool Contains(Address a) {
    return (reinterpret_cast<uintptr_t>(a) & address_mask_)
           == reinterpret_cast<uintptr_t>(start_);
  }

  // True if the object is a heap object in the address range of this
  // semispace (not necessarily below the allocation pointer).
  bool Contains(Object* o) {
    return (reinterpret_cast<uintptr_t>(o) & object_mask_) == object_expected_;
  }

  // The offset of an address from the beginning of the space.
  int SpaceOffsetForAddress(Address addr) {
    return static_cast<int>(addr - low());
  }

  // If we don't have these here then SemiSpace will be abstract.  However
  // they should never be called.
  virtual intptr_t Size() {
    UNREACHABLE();
    return 0;
  }

  virtual bool ReserveSpace(int bytes) {
    UNREACHABLE();
    return false;
  }

  bool is_committed() { return committed_; }
  bool Commit();
  bool Uncommit();

  NewSpacePage* first_page() { return NewSpacePage::FromAddress(start_); }
  NewSpacePage* current_page() { return current_page_; }

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect the space by marking it read-only/writable.
  virtual void Protect() {}
  virtual void Unprotect() {}
#endif

#ifdef DEBUG
  virtual void Print();
  virtual void Verify();
#endif

  // Returns the current capacity of the semi space.
  int Capacity() { return capacity_; }

  // Returns the maximum capacity of the semi space.
  int MaximumCapacity() { return maximum_capacity_; }

  // Returns the initial capacity of the semi space.
  int InitialCapacity() { return initial_capacity_; }

 private:
  // The current and maximum capacity of the space.
  int capacity_;
  int maximum_capacity_;
  int initial_capacity_;

  // The start address of the space.
  Address start_;
  // Used to govern object promotion during mark-compact collection.
  Address age_mark_;

  // Masks and comparison values to test for containment in this semispace.
  uintptr_t address_mask_;
  uintptr_t object_mask_;
  uintptr_t object_expected_;

  bool committed_;

  NewSpacePage* current_page_;

 public:
  TRACK_MEMORY("SemiSpace")
};


// A SemiSpaceIterator is an ObjectIterator that iterates over the active
// semispace of the heap's new space.  It iterates over the objects in the
// semispace from a given start address (defaulting to the bottom of the
// semispace) to the top of the semispace.  New objects allocated after the
// iterator is created are not iterated.
class SemiSpaceIterator : public ObjectIterator {
 public:
  // Create an iterator over the objects in the given space.  If no start
  // address is given, the iterator starts from the bottom of the space.  If
  // no size function is given, the iterator calls Object::Size().
  explicit SemiSpaceIterator(NewSpace* space);
  SemiSpaceIterator(NewSpace* space, HeapObjectCallback size_func);
  SemiSpaceIterator(NewSpace* space, Address start);

  HeapObject* next() {
    if (current_ == current_page_limit_) {
      // TODO(gc): Add something here when we have more than one page.
    }
    if (current_ == limit_) return NULL;

    HeapObject* object = HeapObject::FromAddress(current_);
    int size = (size_func_ == NULL) ? object->Size() : size_func_(object);

    current_ += size;
    return object;
  }

  // Implementation of the ObjectIterator functions.
  virtual HeapObject* next_object() { return next(); }

 private:
  void Initialize(NewSpace* space,
                  Address start,
                  Address end,
                  HeapObjectCallback size_func);

  // The semispace.
  SemiSpace* space_;
  // The current iteration point.
  Address current_;
  // The end of the current page.
  Address current_page_limit_;
  // The end of iteration.
  Address limit_;
  // The callback function.
  HeapObjectCallback size_func_;
};


// -----------------------------------------------------------------------------
// The young generation space.
//
// The new space consists of a contiguous pair of semispaces.  It simply
// forwards most functions to the appropriate semispace.

class NewSpace : public Space {
 public:
  // Constructor.
  explicit NewSpace(Heap* heap)
    : Space(heap, NEW_SPACE, NOT_EXECUTABLE),
      to_space_(heap),
      from_space_(heap) {}

  // Sets up the new space using the given chunk.
  bool Setup(int max_semispace_size);

  // Tears down the space.  Heap memory was not allocated by the space, so it
  // is not deallocated here.
  void TearDown();

  // True if the space has been set up but not torn down.
  bool HasBeenSetup() {
    return to_space_.HasBeenSetup() && from_space_.HasBeenSetup();
  }

  // Flip the pair of spaces.
  void Flip();

  // Grow the capacity of the semispaces.  Assumes that they are not at
  // their maximum capacity.
  void Grow();

  // Shrink the capacity of the semispaces.
  void Shrink();

  // True if the address or object lies in the address range of either
  // semispace (not necessarily below the allocation pointer).
  bool Contains(Address a) {
    return (reinterpret_cast<uintptr_t>(a) & address_mask_)
        == reinterpret_cast<uintptr_t>(start_);
  }
  bool Contains(Object* o) {
    return (reinterpret_cast<uintptr_t>(o) & object_mask_) == object_expected_;
  }

  // Return the allocated bytes in the active semispace.
  virtual intptr_t Size() { return static_cast<int>(top() - bottom()); }
  // The same, but returning an int.  We have to have the one that returns
  // intptr_t because it is inherited, but if we know we are dealing with the
  // new space, which can't get as big as the other spaces then this is useful:
  int SizeAsInt() { return static_cast<int>(Size()); }

  // Return the current capacity of a semispace.
  intptr_t Capacity() {
    ASSERT(to_space_.Capacity() == from_space_.Capacity());
    return to_space_.Capacity();
  }

  // Return the total amount of memory committed for new space.
  intptr_t CommittedMemory() {
    if (from_space_.is_committed()) return 2 * Capacity();
    return Capacity();
  }

  // Return the available bytes without growing in the active semispace.
  intptr_t Available() { return Capacity() - Size(); }

  // Return the maximum capacity of a semispace.
  int MaximumCapacity() {
    ASSERT(to_space_.MaximumCapacity() == from_space_.MaximumCapacity());
    return to_space_.MaximumCapacity();
  }

  // Returns the initial capacity of a semispace.
  int InitialCapacity() {
    ASSERT(to_space_.InitialCapacity() == from_space_.InitialCapacity());
    return to_space_.InitialCapacity();
  }

  // Return the address of the allocation pointer in the active semispace.
  Address top() { return allocation_info_.top; }
  // Return the address of the first object in the active semispace.
  Address bottom() { return to_space_.low(); }

  // Get the age mark of the inactive semispace.
  Address age_mark() { return from_space_.age_mark(); }
  // Set the age mark in the active semispace.
  void set_age_mark(Address mark) { to_space_.set_age_mark(mark); }

  // The start address of the space and a bit mask. Anding an address in the
  // new space with the mask will result in the start address.
  Address start() { return start_; }
  uintptr_t mask() { return address_mask_; }

  INLINE(uint32_t AddressToMarkbitIndex(Address addr)) {
    ASSERT(Contains(addr));
    ASSERT(IsAligned(OffsetFrom(addr), kPointerSize) ||
           IsAligned(OffsetFrom(addr) - 1, kPointerSize));
    return static_cast<uint32_t>(addr - start_) >> kPointerSizeLog2;
  }

  INLINE(Address MarkbitIndexToAddress(uint32_t index)) {
    return reinterpret_cast<Address>(index << kPointerSizeLog2);
  }

  // The allocation top and limit addresses.
  Address* allocation_top_address() { return &allocation_info_.top; }
  Address* allocation_limit_address() { return &allocation_info_.limit; }

  MUST_USE_RESULT MaybeObject* AllocateRaw(int size_in_bytes) {
    return AllocateRawInternal(size_in_bytes);
  }

  // Reset the allocation pointer to the beginning of the active semispace.
  void ResetAllocationInfo();

  void LowerInlineAllocationLimit(intptr_t step) {
    inline_alloction_limit_step_ = step;
    allocation_info_.limit = Min(
        allocation_info_.top + inline_alloction_limit_step_,
        allocation_info_.limit);
    top_on_previous_step_ = allocation_info_.top;
  }

  // Get the extent of the inactive semispace (for use as a marking stack).
  Address FromSpaceLow() { return from_space_.low(); }
  Address FromSpaceHigh() { return from_space_.high(); }

  // Get the extent of the active semispace (to sweep newly copied objects
  // during a scavenge collection).
  Address ToSpaceLow() { return to_space_.low(); }
  Address ToSpaceHigh() { return to_space_.high(); }

  // Offsets from the beginning of the semispaces.
  int ToSpaceOffsetForAddress(Address a) {
    return to_space_.SpaceOffsetForAddress(a);
  }
  int FromSpaceOffsetForAddress(Address a) {
    return from_space_.SpaceOffsetForAddress(a);
  }

  // True if the object is a heap object in the address range of the
  // respective semispace (not necessarily below the allocation pointer of the
  // semispace).
  bool ToSpaceContains(Object* o) { return to_space_.Contains(o); }
  bool FromSpaceContains(Object* o) { return from_space_.Contains(o); }

  bool ToSpaceContains(Address a) { return to_space_.Contains(a); }
  bool FromSpaceContains(Address a) { return from_space_.Contains(a); }

  virtual bool ReserveSpace(int bytes);

  // Resizes a sequential string which must be the most recent thing that was
  // allocated in new space.
  template <typename StringType>
  inline void ShrinkStringAtAllocationBoundary(String* string, int len);

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect the space by marking it read-only/writable.
  virtual void Protect();
  virtual void Unprotect();
#endif

#ifdef DEBUG
  // Verify the active semispace.
  virtual void Verify();
  // Print the active semispace.
  virtual void Print() { to_space_.Print(); }
#endif

#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
  // Iterates the active semispace to collect statistics.
  void CollectStatistics();
  // Reports previously collected statistics of the active semispace.
  void ReportStatistics();
  // Clears previously collected statistics.
  void ClearHistograms();

  // Record the allocation or promotion of a heap object.  Note that we don't
  // record every single allocation, but only those that happen in the
  // to space during a scavenge GC.
  void RecordAllocation(HeapObject* obj);
  void RecordPromotion(HeapObject* obj);
#endif

  // Return whether the operation succeded.
  bool CommitFromSpaceIfNeeded() {
    if (from_space_.is_committed()) return true;
    return from_space_.Commit();
  }

  bool UncommitFromSpace() {
    if (!from_space_.is_committed()) return true;
    return from_space_.Uncommit();
  }

 private:
  Address chunk_base_;
  uintptr_t chunk_size_;

  // The semispaces.
  SemiSpace to_space_;
  SemiSpace from_space_;

  // Start address and bit mask for containment testing.
  Address start_;
  uintptr_t address_mask_;
  uintptr_t object_mask_;
  uintptr_t object_expected_;

  // Allocation pointer and limit for normal allocation and allocation during
  // mark-compact collection.
  AllocationInfo allocation_info_;

  // When incremental marking is active we will set allocation_info_.limit
  // to be lower than actual limit and then will gradually increase it
  // in steps to guarantee that we do incremental marking steps even
  // when all allocation is performed from inlined generated code.
  intptr_t inline_alloction_limit_step_;

  Address top_on_previous_step_;

#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
  HistogramInfo* allocated_histogram_;
  HistogramInfo* promoted_histogram_;
#endif

  // Implementation of AllocateRaw.
  MUST_USE_RESULT inline MaybeObject* AllocateRawInternal(int size_in_bytes);

  friend class SemiSpaceIterator;

 public:
  TRACK_MEMORY("NewSpace")
};


// -----------------------------------------------------------------------------
// Old object space (excluding map objects)

class OldSpace : public PagedSpace {
 public:
  // Creates an old space object with a given maximum capacity.
  // The constructor does not allocate pages from OS.
  explicit OldSpace(Heap* heap,
                    intptr_t max_capacity,
                    AllocationSpace id,
                    Executability executable)
      : PagedSpace(heap, max_capacity, id, executable) {
    page_extra_ = 0;
  }

  // The limit of allocation for a page in this space.
  virtual Address PageAllocationLimit(Page* page) {
    return page->ObjectAreaEnd();
  }

  // Prepare for full garbage collection.  Resets the relocation pointer and
  // clears the free list.
  virtual void PrepareForMarkCompact(bool will_compact);

 public:
  TRACK_MEMORY("OldSpace")
};


// For contiguous spaces, top should be in the space (or at the end) and limit
// should be the end of the space.
#define ASSERT_SEMISPACE_ALLOCATION_INFO(info, space) \
  ASSERT((space).low() <= (info).top                  \
         && (info).top <= (space).high()              \
         && (info).limit <= (space).high())


// -----------------------------------------------------------------------------
// Old space for objects of a fixed size

class FixedSpace : public PagedSpace {
 public:
  FixedSpace(Heap* heap,
             intptr_t max_capacity,
             AllocationSpace id,
             int object_size_in_bytes,
             const char* name)
      : PagedSpace(heap, max_capacity, id, NOT_EXECUTABLE),
        object_size_in_bytes_(object_size_in_bytes),
        name_(name) {
    page_extra_ = Page::kObjectAreaSize % object_size_in_bytes;
  }

  // The limit of allocation for a page in this space.
  virtual Address PageAllocationLimit(Page* page) {
    return page->ObjectAreaEnd() - page_extra_;
  }

  int object_size_in_bytes() { return object_size_in_bytes_; }

  // Prepares for a mark-compact GC.
  virtual void PrepareForMarkCompact(bool will_compact);

  void MarkFreeListNodes() { free_list_.MarkNodes(); }

 protected:
  void ResetFreeList() {
    free_list_.Reset();
  }

 private:
  // The size of objects in this space.
  int object_size_in_bytes_;

  // The name of this space.
  const char* name_;
};


// -----------------------------------------------------------------------------
// Old space for all map objects

class MapSpace : public FixedSpace {
 public:
  // Creates a map space object with a maximum capacity.
  MapSpace(Heap* heap,
           intptr_t max_capacity,
           int max_map_space_pages,
           AllocationSpace id)
      : FixedSpace(heap, max_capacity, id, Map::kSize, "map"),
        max_map_space_pages_(max_map_space_pages) {
  }

  // Prepares for a mark-compact GC.
  virtual void PrepareForMarkCompact(bool will_compact);

  // Given an index, returns the page address.
  // TODO(gc): this limit is artifical just to keep code compilable
  static const int kMaxMapPageIndex = 1 << 16;

  // Are map pointers encodable into map word?
  bool MapPointersEncodable() {
    return false;
  }

  // Should be called after forced sweep to find out if map space needs
  // compaction.
  bool NeedsCompaction(int live_maps) {
    return false;  // TODO(gc): Bring back map compaction.
  }

 protected:
#ifdef DEBUG
  virtual void VerifyObject(HeapObject* obj);
#endif

 private:
  static const int kMapsPerPage = Page::kObjectAreaSize / Map::kSize;

  // Do map space compaction if there is a page gap.
  int CompactionThreshold() {
    return kMapsPerPage * (max_map_space_pages_ - 1);
  }

  const int max_map_space_pages_;

 public:
  TRACK_MEMORY("MapSpace")
};


// -----------------------------------------------------------------------------
// Old space for all global object property cell objects

class CellSpace : public FixedSpace {
 public:
  // Creates a property cell space object with a maximum capacity.
  CellSpace(Heap* heap, intptr_t max_capacity, AllocationSpace id)
      : FixedSpace(heap, max_capacity, id, JSGlobalPropertyCell::kSize, "cell")
  {}

 protected:
#ifdef DEBUG
  virtual void VerifyObject(HeapObject* obj);
#endif

 public:
  TRACK_MEMORY("CellSpace")
};


// -----------------------------------------------------------------------------
// Large objects ( > Page::kMaxHeapObjectSize ) are allocated and managed by
// the large object space. A large object is allocated from OS heap with
// extra padding bytes (Page::kPageSize + Page::kObjectStartOffset).
// A large object always starts at Page::kObjectStartOffset to a page.
// Large objects do not move during garbage collections.

class LargeObjectSpace : public Space {
 public:
  LargeObjectSpace(Heap* heap, AllocationSpace id);
  virtual ~LargeObjectSpace() {}

  // Initializes internal data structures.
  bool Setup();

  // Releases internal resources, frees objects in this space.
  void TearDown();

  // Allocates a (non-FixedArray, non-Code) large object.
  MUST_USE_RESULT MaybeObject* AllocateRaw(int size_in_bytes);
  // Allocates a large Code object.
  MUST_USE_RESULT MaybeObject* AllocateRawCode(int size_in_bytes);
  // Allocates a large FixedArray.
  MUST_USE_RESULT MaybeObject* AllocateRawFixedArray(int size_in_bytes);

  static intptr_t ObjectSizeFor(intptr_t chunk_size) {
    if (chunk_size <= (Page::kPageSize + Page::kObjectStartOffset)) return 0;
    return chunk_size - Page::kPageSize - Page::kObjectStartOffset;
  }

  // Available bytes for objects in this space.
  inline intptr_t Available();

  virtual intptr_t Size() {
    return size_;
  }

  virtual intptr_t SizeOfObjects() {
    return objects_size_;
  }

  int PageCount() {
    return page_count_;
  }

  // Finds an object for a given address, returns Failure::Exception()
  // if it is not found. The function iterates through all objects in this
  // space, may be slow.
  MaybeObject* FindObject(Address a);

  // Finds a large object page containing the given pc, returns NULL
  // if such a page doesn't exist.
  LargePage* FindPageContainingPc(Address pc);

  // Iterates over pointers to new space.
  void IteratePointersToNewSpace(ObjectSlotCallback func);

  // Frees unmarked objects.
  void FreeUnmarkedObjects();

  // Checks whether a heap object is in this space; O(1).
  bool Contains(HeapObject* obj);

  // Checks whether the space is empty.
  bool IsEmpty() { return first_page_ == NULL; }

  // See the comments for ReserveSpace in the Space class.  This has to be
  // called after ReserveSpace has been called on the paged spaces, since they
  // may use some memory, leaving less for large objects.
  virtual bool ReserveSpace(int bytes);

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect the space by marking it read-only/writable.
  void Protect();
  void Unprotect();
#endif

#ifdef DEBUG
  virtual void Verify();
  virtual void Print();
  void ReportStatistics();
  void CollectCodeStatistics();
#endif
  // Checks whether an address is in the object area in this space.  It
  // iterates all objects in the space. May be slow.
  bool SlowContains(Address addr) { return !FindObject(addr)->IsFailure(); }

 private:
  // The head of the linked list of large object chunks.
  LargePage* first_page_;
  intptr_t size_;  // allocated bytes
  int page_count_;  // number of chunks
  intptr_t objects_size_;  // size of objects

  // Shared implementation of AllocateRaw, AllocateRawCode and
  // AllocateRawFixedArray.
  MUST_USE_RESULT MaybeObject* AllocateRawInternal(int object_size,
                                                   Executability executable);

  friend class LargeObjectIterator;

 public:
  TRACK_MEMORY("LargeObjectSpace")
};


class LargeObjectIterator: public ObjectIterator {
 public:
  explicit LargeObjectIterator(LargeObjectSpace* space);
  LargeObjectIterator(LargeObjectSpace* space, HeapObjectCallback size_func);

  HeapObject* next();

  // implementation of ObjectIterator.
  virtual HeapObject* next_object() { return next(); }

 private:
  LargePage* current_;
  HeapObjectCallback size_func_;
};


// Iterates over the chunks (pages and large object pages) that can contain
// pointers to new space.
class PointerChunkIterator BASE_EMBEDDED {
 public:
  inline PointerChunkIterator();

  // Return NULL when the iterator is done.
  MemoryChunk* next() {
    switch (state_) {
      case kOldPointerState: {
        if (old_pointer_iterator_.has_next()) {
          return old_pointer_iterator_.next();
        }
        state_ = kMapState;
        // Fall through.
      }
      case kMapState: {
        if (map_iterator_.has_next()) {
          return map_iterator_.next();
        }
        state_ = kLargeObjectState;
        // Fall through.
      }
      case kLargeObjectState: {
        HeapObject* heap_object;
        do {
          heap_object = lo_iterator_.next();
          if (heap_object == NULL) {
            state_ = kFinishedState;
            return NULL;
          }
          // Fixed arrays are the only pointer-containing objects in large
          // object space.
        } while (!heap_object->IsFixedArray());
        MemoryChunk* answer = MemoryChunk::FromAddress(heap_object->address());
        return answer;
      }
      case kFinishedState:
        return NULL;
      default:
        break;
    }
    UNREACHABLE();
    return NULL;
  }


 private:
  enum State {
    kOldPointerState,
    kMapState,
    kLargeObjectState,
    kFinishedState
  };
  State state_;
  PageIterator old_pointer_iterator_;
  PageIterator map_iterator_;
  LargeObjectIterator lo_iterator_;
};


#ifdef DEBUG
struct CommentStatistic {
  const char* comment;
  int size;
  int count;
  void Clear() {
    comment = NULL;
    size = 0;
    count = 0;
  }
  // Must be small, since an iteration is used for lookup.
  static const int kMaxComments = 64;
};
#endif


} }  // namespace v8::internal

#endif  // V8_SPACES_H_
