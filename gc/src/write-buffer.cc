// Copyright 2010 the V8 project authors. All rights reserved.
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

#include "v8-counters.h"
#include "write-buffer.h"
#include "write-buffer-inl.h"

namespace v8 {
namespace internal {

Address* WriteBuffer::start_ = NULL;
Address* WriteBuffer::limit_ = NULL;
uintptr_t* WriteBuffer::hash_map_1_ = NULL;
uintptr_t* WriteBuffer::hash_map_2_ = NULL;
VirtualMemory* WriteBuffer::virtual_memory_ = NULL;

void WriteBuffer::Setup() {
  virtual_memory_ = new VirtualMemory(kWriteBufferSize * 3);
  uintptr_t start_as_int =
      reinterpret_cast<uintptr_t>(virtual_memory_->address());
  start_ =
      reinterpret_cast<Address*>(RoundUp(start_as_int, kWriteBufferSize * 2));
  limit_ = start_ + (kWriteBufferSize / sizeof(*start_));

  ASSERT(reinterpret_cast<Address>(start_) >= virtual_memory_->address());
  ASSERT(reinterpret_cast<Address>(limit_) >= virtual_memory_->address());
  Address* vm_limit = reinterpret_cast<Address*>(
      reinterpret_cast<char*>(virtual_memory_->address()) +
          virtual_memory_->size());
  ASSERT(start_ <= vm_limit);
  ASSERT(limit_ <= vm_limit);
  USE(vm_limit);
  ASSERT((reinterpret_cast<uintptr_t>(limit_) & kWriteBufferOverflowBit) != 0);
  ASSERT((reinterpret_cast<uintptr_t>(limit_ - 1) & kWriteBufferOverflowBit) ==
         0);

  virtual_memory_->Commit(reinterpret_cast<Address>(start_),
                          kWriteBufferSize,
                          false);  // Not executable.
  Heap::public_set_write_buffer_top(start_);

  hash_map_1_ = new uintptr_t[kHashMapLength];
  hash_map_2_ = new uintptr_t[kHashMapLength];
}


void WriteBuffer::TearDown() {
  delete virtual_memory_;
  delete[] hash_map_1_;
  delete[] hash_map_2_;
  start_ = limit_ = NULL;
  Heap::public_set_write_buffer_top(start_);
}


void WriteBuffer::Compact() {
  memset(reinterpret_cast<void*>(hash_map_1_),
         0,
         sizeof(uintptr_t) * kHashMapLength);
  memset(reinterpret_cast<void*>(hash_map_2_),
         0,
         sizeof(uintptr_t) * kHashMapLength);
  Address* top = reinterpret_cast<Address*>(Heap::write_buffer_top());
  Address* stop = top;
  ASSERT(top <= limit_);
  top = start_;
  // Goes through the addresses in the write buffer attempting to remove
  // duplicates.  In the interest of speed this is a lossy operation.  Some
  // duplicates will remain.  We have two hash tables with different hash
  // functions to reduce the number of unnecessary clashes.
  for (Address* current = start_; current < stop; current++) {
    uintptr_t int_addr = reinterpret_cast<uintptr_t>(*current);
    // Shift out the last bits including any tags.
    int_addr >>= kPointerSizeLog2;
    int hash1 =
        ((int_addr ^ (int_addr >> kHashMapLengthLog2)) & (kHashMapLength - 1));
    if (hash_map_1_[hash1] == int_addr) continue;
    int hash2 =
        ((int_addr - (int_addr >> kHashMapLengthLog2)) & (kHashMapLength - 1));
    hash2 ^= hash2 >> (kHashMapLengthLog2 * 2);
    if (hash_map_2_[hash2] == int_addr) continue;
    if (hash_map_1_[hash1] == 0) {
      hash_map_1_[hash1] = int_addr;
    } else if (hash_map_2_[hash2] == 0) {
      hash_map_2_[hash2] = int_addr;
    } else {
      // Rather than slowing down we just throw away some entries.  This will
      // cause some duplicates to remain undetected.
      hash_map_1_[hash1] = int_addr;
      hash_map_2_[hash2] = 0;
    }
    ASSERT(top <= current);
    ASSERT(top <= limit_);
    *top++ = reinterpret_cast<Address>(int_addr << kPointerSizeLog2);
  }
  Counters::write_buffer_compactions.Increment();
  if (limit_ - top < top - start_) {
    // Compression did not free up at least half.
    // TODO(gc): Set an interrupt to do a GC on the next back edge.
    // TODO(gc): Allocate the rest of new space to force a GC on the next
    // allocation.
    if (limit_ - top < (top - start_) >> 1) {
      // Compression did not free up at least one quarter.
      // TODO(gc): Set a flag to scan all of memory.
      top = start_;
      Counters::write_buffer_overflows.Increment();
    }
  }
  Heap::public_set_write_buffer_top(top);
}

} }  // namespace v8::internal
