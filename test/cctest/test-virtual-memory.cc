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

#include "v8.h"

#include "cctest.h"
#include "platform/virtual-memory.h"

using namespace ::v8::internal;


TEST(CommitAndUncommit) {
  static const size_t kSize = 1 * MB;
  static const size_t kBlockSize = 4 * KB;
  VirtualMemory vm(kSize);
  CHECK(vm.IsReserved());
  void* block_addr = vm.address();
  CHECK(vm.Commit(block_addr, kBlockSize, VirtualMemory::NOT_EXECUTABLE));
  // Check whether we can write to memory.
  int* addr = static_cast<int*>(block_addr);
  addr[5] = 2;
  CHECK(vm.Uncommit(block_addr, kBlockSize));
}


TEST(Release) {
  static const size_t kSize = 4 * KB;
  VirtualMemory vm(kSize);
  CHECK(vm.IsReserved());
  CHECK_LE(kSize, vm.size());
  CHECK_NE(NULL, vm.address());
  vm.Release();
  CHECK(!vm.IsReserved());
}


TEST(TakeControl) {
  static const size_t kSize = 64 * KB;

  VirtualMemory vm1(kSize);
  size_t size1 = vm1.size();
  CHECK(vm1.IsReserved());
  CHECK_LE(kSize, size1);

  VirtualMemory vm2;
  CHECK(!vm2.IsReserved());

  vm2.TakeControl(&vm1);
  CHECK(vm2.IsReserved());
  CHECK(!vm1.IsReserved());
  CHECK(vm2.size() == size1);
}


TEST(AllocationGranularityIsPowerOf2) {
  CHECK(IsPowerOf2(VirtualMemory::GetAllocationGranularity()));
}


TEST(PageSizeIsPowerOf2) {
  CHECK(IsPowerOf2(VirtualMemory::GetPageSize()));
}
