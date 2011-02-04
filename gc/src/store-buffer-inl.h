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

#ifndef V8_WRITE_BARRIER_INL_H_
#define V8_WRITE_BARRIER_INL_H_

#include "v8.h"
#include "store-buffer.h"

namespace v8 {
namespace internal {

Address StoreBuffer::TopAddress() {
  return reinterpret_cast<Address>(Heap::store_buffer_top_address());
}


void StoreBuffer::Mark(Address addr) {
  Address* top = reinterpret_cast<Address*>(Heap::store_buffer_top());
  *top++ = addr;
  Heap::public_set_store_buffer_top(top);
  if ((reinterpret_cast<uintptr_t>(top) & kStoreBufferOverflowBit) != 0) {
    ASSERT(top == limit_);
    Compact();
  } else {
    ASSERT(top < limit_);
  }
}


void StoreBuffer::set_store_buffer_mode(StoreBuffer::StoreBufferMode mode) {
  if (FLAG_trace_gc) {
    if (mode != store_buffer_mode_) {
      if (mode == kStoreBufferDisabled) {
        PrintF("Store buffer overflowed.\n");
      } else if (mode == kStoreBufferBeingRebuilt) {
        PrintF("Store buffer being rebuilt.\n");
      } else if (mode == kStoreBufferFunctional) {
        PrintF("Store buffer reenabled.\n");
      }
    }
  }
  store_buffer_mode_ = mode;
}


void StoreBuffer::EnterDirectlyIntoStoreBuffer(Address addr) {
  if (store_buffer_rebuilding_enabled_) {
    Address* top = old_top_;
    *top++ = addr;
    old_top_ = top;
    if (top >= old_limit_) {
      Counters::store_buffer_overflows.Increment();
      set_store_buffer_mode(kStoreBufferDisabled);
      old_top_ = old_start_;
    }
    old_buffer_is_sorted_ = false;
  }
}


} }  // namespace v8::internal

#endif  // V8_WRITE_BARRIER_INL_H_
