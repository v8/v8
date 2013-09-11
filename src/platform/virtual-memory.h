// Copyright 2013 the V8 project authors. All rights reserved.
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

#ifndef V8_PLATFORM_VIRTUAL_MEMORY_H_
#define V8_PLATFORM_VIRTUAL_MEMORY_H_

#include "checks.h"
#include "globals.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// VirtualMemory
//
// This class represents and controls an area of reserved memory.
// Control of the reserved memory can be assigned to another VirtualMemory
// object by assignment or copy-constructing. This removes the reserved memory
// from the original object.
class VirtualMemory V8_FINAL {
 public:
  // The executability of a memory region.
  enum Executability { NOT_EXECUTABLE, EXECUTABLE };

  // Empty VirtualMemory object, controlling no reserved memory.
  VirtualMemory() : address_(NULL), size_(0) {}

  // Reserves virtual memory with size.
  explicit VirtualMemory(size_t size) : size_(0) {
    address_ = ReserveRegion(size, &size_);
  }

  // Reserves virtual memory containing an area of the given size that
  // is aligned per alignment. This may not be at the position returned
  // by address().
  VirtualMemory(size_t size, size_t alignment) : size_(0) {
    address_ = ReserveRegion(size, &size_, alignment);
  }

  // Releases the reserved memory, if any, controlled by this VirtualMemory
  // object.
  ~VirtualMemory() {
    if (IsReserved()) {
      bool result = ReleaseRegion(address_, size_);
      ASSERT(result);
      USE(result);
    }
  }

  // Returns whether the memory contains the specified address.
  bool Contains(const void* address) const V8_WARN_UNUSED_RESULT {
    if (!IsReserved()) return false;
    if (address < address_) return false;
    if (address >= reinterpret_cast<uint8_t*>(address_) + size_) return false;
    return true;
  }

  // Returns whether the memory has been reserved.
  bool IsReserved() const V8_WARN_UNUSED_RESULT {
    return address_ != NULL;
  }

  // Initialize or resets an embedded VirtualMemory object.
  void Reset() {
    address_ = NULL;
    size_ = 0;
  }

  // Returns the start address of the reserved memory. The returned value is
  // only meaningful if |IsReserved()| returns true.
  // If the memory was reserved with an alignment, this address is not
  // necessarily aligned. The user might need to round it up to a multiple of
  // the alignment to get the start of the aligned block.
  void* address() const V8_WARN_UNUSED_RESULT { return address_; }

  // Returns the size of the reserved memory. The returned value is only
  // meaningful when |IsReserved()| returns true.
  // If the memory was reserved with an alignment, this size may be larger
  // than the requested size.
  size_t size() const V8_WARN_UNUSED_RESULT { return size_; }

  // Commits real memory. Returns whether the operation succeeded.
  bool Commit(void* address,
              size_t size,
              Executability executability) V8_WARN_UNUSED_RESULT {
    ASSERT(IsReserved());
    ASSERT(Contains(address));
    ASSERT(Contains(reinterpret_cast<uint8_t*>(address) + size - 1));
    return CommitRegion(address, size, executability);
  }

  // Uncommit real memory.  Returns whether the operation succeeded.
  bool Uncommit(void* address, size_t size) V8_WARN_UNUSED_RESULT {
    ASSERT(IsReserved());
    ASSERT(Contains(address));
    ASSERT(Contains(reinterpret_cast<uint8_t*>(address) + size - 1));
    return UncommitRegion(address, size);
  }

  // Creates guard pages at the given address.
  bool Guard(void* address, size_t size) V8_WARN_UNUSED_RESULT {
    // We can simply uncommit the specified pages. Any access
    // to them will cause a processor exception.
    return Uncommit(address, size);
  }

  void Release() {
    ASSERT(IsReserved());
    // WARNING: Order is important here. The VirtualMemory
    // object might live inside the allocated region.
    void* address = address_;
    size_t size = size_;
    Reset();
    bool result = ReleaseRegion(address, size);
    USE(result);
    ASSERT(result);
  }

  // Assign control of the reserved region to a different VirtualMemory object.
  // The old object is no longer functional (IsReserved() returns false).
  void TakeControl(VirtualMemory* from) {
    ASSERT(!IsReserved());
    address_ = from->address_;
    size_ = from->size_;
    from->Reset();
  }

  // Allocates a region of memory pages. The pages are readable/writable,
  // but are not guaranteed to be executable unless explicitly requested.
  // Returns the base address of the allocated memory region, or NULL in
  // case of an error.
  static void* AllocateRegion(size_t size,
                              size_t* size_return,
                              Executability executability)
      V8_WARN_UNUSED_RESULT;

  static void* ReserveRegion(size_t size,
                             size_t* size_return) V8_WARN_UNUSED_RESULT;

  static void* ReserveRegion(size_t size,
                             size_t* size_return,
                             size_t alignment) V8_WARN_UNUSED_RESULT;

  static bool CommitRegion(void* address,
                           size_t size,
                           Executability executability) V8_WARN_UNUSED_RESULT;

  static bool UncommitRegion(void* address, size_t size) V8_WARN_UNUSED_RESULT;

  // Mark code segments readable-executable.
  static bool WriteProtectRegion(void* address,
                                 size_t size) V8_WARN_UNUSED_RESULT;

  // Must be called with a base pointer that has been returned by ReserveRegion
  // and the same size it was reserved with.
  static bool ReleaseRegion(void* address, size_t size) V8_WARN_UNUSED_RESULT;

  // The granularity for the starting address at which virtual memory can be
  // reserved (or allocated in terms of the underlying operating system).
  static size_t GetAllocationGranularity() V8_PURE;

  // The maximum size of the virtual memory. 0 means there is no artificial
  // limit.
  static size_t GetLimit() V8_PURE;

  // The page size and the granularity of page protection and commitment.
  static size_t GetPageSize() V8_PURE;

  // Returns true if OS performs lazy commits, i.e. the memory allocation call
  // defers actual physical memory allocation till the first memory access.
  // Otherwise returns false.
  static V8_INLINE(bool HasLazyCommits()) {
#if V8_OS_LINUX
    return true;
#else
    return false;
#endif
  }

 private:
  void* address_;  // Start address of the virtual memory.
  size_t size_;  // Size of the virtual memory.
};

} }  // namespace v8::internal

#endif  // V8_PLATFORM_VIRTUAL_MEMORY_H_
