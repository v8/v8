// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_REGION_ALLOCATOR_H_
#define V8_BASE_REGION_ALLOCATOR_H_

#include <set>

#include "src/base/utils/random-number-generator.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace base {

// Helper class for managing used/free regions within [address, address+size)
// region. Minimum allocation unit is |min_region_size|.
// Requested allocation size is rounded up to |min_region_size|.
// The region allocation algorithm implements best-fit with coalescing strategy:
// it tries to find a smallest suitable free region upon allocation and tries
// to merge region with its neighbors upon freeing.
//
// This class does not perform any actual region reservation.
// Not thread-safe.
class V8_BASE_EXPORT RegionAllocator final {
 public:
  typedef uintptr_t Address;

  static constexpr Address kAllocationFailure = static_cast<Address>(-1);

  RegionAllocator(Address address, size_t size, size_t min_region_size);
  ~RegionAllocator();

  // Allocates region of |size| (must be |min_region_size|-aligned). Returns
  // the address of the region on success or kAllocationFailure.
  Address AllocateRegion(size_t size);
  // Same as above but tries to randomize the region displacement.
  Address AllocateRegion(RandomNumberGenerator* rng, size_t size);

  // Allocates region of |size| at |requested_address| if it's free. Both the
  // address and the size must be |min_region_size|-aligned. On success returns
  // true.
  // This kind of allocation is supposed to be used during setup phase to mark
  // certain regions as used or for randomizing regions displacement.
  bool AllocateRegionAt(Address requested_address, size_t size);

  // Frees region at given |address|, returns the size of the region.
  // The region must be previously allocated. Return 0 on failure.
  size_t FreeRegion(Address address);

  Address begin() const { return whole_region_.begin(); }
  Address end() const { return whole_region_.end(); }
  size_t size() const { return whole_region_.size(); }

  // Total size of not yet aquired regions.
  size_t free_size() const { return free_size_; }

  void Print(std::ostream& os) const;

 private:
  class Region {
   public:
    Address begin() const { return address_; }
    Address end() const { return address_ + size_; }

    size_t size() const { return size_; }
    void set_size(size_t size) { size_ = size; }

    bool contains(Address address) const {
      STATIC_ASSERT(std::is_unsigned<Address>::value);
      return (address - begin()) < size();
    }

    bool is_used() const { return is_used_; }
    void set_is_used(bool used) { is_used_ = used; }

    Region(Address address, size_t size, bool is_used)
        : address_(address), size_(size), is_used_(is_used) {}

    void Print(std::ostream& os) const;

   private:
    Address address_;
    size_t size_;
    bool is_used_;
  };

  // The whole region.
  const Region whole_region_;

  // Number of |min_region_size_| in the whole region.
  const size_t region_size_in_min_regions_;

  // If the free size is less than this value - stop trying to randomize the
  // allocation addresses.
  const size_t max_load_for_randomization_;

  // Size of all free regions.
  size_t free_size_;

  // Minimum region size. Must be a pow of 2.
  const size_t min_region_size_;

  struct AddressEndOrder {
    bool operator()(const Region* a, const Region* b) const {
      return a->end() < b->end();
    }
  };
  // All regions ordered by addresses.
  typedef std::set<Region*, AddressEndOrder> AllRegionsSet;
  AllRegionsSet all_regions_;

  struct SizeAddressOrder {
    bool operator()(const Region* a, const Region* b) const {
      if (a->size() != b->size()) return a->size() < b->size();
      return a->begin() < b->begin();
    }
  };
  // Free regions ordered by sizes and addresses.
  std::set<Region*, SizeAddressOrder> free_regions_;

  // Returns region containing given address or nullptr.
  AllRegionsSet::iterator FindRegion(Address address);

  // Adds given region to the set of free regions.
  void FreeListAddRegion(Region* region);

  // Finds best-fit free region for given size.
  Region* FreeListFindRegion(size_t size);

  // Removes given region from the set of free regions.
  void FreeListRemoveRegion(Region* region);

  // Splits given |region| into two: one of |new_size| size and a new one
  // having the rest. The new region is returned.
  Region* Split(Region* region, size_t new_size);

  // For two coalescing regions merges |next| to |prev| and deletes |next|.
  void Merge(AllRegionsSet::iterator prev_iter,
             AllRegionsSet::iterator next_iter);

  FRIEND_TEST(RegionAllocatorTest, AllocateRegionRandom);
  FRIEND_TEST(RegionAllocatorTest, Fragmentation);
  FRIEND_TEST(RegionAllocatorTest, FindRegion);

  DISALLOW_COPY_AND_ASSIGN(RegionAllocator);
};

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_REGION_ALLOCATOR_H_
