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
#include <set>

#include "v8.h"

#include "cctest.h"
#include "zone-inl.h"
#include "parser.h"
#include "ast.h"
#include "jsregexp-inl.h"
#include "assembler-re2k.h"
#include "regexp-macro-assembler.h"
#include "regexp-macro-assembler-re2k.h"
#include "interpreter-re2k.h"


using namespace v8::internal;


static SmartPointer<const char> Parse(const char* input) {
  v8::HandleScope scope;
  unibrow::Utf8InputBuffer<> buffer(input, strlen(input));
  ZoneScope zone_scope(DELETE_ON_EXIT);
  RegExpParseResult result;
  CHECK(v8::internal::ParseRegExp(&buffer, &result));
  CHECK(result.tree != NULL);
  CHECK(result.error.is_null());
  SmartPointer<const char> output = result.tree->ToString();
  return output;
}

static bool ParseEscapes(const char* input) {
  v8::HandleScope scope;
  unibrow::Utf8InputBuffer<> buffer(input, strlen(input));
  ZoneScope zone_scope(DELETE_ON_EXIT);
  RegExpParseResult result;
  CHECK(v8::internal::ParseRegExp(&buffer, &result));
  CHECK(result.tree != NULL);
  CHECK(result.error.is_null());
  return result.has_character_escapes;
}


#define CHECK_PARSE_EQ(input, expected) CHECK_EQ(expected, *Parse(input))
#define CHECK_ESCAPES(input, has_escapes) CHECK_EQ(has_escapes, \
                                                   ParseEscapes(input));

TEST(Parser) {
  V8::Initialize(NULL);
  CHECK_PARSE_EQ("abc", "'abc'");
  CHECK_PARSE_EQ("", "%");
  CHECK_PARSE_EQ("abc|def", "(| 'abc' 'def')");
  CHECK_PARSE_EQ("abc|def|ghi", "(| 'abc' 'def' 'ghi')");
  CHECK_PARSE_EQ("^xxx$", "(: @^i 'xxx' @$i)");
  CHECK_PARSE_EQ("ab\\b\\d\\bcd", "(: 'ab' @b [0-9] @b 'cd')");
  CHECK_PARSE_EQ("\\w|\\d", "(| [0-9 A-Z _ a-z] [0-9])");
  CHECK_PARSE_EQ("a*", "(# 0 - g 'a')");
  CHECK_PARSE_EQ("a*?", "(# 0 - n 'a')");
  CHECK_PARSE_EQ("abc+", "(: 'ab' (# 1 - g 'c'))");
  CHECK_PARSE_EQ("abc+?", "(: 'ab' (# 1 - n 'c'))");
  CHECK_PARSE_EQ("xyz?", "(: 'xy' (# 0 1 g 'z'))");
  CHECK_PARSE_EQ("xyz??", "(: 'xy' (# 0 1 n 'z'))");
  CHECK_PARSE_EQ("xyz{0,1}", "(: 'xy' (# 0 1 g 'z'))");
  CHECK_PARSE_EQ("xyz{0,1}?", "(: 'xy' (# 0 1 n 'z'))");
  CHECK_PARSE_EQ("xyz{93}", "(: 'xy' (# 93 93 g 'z'))");
  CHECK_PARSE_EQ("xyz{93}?", "(: 'xy' (# 93 93 n 'z'))");
  CHECK_PARSE_EQ("xyz{1,32}", "(: 'xy' (# 1 32 g 'z'))");
  CHECK_PARSE_EQ("xyz{1,32}?", "(: 'xy' (# 1 32 n 'z'))");
  CHECK_PARSE_EQ("xyz{1,}", "(: 'xy' (# 1 - g 'z'))");
  CHECK_PARSE_EQ("xyz{1,}?", "(: 'xy' (# 1 - n 'z'))");
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
  CHECK_PARSE_EQ("[]", "^[\x00-\uffff]");
  CHECK_PARSE_EQ("[^]", "[\x00-\uffff]");
  CHECK_PARSE_EQ("[x]", "[x]");
  CHECK_PARSE_EQ("[xyz]", "[x y z]");
  CHECK_PARSE_EQ("[a-zA-Z0-9]", "[a-z A-Z 0-9]");
  CHECK_PARSE_EQ("[-123]", "[- 1 2 3]");
  CHECK_PARSE_EQ("[^123]", "^[1 2 3]");
  CHECK_PARSE_EQ("]", "']'");
  CHECK_PARSE_EQ("}", "'}'");
  CHECK_PARSE_EQ("[a-b-c]", "[a-b - c]");
  CHECK_PARSE_EQ("[\\d]", "[0-9]");
  CHECK_PARSE_EQ("[x\\dz]", "[x 0-9 z]");
  CHECK_PARSE_EQ("[\\d-z]", "[0-9 - z]");
  CHECK_PARSE_EQ("[\\d-\\d]", "[0-9 - 0-9]");
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
  CHECK_PARSE_EQ("(x)(x)(x)\\1", "(: (^ 'x') (^ 'x') (^ 'x') (<- 1))");
  CHECK_PARSE_EQ("(x)(x)(x)\\2", "(: (^ 'x') (^ 'x') (^ 'x') (<- 2))");
  CHECK_PARSE_EQ("(x)(x)(x)\\3", "(: (^ 'x') (^ 'x') (^ 'x') (<- 3))");
  CHECK_PARSE_EQ("(x)(x)(x)\\4", "(: (^ 'x') (^ 'x') (^ 'x') '\x04')");
  CHECK_PARSE_EQ("(x)(x)(x)\\1*", "(: (^ 'x') (^ 'x') (^ 'x')"
                               " (# 0 - g (<- 1)))");
  CHECK_PARSE_EQ("(x)(x)(x)\\2*", "(: (^ 'x') (^ 'x') (^ 'x')"
                               " (# 0 - g (<- 2)))");
  CHECK_PARSE_EQ("(x)(x)(x)\\3*", "(: (^ 'x') (^ 'x') (^ 'x')"
                               " (# 0 - g (<- 3)))");
  CHECK_PARSE_EQ("(x)(x)(x)\\4*", "(: (^ 'x') (^ 'x') (^ 'x')"
                               " (# 0 - g '\x04'))");
  CHECK_PARSE_EQ("(x)(x)(x)(x)(x)(x)(x)(x)(x)(x)\\10",
              "(: (^ 'x') (^ 'x') (^ 'x') (^ 'x') (^ 'x') (^ 'x')"
              " (^ 'x') (^ 'x') (^ 'x') (^ 'x') (<- 10))");
  CHECK_PARSE_EQ("(x)(x)(x)(x)(x)(x)(x)(x)(x)(x)\\11",
              "(: (^ 'x') (^ 'x') (^ 'x') (^ 'x') (^ 'x') (^ 'x')"
              " (^ 'x') (^ 'x') (^ 'x') (^ 'x') '\x09')");
  CHECK_PARSE_EQ("(a)\\1", "(: (^ 'a') (<- 1))");
  CHECK_PARSE_EQ("(a\\1)", "(^ 'a')");
  CHECK_PARSE_EQ("(\\1a)", "(^ 'a')");
  CHECK_PARSE_EQ("\\1(a)", "(: '\x01' (^ 'a'))");
  CHECK_PARSE_EQ("(?!(a))\\1", "(-> - (^ 'a'))");
  CHECK_PARSE_EQ("(?!\\1(a\\1)\\1)\\1", "(-> - (: '\x01' (^ 'a') (<- 1)))");
  CHECK_PARSE_EQ("[\\0]", "[\0]");
  CHECK_PARSE_EQ("[\\11]", "[\t]");
  CHECK_PARSE_EQ("[\\11a]", "[\t a]");
  CHECK_PARSE_EQ("[\\011]", "[\t]");
  CHECK_PARSE_EQ("[\\00011]", "[\000 1 1]");
  CHECK_PARSE_EQ("[\\118]", "[\t 8]");
  CHECK_PARSE_EQ("[\\111]", "[I]");
  CHECK_PARSE_EQ("[\\1111]", "[I 1]");
  CHECK_PARSE_EQ("\\x34", "'\x34'");
  CHECK_PARSE_EQ("\\x60", "'\x60'");
  CHECK_PARSE_EQ("\\x3z", "'x3z'");
  CHECK_PARSE_EQ("\\u0034", "'\x34'");
  CHECK_PARSE_EQ("\\u003z", "'u003z'");

  CHECK_ESCAPES("a", false);
  CHECK_ESCAPES("a|b", false);
  CHECK_ESCAPES("a\\n", true);
  CHECK_ESCAPES("^a", false);
  CHECK_ESCAPES("a$", false);
  CHECK_ESCAPES("a\\b!", false);
  CHECK_ESCAPES("a\\Bb", false);
  CHECK_ESCAPES("a*", false);
  CHECK_ESCAPES("a*?", false);
  CHECK_ESCAPES("a?", false);
  CHECK_ESCAPES("a??", false);
  CHECK_ESCAPES("a{0,1}?", false);
  CHECK_ESCAPES("a{1,1}?", false);
  CHECK_ESCAPES("a{1,2}?", false);
  CHECK_ESCAPES("a+?", false);
  CHECK_ESCAPES("(a)", false);
  CHECK_ESCAPES("(a)\\1", false);
  CHECK_ESCAPES("(\\1a)", false);
  CHECK_ESCAPES("\\1(a)", true);
  CHECK_ESCAPES("a\\s", false);
  CHECK_ESCAPES("a\\S", false);
  CHECK_ESCAPES("a\\d", false);
  CHECK_ESCAPES("a\\D", false);
  CHECK_ESCAPES("a\\w", false);
  CHECK_ESCAPES("a\\W", false);
  CHECK_ESCAPES("a.", false);
  CHECK_ESCAPES("a\\q", true);
  CHECK_ESCAPES("a[a]", false);
  CHECK_ESCAPES("a[^a]", false);
  CHECK_ESCAPES("a[a-z]", false);
  CHECK_ESCAPES("a[\\q]", false);
  CHECK_ESCAPES("a(?:b)", false);
  CHECK_ESCAPES("a(?=b)", false);
  CHECK_ESCAPES("a(?!b)", false);
  CHECK_ESCAPES("\\x60", true);
  CHECK_ESCAPES("\\u0060", true);
  CHECK_ESCAPES("\\cA", true);
  CHECK_ESCAPES("\\q", true);
  CHECK_ESCAPES("\\1112", true);
  CHECK_ESCAPES("\\0", true);
  CHECK_ESCAPES("(a)\\1", false);
}

TEST(ParserRegression) {
  CHECK_PARSE_EQ("[A-Z$-][x]", "(: [A-Z $ -] [x])");
}

static void ExpectError(const char* input,
                        const char* expected) {
  v8::HandleScope scope;
  unibrow::Utf8InputBuffer<> buffer(input, strlen(input));
  ZoneScope zone_scope(DELETE_ON_EXIT);
  RegExpParseResult result;
  CHECK_EQ(false, v8::internal::ParseRegExp(&buffer, &result));
  CHECK(result.tree == NULL);
  CHECK(!result.error.is_null());
  SmartPointer<char> str = result.error->ToCString(ALLOW_NULLS);
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


static bool IsDigit(uc16 c) {
  return ('0' <= c && c <= '9');
}


static bool NotDigit(uc16 c) {
  return !IsDigit(c);
}


static bool IsWhiteSpace(uc16 c) {
  switch (c) {
    case 0x09: case 0x0B: case 0x0C: case 0x20: case 0xA0:
      return true;
    default:
      return unibrow::Space::Is(c);
  }
}


static bool NotWhiteSpace(uc16 c) {
  return !IsWhiteSpace(c);
}


static bool IsWord(uc16 c) {
  return ('a' <= c && c <= 'z')
      || ('A' <= c && c <= 'Z')
      || ('0' <= c && c <= '9')
      || (c == '_');
}


static bool NotWord(uc16 c) {
  return !IsWord(c);
}


static bool Dot(uc16 c) {
  return true;
}


static void TestCharacterClassEscapes(uc16 c, bool (pred)(uc16 c)) {
  ZoneScope scope(DELETE_ON_EXIT);
  ZoneList<CharacterRange>* ranges = new ZoneList<CharacterRange>(2);
  CharacterRange::AddClassEscape(c, ranges);
  for (unsigned i = 0; i < (1 << 16); i++) {
    bool in_class = false;
    for (int j = 0; !in_class && j < ranges->length(); j++) {
      CharacterRange& range = ranges->at(j);
      in_class = (range.from() <= i && i <= range.to());
    }
    CHECK_EQ(pred(i), in_class);
  }
}


TEST(CharacterClassEscapes) {
  TestCharacterClassEscapes('.', Dot);
  TestCharacterClassEscapes('d', IsDigit);
  TestCharacterClassEscapes('D', NotDigit);
  TestCharacterClassEscapes('s', IsWhiteSpace);
  TestCharacterClassEscapes('S', NotWhiteSpace);
  TestCharacterClassEscapes('w', IsWord);
  TestCharacterClassEscapes('W', NotWord);
}


static void Execute(const char* input,
                    const char* str,
                    bool dot_output = false) {
  v8::HandleScope scope;
  unibrow::Utf8InputBuffer<> buffer(input, strlen(input));
  ZoneScope zone_scope(DELETE_ON_EXIT);
  RegExpParseResult result;
  if (!v8::internal::ParseRegExp(&buffer, &result))
    return;
  RegExpNode* node = NULL;
  RegExpEngine::Compile(&result, &node, false);
  USE(node);
#ifdef DEBUG
  if (dot_output) {
    RegExpEngine::DotPrint(input, node);
    exit(0);
  }
#endif  // DEBUG
}


TEST(Execution) {
  V8::Initialize(NULL);
  Execute(".*?(?:a[bc]d|e[fg]h)", "xxxabbegh");
  Execute(".*?(?:a[bc]d|e[fg]h)", "xxxabbefh");
  Execute(".*?(?:a[bc]d|e[fg]h)", "xxxabbefd");
}


class TestConfig {
 public:
  typedef int Key;
  typedef int Value;
  static const int kNoKey;
  static const int kNoValue;
  static inline int Compare(int a, int b) {
    if (a < b)
      return -1;
    else if (a > b)
      return 1;
    else
      return 0;
  }
};


const int TestConfig::kNoKey = 0;
const int TestConfig::kNoValue = 0;


static int PseudoRandom(int i, int j) {
  return ~(~((i * 781) ^ (j * 329)));
}


TEST(SplayTreeSimple) {
  static const int kLimit = 1000;
  ZoneScope zone_scope(DELETE_ON_EXIT);
  ZoneSplayTree<TestConfig> tree;
  std::set<int> seen;
#define CHECK_MAPS_EQUAL() do {                                      \
    for (int k = 0; k < kLimit; k++)                                 \
      CHECK_EQ(seen.find(k) != seen.end(), tree.Find(k, &loc));      \
  } while (false)
  for (int i = 0; i < 50; i++) {
    for (int j = 0; j < 50; j++) {
      int next = PseudoRandom(i, j) % kLimit;
      if (seen.find(next) != seen.end()) {
        // We've already seen this one.  Check the value and remove
        // it.
        ZoneSplayTree<TestConfig>::Locator loc;
        CHECK(tree.Find(next, &loc));
        CHECK_EQ(next, loc.key());
        CHECK_EQ(3 * next, loc.value());
        tree.Remove(next);
        seen.erase(next);
        CHECK_MAPS_EQUAL();
      } else {
        // Check that it wasn't there already and then add it.
        ZoneSplayTree<TestConfig>::Locator loc;
        CHECK(!tree.Find(next, &loc));
        CHECK(tree.Insert(next, &loc));
        CHECK_EQ(next, loc.key());
        loc.set_value(3 * next);
        seen.insert(next);
        CHECK_MAPS_EQUAL();
      }
      int val = PseudoRandom(j, i) % kLimit;
      for (int k = val; k >= 0; k--) {
        if (seen.find(val) != seen.end()) {
          ZoneSplayTree<TestConfig>::Locator loc;
          CHECK(tree.FindGreatestLessThan(val, &loc));
          CHECK_EQ(loc.key(), val);
          break;
        }
      }
      val = PseudoRandom(i + j, i - j) % kLimit;
      for (int k = val; k < kLimit; k++) {
        if (seen.find(val) != seen.end()) {
          ZoneSplayTree<TestConfig>::Locator loc;
          CHECK(tree.FindLeastGreaterThan(val, &loc));
          CHECK_EQ(loc.key(), val);
          break;
        }
      }
    }
  }
}


TEST(DispatchTableConstruction) {
  // Initialize test data.
  static const int kLimit = 1000;
  static const int kRangeCount = 8;
  static const int kRangeSize = 16;
  uc16 ranges[kRangeCount][2 * kRangeSize];
  for (int i = 0; i < kRangeCount; i++) {
    Vector<uc16> range(ranges[i], 2 * kRangeSize);
    for (int j = 0; j < 2 * kRangeSize; j++) {
      range[j] = PseudoRandom(i + 25, j + 87) % kLimit;
    }
    range.Sort();
    for (int j = 1; j < 2 * kRangeSize; j++) {
      CHECK(range[j-1] <= range[j]);
    }
  }
  // Enter test data into dispatch table.
  ZoneScope zone_scope(DELETE_ON_EXIT);
  DispatchTable table;
  for (int i = 0; i < kRangeCount; i++) {
    uc16* range = ranges[i];
    for (int j = 0; j < 2 * kRangeSize; j += 2)
      table.AddRange(CharacterRange(range[j], range[j + 1]), i);
  }
  // Check that the table looks as we would expect
  for (int p = 0; p < kLimit; p++) {
    OutSet* outs = table.Get(p);
    for (int j = 0; j < kRangeCount; j++) {
      uc16* range = ranges[j];
      bool is_on = false;
      for (int k = 0; !is_on && (k < 2 * kRangeSize); k += 2)
        is_on = (range[k] <= p && p <= range[k + 1]);
      CHECK_EQ(is_on, outs->Get(j));
    }
  }
}


TEST(Assembler) {
  V8::Initialize(NULL);
  byte codes[1024];
  Re2kAssembler assembler(Vector<byte>(codes, 1024));
#define __ assembler.
  Label advance;
  Label look_for_foo;
  Label fail;
  __ GoTo(&look_for_foo);
  __ Bind(&advance);
  __ AdvanceCP(1);
  __ Bind(&look_for_foo);
  __ LoadCurrentChar(0, &fail);
  __ CheckChar('f', &advance);
  __ LoadCurrentChar(1, &fail);
  __ CheckChar('o', &advance);
  __ LoadCurrentChar(2, &fail);
  __ CheckChar('o', &advance);
  __ SetRegisterToCurrentPosition(0);
  __ SetRegisterToCurrentPosition(1, 2);
  __ Succeed();
  __ Bind(&fail);
  __ Fail();

  v8::HandleScope scope;
  Handle<ByteArray> array = Factory::NewByteArray(assembler.length());
  assembler.Copy(array->GetDataStartAddress());
  int captures[2];

  Handle<String> f1 =
      Factory::NewStringFromAscii(CStrVector("Now is the time"));
  CHECK(!Re2kInterpreter::Match(array, f1, captures, 0));

  Handle<String> f2 = Factory::NewStringFromAscii(CStrVector("foo bar baz"));
  CHECK(Re2kInterpreter::Match(array, f2, captures, 0));
  CHECK_EQ(0, captures[0]);
  CHECK_EQ(2, captures[1]);

  Handle<String> f3 = Factory::NewStringFromAscii(CStrVector("tomfoolery"));
  CHECK(Re2kInterpreter::Match(array, f3, captures, 0));
  CHECK_EQ(3, captures[0]);
  CHECK_EQ(5, captures[1]);
}


TEST(Assembler2) {
  V8::Initialize(NULL);
  byte codes[1024];
  Re2kAssembler assembler(Vector<byte>(codes, 1024));
#define __ assembler.
  // /^.*foo/
  Label more_dots;
  Label unwind_dot;
  Label failure;
  Label foo;
  Label foo_failed;
  Label dot_match;
  // ^
  __ PushCurrentPosition();
  __ PushRegister(0);
  __ SetRegisterToCurrentPosition(0);
  __ PushBacktrack(&failure);
  __ GoTo(&dot_match);
  // .*
  __ Bind(&more_dots);
  __ AdvanceCP(1);
  __ Bind(&dot_match);
  __ PushCurrentPosition();
  __ PushBacktrack(&unwind_dot);
  __ LoadCurrentChar(0, &foo);
  __ CheckChar('\n', &more_dots);
  // foo
  __ Bind(&foo);
  __ CheckChar('f', &foo_failed);
  __ LoadCurrentChar(1, &foo_failed);
  __ CheckChar('o', &foo_failed);
  __ LoadCurrentChar(2, &foo_failed);
  __ CheckChar('o', &foo_failed);
  __ SetRegisterToCurrentPosition(1, 2);
  __ Succeed();
  __ Break();

  __ Bind(&foo_failed);
  __ PopBacktrack();
  __ Break();

  __ Bind(&unwind_dot);
  __ PopCurrentPosition();
  __ LoadCurrentChar(0, &foo_failed);
  __ GoTo(&foo);

  __ Bind(&failure);
  __ PopRegister(0);
  __ PopCurrentPosition();
  __ Fail();

  v8::HandleScope scope;
  Handle<ByteArray> array = Factory::NewByteArray(assembler.length());
  assembler.Copy(array->GetDataStartAddress());
  int captures[2];

  Handle<String> f1 =
      Factory::NewStringFromAscii(CStrVector("Now is the time"));
  CHECK(!Re2kInterpreter::Match(array, f1, captures, 0));

  Handle<String> f2 = Factory::NewStringFromAscii(CStrVector("foo bar baz"));
  CHECK(Re2kInterpreter::Match(array, f2, captures, 0));
  CHECK_EQ(0, captures[0]);
  CHECK_EQ(2, captures[1]);

  Handle<String> f3 = Factory::NewStringFromAscii(CStrVector("tomfoolery"));
  CHECK(Re2kInterpreter::Match(array, f3, captures, 0));
  CHECK_EQ(0, captures[0]);
  CHECK_EQ(5, captures[1]);

  Handle<String> f4 =
      Factory::NewStringFromAscii(CStrVector("football buffoonery"));
  CHECK(Re2kInterpreter::Match(array, f4, captures, 0));
  CHECK_EQ(0, captures[0]);
  CHECK_EQ(14, captures[1]);

  Handle<String> f5 =
      Factory::NewStringFromAscii(CStrVector("walking\nbarefoot"));
  CHECK(!Re2kInterpreter::Match(array, f5, captures, 0));
}


TEST(MacroAssembler) {
  V8::Initialize(NULL);
  byte codes[1024];
  Re2kAssembler assembler(Vector<byte>(codes, 1024));
  RegExpMacroAssemblerRe2k m(&assembler);
  // ^f(o)o.
  Label fail, fail2, start;
  uc16 foo_chars[3];
  foo_chars[0] = 'f';
  foo_chars[1] = 'o';
  foo_chars[2] = 'o';
  Vector<const uc16> foo(foo_chars, 3);
  m.SetRegister(4, 42);
  m.PushRegister(4);
  m.AdvanceRegister(4, 42);
  m.GoTo(&start);
  m.Fail();
  m.Bind(&start);
  m.PushBacktrack(&fail2);
  m.CheckCharacters(foo, 0, &fail);
  m.WriteCurrentPositionToRegister(0);
  m.PushCurrentPosition();
  m.AdvanceCurrentPosition(3);
  m.WriteCurrentPositionToRegister(1);
  m.PopCurrentPosition();
  m.AdvanceCurrentPosition(1);
  m.WriteCurrentPositionToRegister(2);
  m.AdvanceCurrentPosition(1);
  m.WriteCurrentPositionToRegister(3);
  m.Succeed();

  m.Bind(&fail);
  m.Backtrack();
  m.Succeed();

  m.Bind(&fail2);
  m.PopRegister(0);
  m.Fail();

  v8::HandleScope scope;

  Handle<ByteArray> array = Factory::NewByteArray(assembler.length());
  assembler.Copy(array->GetDataStartAddress());
  int captures[5];

  Handle<String> f1 =
      Factory::NewStringFromAscii(CStrVector("foobar"));
  CHECK(Re2kInterpreter::Match(array, f1, captures, 0));
  CHECK_EQ(0, captures[0]);
  CHECK_EQ(3, captures[1]);
  CHECK_EQ(1, captures[2]);
  CHECK_EQ(2, captures[3]);
  CHECK_EQ(84, captures[4]);

  Handle<String> f2 =
      Factory::NewStringFromAscii(CStrVector("barfoo"));
  CHECK(!Re2kInterpreter::Match(array, f2, captures, 0));
  CHECK_EQ(42, captures[0]);
}


TEST(AddInverseToTable) {
  static const int kLimit = 1000;
  static const int kRangeCount = 16;
  for (int t = 0; t < 10; t++) {
    ZoneScope zone_scope(DELETE_ON_EXIT);
    ZoneList<CharacterRange>* ranges =
        new ZoneList<CharacterRange>(kRangeCount);
    for (int i = 0; i < kRangeCount; i++) {
      int from = PseudoRandom(t + 87, i + 25) % kLimit;
      int to = from + (PseudoRandom(i + 87, t + 25) % (kLimit / 20));
      if (to > kLimit) to = kLimit;
      ranges->Add(CharacterRange(from, to));
    }
    DispatchTable table;
    DispatchTableConstructor cons(&table);
    cons.set_choice_index(0);
    cons.AddInverse(ranges);
    for (int i = 0; i < kLimit; i++) {
      bool is_on = false;
      for (int j = 0; !is_on && j < kRangeCount; j++)
        is_on = ranges->at(j).Contains(i);
      OutSet* set = table.Get(i);
      CHECK_EQ(is_on, set->Get(0) == false);
    }
  }
  ZoneScope zone_scope(DELETE_ON_EXIT);
  ZoneList<CharacterRange>* ranges =
          new ZoneList<CharacterRange>(1);
  ranges->Add(CharacterRange(0xFFF0, 0xFFFE));
  DispatchTable table;
  DispatchTableConstructor cons(&table);
  cons.set_choice_index(0);
  cons.AddInverse(ranges);
  CHECK(!table.Get(0xFFFE)->Get(0));
  CHECK(table.Get(0xFFFF)->Get(0));
}


TEST(Graph) {
  Execute("a|(b|c)|d", "", true);
}
