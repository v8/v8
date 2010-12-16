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

#ifndef V8_WRITE_BARRIER_H_
#define V8_WRITE_BARRIER_H_

#include "allocation.h"
#include "checks.h"
#include "globals.h"
#include "platform.h"

namespace v8 {
namespace internal {


// Used to implement the write barrier by collecting addresses of pointers
// between spaces.
class WriteBuffer : public AllStatic {
 public:
  static Address TopAddress() { return reinterpret_cast<Address>(&top_); }

  static void Setup();
  static void TearDown();

  static inline void Mark(Address addr) {
    *top_++ = addr;
    if ((reinterpret_cast<uintptr_t>(top_) & kWriteBufferOverflowBit) != 0) {
      ASSERT(top_ == limit_);
      Compact();
    } else {
      ASSERT(top_ < limit_);
    }
  }

  static const int kWriteBufferOverflowBit = 1 << 16;
  static const int kWriteBufferSize = kWriteBufferOverflowBit;
  static const int kHashMapLengthLog2 = 12;
  static const int kHashMapLength = 1 << kHashMapLengthLog2;

 private:
  static Address* top_;
  static Address* start_;
  static Address* limit_;
  static VirtualMemory* virtual_memory_;
  static uintptr_t* hash_map_1_;
  static uintptr_t* hash_map_2_;

  static void Compact();
};

} }  // namespace v8::internal

#endif  // V8_WRITE_BARRIER_H_
