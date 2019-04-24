// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/torque-compiler.h"
#include "src/torque/utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock-support.h"

namespace v8 {
namespace internal {
namespace torque {

TEST(Torque, StackDeleteRange) {
  Stack<int> stack = {1, 2, 3, 4, 5, 6, 7};
  stack.DeleteRange(StackRange{BottomOffset{2}, BottomOffset{4}});
  Stack<int> result = {1, 2, 5, 6, 7};
  ASSERT_TRUE(stack == result);
}

using ::testing::HasSubstr;
TEST(Torque, TypeNamingConventionLintError) {
  std::string source = R"(
    type void;
    type never;

    type foo generates 'TNode<Foo>';
  )";

  TorqueCompilerOptions options;
  options.output_directory = "";
  options.verbose = false;
  options.collect_language_server_data = false;

  const TorqueCompilerResult result = CompileTorque(source, options);

  ASSERT_EQ(result.lint_errors.size(), static_cast<size_t>(1));
  EXPECT_THAT(result.lint_errors[0].message, HasSubstr("\"foo\""));
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
