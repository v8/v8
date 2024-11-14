// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "glue.h"

#include <string>
#include <vector>

#include "test_platform.h"

namespace v8_crdtp {
namespace glue {
// =============================================================================
// glue::detail::PtrMaybe, templates for optional
// pointers / values which are used in ../lib/Forward_h.template.
// =============================================================================
TEST(PtrMaybeTest, SmokeTest) {
  detail::PtrMaybe<std::vector<uint32_t>> example;
  EXPECT_FALSE(example.isJust());
  EXPECT_TRUE(nullptr == example.fromMaybe(nullptr));
  std::unique_ptr<std::vector<uint32_t>> v(new std::vector<uint32_t>);
  v->push_back(42);
  v->push_back(21);
  example = std::move(v);
  EXPECT_TRUE(example.isJust());
  EXPECT_THAT(*example.fromJust(), testing::ElementsAre(42, 21));
  std::unique_ptr<std::vector<uint32_t>> out = example.takeJust();
  EXPECT_FALSE(example.isJust());
  EXPECT_THAT(*out, testing::ElementsAre(42, 21));
}
}  // namespace glue
}  // namespace v8_crdtp
