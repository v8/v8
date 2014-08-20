// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_UNITTESTS_COMMON_OPERATOR_UNITTEST_H_
#define V8_COMPILER_UNITTESTS_COMMON_OPERATOR_UNITTEST_H_

#include "src/compiler/common-operator.h"
#include "test/compiler-unittests/compiler-unittests.h"

namespace v8 {
namespace internal {
namespace compiler {

class CommonOperatorTest : public CompilerTest {
 public:
  CommonOperatorTest();
  virtual ~CommonOperatorTest();

 protected:
  CommonOperatorBuilder* common() { return &common_; }

 private:
  CommonOperatorBuilder common_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_UNITTESTS_COMMON_OPERATOR_UNITTEST_H_
