// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/type-traits.h"
#include "include/v8-cppgc.h"
#include "include/v8.h"
#include "src/heap/cppgc/visitor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

namespace {

class TestingVisitor : public JSVisitor {
 public:
  TestingVisitor() : JSVisitor(cppgc::internal::VisitorFactory::CreateKey()) {}

  size_t found() const { return found_; }

  void ExpectReference(const void* expected) { expected_ = expected; }

  cppgc::Visitor& AsBaseVisitor() { return *this; }

 protected:
  void Visit(const JSMemberBase& ref) final {
    if (&ref == expected_) {
      found_++;
    }
  }

 private:
  size_t found_ = 0;
  const void* expected_ = nullptr;
};

}  // namespace

TEST(JSVisitorTest, DispatchJSMember) {
  TestingVisitor visitor;
  JSMember<v8::Value> js_value;
  visitor.ExpectReference(&js_value);
  visitor.AsBaseVisitor().Trace(js_value);
  EXPECT_EQ(1u, visitor.found());
  JSMember<v8::Function> js_function;
  visitor.ExpectReference(&js_function);
  visitor.AsBaseVisitor().Trace(js_function);
  EXPECT_EQ(2u, visitor.found());
}

}  // namespace internal
}  // namespace v8
