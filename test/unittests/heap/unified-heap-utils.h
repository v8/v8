// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_HEAP_UNIFIED_HEAP_UTILS_H_
#define V8_UNITTESTS_HEAP_UNIFIED_HEAP_UTILS_H_

#include "include/v8.h"

namespace v8 {
namespace internal {

// Sets up a V8 API object so that it points back to a C++ object. The setup
// used is recognized by the GC and references will be followed for liveness
// analysis (marking) as well as tooling (snapshot).
v8::Local<v8::Object> ConstructTraceableJSApiObject(
    v8::Local<v8::Context> context, void* object, const char* class_name = "");

}  // namespace internal
}  // namespace v8

#endif  // V8_UNITTESTS_HEAP_UNIFIED_HEAP_UTILS_H_
