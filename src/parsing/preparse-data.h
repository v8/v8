// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PREPARSE_DATA_H_
#define V8_PARSING_PREPARSE_DATA_H_

#include <unordered_map>

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
      : end_(-1),
        num_parameters_(-1),
        function_length_(-1),
        num_inner_functions_(-1) {}

  void LogFunction(int end, int num_parameters, int function_length,
                   int properties, int num_inner_functions) {
    end_ = end;
    num_parameters_ = num_parameters;
    function_length_ = function_length;
    properties_ = properties;
    num_inner_functions_ = num_inner_functions;
  }

  int end() const { return end_; }
  int num_parameters() const {
    return num_parameters_;
  }
  int function_length() const {
    return function_length_;
  }
  int properties() const {
    return properties_;
  }
  int num_inner_functions() const { return num_inner_functions_; }

 private:
  int end_;
  // For function entries.
  int num_parameters_;
  int function_length_;
  int properties_;
  int num_inner_functions_;
};

class ParserLogger final {
 public:
  ParserLogger();

  void LogFunction(int start, int end, int num_parameters, int function_length,
                   int properties, LanguageMode language_mode,
                   bool uses_super_property, bool calls_eval,
                   int num_inner_functions);

  ScriptData* GetScriptData();

 private:
  Collector<unsigned> function_store_;
  unsigned preamble_[PreparseDataConstants::kHeaderSize];

#ifdef DEBUG
  int prev_start_;
#endif
};

class PreParseData final {
 public:
  struct FunctionData {
    int start;
    int end;
    int num_parameters;
    int function_length;
    int expected_property_count;
    int num_inner_functions;
    LanguageMode language_mode;
    bool uses_super_property;
    bool calls_eval;

    FunctionData() : start(-1), end(-1) {}

    FunctionData(int start, int end, int num_parameters, int function_length,
                 int expected_property_count, int num_inner_functions,
                 LanguageMode language_mode, bool uses_super_property,
                 bool calls_eval)
        : start(start),
          end(end),
          num_parameters(num_parameters),
          function_length(function_length),
          expected_property_count(expected_property_count),
          num_inner_functions(num_inner_functions),
          language_mode(language_mode),
          uses_super_property(uses_super_property),
          calls_eval(calls_eval) {}

    bool is_valid() const { return start < end; }
  };

  FunctionData GetTopLevelFunctionData(int start) const;
  void AddTopLevelFunctionData(FunctionData&& data);
  void AddTopLevelFunctionData(const FunctionData& data);

 private:
  std::unordered_map<int, FunctionData> top_level_functions_data_;
};

}  // namespace internal
}  // namespace v8.

#endif  // V8_PARSING_PREPARSE_DATA_H_
