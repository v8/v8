// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_DISPATCHER_UNOPTIMIZED_COMPILE_JOB_H_
#define V8_COMPILER_DISPATCHER_UNOPTIMIZED_COMPILE_JOB_H_

#include <memory>

#include "include/v8.h"
#include "src/base/macros.h"
#include "src/compiler-dispatcher/compiler-dispatcher-job.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

class AstValueFactory;
class AstStringConstants;
class CompilerDispatcherTracer;
class CompilationInfo;
class CompilationJob;
class DeferredHandles;
class FunctionLiteral;
class Isolate;
class ParseInfo;
class Parser;
class SharedFunctionInfo;
class String;
class UnicodeCache;
class Utf16CharacterStream;

class V8_EXPORT_PRIVATE UnoptimizedCompileJobFinishCallback {
 public:
  virtual ~UnoptimizedCompileJobFinishCallback() {}
  virtual void ParseFinished(std::unique_ptr<ParseInfo> parse_info) = 0;
};

class V8_EXPORT_PRIVATE UnoptimizedCompileJob : public CompilerDispatcherJob {
 public:
  enum class Status {
    kInitial,
    kReadyToParse,
    kParsed,
    kReadyToAnalyze,
    kAnalyzed,
    kReadyToCompile,
    kCompiled,
    kDone,
    kFailed,
  };

  // Creates a UnoptimizedCompileJob in the initial state.
  UnoptimizedCompileJob(Isolate* isolate, CompilerDispatcherTracer* tracer,
                        Handle<SharedFunctionInfo> shared,
                        size_t max_stack_size);
  // TODO(wiktorg) document it better once I know how it relates to whole stuff
  // Creates a UnoptimizedCompileJob in ready to parse top-level function state.
  UnoptimizedCompileJob(int main_thread_id, CompilerDispatcherTracer* tracer,
                        size_t max_stack_size, Handle<String> source,
                        int start_position, int end_position,
                        LanguageMode language_mode, int function_literal_id,
                        bool native, bool module, bool is_named_expression,
                        uint32_t hash_seed, AccountingAllocator* zone_allocator,
                        int compiler_hints,
                        const AstStringConstants* ast_string_constants,
                        UnoptimizedCompileJobFinishCallback* finish_callback);

  ~UnoptimizedCompileJob() override;

  Type type() const override { return kUnoptimizedCompile; }

  Handle<SharedFunctionInfo> shared() const { return shared_; }

  // Returns true if this UnoptimizedCompileJob was created for the given
  // function.
  bool IsAssociatedWith(Handle<SharedFunctionInfo> shared) const;

  bool IsFinished() override {
    return status() == Status::kDone || status() == Status::kFailed;
  }

  bool IsFailed() override { return status() == Status::kFailed; }

  // Return true if the next step can be run on any thread, that is when both
  // StepNextOnMainThread and StepNextOnBackgroundThread could be used for the
  // next step.
  bool CanStepNextOnAnyThread() override {
    return status() == Status::kReadyToParse ||
           status() == Status::kReadyToCompile;
  }

  // Step the job forward by one state on the main thread.
  void StepNextOnMainThread(Isolate* isolate) override;

  // Step the job forward by one state on a background thread.
  void StepNextOnBackgroundThread() override;

  // Transition from any state to kInitial and free all resources.
  void ResetOnMainThread(Isolate* isolate) override;

  // Estimate how long the next step will take using the tracer.
  double EstimateRuntimeOfNextStepInMs() const override;

  void ShortPrintOnMainThread() override;

 private:
  friend class CompilerDispatcherTest;
  friend class UnoptimizedCompileJobTest;

  bool has_context() const { return !context_.is_null(); }
  Context* context() { return *context_; }

  Status status() const { return status_; }

  Status status_;
  int main_thread_id_;
  CompilerDispatcherTracer* tracer_;
  Handle<Context> context_;            // Global handle.
  Handle<SharedFunctionInfo> shared_;  // Global handle.
  Handle<String> source_;              // Global handle.
  Handle<String> wrapper_;             // Global handle.
  std::unique_ptr<v8::String::ExternalStringResourceBase> source_wrapper_;
  size_t max_stack_size_;
  UnoptimizedCompileJobFinishCallback* finish_callback_ = nullptr;

  // Members required for parsing.
  std::unique_ptr<UnicodeCache> unicode_cache_;
  std::unique_ptr<Utf16CharacterStream> character_stream_;
  std::unique_ptr<ParseInfo> parse_info_;
  std::unique_ptr<Parser> parser_;

  // Members required for compiling.
  std::shared_ptr<Zone> compile_zone_;
  std::unique_ptr<CompilationInfo> compile_info_;
  std::unique_ptr<CompilationJob> compile_job_;

  bool trace_compiler_dispatcher_jobs_;

  // Transition from kInitial to kReadyToParse.
  void PrepareToParseOnMainThread(Isolate* isolate);

  // Transition from kReadyToParse to kParsed (or kDone if there is
  // finish_callback).
  void Parse();

  // Transition from kParsed to kReadyToAnalyze (or kFailed).
  void FinalizeParsingOnMainThread(Isolate* isolate);

  // Transition from kReadyToAnalyze to kAnalyzed (or kFailed).
  void AnalyzeOnMainThread(Isolate* isolate);

  // Transition from kAnalyzed to kReadyToCompile (or kFailed).
  void PrepareToCompileOnMainThread(Isolate* isolate);

  // Transition from kReadyToCompile to kCompiled.
  void Compile();

  // Transition from kCompiled to kDone (or kFailed).
  void FinalizeCompilingOnMainThread(Isolate* isolate);

  DISALLOW_COPY_AND_ASSIGN(UnoptimizedCompileJob);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_DISPATCHER_UNOPTIMIZED_COMPILE_JOB_H_
