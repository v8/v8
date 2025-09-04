// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/interpreter/bytecode-expectations-parser.h"

#include <string>

#include "src/base/logging.h"

namespace v8::internal::interpreter {

namespace {

bool ParseBoolean(const char* string) {
  if (strcmp(string, "yes") == 0) {
    return true;
  } else if (strcmp(string, "no") == 0) {
    return false;
  } else {
    FATAL("Unrecognised boolean: %s (must be 'yes' or 'no')", string);
  }
}

const char* GetHeaderParam(std::string_view line, const char* key) {
  if (!line.starts_with(key)) return nullptr;
  std::string_view post_key = line.substr(strlen(key));
  if (!post_key.starts_with(": ")) return nullptr;
  return post_key.substr(2).data();
}

std::string UnescapeString(const std::string& escaped_string) {
  std::string unescaped_string;
  bool previous_was_backslash = false;
  for (char c : escaped_string) {
    if (previous_was_backslash) {
      // If it was not an escape sequence, emit the previous backslash.
      if (c != '\\' && c != '"') unescaped_string += '\\';
      unescaped_string += c;
      previous_was_backslash = false;
    } else {
      if (c == '\\') {
        previous_was_backslash = true;
        // Defer emission to the point where we can check if it was an escape.
      } else {
        unescaped_string += c;
      }
    }
  }
  if (previous_was_backslash) {
    // Emit the previous backslash if it wasn't emitted.
    unescaped_string += '\\';
  }
  return unescaped_string;
}

}  // namespace

BytecodeExpectationsHeaderOptions BytecodeExpectationsParser::ParseHeader() {
  BytecodeExpectationsHeaderOptions options;
  std::string line;

  // Skip to the beginning of the options header.
  while (std::getline(*is_, line)) {
    if (line == "---") break;
  }

  while (std::getline(*is_, line)) {
    const char* v;
    if ((v = GetHeaderParam(line, "module"))) {
      options.module = ParseBoolean(v);
    } else if ((v = GetHeaderParam(line, "wrap"))) {
      options.wrap = ParseBoolean(v);
    } else if ((v = GetHeaderParam(line, "test function name"))) {
      options.test_function_name = std::string(v);
    } else if ((v = GetHeaderParam(line, "top level"))) {
      options.top_level = ParseBoolean(v);
    } else if ((v = GetHeaderParam(line, "print callee"))) {
      options.print_callee = ParseBoolean(v);
    } else if ((v = GetHeaderParam(line, "extra flags"))) {
      options.extra_flags = std::string(v);
    } else if (line.empty()) {
      continue;
    } else if (line == "---") {
      break;
    } else {
      FATAL("Unrecognised option: %s", line.c_str());
    }
  }
  return options;
}

bool BytecodeExpectationsParser::ReadNextSnippet(std::string* string_out) {
  std::string line;
  bool found_begin_snippet = false;
  string_out->clear();
  while (std::getline(*is_, line)) {
    if (line == "snippet: \"") {
      found_begin_snippet = true;
      continue;
    }
    if (!found_begin_snippet) continue;
    if (line == "\"") return true;
    if (line.size() == 0) {
      string_out->append("\n");  // consume empty line.
      continue;
    }
    CHECK_GE(line.size(), 2u);  // We should have the indent.
    line = UnescapeString(line);
    string_out->append(line.begin() + 2, line.end());
    *string_out += '\n';
  }
  return false;
}

std::string BytecodeExpectationsParser::ReadToNextSeparator() {
  std::string out;
  std::string line;
  while (std::getline(*is_, line)) {
    if (line == "---") break;
    out += line + "\n";
  }
  return out;
}

}  // namespace v8::internal::interpreter
