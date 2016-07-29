// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_DISPATCHER_COMPILER_DISPATCHER_JOB_H_
#define V8_COMPILER_DISPATCHER_COMPILER_DISPATCHER_JOB_H_

#include <memory>

#include "src/base/macros.h"
#include "src/handles.h"

namespace v8 {
namespace internal {

class CompilationInfo;
class Isolate;
class JSFunction;
class ParseInfo;
class UnicodeCache;
class Utf16CharacterStream;
class Zone;

enum class CompileJobStatus {
  kInitial,
  kReadyToParse,
};

class CompilerDispatcherJob {
 public:
  CompilerDispatcherJob(Isolate* isolate, Handle<JSFunction> function);
  ~CompilerDispatcherJob();

  CompileJobStatus status() const { return status_; }

  // Transition from kInitial to kReadyToParse.
  void PrepareToParseOnMainThread();

 private:
  CompileJobStatus status_ = CompileJobStatus::kInitial;
  Isolate* isolate_;
  Handle<JSFunction> function_;  // Global handle.

  // Members required for parsing.
  std::unique_ptr<UnicodeCache> unicode_cache_;
  std::unique_ptr<Zone> zone_;
  std::unique_ptr<Utf16CharacterStream> character_stream_;
  std::unique_ptr<ParseInfo> parse_info_;

  bool can_parse_on_background_thread_;

  DISALLOW_COPY_AND_ASSIGN(CompilerDispatcherJob);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_DISPATCHER_COMPILER_DISPATCHER_JOB_H_
