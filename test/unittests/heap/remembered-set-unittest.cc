// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include <set>

#include "src/globals.h"
#include "src/heap/remembered-set.h"
#include "src/heap/spaces.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

TEST(LocalSlotsBuffer, InsertAndIterate) {
  LocalSlotsBuffer buffer;
  std::set<Address> untyped;
  std::set<std::pair<SlotType, Address> > typed;

  for (int k = 1000; k < 10000; k += NUMBER_OF_SLOT_TYPES) {
    untyped.insert(reinterpret_cast<Address>(k));
    buffer.Record(reinterpret_cast<Address>(k));
    for (int i = 0; i < NUMBER_OF_SLOT_TYPES; i++) {
      typed.insert(std::make_pair(static_cast<SlotType>(i),
                                  reinterpret_cast<Address>(k + i)));
      buffer.Record(static_cast<SlotType>(i), reinterpret_cast<Address>(k + i));
    }
  }
  buffer.Iterate(
      [&untyped](Address addr) {
        EXPECT_NE(untyped.count(addr), 0);
        untyped.erase(addr);
      },
      [&typed](SlotType type, Address addr) {
        EXPECT_NE(typed.count(std::make_pair(type, addr)), 0);
        typed.erase(std::make_pair(type, addr));
      });
  EXPECT_EQ(untyped.size(), 0);
  EXPECT_EQ(typed.size(), 0);
}

}  // namespace internal
}  // namespace v8
