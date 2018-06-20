// Copyright 2007-2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdlib.h>

#include "src/v8.h"

#include "src/api.h"
#include "src/debug/liveedit.h"
#include "src/objects-inl.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace {
void CompareStringsOneWay(const char* s1, const char* s2,
                          int expected_diff_parameter,
                          std::vector<SourceChangeRange>* changes) {
  i::Isolate* isolate = CcTest::i_isolate();
  i::Handle<i::String> i_s1 = isolate->factory()->NewStringFromAsciiChecked(s1);
  i::Handle<i::String> i_s2 = isolate->factory()->NewStringFromAsciiChecked(s2);
  changes->clear();
  LiveEdit::CompareStrings(isolate, i_s1, i_s2, changes);

  int len1 = StrLength(s1);
  int len2 = StrLength(s2);

  int pos1 = 0;
  int pos2 = 0;

  int diff_parameter = 0;
  for (const auto& diff : *changes) {
    int diff_pos1 = diff.start_position;
    int similar_part_length = diff_pos1 - pos1;
    int diff_pos2 = pos2 + similar_part_length;

    CHECK_EQ(diff_pos2, diff.new_start_position);

    for (int j = 0; j < similar_part_length; j++) {
      CHECK(pos1 + j < len1);
      CHECK(pos2 + j < len2);
      CHECK_EQ(s1[pos1 + j], s2[pos2 + j]);
    }
    int diff_len1 = diff.end_position - diff.start_position;
    int diff_len2 = diff.new_end_position - diff.new_start_position;
    diff_parameter += diff_len1 + diff_len2;
    pos1 = diff_pos1 + diff_len1;
    pos2 = diff_pos2 + diff_len2;
  }
  {
    // After last chunk.
    int similar_part_length = len1 - pos1;
    CHECK_EQ(similar_part_length, len2 - pos2);
    USE(len2);
    for (int j = 0; j < similar_part_length; j++) {
      CHECK(pos1 + j < len1);
      CHECK(pos2 + j < len2);
      CHECK_EQ(s1[pos1 + j], s2[pos2 + j]);
    }
  }

  if (expected_diff_parameter != -1) {
    CHECK_EQ(expected_diff_parameter, diff_parameter);
  }
}

void CompareStringsOneWay(const char* s1, const char* s2,
                          int expected_diff_parameter = -1) {
  std::vector<SourceChangeRange> changes;
  CompareStringsOneWay(s1, s2, expected_diff_parameter, &changes);
}

void CompareStringsOneWay(const char* s1, const char* s2,
                          std::vector<SourceChangeRange>* changes) {
  CompareStringsOneWay(s1, s2, -1, changes);
}

void CompareStrings(const char* s1, const char* s2,
                    int expected_diff_parameter = -1) {
  CompareStringsOneWay(s1, s2, expected_diff_parameter);
  CompareStringsOneWay(s2, s1, expected_diff_parameter);
}

void CompareOneWayPlayWithLF(const char* s1, const char* s2) {
  std::string s1_one_line(s1);
  std::replace(s1_one_line.begin(), s1_one_line.end(), '\n', ' ');
  std::string s2_one_line(s2);
  std::replace(s2_one_line.begin(), s2_one_line.end(), '\n', ' ');
  CompareStringsOneWay(s1, s2, -1);
  CompareStringsOneWay(s1_one_line.c_str(), s2, -1);
  CompareStringsOneWay(s1, s2_one_line.c_str(), -1);
  CompareStringsOneWay(s1_one_line.c_str(), s2_one_line.c_str(), -1);
}

void CompareStringsPlayWithLF(const char* s1, const char* s2) {
  CompareOneWayPlayWithLF(s1, s2);
  CompareOneWayPlayWithLF(s2, s1);
}
}  // anonymous namespace

TEST(LiveEditDiffer) {
  v8::HandleScope handle_scope(CcTest::isolate());
  CompareStrings("zz1zzz12zz123zzz", "zzzzzzzzzz", 6);
  CompareStrings("zz1zzz12zz123zzz", "zz0zzz0zz0zzz", 9);
  CompareStrings("123456789", "987654321", 16);
  CompareStrings("zzz", "yyy", 6);
  CompareStrings("zzz", "zzz12", 2);
  CompareStrings("zzz", "21zzz", 2);
  CompareStrings("cat", "cut", 2);
  CompareStrings("ct", "cut", 1);
  CompareStrings("cat", "ct", 1);
  CompareStrings("cat", "cat", 0);
  CompareStrings("", "", 0);
  CompareStrings("cat", "", 3);
  CompareStrings("a cat", "a capybara", 7);
  CompareStrings("abbabababababaaabbabababababbabbbbbbbababa",
                 "bbbbabababbbabababbbabababababbabbababa");
  CompareStringsPlayWithLF("", "");
  CompareStringsPlayWithLF("a", "b");
  CompareStringsPlayWithLF(
      "yesterday\nall\nmy\ntroubles\nseemed\nso\nfar\naway",
      "yesterday\nall\nmy\ntroubles\nseem\nso\nfar\naway");
  CompareStringsPlayWithLF(
      "yesterday\nall\nmy\ntroubles\nseemed\nso\nfar\naway",
      "\nall\nmy\ntroubles\nseemed\nso\nfar\naway");
  CompareStringsPlayWithLF(
      "yesterday\nall\nmy\ntroubles\nseemed\nso\nfar\naway",
      "all\nmy\ntroubles\nseemed\nso\nfar\naway");
  CompareStringsPlayWithLF(
      "yesterday\nall\nmy\ntroubles\nseemed\nso\nfar\naway",
      "yesterday\nall\nmy\ntroubles\nseemed\nso\nfar\naway\n");
  CompareStringsPlayWithLF(
      "yesterday\nall\nmy\ntroubles\nseemed\nso\nfar\naway",
      "yesterday\nall\nmy\ntroubles\nseemed\nso\n");
}

TEST(LiveEditTranslatePosition) {
  v8::HandleScope handle_scope(CcTest::isolate());
  std::vector<SourceChangeRange> changes;
  CompareStringsOneWay("a", "a", &changes);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 0), 0);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 1), 1);
  CompareStringsOneWay("a", "b", &changes);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 0), 0);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 1), 1);
  CompareStringsOneWay("ababa", "aaa", &changes);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 0), 0);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 1), 1);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 2), 1);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 3), 2);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 4), 2);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 5), 3);
  CompareStringsOneWay("ababa", "acaca", &changes);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 0), 0);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 1), 1);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 2), 2);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 3), 3);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 4), 4);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 5), 5);
  CompareStringsOneWay("aaa", "ababa", &changes);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 0), 0);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 1), 2);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 2), 4);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 3), 5);
  CompareStringsOneWay("aabbaaaa", "aaaabbaa", &changes);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 0), 0);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 1), 1);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 2), 4);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 3), 5);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 4), 6);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 5), 7);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 6), 8);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 8), 8);
}
}  // namespace internal
}  // namespace v8
