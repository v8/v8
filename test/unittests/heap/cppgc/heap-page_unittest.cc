// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap-page.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

class PageTest : public testing::TestWithHeap {};

}  // namespace

TEST_F(PageTest, PageLayout) {
  auto* np = NormalPage::Create(Heap::From(GetHeap()));
  NormalPage::Destroy(np);
}

}  // namespace internal
}  // namespace cppgc
