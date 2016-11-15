// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PREPARSE_DATA_H_
#define V8_PARSING_PREPARSE_DATA_H_

#include "src/allocation.h"
#include "src/base/hashmap.h"
#include "src/collector.h"
#include "src/messages.h"
#include "src/parsing/preparse-data-format.h"

namespace v8 {
namespace internal {

class ScriptData {
 public:
  ScriptData(const byte* data, int length);
  ~ScriptData() {
    if (owns_data_) DeleteArray(data_);
  }

  const byte* data() const { return data_; }
  int length() const { return length_; }
  bool rejected() const { return rejected_; }

  void Reject() { rejected_ = true; }

  void AcquireDataOwnership() {
    DCHECK(!owns_data_);
    owns_data_ = true;
  }

  void ReleaseDataOwnership() {
    DCHECK(owns_data_);
    owns_data_ = false;
  }

 private:
  bool owns_data_ : 1;
  bool rejected_ : 1;
  const byte* data_;
  int length_;

  DISALLOW_COPY_AND_ASSIGN(ScriptData);
};

class PreParserLogger final {
 public:
  PreParserLogger()
      : has_error_(false),
        start_(-1),
        end_(-1),
        num_parameters_(-1),
        function_length_(-1),
        has_duplicate_parameters_(false),
        error_type_(kSyntaxError) {}

  void LogFunction(int end, int num_parameters, int function_length,
                   bool has_duplicate_parameters, int literals,
                   int properties) {
    DCHECK(!has_error_);
    end_ = end;
    num_parameters_ = num_parameters;
    function_length_ = function_length;
    has_duplicate_parameters_ = has_duplicate_parameters;
    literals_ = literals;
    properties_ = properties;
  }

  // Logs an error message and marks the log as containing an error.
  // Further logging will be ignored, and ExtractData will return a vector
  // representing the error only.
  void LogMessage(int start, int end, MessageTemplate::Template message,
                  const char* argument_opt, ParseErrorType error_type) {
    if (has_error_) return;
    has_error_ = true;
    start_ = start;
    end_ = end;
    message_ = message;
    argument_opt_ = argument_opt;
    error_type_ = error_type;
  }

  bool has_error() const { return has_error_; }

  int start() const { return start_; }
  int end() const { return end_; }
  int num_parameters() const {
    DCHECK(!has_error_);
    return num_parameters_;
  }
  int function_length() const {
    DCHECK(!has_error_);
    return function_length_;
  }
  bool has_duplicate_parameters() const {
    DCHECK(!has_error_);
    return has_duplicate_parameters_;
  }
  int literals() const {
    DCHECK(!has_error_);
    return literals_;
  }
  int properties() const {
    DCHECK(!has_error_);
    return properties_;
  }
  ParseErrorType error_type() const {
    DCHECK(has_error_);
    return error_type_;
  }
  MessageTemplate::Template message() {
    DCHECK(has_error_);
    return message_;
  }
  const char* argument_opt() const {
    DCHECK(has_error_);
    return argument_opt_;
  }

 private:
  bool has_error_;
  int start_;
  int end_;
  // For function entries.
  int num_parameters_;
  int function_length_;
  bool has_duplicate_parameters_;
  int literals_;
  int properties_;
  // For error messages.
  MessageTemplate::Template message_;
  const char* argument_opt_;
  ParseErrorType error_type_;
};

class ParserLogger final {
 public:
  struct Key {
    bool is_one_byte;
    Vector<const byte> literal_bytes;
  };

  ParserLogger();

  void LogFunction(int start, int end, int num_parameters, int function_length,
                   bool has_duplicate_parameters, int literals, int properties,
                   LanguageMode language_mode, bool uses_super_property,
                   bool calls_eval);

  // Logs an error message and marks the log as containing an error.
  // Further logging will be ignored, and ExtractData will return a vector
  // representing the error only.
  void LogMessage(int start, int end, MessageTemplate::Template message,
                  const char* argument_opt, ParseErrorType error_type);
  ScriptData* GetScriptData();

  bool HasError() {
    return static_cast<bool>(preamble_[PreparseDataConstants::kHasErrorOffset]);
  }
  Vector<unsigned> ErrorMessageData() {
    DCHECK(HasError());
    return function_store_.ToVector();
  }

 private:
  void WriteString(Vector<const char> str);

  Collector<unsigned> function_store_;
  unsigned preamble_[PreparseDataConstants::kHeaderSize];

#ifdef DEBUG
  int prev_start_;
#endif
};


}  // namespace internal
}  // namespace v8.

#endif  // V8_PARSING_PREPARSE_DATA_H_
