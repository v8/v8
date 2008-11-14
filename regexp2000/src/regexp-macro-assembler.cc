// Copyright 2008 the V8 project authors. All rights reserved.
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
#include "regexp-macro-assembler.h"

namespace v8 { namespace internal {


ByteArrayProvider::ByteArrayProvider(int initial_size)
  : byte_array_size_(initial_size),
    current_byte_array_(NULL),
    current_byte_array_free_offset_(initial_size) {}


template <typename T>
ArraySlice<T> ByteArrayProvider::GetBuffer(int size) {
  ASSERT(sze > 0);
  size_t elem_size = sizeof(T);
  size_t byte_size = size * elem_size;
  int free_offset = current_byte_array_free_offset_;
  // align elements
  free_offset += elem_size - 1;
  free_offset = free_offset - (free_offset % elem_size);

  if (free_offset + byte_size > byte_array_size_) {
    if (byte_size > (byte_array_size_ / 2)) {
      Handle<ByteArray> solo_buffer = Factory::NewByteArray(byte_size, TENURED);
      return ArraySlice<T>(solo_buffer, 0);
    }
    current_byte_array_ = Factory::NewByteArray(byte_array_size_, TENURED);
    free_offset = 0;
  }
  current_byte_array_free_offset_ = free_offset + size;
  return ArraySlice<T>(current_byte_array_, free_offset);
}
}}
