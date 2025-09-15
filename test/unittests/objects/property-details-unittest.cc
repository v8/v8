// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/property-details.h"

#include <limits>

#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

namespace {

std::vector<PropertyDetails> make_details() {
  std::vector<PropertyDetails> result;
  for (PropertyKind kind : {PropertyKind::kData, PropertyKind::kAccessor}) {
    for (PropertyConstness constness :
         {PropertyConstness::kConst, PropertyConstness::kMutable}) {
      for (PropertyCellType cell_type :
           {PropertyCellType::kConstant, PropertyCellType::kConstantType,
            PropertyCellType::kMutable, PropertyCellType::kUndefined,
            PropertyCellType::kNoCell}) {
        for (int attrs = 0; attrs < 8; ++attrs) {
          PropertyAttributes attributes =
              static_cast<PropertyAttributes>(attrs);
          PropertyDetails details(kind, attributes, cell_type);
          details = details.CopyWithConstness(constness);
          result.push_back(details);
        }
      }
    }
  }
  return result;
}

}  // namespace

// This test will trigger a CHECK failure in. We must ensure that in
// release mode, the enum index doesn't interfere with other fields once it
// becomes too large.
TEST(PropertyDetailsTest, ExceedMaxEnumerationIndex) {
  int too_large_enum_index = std::numeric_limits<int>::max();

  for (PropertyDetails d : make_details()) {
    EXPECT_DEATH_IF_SUPPORTED(d = d.set_index(too_large_enum_index),
                              "Check failed: is_valid\\(value\\)");
  }
}

TEST(PropertyDetailsTest, AsByte) {
  for (PropertyDetails original : make_details()) {
    if (original.cell_type() != PropertyCellType::kNoCell) continue;

    uint8_t as_byte = original.ToByte();
    PropertyDetails from_byte = PropertyDetails::FromByte(as_byte);

    CHECK_EQ(original, from_byte);
  }
}

}  // namespace internal
}  // namespace v8
