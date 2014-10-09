// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/char-predicates.h"
#include "src/unicode.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

TEST(UnicodePredicatesTest, WhiteSpace) {
  // As of Unicode 6.3.0, \u180E is no longer a white space. We still consider
  // it to be one though, since JS recognizes all white spaces in Unicode 5.1.
  EXPECT_TRUE(WhiteSpace::Is(0x0009));
  EXPECT_TRUE(WhiteSpace::Is(0x000B));
  EXPECT_TRUE(WhiteSpace::Is(0x000C));
  EXPECT_TRUE(WhiteSpace::Is(' '));
  EXPECT_TRUE(WhiteSpace::Is(0x00A0));
  EXPECT_TRUE(WhiteSpace::Is(0x180E));
  EXPECT_TRUE(WhiteSpace::Is(0xFEFF));
}


TEST(UnicodePredicatesTest, WhiteSpaceOrLineTerminator) {
  // As of Unicode 6.3.0, \u180E is no longer a white space. We still consider
  // it to be one though, since JS recognizes all white spaces in Unicode 5.1.
  // White spaces
  EXPECT_TRUE(WhiteSpaceOrLineTerminator::Is(0x0009));
  EXPECT_TRUE(WhiteSpaceOrLineTerminator::Is(0x000B));
  EXPECT_TRUE(WhiteSpaceOrLineTerminator::Is(0x000C));
  EXPECT_TRUE(WhiteSpaceOrLineTerminator::Is(' '));
  EXPECT_TRUE(WhiteSpaceOrLineTerminator::Is(0x00A0));
  EXPECT_TRUE(WhiteSpaceOrLineTerminator::Is(0x180E));
  EXPECT_TRUE(WhiteSpaceOrLineTerminator::Is(0xFEFF));
  // Line terminators
  EXPECT_TRUE(WhiteSpaceOrLineTerminator::Is(0x000A));
  EXPECT_TRUE(WhiteSpaceOrLineTerminator::Is(0x000D));
  EXPECT_TRUE(WhiteSpaceOrLineTerminator::Is(0x2028));
  EXPECT_TRUE(WhiteSpaceOrLineTerminator::Is(0x2029));
}


TEST(UnicodePredicatesTest, IdentifierStart) {
  EXPECT_TRUE(IdentifierStart::Is('$'));
  EXPECT_TRUE(IdentifierStart::Is('_'));
  EXPECT_TRUE(IdentifierStart::Is('\\'));

  // http://www.unicode.org/reports/tr31/
  // Other_ID_Start
  EXPECT_TRUE(IdentifierStart::Is(0x2118));
  EXPECT_TRUE(IdentifierStart::Is(0x212E));
  EXPECT_TRUE(IdentifierStart::Is(0x309B));
  EXPECT_TRUE(IdentifierStart::Is(0x309C));

  // Issue 2892:
  // \u2E2F has the Pattern_Syntax property, excluding it from ID_Start.
  EXPECT_FALSE(unibrow::ID_Start::Is(0x2E2F));
}


TEST(UnicodePredicatesTest, IdentifierPart) {
  EXPECT_TRUE(IdentifierPart::Is('$'));
  EXPECT_TRUE(IdentifierPart::Is('_'));
  EXPECT_TRUE(IdentifierPart::Is('\\'));
  EXPECT_TRUE(IdentifierPart::Is(0x200C));
  EXPECT_TRUE(IdentifierPart::Is(0x200D));

  // http://www.unicode.org/reports/tr31/
  // Other_ID_Start
  EXPECT_TRUE(IdentifierPart::Is(0x2118));
  EXPECT_TRUE(IdentifierPart::Is(0x212E));
  EXPECT_TRUE(IdentifierPart::Is(0x309B));
  EXPECT_TRUE(IdentifierPart::Is(0x309C));

  // Other_ID_Continue
  EXPECT_TRUE(IdentifierPart::Is(0x00B7));
  EXPECT_TRUE(IdentifierPart::Is(0x0387));
  EXPECT_TRUE(IdentifierPart::Is(0x1369));
  EXPECT_TRUE(IdentifierPart::Is(0x1370));
  EXPECT_TRUE(IdentifierPart::Is(0x1371));
  EXPECT_TRUE(IdentifierPart::Is(0x19DA));

  // Issue 2892:
  // \u2E2F has the Pattern_Syntax property, excluding it from ID_Start.
  EXPECT_FALSE(IdentifierPart::Is(0x2E2F));
}

}  // namespace internal
}  // namespace v8
