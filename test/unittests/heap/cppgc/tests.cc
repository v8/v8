// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/heap/cppgc/tests.h"

#include <memory>

#include "test/unittests/heap/cppgc/test-platform.h"

namespace cppgc {
namespace internal {
namespace testing {

// static
std::unique_ptr<TestPlatform> TestWithPlatform::platform_;

// static
void TestWithPlatform::SetUpTestSuite() {
  platform_ = std::make_unique<TestPlatform>();
  cppgc::InitializePlatform(platform_.get());
}

// static
void TestWithPlatform::TearDownTestSuite() {
  cppgc::ShutdownPlatform();
  platform_.reset();
}

TestWithHeap::TestWithHeap() : heap_(Heap::Create()) {}

TestSupportingAllocationOnly::TestSupportingAllocationOnly()
    : no_gc_scope_(internal::Heap::From(GetHeap())) {}

}  // namespace testing
}  // namespace internal
}  // namespace cppgc
