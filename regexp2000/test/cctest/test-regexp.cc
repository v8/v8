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


static void ExpectParse(const char* input,
                        const char* expected) {
  v8::HandleScope scope;
  unibrow::Utf8InputBuffer<> buffer(input, strlen(input));
  ZoneScope zone_scope(DELETE_ON_EXIT);
  Handle<String> error;
  RegExpTree* node = v8::internal::ParseRegExp(&buffer, &error);
  CHECK(node != NULL);
  CHECK(error.is_null());
  SmartPointer<char> output = node->ToString();
  CHECK_EQ(expected, *output);
}


TEST(Parser) {
  V8::Initialize(NULL);
  ExpectParse("abc", "'abc'");
  ExpectParse("", "%");
  ExpectParse("abc|def", "(| 'abc' 'def')");
  ExpectParse("abc|def|ghi", "(| 'abc' 'def' 'ghi')");
  ExpectParse("\\w\\W\\s\\S\\d\\D", "(: [&w] [&W] [&s] [&S] [&d] [&D])");
  ExpectParse("^xxx$", "(: @^i 'xxx' @$i)");
  ExpectParse("ab\\b\\w\\bcd", "(: 'ab' @b [&w] @b 'cd')");
  ExpectParse("\\w|\\s|.", "(| [&w] [&s] [&.])");
  ExpectParse("a*", "(# 0 - g 'a')");
  ExpectParse("a*?", "(# 0 - n 'a')");
  ExpectParse("abc+", "(# 1 - g 'abc')");
  ExpectParse("abc+?", "(# 1 - n 'abc')");
  ExpectParse("xyz?", "(# 0 1 g 'xyz')");
  ExpectParse("xyz??", "(# 0 1 n 'xyz')");
  ExpectParse("xyz{0,1}", "(# 0 1 g 'xyz')");
  ExpectParse("xyz{0,1}?", "(# 0 1 n 'xyz')");
  ExpectParse("xyz{93}", "(# 93 93 g 'xyz')");
  ExpectParse("xyz{93}?", "(# 93 93 n 'xyz')");
  ExpectParse("xyz{1,32}", "(# 1 32 g 'xyz')");
  ExpectParse("xyz{1,32}?", "(# 1 32 n 'xyz')");
  ExpectParse("xyz{1,}", "(# 1 - g 'xyz')");
  ExpectParse("xyz{1,}?", "(# 1 - n 'xyz')");
  ExpectParse("a\\fb\\nc\\rd\\te\\vf", "'a\fb\nc\rd\te\vf'");
  ExpectParse("a\\nb\\bc", "(: 'a\nb' @b 'c')");
  ExpectParse("(?:foo)", "'foo'");
  ExpectParse("(?: foo )", "' foo '");
  ExpectParse("(foo|bar|baz)", "(^ (| 'foo' 'bar' 'baz'))");
  ExpectParse("foo|(bar|baz)|quux", "(| 'foo' (^ (| 'bar' 'baz')) 'quux')");
  ExpectParse("foo(?=bar)baz", "(: 'foo' (-> + 'bar') 'baz')");
  ExpectParse("foo(?!bar)baz", "(: 'foo' (-> - 'bar') 'baz')");
  ExpectParse("()", "(^ %)");
  ExpectParse("(?=)", "(-> + %)");
  ExpectParse("[]", "%");
  ExpectParse("[x]", "[x]");
  ExpectParse("[xyz]", "[x y z]");
  ExpectParse("[a-zA-Z0-9]", "[a-z A-Z 0-9]");
  ExpectParse("[-123]", "[- 1 2 3]");
  ExpectParse("[^123]", "^[1 2 3]");
  ExpectParse("]", "']'");
  ExpectParse("}", "'}'");
  ExpectParse("[a-b-c]", "[a-b - c]");
  ExpectParse("[\\w]", "[&w]");
  ExpectParse("[x\\wz]", "[x &w z]");
  ExpectParse("[\\w-z]", "[&w - z]");
  ExpectParse("[\\w-\\d]", "[&w - &d]");
  ExpectParse("\\cj\\cJ\\ci\\cI\\ck\\cK", "'\n\n\t\t\v\v'");
  ExpectParse("[a\\]c]", "[a ] c]");
  ExpectParse("\\[\\]\\{\\}\\(\\)\\%\\^\\#\\ ", "'[]{}()%^# '");
  ExpectParse("[\\[\\]\\{\\}\\(\\)\\%\\^\\#\\ ]", "[[ ] { } ( ) % ^ #  ]");
  ExpectParse("\\0", "'\0'");
  ExpectParse("\\11", "'\t'");
  ExpectParse("\\11a", "'\ta'");
  ExpectParse("\\011", "'\t'");
  ExpectParse("\\00011", "'\t'");
  ExpectParse("\\118", "'\t8'");
  ExpectParse("\\111", "'I'");
  ExpectParse("\\1111", "'I1'");
  ExpectParse("(.)(.)(.)\\1", "(: (^ [&.]) (^ [&.]) (^ [&.]) (<- 1))");
  ExpectParse("(.)(.)(.)\\2", "(: (^ [&.]) (^ [&.]) (^ [&.]) (<- 2))");
  ExpectParse("(.)(.)(.)\\3", "(: (^ [&.]) (^ [&.]) (^ [&.]) (<- 3))");
  ExpectParse("(.)(.)(.)\\4", "(: (^ [&.]) (^ [&.]) (^ [&.]) '\x04')");
  ExpectParse("(.)(.)(.)\\1*", "(: (^ [&.]) (^ [&.]) (^ [&.])"
                               " (# 0 - g (<- 1)))");
  ExpectParse("(.)(.)(.)\\2*", "(: (^ [&.]) (^ [&.]) (^ [&.])"
                               " (# 0 - g (<- 2)))");
  ExpectParse("(.)(.)(.)\\3*", "(: (^ [&.]) (^ [&.]) (^ [&.])"
                               " (# 0 - g (<- 3)))");
  ExpectParse("(.)(.)(.)\\4*", "(: (^ [&.]) (^ [&.]) (^ [&.])"
                               " (# 0 - g '\x04'))");
  ExpectParse("(.)(.)(.)(.)(.)(.)(.)(.)(.)(.)\\10",
              "(: (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.])"
              " (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.]) (<- 10))");
  ExpectParse("(.)(.)(.)(.)(.)(.)(.)(.)(.)(.)\\11",
              "(: (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.])"
              " (^ [&.]) (^ [&.]) (^ [&.]) (^ [&.]) '\x09')");
  ExpectParse("[\\0]", "[\0]");
  ExpectParse("[\\11]", "[\t]");
  ExpectParse("[\\11a]", "[\t a]");
  ExpectParse("[\\011]", "[\t]");
  ExpectParse("[\\00011]", "[\t]");
  ExpectParse("[\\118]", "[\t 8]");
  ExpectParse("[\\111]", "[I]");
  ExpectParse("[\\1111]", "[I 1]");
  ExpectParse("\\x34", "'\x34'");
  ExpectParse("\\x3z", "'\x03z'");
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
  const char* kIllegalControl = "Illegal control letter";
  ExpectError("\\c!", kIllegalControl);
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
