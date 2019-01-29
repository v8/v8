// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CCTEST_COMPILER_SERIALIZER_TESTER_H_
#define V8_CCTEST_COMPILER_SERIALIZER_TESTER_H_

#include "src/compiler/js-heap-broker.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace compiler {

class ZoneStats;

class SerializerTester : public HandleAndZoneScope {
 public:
  explicit SerializerTester(const char* source);

  JSFunctionRef function() const { return function_.value(); }

 private:
  CanonicalHandleScope canonical_;
  base::Optional<JSFunctionRef> function_;
  JSHeapBroker* broker_ = nullptr;
};
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_CCTEST_COMPILER_SERIALIZER_TESTER_H_
