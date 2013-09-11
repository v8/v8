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

#include "platform/virtual-memory.h"

#if V8_OS_POSIX
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <unistd.h>
#endif

#if V8_OS_MACOSX
#include <mach/vm_statistics.h>
#endif

#include <cerrno>

#include "platform/mutex.h"
#include "utils.h"
#include "utils/random-number-generator.h"
#if V8_OS_CYGIN || V8_OS_WIN
#include "win32-headers.h"
#endif

namespace v8 {
namespace internal {

class RandomAddressGenerator V8_FINAL {
  public:
  V8_INLINE uintptr_t NextAddress() {
    LockGuard<Mutex> lock_guard(&mutex_);
    uintptr_t address = rng_.NextInt();
#if V8_HOST_ARCH_64_BIT
    address = (address << 32) + static_cast<uintptr_t>(rng_.NextInt());
#endif
    return address;
  }

  private:
  Mutex mutex_;
  RandomNumberGenerator rng_;
};

typedef LazyInstance<RandomAddressGenerator,
                     DefaultConstructTrait<RandomAddressGenerator>,
                     ThreadSafeInitOnceTrait>::type LazyRandomAddressGenerator;

#define LAZY_RANDOM_ADDRESS_GENERATOR_INITIALIZER LAZY_INSTANCE_INITIALIZER


static V8_INLINE void* GenerateRandomAddress() {
#if V8_OS_NACL
  // TODO(bradchen): Restore randomization once Native Client gets smarter
  // about using mmap address hints.
  // See http://code.google.com/p/nativeclient/issues/3341
  return NULL;
#else  // V8_OS_NACL
  LazyRandomAddressGenerator random_address_generator =
      LAZY_RANDOM_ADDRESS_GENERATOR_INITIALIZER;
  uintptr_t address = random_address_generator.Pointer()->NextAddress();

# if V8_TARGET_ARCH_X64
#  if V8_OS_CYGWIN || V8_OS_WIN
    // Try not to map pages into the default range that windows loads DLLs.
    // Use a multiple of 64KiB to prevent committing unused memory.
    address += V8_UINT64_C(0x00080000000);
    address &= V8_UINT64_C(0x3ffffff0000);
#  else  // V8_OS_CYGWIN || V8_OS_WIN
    // Currently available CPUs have 48 bits of virtual addressing. Truncate
    // the hint address to 46 bits to give the kernel a fighting chance of
    // fulfilling our placement request.
    address &= V8_UINT64_C(0x3ffffffff000);
#  endif  // V8_OS_CYGWIN || V8_OS_WIN
# else  // V8_TARGET_ARCH_X64
#  if V8_OS_CYGWIN || V8_OS_WIN
    // Try not to map pages into the default range that windows loads DLLs.
    // Use a multiple of 64KiB to prevent committing unused memory.
    address += 0x04000000;
    address &= 0x3fff0000;
#  elif V8_OS_SOLARIS
    // For our Solaris/illumos mmap hint, we pick a random address in the bottom
    // half of the top half of the address space (that is, the third quarter).
    // Because we do not MAP_FIXED, this will be treated only as a hint -- the
    // system will not fail to mmap() because something else happens to already
    // be mapped at our random address. We deliberately set the hint high enough
    // to get well above the system's break (that is, the heap); Solaris and
    // illumos will try the hint and if that fails allocate as if there were
    // no hint at all. The high hint prevents the break from getting hemmed in
    // at low values, ceding half of the address space to the system heap.
    address &= 0x3ffff000;
    address += 0x80000000;
#  else  // V8_OS_CYGWIN || V8_OS_WIN
    // The range 0x20000000 - 0x60000000 is relatively unpopulated across a
    // variety of ASLR modes (PAE kernel, NX compat mode, etc) and on Mac OS X
    // 10.6 and 10.7.
    address &= 0x3ffff000;
    address += 0x20000000;
#  endif  // V8_OS_CYGIN || V8_OS_WIN
# endif  // V8_TARGET_ARCH_X64
    return reinterpret_cast<void*>(address);
#endif  // V8_OS_NACL
}


// static
void* VirtualMemory::AllocateRegion(size_t size,
                                    size_t* size_return,
                                    Executability executability) {
  ASSERT_LT(0, size);
  ASSERT_NE(NULL, size_return);
  void* address = ReserveRegion(size, &size);
  if (address == NULL) return NULL;
  if (!CommitRegion(address, size, executability)) {
    bool result = ReleaseRegion(address, size);
    ASSERT(result);
    USE(result);
    return NULL;
  }
  *size_return = size;
  return address;
}

#if V8_OS_CYGWIN || V8_OS_WIN

// static
void* VirtualMemory::ReserveRegion(size_t size, size_t* size_return) {
  ASSERT_LT(0, size);
  ASSERT_NE(NULL, size_return);
  // The minimum size that can be reserved is 64KiB, see
  // http://msdn.microsoft.com/en-us/library/ms810627.aspx
  if (size < 64 * KB) {
    size = 64 * KB;
  }
  size = RoundUp(size, GetAllocationGranularity());
  LPVOID address = NULL;
  // Try and randomize the allocation address (up to three attempts).
  for (unsigned attempts = 0; address == NULL && attempts < 3; ++attempts) {
    address = VirtualAlloc(GenerateRandomAddress(),
                           size,
                           MEM_RESERVE,
                           PAGE_NOACCESS);
  }
  if (address == NULL) {
    // After three attempts give up and let the kernel find an address.
    address = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
  }
  if (address == NULL) {
    return NULL;
  }
  ASSERT(IsAligned(reinterpret_cast<uintptr_t>(address),
                   GetAllocationGranularity()));
  *size_return = size;
  return address;
}


// static
void* VirtualMemory::ReserveRegion(size_t size,
                                   size_t* size_return,
                                   size_t alignment) {
  ASSERT_LT(0, size);
  ASSERT_NE(NULL, size_return);
  ASSERT(IsAligned(alignment, GetAllocationGranularity()));

  size_t reserved_size;
  Address reserved_base = static_cast<Address>(
      ReserveRegion(size + alignment, &reserved_size));
  if (reserved_base == NULL) {
    return NULL;
  }
  ASSERT_LE(size, reserved_size);
  ASSERT(IsAligned(reserved_size, GetPageSize()));

  // Try reducing the size by freeing and then reallocating a specific area.
  bool result = ReleaseRegion(reserved_base, reserved_size);
  USE(result);
  ASSERT(result);
  size_t aligned_size = RoundUp(size, GetPageSize());
  Address aligned_base = static_cast<Address>(
      VirtualAlloc(RoundUp(reserved_base, alignment),
                   aligned_size,
                   MEM_RESERVE,
                   PAGE_NOACCESS));
  if (aligned_base != NULL) {
    ASSERT(aligned_base == RoundUp(reserved_base, alignment));
    ASSERT(IsAligned(reinterpret_cast<uintptr_t>(aligned_base),
                     GetAllocationGranularity()));
    ASSERT(IsAligned(aligned_size, GetPageSize()));
    *size_return = aligned_size;
    return aligned_base;
  }

  // Resizing failed, just go with a bigger area.
  return ReserveRegion(reserved_size, size_return);
}


// static
bool VirtualMemory::CommitRegion(void* address,
                                 size_t size,
                                 Executability executability) {
  ASSERT_NE(NULL, address);
  ASSERT_LT(0, size);
  DWORD protect = 0;
  switch (executability) {
    case NOT_EXECUTABLE:
      protect = PAGE_READWRITE;
      break;

    case EXECUTABLE:
      protect = PAGE_EXECUTE_READWRITE;
      break;
  }
  LPVOID result = VirtualAlloc(address, size, MEM_COMMIT, protect);
  if (result == NULL) {
    ASSERT(GetLastError() != ERROR_INVALID_ADDRESS);
    return false;
  }
  ASSERT_EQ(address, result);
  return true;
}


// static
bool VirtualMemory::UncommitRegion(void* address, size_t size) {
  ASSERT_NE(NULL, address);
  ASSERT_LT(0, size);
  int result = VirtualFree(address, size, MEM_DECOMMIT);
  if (result == 0) {
    return false;
  }
  return true;
}


// static
bool VirtualMemory::WriteProtectRegion(void* address, size_t size) {
  ASSERT_NE(NULL, address);
  ASSERT_LT(0, size);
  DWORD old_protect;
  return VirtualProtect(address, size, PAGE_EXECUTE_READ, &old_protect);
}


// static
bool VirtualMemory::ReleaseRegion(void* address, size_t size) {
  ASSERT_NE(NULL, address);
  ASSERT_LT(0, size);
  USE(size);
  int result = VirtualFree(address, 0, MEM_RELEASE);
  if (result == 0) {
    return false;
  }
  return true;
}


// static
size_t VirtualMemory::GetAllocationGranularity() {
  static size_t allocation_granularity = 0;
  if (allocation_granularity == 0) {
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    allocation_granularity = system_info.dwAllocationGranularity;
    MemoryBarrier();
  }
  return allocation_granularity;
}


// static
size_t VirtualMemory::GetLimit() {
  return 0;
}


// static
size_t VirtualMemory::GetPageSize() {
  static size_t page_size = 0;
  if (page_size == 0) {
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    page_size = system_info.dwPageSize;
    MemoryBarrier();
  }
  return page_size;
}


#else  // V8_OS_CYGIN || V8_OS_WIN


// Constants used for mmap.
#if V8_OS_MACOSX
// kMmapFd is used to pass vm_alloc flags to tag the region with the user
// defined tag 255 This helps identify V8-allocated regions in memory analysis
// tools like vmmap(1).
static const int kMmapFd = VM_MAKE_TAG(255);
#else
static const int kMmapFd = -1;
#endif  // V8_OS_MACOSX
static const off_t kMmapFdOffset = 0;


// static
void* VirtualMemory::ReserveRegion(size_t size, size_t* size_return) {
  ASSERT_LT(0, size);
  ASSERT_NE(NULL, size_return);

  size = RoundUp(size, GetPageSize());
  void* address = mmap(GenerateRandomAddress(),
                       size,
                       PROT_NONE,
                       MAP_ANON | MAP_NORESERVE | MAP_PRIVATE,
                       kMmapFd,
                       kMmapFdOffset);
  if (address == MAP_FAILED) {
    ASSERT_NE(EINVAL, errno);
    return NULL;
  }
  *size_return = size;
  return address;
}


// static
void* VirtualMemory::ReserveRegion(size_t size,
                                   size_t* size_return,
                                   size_t alignment) {
  ASSERT_LT(0, size);
  ASSERT_NE(NULL, size_return);
  ASSERT(IsAligned(alignment, GetPageSize()));

  size_t reserved_size;
  Address reserved_base = static_cast<Address>(
      ReserveRegion(size + alignment, &reserved_size));
  if (reserved_base == NULL) {
    return NULL;
  }

  Address aligned_base = RoundUp(reserved_base, alignment);
  ASSERT_LE(reserved_base, aligned_base);

  // Unmap extra memory reserved before the aligned region.
  if (aligned_base != reserved_base) {
    size_t prefix_size = static_cast<size_t>(aligned_base - reserved_base);
    bool result = ReleaseRegion(reserved_base, prefix_size);
    ASSERT(result);
    USE(result);
    reserved_size -= prefix_size;
  }

  size_t aligned_size = RoundUp(size, GetPageSize());
  ASSERT_LE(aligned_size, reserved_size);

  // Unmap extra memory reserved after the aligned region.
  if (aligned_size != reserved_size) {
    size_t suffix_size = reserved_size - aligned_size;
    bool result = ReleaseRegion(aligned_base + aligned_size, suffix_size);
    ASSERT(result);
    USE(result);
    reserved_size -= suffix_size;
  }

  ASSERT(aligned_size == reserved_size);
  ASSERT_NE(NULL, aligned_base);

  *size_return = aligned_size;
  return aligned_base;
}


// static
bool VirtualMemory::CommitRegion(void* address,
                                 size_t size,
                                 Executability executability) {
  ASSERT_NE(NULL, address);
  ASSERT_LT(0, size);
  int prot = 0;
  // The Native Client port of V8 uses an interpreter,
  // so code pages don't need PROT_EXEC.
#if V8_OS_NACL
  executability = NOT_EXECUTABLE;
#endif
  switch (executability) {
    case NOT_EXECUTABLE:
      prot = PROT_READ | PROT_WRITE;
      break;

    case EXECUTABLE:
      prot = PROT_EXEC | PROT_READ | PROT_WRITE;
      break;
  }
  void* result = mmap(address,
                      size,
                      prot,
                      MAP_ANON | MAP_FIXED | MAP_PRIVATE,
                      kMmapFd,
                      kMmapFdOffset);
  if (result == MAP_FAILED) {
    ASSERT_NE(EINVAL, errno);
    return false;
  }
  return true;
}


// static
bool VirtualMemory::UncommitRegion(void* address, size_t size) {
  ASSERT_NE(NULL, address);
  ASSERT_LT(0, size);
  void* result = mmap(address,
                      size,
                      PROT_NONE,
                      MAP_ANON | MAP_FIXED | MAP_NORESERVE | MAP_PRIVATE,
                      kMmapFd,
                      kMmapFdOffset);
  if (result == MAP_FAILED) {
    ASSERT_NE(EINVAL, errno);
    return false;
  }
  return true;
}


// static
bool VirtualMemory::WriteProtectRegion(void* address, size_t size) {
  ASSERT_NE(NULL, address);
  ASSERT_LT(0, size);
#if V8_OS_NACL
  // The Native Client port of V8 uses an interpreter,
  // so code pages don't need PROT_EXEC.
  int prot = PROT_READ;
#else
  int prot = PROT_EXEC | PROT_READ;
#endif
  int result = mprotect(address, size, prot);
  if (result < 0) {
    ASSERT_NE(EINVAL, errno);
    return false;
  }
  return true;
}


// static
bool VirtualMemory::ReleaseRegion(void* address, size_t size) {
  ASSERT_NE(NULL, address);
  ASSERT_LT(0, size);
  int result = munmap(address, size);
  if (result < 0) {
    ASSERT_NE(EINVAL, errno);
    return false;
  }
  return true;
}


// static
size_t VirtualMemory::GetAllocationGranularity() {
  return GetPageSize();
}


// static
size_t VirtualMemory::GetLimit() {
  struct rlimit rlim;
  int result = getrlimit(RLIMIT_DATA, &rlim);
  ASSERT_EQ(0, result);
  USE(result);
  return rlim.rlim_cur;
}


// static
size_t VirtualMemory::GetPageSize() {
  static const size_t kPageSize = getpagesize();
  return kPageSize;
}

#endif  // V8_OS_CYGWIN || V8_OS_WIN

} }  // namespace v8::internal
