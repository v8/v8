// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "src/globals.h"
#include "src/heap/marking.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

TEST(Marking, MarkWhiteBlackWhite) {
  Bitmap* bitmap = reinterpret_cast<Bitmap*>(
      calloc(Bitmap::kSize / kPointerSize, kPointerSize));
  const int kLocationsSize = 3;
  int position[kLocationsSize] = {
      Bitmap::kBitsPerCell - 2, Bitmap::kBitsPerCell - 1, Bitmap::kBitsPerCell};
  for (int i = 0; i < kLocationsSize; i++) {
    MarkBit mark_bit = bitmap->MarkBitFromIndex(position[i]);
    CHECK(Marking::IsWhite(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
    Marking::MarkBlack(mark_bit);
    CHECK(Marking::IsBlack(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
    Marking::MarkWhite(mark_bit);
    CHECK(Marking::IsWhite(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
  }
  free(bitmap);
}

TEST(Marking, TransitionWhiteBlackWhite) {
  Bitmap* bitmap = reinterpret_cast<Bitmap*>(
      calloc(Bitmap::kSize / kPointerSize, kPointerSize));
  const int kLocationsSize = 3;
  int position[kLocationsSize] = {
      Bitmap::kBitsPerCell - 2, Bitmap::kBitsPerCell - 1, Bitmap::kBitsPerCell};
  for (int i = 0; i < kLocationsSize; i++) {
    MarkBit mark_bit = bitmap->MarkBitFromIndex(position[i]);
    CHECK(Marking::IsWhite(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
    Marking::WhiteToBlack(mark_bit);
    CHECK(Marking::IsBlack(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
    Marking::BlackToWhite(mark_bit);
    CHECK(Marking::IsWhite(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
  }
  free(bitmap);
}

TEST(Marking, TransitionAnyToGrey) {
  Bitmap* bitmap = reinterpret_cast<Bitmap*>(
      calloc(Bitmap::kSize / kPointerSize, kPointerSize));
  const int kLocationsSize = 3;
  int position[kLocationsSize] = {
      Bitmap::kBitsPerCell - 2, Bitmap::kBitsPerCell - 1, Bitmap::kBitsPerCell};
  for (int i = 0; i < kLocationsSize; i++) {
    MarkBit mark_bit = bitmap->MarkBitFromIndex(position[i]);
    CHECK(Marking::IsWhite(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
    Marking::AnyToGrey(mark_bit);
    CHECK(Marking::IsGrey(mark_bit));
    CHECK(Marking::IsBlackOrGrey(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
    Marking::MarkBlack(mark_bit);
    CHECK(Marking::IsBlack(mark_bit));
    CHECK(Marking::IsBlackOrGrey(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
    Marking::AnyToGrey(mark_bit);
    CHECK(Marking::IsGrey(mark_bit));
    CHECK(Marking::IsBlackOrGrey(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
    Marking::MarkWhite(mark_bit);
    CHECK(Marking::IsWhite(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
  }
  free(bitmap);
}

TEST(Marking, TransitionWhiteGreyBlackGrey) {
  Bitmap* bitmap = reinterpret_cast<Bitmap*>(
      calloc(Bitmap::kSize / kPointerSize, kPointerSize));
  const int kLocationsSize = 3;
  int position[kLocationsSize] = {
      Bitmap::kBitsPerCell - 2, Bitmap::kBitsPerCell - 1, Bitmap::kBitsPerCell};
  for (int i = 0; i < kLocationsSize; i++) {
    MarkBit mark_bit = bitmap->MarkBitFromIndex(position[i]);
    CHECK(Marking::IsWhite(mark_bit));
    CHECK(!Marking::IsBlackOrGrey(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
    Marking::WhiteToGrey(mark_bit);
    CHECK(Marking::IsGrey(mark_bit));
    CHECK(Marking::IsBlackOrGrey(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
    Marking::GreyToBlack(mark_bit);
    CHECK(Marking::IsBlack(mark_bit));
    CHECK(Marking::IsBlackOrGrey(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
    Marking::BlackToGrey(mark_bit);
    CHECK(Marking::IsGrey(mark_bit));
    CHECK(Marking::IsBlackOrGrey(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
    Marking::MarkWhite(mark_bit);
    CHECK(Marking::IsWhite(mark_bit));
    CHECK(!Marking::IsImpossible(mark_bit));
  }
  free(bitmap);
}

}  // namespace internal
}  // namespace v8
