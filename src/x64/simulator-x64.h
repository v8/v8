// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_X64_SIMULATOR_X64_H_
#define V8_X64_SIMULATOR_X64_H_

#include "src/allocation.h"

namespace v8 {
namespace internal {

// Since there is no simulator for the x64 architecture the only thing we can
// do is to call the entry directly.
// TODO(X64): Don't pass p0, since it isn't used?
#define CALL_GENERATED_CODE(isolate, entry, p0, p1, p2, p3, p4) \
  (entry(p0, p1, p2, p3, p4))

typedef int (*regexp_matcher)(String*, int, const byte*,
                              const byte*, int*, int, Address, int, Isolate*);

// Call the generated regexp code directly. The code at the entry address should
// expect eight int/pointer sized arguments and return an int.
#define CALL_GENERATED_REGEXP_CODE(isolate, entry, p0, p1, p2, p3, p4, p5, p6, \
                                   p7, p8)                                     \
  (FUNCTION_CAST<regexp_matcher>(entry)(p0, p1, p2, p3, p4, p5, p6, p7, p8))

}  // namespace internal
}  // namespace v8

#endif  // V8_X64_SIMULATOR_X64_H_
