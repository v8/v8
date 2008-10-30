// Copyright 2006-2008 the V8 project authors. All rights reserved.
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

#include "v8.h"

#include "cctest.h"
#include "zone-inl.h"
#include "parser.h"
#include "ast.h"
#include "jsregexp-inl.h"


using namespace v8::internal;


class RegExpTestCase {
 public:
  RegExpTestCase()
    : pattern_(NULL),
      flags_(NULL),
      input_(NULL),
      compile_error_(NULL) { }
  RegExpTestCase(const char* pattern,
                 const char* flags,
                 const char* input,
                 const char* compile_error)
    : pattern_(pattern),
      flags_(flags),
      input_(input),
      compile_error_(compile_error) { }
  const char* pattern() const { return pattern_; }
  bool expect_error() const { return compile_error_ != NULL; }
 private:
  const char* pattern_;
  const char* flags_;
  const char* input_;
  const char* compile_error_;
};


#ifdef USE_FUZZ_TEST_DATA
#include "regexp-test-data.cc"
#else
static const int kCaseCount = 0;
static const RegExpTestCase kCases[1] = { RegExpTestCase() };
#endif


static SmartPointer<char> Parse(const char* input) {
  v8::HandleScope scope;
  unibrow::Utf8InputBuffer<> buffer(input, strlen(input));
  ZoneScope zone_scope(DELETE_ON_EXIT);
  Handle<String> error;
  RegExpTree* node = v8::internal::ParseRegExp(&buffer, &error);
  CHECK(node != NULL);
  CHECK(error.is_null());
  SmartPointer<char> output = node->ToString();
  return output;
}


#define CHECK_PARSE_EQ(input, expected) CHECK_EQ(expected, *Parse(input))


TEST(Parser) {
  V8::Initialize(NULL);
  CHECK_PARSE_EQ("abc", "'abc'");
  CHECK_PARSE_EQ("", "%");
  CHECK_PARSE_EQ("abc|def", "(| 'abc' 'def')");
  CHECK_PARSE_EQ("abc|def|ghi", "(| 'abc' 'def' 'ghi')");
  CHECK_PARSE_EQ("\\w\\W\\s\\S\\d\\D", "(: [&w] [&W] [&s] [&S] [&d] [&D])");
  CHECK_PARSE_EQ("^xxx$", "(: @^i 'xxx' @$i)");
  CHECK_PARSE_EQ("ab\\b\\w\\bcd", "(: 'ab' @b [&w] @b 'cd')");
  CHECK_PARSE_EQ("\\w|\\s|.", "(| [&w] [&s] [&.])");
  CHECK_PARSE_EQ("a*", "(# 0 - g 'a')");
  CHECK_PARSE_EQ("a*?", "(# 0 - n 'a')");
  CHECK_PARSE_EQ("abc+", "(# 1 - g 'abc')");
  CHECK_PARSE_EQ("abc+?", "(# 1 - n 'abc')");
  CHECK_PARSE_EQ("xyz?", "(# 0 1 g 'xyz')");
  CHECK_PARSE_EQ("xyz??", "(# 0 1 n 'xyz')");
  CHECK_PARSE_EQ("xyz{0,1}", "(# 0 1 g 'xyz')");
  CHECK_PARSE_EQ("xyz{0,1}?", "(# 0 1 n 'xyz')");
  CHECK_PARSE_EQ("xyz{93}", "(# 93 93 g 'xyz')");
  CHECK_PARSE_EQ("xyz{93}?", "(# 93 93 n 'xyz')");
  CHECK_PARSE_EQ("xyz{1,32}", "(# 1 32 g 'xyz')");
  CHECK_PARSE_EQ("xyz{1,32}?", "(# 1 32 n 'xyz')");
  CHECK_PARSE_EQ("xyz{1,}", "(# 1 - g 'xyz')");
  CHECK_PARSE_EQ("xyz{1,}?", "(# 1 - n 'xyz')");
  CHECK_PARSE_EQ("a\\fb\\nc\\rd\\te\\vf", "'a\fb\nc\rd\te\vf'");
  CHECK_PARSE_EQ("a\\nb\\bc", "(: 'a\nb' @b 'c')");
  CHECK_PARSE_EQ("(?:foo)", "'foo'");
  CHECK_PARSE_EQ("(?: foo )", "' foo '");
  CHECK_PARSE_EQ("(foo|bar|baz)", "(^ (| 'foo' 'bar' 'baz'))");
  CHECK_PARSE_EQ("foo|(bar|baz)|quux", "(| 'foo' (^ (| 'bar' 'baz')) 'quux')");
  CHECK_PARSE_EQ("foo(?=bar)baz", "(: 'foo' (-> + 'bar') 'baz')");
  CHECK_PARSE_EQ("foo(?!bar)baz", "(: 'foo' (-> - 'bar') 'baz')");
  CHECK_PARSE_EQ("()", "(^ %)");
  CHECK_PARSE_EQ("(?=)", "(-> + %)");
  CHECK_PARSE_EQ("[]", "%");
  CHECK_PARSE_EQ("[x]", "[x]");
  CHECK_PARSE_EQ("[xyz]", "[x y z]");
  CHECK_PARSE_EQ("[a-zA-Z0-9]", "[a-z A-Z 0-9]");
  CHECK_PARSE_EQ("[-123]", "[- 1 2 3]");
  CHECK_PARSE_EQ("[^123]", "^[1 2 3]");
  CHECK_PARSE_EQ("]", "']'");
  CHECK_PARSE_EQ("}", "'}'");
  CHECK_PARSE_EQ("[a-b-c]", "[a-b - c]");
  CHECK_PARSE_EQ("[\\w]", "[&w]");
  CHECK_PARSE_EQ("[x\\wz]", "[x &w z]");
  CHECK_PARSE_EQ("[\\w-z]", "[&w - z]");
  CHECK_PARSE_EQ("[\\w-\\d]", "[&w - &d]");
  CHECK_PARSE_EQ("\\cj\\cJ\\ci\\cI\\ck\\cK", "'\n\n\t\t\v\v'");
  CHECK_PARSE_EQ("\\c!", "'c!'");
  CHECK_PARSE_EQ("\\c_", "'c_'");
  CHECK_PARSE_EQ("\\c~", "'c~'");
  CHECK_PARSE_EQ("[a\\]c]", "[a ] c]");
  CHECK_PARSE_EQ("\\[\\]\\{\\}\\(\\)\\%\\^\\#\\ ", "'[]{}()%^# '");
  CHECK_PARSE_EQ("[\\[\\]\\{\\}\\(\\)\\%\\^\\#\\ ]", "[[ ] { } ( ) % ^ #  ]");
  CHECK_PARSE_EQ("\\0", "'\0'");
  CHECK_PARSE_EQ("\\8", "'8'");
  CHECK_PARSE_EQ("\\9", "'9'");
  CHECK_PARSE_EQ("\\11", "'\t'");
  CHECK_PARSE_EQ("\\11a", "'\ta'");
  CHECK_PARSE_EQ("\\011", "'\t'");
  CHECK_PARSE_EQ("\\00011", "'\00011'");
  CHECK_PARSE_EQ("\\118", "'\t8'");
  CHECK_PARSE_EQ("\\111", "'I'");
  CHECK_PARSE_EQ("\\1111", "'I1'");
  CHECK_PARSE_EQ("(.)(.)(.)\\1", "(: (^ [&.]) (^ [&.]) (^ [&.]) (<- 1))");
  CHECK_PARSE_EQ("(.)(.)(.)\\2", "(: (^ [&.]) (^ [&.]) (^ [&.]) (<- 2))");
  CHECK_PARSE_EQ("(.)(.)(.)\\3", "(: (^ [&.]) (^ [&.]) (^ [&.]) (<- 3))");
  CHECK_PARSE_EQ("(.)(.)(.)\\4", "(: (^ [&.]) (^ [&.]) (^ [&.]) '\x04')");
  CHECK_PARSE_EQ("(.)(.)(.)\\1*", "(: (^ [&.]) (^ [&.]) (^ [&.])"
                               " (# 0 - g (<- 1)))");
  CHECK_PARSE_EQ("(.)(.)(.)\\2*", "(: (^ [&.]) (^ [&.]) (^ [&.])"
                               " (# 0 - g (<- 2)))");
  CHECK_PARSE_EQ("(.)(.)(.)\\3*", "(: (^ [&.]) (^ [&.]) (^ [&.])"
                               " (# 0 - g (<- 3)))");
  CHECK_PARSE_EQ("(.)(.)(.)\\4*", "(: (^ [&.]) (^ [&.]) (^ [&.])"
                               " (# 0 - g '\x04'))");
  CHECK_PARSE_EQ("(.)(.)(.)(.)(.)(.)(.)(.)(.)(.)\\10",
              "(: (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.])"
              " (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.]) (<- 10))");
  CHECK_PARSE_EQ("(.)(.)(.)(.)(.)(.)(.)(.)(.)(.)\\11",
              "(: (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.])"
              " (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.]) '\x09')");
  CHECK_PARSE_EQ("[\\0]", "[\0]");
  CHECK_PARSE_EQ("[\\11]", "[\t]");
  CHECK_PARSE_EQ("[\\11a]", "[\t a]");
  CHECK_PARSE_EQ("[\\011]", "[\t]");
  CHECK_PARSE_EQ("[\\00011]", "[\000 1 1]");
  CHECK_PARSE_EQ("[\\118]", "[\t 8]");
  CHECK_PARSE_EQ("[\\111]", "[I]");
  CHECK_PARSE_EQ("[\\1111]", "[I 1]");
  CHECK_PARSE_EQ("\\x34", "'\x34'");
  CHECK_PARSE_EQ("\\x3z", "'x3z'");
  CHECK_PARSE_EQ("\\u0034", "'\x34'");
  CHECK_PARSE_EQ("\\u003z", "'u003z'");
}


static void ExpectError(const char* input,
                        const char* expected) {
  v8::HandleScope scope;
  unibrow::Utf8InputBuffer<> buffer(input, strlen(input));
  ZoneScope zone_scope(DELETE_ON_EXIT);
  Handle<String> error;
  RegExpTree* node = v8::internal::ParseRegExp(&buffer, &error);
  CHECK(node == NULL);
  CHECK(!error.is_null());
  SmartPointer<char> str = error->ToCString(ALLOW_NULLS);
  CHECK_EQ(expected, *str);
}


TEST(Errors) {
  V8::Initialize(NULL);
  const char* kEndBackslash = "\\ at end of pattern";
  ExpectError("\\", kEndBackslash);
  const char* kInvalidQuantifier = "Invalid quantifier";
  ExpectError("a{}", kInvalidQuantifier);
  ExpectError("a{,}", kInvalidQuantifier);
  ExpectError("a{", kInvalidQuantifier);
  ExpectError("a{z}", kInvalidQuantifier);
  ExpectError("a{1z}", kInvalidQuantifier);
  ExpectError("a{12z}", kInvalidQuantifier);
  ExpectError("a{12,", kInvalidQuantifier);
  ExpectError("a{12,3b", kInvalidQuantifier);
  const char* kUnterminatedGroup = "Unterminated group";
  ExpectError("(foo", kUnterminatedGroup);
  const char* kInvalidGroup = "Invalid group";
  ExpectError("(?", kInvalidGroup);
  const char* kUnterminatedCharacterClass = "Unterminated character class";
  ExpectError("[", kUnterminatedCharacterClass);
  ExpectError("[a-", kUnterminatedCharacterClass);
  const char* kIllegalCharacterClass = "Illegal character class";
  ExpectError("[a-\\w]", kIllegalCharacterClass);
  const char* kEndControl = "\\c at end of pattern";
  ExpectError("\\c", kEndControl);
}


static void Execute(bool expected, const char* input, const char* str) {
  v8::HandleScope scops;
  unibrow::Utf8InputBuffer<> buffer(input, strlen(input));
  ZoneScope zone_scope(DELETE_ON_EXIT);
  Handle<String> error;
  RegExpTree* tree = v8::internal::ParseRegExp(&buffer, &error);
  CHECK(tree != NULL);
  CHECK(error.is_null());
  RegExpNode<const char>* node = RegExpEngine::Compile<const char>(tree);
  bool outcome = RegExpEngine::Execute(node, CStrVector(str));
  CHECK_EQ(outcome, expected);
}


TEST(Execution) {
  V8::Initialize(NULL);
  Execute(true, ".*?(?:a[bc]d|e[fg]h)", "xxxabbegh");
  Execute(true, ".*?(?:a[bc]d|e[fg]h)", "xxxabbefh");
  Execute(false, ".*?(?:a[bc]d|e[fg]h)", "xxxabbefd");
}


TEST(Fuzz) {
  V8::Initialize(NULL);
  for (int i = 0; i < kCaseCount; i++) {
    const RegExpTestCase* c = &kCases[i];
    v8::HandleScope scope;
    printf("%s\n", c->pattern());
    unibrow::Utf8InputBuffer<> buffer(c->pattern(), strlen(c->pattern()));
    ZoneScope zone_scope(DELETE_ON_EXIT);
    Handle<String> error;
    RegExpTree* node = v8::internal::ParseRegExp(&buffer, &error);
    if (c->expect_error()) {
      CHECK(node == NULL);
      CHECK(!error.is_null());
    } else {
      CHECK(node != NULL);
      CHECK(error.is_null());
    }
  }
}


TEST(SingletonField) {
  // Test all bits from 0 to 256
  for (int i = 0; i < 256; i++) {
    CharacterClass entry = CharacterClass::SingletonField(i);
    for (int j = 0; j < 256; j++) {
      CHECK_EQ(i == j, entry.Contains(j));
    }
  }
  // Test upwards through the data range
  static const uint32_t kMax = 1 << 16;
  for (uint32_t i = 0; i < kMax; i = 1 + static_cast<uint32_t>(i * 1.2)) {
    CharacterClass entry = CharacterClass::SingletonField(i);
    for (uint32_t j = 0; j < kMax; j = 1 + static_cast<uint32_t>(j * 1.2)) {
      CHECK_EQ(i == j, entry.Contains(j));
    }
  }
}


TEST(RangeField) {
  // Test bitfields containing a single range.
  for (int i = 256; i < 320; i++) {
    for (int j = i; j < 320; j++) {
      CharacterClass::Range range(i, j);
      CharacterClass entry = CharacterClass::RangeField(range);
      for (int k = 256; k < 320; k++) {
        CHECK_EQ(i <= k && k <= j, entry.Contains(k));
      }
    }
  }
}


static void TestBuiltins(CharacterClass klass, bool (pred)(uc16)) {
  for (int i = 0; i < (1 << 16); i++)
    CHECK_EQ(pred(i), klass.Contains(i));
}


static bool IsDigit(uc16 c) {
  return ('0' <= c && c <= '9');
}


static bool IsWhiteSpace(uc16 c) {
  switch (c) {
    case 0x09: case 0x0B: case 0x0C: case 0x20: case 0xA0:
      return true;
    default:
      return unibrow::Space::Is(c);
  }
}


static bool IsWord(uc16 c) {
  return ('a' <= c && c <= 'z')
      || ('A' <= c && c <= 'Z')
      || ('0' <= c && c <= '9')
      || (c == '_');
}


TEST(Builtins) {
  TestBuiltins(*CharacterClass::GetCharacterClass('d'), IsDigit);
  TestBuiltins(*CharacterClass::GetCharacterClass('s'), IsWhiteSpace);
  TestBuiltins(*CharacterClass::GetCharacterClass('w'), IsWord);
}


TEST(SimpleRanges) {
  // Test range classes containing a single range.
  for (int i = 365; i < 730; i += 3) {
    for (int j = i; j < 1095; j += 3) {
      EmbeddedVector<CharacterClass::Range, 1> entries;
      entries[0] = CharacterClass::Range(i, j);
      CharacterClass klass = CharacterClass::Ranges(entries, NULL);
      for (int k = 0; k < 1095; k += 3) {
        CHECK_EQ(i <= k && k <= j, klass.Contains(k));
      }
    }
  }
}


static unsigned PseudoRandom(int i, int j) {
  return (~((i * 781) ^ (j * 329)));
}


// Generates pseudo-random character-class with kCount pseudo-random
// ranges set.
template <int kCount>
class SimpleRangeGenerator {
 public:
  SimpleRangeGenerator() : i_(0) { }

  // Returns the next character class and sets the ranges vector.  The
  // returned value will not have any ranges that extend beyond the 'max'
  // value.
  CharacterClass Next(int max) {
    for (int j = 0; j < 2 * kCount; j++) {
      entries_[j] = PseudoRandom(i_, j) % max;
    }
    i_++;
    qsort(entries_.start(), 2 * kCount, sizeof(uc16), Compare);
    for (int j = 0; j < kCount; j++) {
      ranges_[j] = CharacterClass::Range(entries_[2 * j],
                                         entries_[2 * j + 1]);
    }
    return CharacterClass::Ranges(ranges_, NULL);
  }

  // Returns the ranges of the last range that was returned.  Note that
  // the returned vector will be clobbered the next time Next() is called.
  Vector<CharacterClass::Range> ranges() { return ranges_; }
 private:

  static int Compare(const void* a, const void* b) {
    return *static_cast<const uc16*>(a) - *static_cast<const uc16*>(b);
  }

  int i_;
  EmbeddedVector<uc16, 2 * kCount> entries_;
  EmbeddedVector<CharacterClass::Range, kCount> ranges_;
};


TEST(LessSimpleRanges) {
  // Tests range character classes containing 3 pseudo-random ranges.
  SimpleRangeGenerator<3> gen;
  for (int i = 0; i < 1024; i++) {
    CharacterClass klass = gen.Next(256);
    Vector<CharacterClass::Range> entries = gen.ranges();
    for (int j = 0; j < 256; j++) {
      bool is_on = false;
      for (int k = 0; !is_on && k < 3; k++)
        is_on = (entries[k].from() <= j && j <= entries[k].to());
      CHECK_EQ(is_on, klass.Contains(j));
    }
  }
}


TEST(Unions) {
  SimpleRangeGenerator<3> gen1;
  SimpleRangeGenerator<3> gen2;
  for (int i = 0; i < 1024; i++) {
    CharacterClass klass1 = gen1.Next(256);
    CharacterClass klass2 = gen2.Next(256);
    CharacterClass uhnion = CharacterClass::Union(&klass1, &klass2);
    for (int j = 0; j < 256; j++)
      CHECK_EQ(klass1.Contains(j) || klass2.Contains(j), uhnion.Contains(j));
  }
}
