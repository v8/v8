// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_UNITTESTS_INTERPRETER_BYTECODE_EXPECTATIONS_PARSER_H_
#define TEST_UNITTESTS_INTERPRETER_BYTECODE_EXPECTATIONS_PARSER_H_

#include <iosfwd>
#include <string>

namespace v8::internal::interpreter {

struct BytecodeExpectationsHeaderOptions {
  bool wrap = true;
  bool module = false;
  bool top_level = false;
  bool print_callee = false;
  std::string test_function_name;
  std::string extra_flags;
};

class BytecodeExpectationsParser {
 public:
  explicit BytecodeExpectationsParser(std::istream* is) : is_(is) {}

  BytecodeExpectationsHeaderOptions ParseHeader();
  bool ReadNextSnippet(std::string* string_out);
  std::string ReadToNextSeparator();

 private:
  std::istream* is_;
};

}  // namespace v8::internal::interpreter

#endif  // TEST_UNITTESTS_INTERPRETER_BYTECODE_EXPECTATIONS_PARSER_H_
