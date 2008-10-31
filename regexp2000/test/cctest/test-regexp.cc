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


#include "v8.h"

#include "cctest.h"
#include "zone-inl.h"
#include "parser.h"
#include "ast.h"
#include "jsregexp.h"


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


// "123456789abcdb".match(/(.)(.)(.)(.)(.)(.)(.)(.)(.)(.)(.)(.)(.)(\11)/)
// 123456789abcdb,1,2,3,4,5,6,7,8,9,a,b,c,d,b
