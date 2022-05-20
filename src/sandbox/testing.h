// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_TESTING_H_
#define V8_SANDBOX_TESTING_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

#ifdef V8_EXPOSE_MEMORY_CORRUPTION_API
// A JavaScript API that emulates typical exploit primitives.
//
// This can be used for testing the sandbox, for example to write regression
// tests for bugs in the sandbox or to develop fuzzers.
class MemoryCorruptionApi {
 public:
  V8_EXPORT_PRIVATE static void Install(Isolate* isolate);
};

#endif  // V8_EXPOSE_MEMORY_CORRUPTION_API

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_TESTING_H_
