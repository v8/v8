// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_READ_ONLY_PROMOTION_H_
#define V8_HEAP_READ_ONLY_PROMOTION_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

class AllocationResult;
class Isolate;
class SafepointScope;

class ReadOnlyPromotion final : public AllStatic {
 public:
  V8_EXPORT_PRIVATE static void Promote(Isolate* isolate,
                                        const SafepointScope& safepoint_scope);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_READ_ONLY_PROMOTION_H_
