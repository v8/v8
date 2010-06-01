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

#ifndef V8_ISOLATE_H_
#define V8_ISOLATE_H_

#include "heap.h"

namespace v8 {
namespace internal {

class Deserializer;

class Isolate {
 public:
  // Returns the single global isolate.
  static Isolate* Current() {
    ASSERT(global_isolate != NULL);
    return global_isolate;
  }

  // Creates a new isolate (perhaps using a deserializer). Returns null
  // on failure.
  static Isolate* Create(Deserializer* des);
  
  // Initialize process-wide state.
  static void InitOnce();
  
  ~Isolate();
  
  // Accessors.
  Heap* heap() { return &heap_; }
  
 private:
  Isolate();
  
  static Isolate* global_isolate;
  
  bool Init(Deserializer* des);

  Heap heap_;

  DISALLOW_COPY_AND_ASSIGN(Isolate);
};

// Temporary macros for accessing fields off the global isolate.
#define HEAP (v8::internal::Isolate::Current()->heap())


} }  // namespace v8::internal

#endif  // V8_ISOLATE_H_
