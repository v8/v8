// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/init/vm-cage.h"

#include "include/v8-internal.h"
#include "src/base/bits.h"
#include "src/base/bounded-page-allocator.h"
#include "src/base/lazy-instance.h"
#include "src/base/utils/random-number-generator.h"
#include "src/flags/flags.h"
#include "src/utils/allocation.h"

#if defined(V8_OS_WIN)
#include <windows.h>
// This has to come after windows.h.
#include <versionhelpers.h>  // For IsWindows8Point1OrGreater().
#endif

namespace v8 {
namespace internal {

#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE

// A PageAllocator that allocates pages inside a given virtual address range
// like the BoundedPageAllocator, except that only a (small) part of the range
// has actually been reserved. As such, this allocator relies on page
// allocation hints for the OS to obtain pages inside the non-reserved part.
// This allocator is used on OSes where reserving virtual address space (and
// thus a virtual memory cage) is too expensive, notabley Windows pre 8.1.
class FakeBoundedPageAllocator : public v8::PageAllocator {
 public:
  FakeBoundedPageAllocator(v8::PageAllocator* page_allocator, Address start,
                           size_t size, size_t reserved_size)
      : page_allocator_(page_allocator),
        start_(start),
        size_(size),
        reserved_size_(reserved_size),
        end_of_reserved_region_(start + reserved_size) {
    // The size is required to be a power of two so that obtaining a random
    // address inside the managed region simply requires a fixed number of
    // random bits as offset.
    DCHECK(base::bits::IsPowerOfTwo(size));
    DCHECK_LT(reserved_size, size);

    if (FLAG_random_seed != 0) {
      rng_.SetSeed(FLAG_random_seed);
    }

    reserved_region_page_allocator_ =
        std::make_unique<base::BoundedPageAllocator>(
            page_allocator_, start_, reserved_size_,
            page_allocator_->AllocatePageSize(),
            base::PageInitializationMode::kAllocatedPagesMustBeZeroInitialized);
  }

  ~FakeBoundedPageAllocator() override = default;

  size_t AllocatePageSize() override {
    return page_allocator_->AllocatePageSize();
  }

  size_t CommitPageSize() override { return page_allocator_->CommitPageSize(); }

  void SetRandomMmapSeed(int64_t seed) override { rng_.SetSeed(seed); }

  void* GetRandomMmapAddr() override {
    // Generate a random number between 0 and size_, then add that to the start
    // address to obtain a random mmap address. We deliberately don't use our
    // provided page allocator's GetRandomMmapAddr here since that could be
    // biased, while we want uniformly distributed random numbers here.
    Address addr = rng_.NextInt64() % size_ + start_;
    addr = RoundDown(addr, AllocatePageSize());
    void* ptr = reinterpret_cast<void*>(addr);
    DCHECK(Contains(ptr, 1));
    return ptr;
  }

  void* AllocatePages(void* hint, size_t size, size_t alignment,
                      Permission access) override {
    DCHECK(IsAligned(size, AllocatePageSize()));
    DCHECK(IsAligned(alignment, AllocatePageSize()));

    // First, try allocating the memory inside the reserved region.
    void* ptr = reserved_region_page_allocator_->AllocatePages(
        hint, size, alignment, access);
    if (ptr) return ptr;

    // Then, fall back to allocating memory outside of the reserved region
    // through page allocator hints.

    // Somewhat arbitrary size limitation to ensure that the loop below for
    // finding a fitting base address hint terminates quickly.
    if (size >= size_ / 2) return nullptr;

    if (!hint || !Contains(hint, size)) hint = GetRandomMmapAddr();

    static constexpr int kMaxAttempts = 10;
    for (int i = 0; i < kMaxAttempts; i++) {
      // If the hint wouldn't result in the entire allocation being inside the
      // managed region, simply retry. There is at least a 50% chance of
      // getting a usable address due to the size restriction above.
      while (!Contains(hint, size)) {
        hint = GetRandomMmapAddr();
      }

      ptr = page_allocator_->AllocatePages(hint, size, alignment, access);
      if (ptr && Contains(ptr, size)) {
        return ptr;
      } else if (ptr) {
        page_allocator_->FreePages(ptr, size);
      }

      // Retry at a different address.
      hint = GetRandomMmapAddr();
    }

    return nullptr;
  }

  bool FreePages(void* address, size_t size) override {
    return AllocatorFor(address)->FreePages(address, size);
  }

  bool ReleasePages(void* address, size_t size, size_t new_length) override {
    return AllocatorFor(address)->ReleasePages(address, size, new_length);
  }

  bool SetPermissions(void* address, size_t size,
                      Permission permissions) override {
    return AllocatorFor(address)->SetPermissions(address, size, permissions);
  }

  bool DiscardSystemPages(void* address, size_t size) override {
    return AllocatorFor(address)->DiscardSystemPages(address, size);
  }

  bool DecommitPages(void* address, size_t size) override {
    return AllocatorFor(address)->DecommitPages(address, size);
  }

 private:
  bool Contains(void* ptr, size_t length) {
    Address addr = reinterpret_cast<Address>(ptr);
    return (addr >= start_) && ((addr + length) < (start_ + size_));
  }

  v8::PageAllocator* AllocatorFor(void* ptr) {
    Address addr = reinterpret_cast<Address>(ptr);
    if (addr < end_of_reserved_region_) {
      DCHECK_GE(addr, start_);
      return reserved_region_page_allocator_.get();
    } else {
      return page_allocator_;
    }
  }

  // The page allocator through which pages inside the region are allocated.
  v8::PageAllocator* const page_allocator_;
  // The bounded page allocator managing the sub-region that was actually
  // reserved.
  std::unique_ptr<base::BoundedPageAllocator> reserved_region_page_allocator_;

  // Random number generator for generating random addresses.
  base::RandomNumberGenerator rng_;

  // The start of the virtual memory region in which to allocate pages. This is
  // also the start of the sub-region that was reserved.
  const Address start_;
  // The total size of the address space in which to allocate pages.
  const size_t size_;
  // The size of the sub-region that has actually been reserved.
  const size_t reserved_size_;
  // The end of the sub-region that has actually been reserved.
  const Address end_of_reserved_region_;
};

static uintptr_t DetermineAddressSpaceLimit() {
  // TODO(saelo) should this also take things like rlimits into account?
#ifdef V8_TARGET_ARCH_64_BIT
  // TODO(saelo) this should be deteremined based on the CPU model being used
  // and its number of virtual address bits.
  uintptr_t virtual_address_bits = 48;
  // Virtual address space is split 50/50 between userspace and kernel
  uintptr_t userspace_virtual_address_bits = virtual_address_bits / 2;
  uintptr_t address_space_limit = 1UL << userspace_virtual_address_bits;
  return address_space_limit;
#else
#error Unsupported target architecture.
#endif
}

bool V8VirtualMemoryCage::Initialize(PageAllocator* page_allocator) {
  // TODO(saelo) We need to take the number of virtual address bits of the CPU
  // into account when deteriming the size of the cage. For example, if there
  // are only 39 bits available (some older Intel CPUs), split evenly between
  // userspace and kernel, then userspace can only address 256GB and so the
  // maximum cage size should probably be something around 64GB to 128GB.
  const size_t size = kVirtualMemoryCageSize;
#if defined(V8_OS_WIN)
  if (!IsWindows8Point1OrGreater()) {
    // On Windows pre 8.1, reserving virtual memory is an expensive operation,
    // apparently because the OS already charges for the memory required for
    // all page table entries. For example, a 1TB reservation increases private
    // memory usage by 2GB. As such, it is not possible to create a proper
    // virtual memory cage there and so a fake cage is created which doesn't
    // reserve most of the virtual memory, and so doesn't incur the cost, but
    // also doesn't provide the desired security benefits.
    const size_t size_to_reserve = kFakeVirtualMemoryCageMinReservationSize;
    return InitializeAsFakeCage(page_allocator, size, size_to_reserve);
  }
#endif
  // TODO(saelo) if this fails, we could still fall back to creating a fake
  // cage.
  const bool use_guard_regions = true;
  return Initialize(page_allocator, size, use_guard_regions);
}

bool V8VirtualMemoryCage::Initialize(v8::PageAllocator* page_allocator,
                                     size_t size, bool use_guard_regions) {
  CHECK(!initialized_);
  CHECK(!disabled_);
  CHECK(base::bits::IsPowerOfTwo(size));
  CHECK_GE(size, kVirtualMemoryCageMinimumSize);

  // Currently, we allow the cage to be smaller than the requested size. This
  // way, we can gracefully handle cage reservation failures during the initial
  // rollout and can collect data on how often these occur. In the future, we
  // will likely either require the cage to always have a fixed size or will
  // design CagedPointers (pointers that are guaranteed to point into the cage,
  // e.g. because they are stored as offsets from the cage base) in a way that
  // doesn't reduce the cage's security properties if it has a smaller size.
  // Which of these options is ultimately taken likey depends on how frequently
  // cage reservation failures occur in practice.
  size_t reservation_size;
  while (!reservation_base_ && size >= kVirtualMemoryCageMinimumSize) {
    reservation_size = size;
    if (use_guard_regions) {
      reservation_size += 2 * kVirtualMemoryCageGuardRegionSize;
    }

    // Technically, we should use kNoAccessWillJitLater here instead since the
    // cage will contain JIT pages. However, currently this is not required as
    // PA anyway uses MAP_JIT for V8 mappings. Further, we want to eventually
    // move JIT pages out of the cage, at which point we'd like to forbid
    // making pages inside the cage executable, and so don't want MAP_JIT.
    void* hint = page_allocator->GetRandomMmapAddr();
    reservation_base_ = reinterpret_cast<Address>(page_allocator->AllocatePages(
        hint, reservation_size, kVirtualMemoryCageAlignment,
        PageAllocator::kNoAccess));
    if (!reservation_base_) {
      size /= 2;
    }
  }

  if (!reservation_base_) return false;

  base_ = reservation_base_;
  if (use_guard_regions) {
    base_ += kVirtualMemoryCageGuardRegionSize;
  }

  page_allocator_ = page_allocator;
  size_ = size;
  reservation_size_ = reservation_size;

  cage_page_allocator_ = std::make_unique<base::BoundedPageAllocator>(
      page_allocator_, base_, size_, page_allocator_->AllocatePageSize(),
      base::PageInitializationMode::kAllocatedPagesMustBeZeroInitialized);

  initialized_ = true;
  is_fake_cage_ = false;

  return true;
}

bool V8VirtualMemoryCage::InitializeAsFakeCage(
    v8::PageAllocator* page_allocator, size_t size, size_t size_to_reserve) {
  CHECK(!initialized_);
  CHECK(!disabled_);
  CHECK(base::bits::IsPowerOfTwo(size));
  CHECK(base::bits::IsPowerOfTwo(size_to_reserve));
  CHECK_GE(size, kVirtualMemoryCageMinimumSize);
  CHECK_LT(size_to_reserve, size);

  // Use a custom random number generator here to ensure that we get uniformly
  // distributed random numbers. We figure out the available address space
  // ourselves, and so are potentially better positioned to determine a good
  // base address for the cage than the embedder-provided GetRandomMmapAddr().
  base::RandomNumberGenerator rng;
  if (FLAG_random_seed != 0) {
    rng.SetSeed(FLAG_random_seed);
  }

  // We try to ensure that base + size is still fully within the process'
  // address space, even though we only reserve a fraction of the memory.
  Address address_space_end = DetermineAddressSpaceLimit();
  DCHECK(base::bits::IsPowerOfTwo(address_space_end));
  Address highest_possible_address = address_space_end - size;
  constexpr int kMaxAttempts = 10;
  for (int i = 1; i <= kMaxAttempts; i++) {
    // The size of the cage is small relative to the size of the usable address
    // space, so we can just retry until we get a usable hint.
    Address hint;
    do {
      hint = rng.NextInt64() % address_space_end;
    } while (hint > highest_possible_address);

    // Align to page size.
    hint = RoundDown(hint, page_allocator->AllocatePageSize());

    reservation_base_ = reinterpret_cast<Address>(page_allocator->AllocatePages(
        reinterpret_cast<void*>(hint), size_to_reserve,
        kVirtualMemoryCageAlignment, PageAllocator::kNoAccess));

    if (!reservation_base_) return false;

    // Take this base if it meets the requirements or if this is the last
    // attempt.
    if (reservation_base_ <= highest_possible_address || i == kMaxAttempts)
      break;

    // Can't use this base, so free the reservation and try again
    page_allocator_->FreePages(reinterpret_cast<void*>(reservation_base_),
                               size_to_reserve);
    reservation_base_ = kNullAddress;
  }
  DCHECK(reservation_base_);

  base_ = reservation_base_;
  size_ = size;
  reservation_size_ = size_to_reserve;
  initialized_ = true;
  is_fake_cage_ = true;
  page_allocator_ = page_allocator;
  cage_page_allocator_ = std::make_unique<FakeBoundedPageAllocator>(
      page_allocator_, base_, size_, reservation_size_);

  return true;
}

void V8VirtualMemoryCage::TearDown() {
  if (initialized_) {
    cage_page_allocator_.reset();
    CHECK(page_allocator_->FreePages(reinterpret_cast<void*>(reservation_base_),
                                     reservation_size_));
    base_ = kNullAddress;
    size_ = 0;
    reservation_base_ = kNullAddress;
    reservation_size_ = 0;
    initialized_ = false;
    is_fake_cage_ = false;
    page_allocator_ = nullptr;
  }
  disabled_ = false;
}

#endif  // V8_VIRTUAL_MEMORY_CAGE_IS_AVAILABLE

#ifdef V8_VIRTUAL_MEMORY_CAGE
DEFINE_LAZY_LEAKY_OBJECT_GETTER(V8VirtualMemoryCage,
                                GetProcessWideVirtualMemoryCage)
#endif

}  // namespace internal
}  // namespace v8
