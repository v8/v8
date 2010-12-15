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

#include "write-buffer.h"

namespace v8 {
namespace internal {

Address* WriteBuffer::top_ = NULL;
Address* WriteBuffer::start_ = NULL;
Address* WriteBuffer::limit_ = NULL;
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
  top_ = start_;
}


void WriteBuffer::TearDown() {
  delete virtual_memory_;
  top_ = start_ = limit_ = NULL;
}


void WriteBuffer::Compact() {
  top_ = start_;   // TODO(gc) fix.
}

} }  // namespace v8::internal
