// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_UNITTESTS_H_
#define V8_UNITTESTS_UNITTESTS_H_

#include "src/compiler/pipeline.h"
#include "testing/gtest/include/gtest/gtest.h"

// The COMPILER_TEST(Case, Name) macro works just like
// TEST(Case, Name), except that the test is disabled
// if the platform is not a supported TurboFan target.
#if V8_TURBOFAN_TARGET
#define COMPILER_TEST(Case, Name) TEST(Case, Name)
#else  // V8_TURBOFAN_TARGET
#define COMPILER_TEST(Case, Name) TEST(Case, DISABLED_##Name)
#endif  // V8_TURBOFAN_TARGET


// The COMPILER_TEST_F(Case, Name) macro works just like
// TEST_F(Case, Name), except that the test is disabled
// if the platform is not a supported TurboFan target.
#if V8_TURBOFAN_TARGET
#define COMPILER_TEST_F(Case, Name) TEST_F(Case, Name)
#else  // V8_TURBOFAN_TARGET
#define COMPILER_TEST_F(Case, Name) TEST_F(Case, DISABLED_##Name)
#endif  // V8_TURBOFAN_TARGET

namespace v8 {
namespace internal {

class EngineTest : public ::testing::Test {
 public:
  virtual ~EngineTest() {}

  static void SetUpTestCase();
  static void TearDownTestCase();

 private:
  static v8::Platform* platform_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_UNITTESTS_UNITTESTS_H_
