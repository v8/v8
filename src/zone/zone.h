// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ZONE_ZONE_H_
#define V8_ZONE_ZONE_H_

#include <limits>

#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/zone-segment.h"

#ifndef ZONE_NAME
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define ZONE_NAME __FILE__ ":" TOSTRING(__LINE__)
#endif

namespace v8 {
namespace internal {

// The Zone supports very fast allocation of small chunks of
// memory. The chunks cannot be deallocated individually, but instead
// the Zone supports deallocating all chunks in one fast
// operation. The Zone is used to hold temporary data structures like
// the abstract syntax tree, which is deallocated after compilation.
//
// Note: There is no need to initialize the Zone; the first time an
// allocation is attempted, a segment of memory will be requested
// through the allocator.
//
// Note: The implementation is inherently not thread safe. Do not use
// from multi-threaded code.

class V8_EXPORT_PRIVATE Zone final {
 public:
  Zone(AccountingAllocator* allocator, const char* name);
  ~Zone();

  // TODO(v8:10689): Remove once all allocation sites are migrated.
  void* New(size_t size) { return Allocate<void>(size); }

  // Allocate 'size' bytes of uninitialized memory in the Zone; expands the Zone
  // by allocating new segments of memory on demand using AccountingAllocator
  // (see AccountingAllocator::AllocateSegment()).
  // TODO(v8:10689): account allocated bytes with the provided TypeTag type.
  template <typename TypeTag>
  void* Allocate(size_t size) {
#ifdef V8_USE_ADDRESS_SANITIZER
    return AsanNew(size);
#else
    size = RoundUp(size, kAlignmentInBytes);
    Address result = position_;
    if (V8_UNLIKELY(size > limit_ - position_)) {
      result = NewExpand(size);
    } else {
      position_ += size;
    }
    return reinterpret_cast<void*>(result);
#endif
  }

  // Allocates memory for T instance and constructs object by calling respective
  // Args... constructor.
  // TODO(v8:10689): account allocated bytes with the T type.
  template <typename T, typename... Args>
  T* New(Args&&... args) {
    size_t size = RoundUp(sizeof(T), kAlignmentInBytes);
    void* memory = Allocate<T>(size);
    return new (memory) T(std::forward<Args>(args)...);
  }

  // Allocates uninitialized memory for 'length' number of T instances.
  // TODO(v8:10689): account allocated bytes with the provided TypeTag type.
  // It might be useful to tag buffer allocations with meaningful names to make
  // buffer allocation sites distinguishable between each other.
  template <typename T, typename TypeTag = T[]>
  T* NewArray(size_t length) {
    DCHECK_LT(length, std::numeric_limits<size_t>::max() / sizeof(T));
    return static_cast<T*>(Allocate<TypeTag>(length * sizeof(T)));
  }

  template <typename T>
  void DeleteArray(T* pointer, size_t length) {
    DCHECK_NOT_NULL(pointer);
    DCHECK_NE(length, 0);
    // TODO(v8:10572): implement accounting for reusable zone memory
#ifdef DEBUG
    size_t size = RoundUp(length * sizeof(T), kAlignmentInBytes);
    static const unsigned char kZapDeadByte = 0xcd;
    memset(pointer, kZapDeadByte, size);
#endif
  }

  // Seals the zone to prevent any further allocation.
  void Seal() { sealed_ = true; }

  // Allows the zone to be safely reused. Releases the memory and fires zone
  // destruction and creation events for the accounting allocator.
  void ReleaseMemory();

  // Returns true if more memory has been allocated in zones than
  // the limit allows.
  bool excess_allocation() const {
    return segment_bytes_allocated_ > kExcessLimit;
  }

  size_t segment_bytes_allocated() const { return segment_bytes_allocated_; }

  const char* name() const { return name_; }

  // Returns precise value of used zone memory, allowed to be called only
  // from thread owning the zone.
  size_t allocation_size() const {
    size_t extra = segment_head_ ? position_ - segment_head_->start() : 0;
    return allocation_size_ + extra;
  }

  // Returns used zone memory not including the head segment, can be called
  // from threads not owning the zone.
  size_t allocation_size_for_tracing() const { return allocation_size_; }

  AccountingAllocator* allocator() const { return allocator_; }

 private:
  void* AsanNew(size_t size);

  // Deletes all objects and free all memory allocated in the Zone.
  void DeleteAll();

  // All pointers returned from New() are 8-byte aligned.
  static const size_t kAlignmentInBytes = 8;

  // Never allocate segments smaller than this size in bytes.
  static const size_t kMinimumSegmentSize = 8 * KB;

  // Never allocate segments larger than this size in bytes.
  static const size_t kMaximumSegmentSize = 32 * KB;

  // Report zone excess when allocation exceeds this limit.
  static const size_t kExcessLimit = 256 * MB;

  // The number of bytes allocated in this zone so far.
  size_t allocation_size_;

  // The number of bytes allocated in segments.  Note that this number
  // includes memory allocated from the OS but not yet allocated from
  // the zone.
  size_t segment_bytes_allocated_;

  // Expand the Zone to hold at least 'size' more bytes and allocate
  // the bytes. Returns the address of the newly allocated chunk of
  // memory in the Zone. Should only be called if there isn't enough
  // room in the Zone already.
  Address NewExpand(size_t size);

  // The free region in the current (front) segment is represented as
  // the half-open interval [position, limit). The 'position' variable
  // is guaranteed to be aligned as dictated by kAlignment.
  Address position_;
  Address limit_;

  AccountingAllocator* allocator_;

  Segment* segment_head_;
  const char* name_;
  bool sealed_;
};

// ZoneObject is an abstraction that helps define classes of objects
// allocated in the Zone. Use it as a base class; see ast.h.
class ZoneObject {
 public:
  // Allocate a new ZoneObject of 'size' bytes in the Zone.
  void* operator new(size_t size, Zone* zone) { return zone->New(size); }
  // Allow non-allocating placement new.
  void* operator new(size_t size, void* ptr) { return ptr; }

  // Ideally, the delete operator should be private instead of
  // public, but unfortunately the compiler sometimes synthesizes
  // (unused) destructors for classes derived from ZoneObject, which
  // require the operator to be visible. MSVC requires the delete
  // operator to be public.

  // ZoneObjects should never be deleted individually; use
  // Zone::DeleteAll() to delete all zone objects in one go.
  void operator delete(void*, size_t) { UNREACHABLE(); }
  void operator delete(void* pointer, Zone* zone) { UNREACHABLE(); }
};

// The ZoneAllocationPolicy is used to specialize generic data
// structures to allocate themselves and their elements in the Zone.
class ZoneAllocationPolicy final {
 public:
  // Creates unusable allocation policy.
  ZoneAllocationPolicy() : zone_(nullptr) {}
  explicit ZoneAllocationPolicy(Zone* zone) : zone_(zone) {}
  void* New(size_t size) { return zone()->New(size); }
  static void Delete(void* pointer) {}
  Zone* zone() const { return zone_; }

 private:
  Zone* zone_;
};

}  // namespace internal
}  // namespace v8

// The accidential pattern
//    new (zone) SomeObject()
// where SomeObject does not inherit from ZoneObject leads to nasty crashes.
// This triggers a compile-time error instead.
template <class T, typename = typename std::enable_if<std::is_convertible<
                       T, const v8::internal::Zone*>::value>::type>
void* operator new(size_t size, T zone) {
  static_assert(false && sizeof(T),
                "Placement new with a zone is only permitted for classes "
                "inheriting from ZoneObject");
  UNREACHABLE();
}

#endif  // V8_ZONE_ZONE_H_
