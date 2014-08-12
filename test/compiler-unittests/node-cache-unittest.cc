// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "src/base/utils/random-number-generator.h"
#include "src/compiler/node.h"
#include "src/compiler/node-cache.h"
#include "src/flags.h"
#include "test/compiler-unittests/compiler-unittests.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest-type-names.h"

using testing::AllOf;
using testing::IsNull;
using testing::NotNull;
using testing::Pointee;

namespace v8 {
namespace internal {
namespace compiler {

template <typename T>
class NodeCacheTest : public CompilerTest {
 public:
  NodeCacheTest() : rng_(FLAG_random_seed) {}
  virtual ~NodeCacheTest() {}

 protected:
  NodeCache<T>* cache() { return &cache_; }
  base::RandomNumberGenerator* rng() { return &rng_; }

  void GenerateRandom(T* first, T* last) {
    for (T* i = first; i != last; ++i) {
      do {
        *i = GenerateRandom();
      } while (std::find(first, i, *i) != i);
    }
  }

 private:
  T GenerateRandom();

  NodeCache<T> cache_;
  base::RandomNumberGenerator rng_;
};


template <>
int32_t NodeCacheTest<int32_t>::GenerateRandom() {
  return rng()->NextInt();
}


template <>
int64_t NodeCacheTest<int64_t>::GenerateRandom() {
  int64_t v;
  rng()->NextBytes(&v, sizeof(v));
  return v;
}


typedef ::testing::Types<int32_t, int64_t> NodeCacheTypes;
TYPED_TEST_CASE(NodeCacheTest, NodeCacheTypes);


TYPED_TEST(NodeCacheTest, BackToBack) {
  static const size_t kSize = 100;
  TypeParam values[kSize];
  this->GenerateRandom(&values[0], &values[kSize]);
  for (const TypeParam* i = &values[0]; i != &values[kSize]; ++i) {
    TypeParam value = *i;
    SCOPED_TRACE(::testing::Message() << "value " << value);
    Node** location = this->cache()->Find(this->zone(), value);
    ASSERT_THAT(location, AllOf(NotNull(), Pointee(IsNull())));
    for (int attempt = 1; attempt < 4; ++attempt) {
      SCOPED_TRACE(::testing::Message() << "attempt " << attempt);
      EXPECT_EQ(location, this->cache()->Find(this->zone(), value));
    }
  }
}


TYPED_TEST(NodeCacheTest, MinimumSize) {
  static const size_t kSize = 5;
  TypeParam values[kSize];
  this->GenerateRandom(&values[0], &values[kSize]);
  Node** locations[kSize];
  Node* nodes = this->zone()->template NewArray<Node>(kSize);
  for (size_t i = 0; i < kSize; ++i) {
    locations[i] = this->cache()->Find(this->zone(), values[i]);
    ASSERT_THAT(locations[i], NotNull());
    EXPECT_EQ(&locations[i],
              std::find(&locations[0], &locations[i], locations[i]));
    *locations[i] = &nodes[i];
  }
  for (size_t i = 0; i < kSize; ++i) {
    EXPECT_EQ(locations[i], this->cache()->Find(this->zone(), values[i]));
  }
}


TYPED_TEST(NodeCacheTest, MinimumHits) {
  static const size_t kSize = 250;
  static const size_t kMinHits = 10;
  TypeParam* values = this->zone()->template NewArray<TypeParam>(kSize);
  this->GenerateRandom(&values[0], &values[kSize]);
  Node* nodes = this->zone()->template NewArray<Node>(kSize);
  for (size_t i = 0; i < kSize; ++i) {
    Node** location = this->cache()->Find(this->zone(), values[i]);
    ASSERT_THAT(location, AllOf(NotNull(), Pointee(IsNull())));
    *location = &nodes[i];
  }
  size_t hits = 0;
  for (size_t i = 0; i < kSize; ++i) {
    Node** location = this->cache()->Find(this->zone(), values[i]);
    ASSERT_THAT(location, NotNull());
    if (*location != NULL) {
      EXPECT_EQ(&nodes[i], *location);
      ++hits;
    }
  }
  EXPECT_GE(hits, kMinHits);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
