// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A simple interpreter for the Irregexp byte code.

#ifndef V8_REGEXP_REGEXP_INTERPRETER_H_
#define V8_REGEXP_REGEXP_INTERPRETER_H_

#include "src/regexp/regexp.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE IrregexpInterpreter : public AllStatic {
 public:
  enum Result {
    FAILURE = RegExp::kInternalRegExpFailure,
    SUCCESS = RegExp::kInternalRegExpSuccess,
    EXCEPTION = RegExp::kInternalRegExpException,
    RETRY = RegExp::kInternalRegExpRetry,
  };

  // In case a StackOverflow occurs, a StackOverflowException is created and
  // EXCEPTION is returned.
  static Result MatchForCallFromRuntime(Isolate* isolate,
                                        Handle<JSRegExp> regexp,
                                        Handle<String> subject_string,
                                        int* registers, int registers_length,
                                        int start_position);

  // In case a StackOverflow occurs, EXCEPTION is returned. The caller is
  // responsible for creating the exception.
  static Result MatchForCallFromJs(Isolate* isolate, Address regexp,
                                   Address subject, int* registers,
                                   int32_t registers_length,
                                   int32_t start_position);

  static Result MatchInternal(Isolate* isolate, ByteArray code_array,
                              String subject_string, int* registers,
                              int registers_length, int start_position,
                              RegExp::CallOrigin call_origin);

  static void Disassemble(ByteArray byte_array, const std::string& pattern);

 private:
  static Result Match(Isolate* isolate, JSRegExp regexp, String subject_string,
                      int* registers, int registers_length, int start_position,
                      RegExp::CallOrigin call_origin);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_INTERPRETER_H_
