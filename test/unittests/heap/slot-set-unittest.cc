// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "src/globals.h"
#include "src/heap/slot-set.h"
#include "src/heap/spaces.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

TEST(SlotSet, InsertAndLookup1) {
  SlotSet set;
  set.SetPageStart(0);
  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    EXPECT_FALSE(set.Lookup(i));
  }
  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    set.Insert(i);
  }
  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    EXPECT_TRUE(set.Lookup(i));
  }
}

TEST(SlotSet, InsertAndLookup2) {
  SlotSet set;
  set.SetPageStart(0);
  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    if (i % 7 == 0) {
      set.Insert(i);
    }
  }
  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    if (i % 7 == 0) {
      EXPECT_TRUE(set.Lookup(i));
    } else {
      EXPECT_FALSE(set.Lookup(i));
    }
  }
}

TEST(SlotSet, Iterate) {
  SlotSet set;
  set.SetPageStart(0);
  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    if (i % 7 == 0) {
      set.Insert(i);
    }
  }

  set.Iterate([](Address slot_address) {
    uintptr_t intaddr = reinterpret_cast<uintptr_t>(slot_address);
    if (intaddr % 3 == 0) {
      return SlotSet::KEEP_SLOT;
    } else {
      return SlotSet::REMOVE_SLOT;
    }
  });

  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    if (i % 21 == 0) {
      EXPECT_TRUE(set.Lookup(i));
    } else {
      EXPECT_FALSE(set.Lookup(i));
    }
  }
}

TEST(SlotSet, Remove) {
  SlotSet set;
  set.SetPageStart(0);
  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    if (i % 7 == 0) {
      set.Insert(i);
    }
  }

  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    if (i % 3 != 0) {
      set.Remove(i);
    }
  }

  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    if (i % 21 == 0) {
      EXPECT_TRUE(set.Lookup(i));
    } else {
      EXPECT_FALSE(set.Lookup(i));
    }
  }
}

TEST(SlotSet, RemoveRange) {
  SlotSet set;
  set.SetPageStart(0);
  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    set.Insert(i);
  }

  set.RemoveRange(0, Page::kPageSize);

  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    EXPECT_FALSE(set.Lookup(i));
  }

  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    set.Insert(i);
  }

  set.RemoveRange(10 * kPointerSize, 10 * kPointerSize);
  EXPECT_TRUE(set.Lookup(9 * kPointerSize));
  EXPECT_TRUE(set.Lookup(10 * kPointerSize));
  EXPECT_TRUE(set.Lookup(11 * kPointerSize));

  set.RemoveRange(10 * kPointerSize, 1000 * kPointerSize);
  for (int i = 0; i < Page::kPageSize; i += kPointerSize) {
    if (10 * kPointerSize <= i && i < 1000 * kPointerSize) {
      EXPECT_FALSE(set.Lookup(i));
    } else {
      EXPECT_TRUE(set.Lookup(i));
    }
  }
}

}  // namespace internal
}  // namespace v8
