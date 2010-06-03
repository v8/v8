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

#include "apiutils.h"
#include "heap.h"
#include "zone.h"

namespace v8 {
namespace internal {

class Bootstrapper;
class Deserializer;
class StubCache;

#define ISOLATE_INIT_LIST(V)                                                   \
  V(bool, zone_allow_allocation, true) /* AssertNoZoneAllocation */

class Isolate {
 public:
  ~Isolate();

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

#define GLOBAL_ACCESSOR(type, name, initialvalue)                              \
  type name() const { return name##_; }                                        \
  void set_##name(type value) { name##_ = value; }
  ISOLATE_INIT_LIST(GLOBAL_ACCESSOR)
#undef GLOBAL_ACCESSOR


  Bootstrapper* bootstrapper() { return bootstrapper_; }
  Heap* heap() { return &heap_; }
  StubCache* stub_cache() { return stub_cache_; }
  v8::ImplementationUtilities::HandleScopeData* handle_scope_data() {
    return &handle_scope_data_;
  }
  Zone* zone() { return &zone_; }

 private:
  Isolate();

  static Isolate* global_isolate;

  bool Init(Deserializer* des);

  Bootstrapper* bootstrapper_;
  Heap heap_;
  StubCache* stub_cache_;
  v8::ImplementationUtilities::HandleScopeData handle_scope_data_;
  Zone zone_;

#define GLOBAL_BACKING_STORE(type, name, initialvalue)                         \
  type name##_;
  ISOLATE_INIT_LIST(GLOBAL_BACKING_STORE)
#undef GLOBAL_BACKING_STORE

  DISALLOW_COPY_AND_ASSIGN(Isolate);
};

// Temporary macros for accessing fields off the global isolate. Define these
// when reformatting code would become burdensome.
#define HEAP (v8::internal::Isolate::Current()->heap())
#define ZONE (v8::internal::Isolate::Current()->zone())


// Temporary macro to be used to flag definitions that are indeed static
// and not per-isolate. (It would be great to be able to grep for [static]!)
#define RLYSTC static


// Temporary macro to be used to flag classes that should be static.
#define STATIC_CLASS class


} }  // namespace v8::internal

#endif  // V8_ISOLATE_H_
