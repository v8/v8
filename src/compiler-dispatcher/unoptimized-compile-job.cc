// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/unoptimized-compile-job.h"

#include "src/assert-scope.h"
#include "src/base/optional.h"
#include "src/compilation-info.h"
#include "src/compiler-dispatcher/compiler-dispatcher-tracer.h"
#include "src/compiler.h"
#include "src/flags.h"
#include "src/global-handles.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/unicode-cache.h"
#include "src/utils.h"

namespace v8 {
namespace internal {

namespace {

class OneByteWrapper : public v8::String::ExternalOneByteStringResource {
 public:
  OneByteWrapper(const void* data, int length) : data_(data), length_(length) {}
  ~OneByteWrapper() override = default;

  const char* data() const override {
    return reinterpret_cast<const char*>(data_);
  }

  size_t length() const override { return static_cast<size_t>(length_); }

 private:
  const void* data_;
  int length_;

  DISALLOW_COPY_AND_ASSIGN(OneByteWrapper);
};

class TwoByteWrapper : public v8::String::ExternalStringResource {
 public:
  TwoByteWrapper(const void* data, int length) : data_(data), length_(length) {}
  ~TwoByteWrapper() override = default;

  const uint16_t* data() const override {
    return reinterpret_cast<const uint16_t*>(data_);
  }

  size_t length() const override { return static_cast<size_t>(length_); }

 private:
  const void* data_;
  int length_;

  DISALLOW_COPY_AND_ASSIGN(TwoByteWrapper);
};

}  // namespace

UnoptimizedCompileJob::UnoptimizedCompileJob(
    int main_thread_id, CompilerDispatcherTracer* tracer, size_t max_stack_size,
    Handle<String> source, int start_position, int end_position,
    LanguageMode language_mode, int function_literal_id, bool native,
    bool module, bool is_named_expression, uint32_t hash_seed,
    AccountingAllocator* zone_allocator, int compiler_hints,
    const AstStringConstants* ast_string_constants,
    UnoptimizedCompileJobFinishCallback* finish_callback)
    : status_(Status::kReadyToParse),
      main_thread_id_(main_thread_id),
      tracer_(tracer),
      max_stack_size_(max_stack_size),
      finish_callback_(finish_callback),
      trace_compiler_dispatcher_jobs_(FLAG_trace_compiler_dispatcher_jobs) {
  parse_info_.reset(new ParseInfo(zone_allocator));
  DCHECK(source->IsExternalTwoByteString() ||
         source->IsExternalOneByteString());
  character_stream_.reset(
      ScannerStream::For(source, start_position, end_position));
  parse_info_->set_character_stream(character_stream_.get());
  parse_info_->set_hash_seed(hash_seed);
  parse_info_->set_compiler_hints(compiler_hints);
  parse_info_->set_start_position(start_position);
  parse_info_->set_end_position(end_position);
  unicode_cache_.reset(new UnicodeCache());
  parse_info_->set_unicode_cache(unicode_cache_.get());
  parse_info_->set_language_mode(language_mode);
  parse_info_->set_function_literal_id(function_literal_id);
  parse_info_->set_ast_string_constants(ast_string_constants);
  if (V8_UNLIKELY(FLAG_runtime_stats)) {
    parse_info_->set_runtime_call_stats(new (parse_info_->zone())
                                            RuntimeCallStats());
  }

  parse_info_->set_native(native);
  parse_info_->set_module(module);
  parse_info_->set_is_named_expression(is_named_expression);

  parser_.reset(new Parser(parse_info_.get()));
  parser_->DeserializeScopeChain(parse_info_.get(), MaybeHandle<ScopeInfo>());

  if (trace_compiler_dispatcher_jobs_) {
    PrintF("UnoptimizedCompileJob[%p] created for ", static_cast<void*>(this));
    ShortPrintOnMainThread();
    PrintF(" in ready to parse state.\n");
  }
}

UnoptimizedCompileJob::UnoptimizedCompileJob(Isolate* isolate,
                                             CompilerDispatcherTracer* tracer,
                                             Handle<SharedFunctionInfo> shared,
                                             size_t max_stack_size)
    : status_(Status::kInitial),
      main_thread_id_(isolate->thread_id().ToInteger()),
      tracer_(tracer),
      context_(isolate->global_handles()->Create(isolate->context())),
      shared_(isolate->global_handles()->Create(*shared)),
      max_stack_size_(max_stack_size),
      trace_compiler_dispatcher_jobs_(FLAG_trace_compiler_dispatcher_jobs) {
  DCHECK(!shared_->is_toplevel());
  HandleScope scope(isolate);
  Handle<Script> script(Script::cast(shared_->script()), isolate);
  Handle<String> source(String::cast(script->source()), isolate);
  if (trace_compiler_dispatcher_jobs_) {
    PrintF("UnoptimizedCompileJob[%p] created for ", static_cast<void*>(this));
    ShortPrintOnMainThread();
    PrintF(" in initial state.\n");
  }
}

UnoptimizedCompileJob::~UnoptimizedCompileJob() {
  DCHECK(status_ == Status::kInitial ||
         (status_ == Status::kReadyToParse && finish_callback_) ||
         status_ == Status::kDone);
  if (!shared_.is_null()) {
    DCHECK_EQ(ThreadId::Current().ToInteger(), main_thread_id_);
    i::GlobalHandles::Destroy(Handle<Object>::cast(shared_).location());
  }
  if (!context_.is_null()) {
    DCHECK_EQ(ThreadId::Current().ToInteger(), main_thread_id_);
    i::GlobalHandles::Destroy(Handle<Object>::cast(context_).location());
  }
}

bool UnoptimizedCompileJob::IsAssociatedWith(
    Handle<SharedFunctionInfo> shared) const {
  return *shared_ == *shared;
}

void UnoptimizedCompileJob::StepNextOnMainThread(Isolate* isolate) {
  DCHECK_EQ(isolate->thread_id().ToInteger(), main_thread_id_);

  // Ensure we are in the correct context for the job.
  SaveContext save(isolate);
  if (has_context()) {
    isolate->set_context(context());
  } else {
    // Phases which can run off the main thread by definition can't execute any
    // JS code, and so we don't need to enter their context.
    DCHECK(CanStepNextOnAnyThread());
  }

  switch (status()) {
    case Status::kInitial:
      return PrepareToParseOnMainThread(isolate);

    case Status::kReadyToParse:
      return Parse();

    case Status::kParsed:
      return FinalizeParsingOnMainThread(isolate);

    case Status::kReadyToAnalyze:
      return AnalyzeOnMainThread(isolate);

    case Status::kAnalyzed:
      return PrepareToCompileOnMainThread(isolate);

    case Status::kReadyToCompile:
      return Compile();

    case Status::kCompiled:
      return FinalizeCompilingOnMainThread(isolate);

    case Status::kFailed:
    case Status::kDone:
      return;
  }
  UNREACHABLE();
}

void UnoptimizedCompileJob::StepNextOnBackgroundThread() {
  DCHECK(CanStepNextOnAnyThread());
  switch (status()) {
    case Status::kReadyToParse:
      return Parse();

    case Status::kReadyToCompile:
      return Compile();

    default:
      UNREACHABLE();
  }
}

void UnoptimizedCompileJob::PrepareToParseOnMainThread(Isolate* isolate) {
  DCHECK_EQ(ThreadId::Current().ToInteger(), main_thread_id_);
  DCHECK_EQ(isolate->thread_id().ToInteger(), main_thread_id_);
  DCHECK(status() == Status::kInitial);
  COMPILER_DISPATCHER_TRACE_SCOPE(tracer_, kPrepareToParse);
  if (trace_compiler_dispatcher_jobs_) {
    PrintF("UnoptimizedCompileJob[%p]: Preparing to parse\n",
           static_cast<void*>(this));
  }
  HandleScope scope(isolate);
  unicode_cache_.reset(new UnicodeCache());
  Handle<Script> script(Script::cast(shared_->script()), isolate);
  DCHECK(script->type() != Script::TYPE_NATIVE);

  Handle<String> source(String::cast(script->source()), isolate);
  parse_info_.reset(new ParseInfo(isolate->allocator()));
  if (source->IsExternalTwoByteString() || source->IsExternalOneByteString()) {
    character_stream_.reset(ScannerStream::For(
        source, shared_->start_position(), shared_->end_position()));
  } else {
    source = String::Flatten(source);
    const void* data;
    int offset = 0;
    int length = source->length();

    // Objects in lo_space don't move, so we can just read the contents from
    // any thread.
    if (isolate->heap()->lo_space()->Contains(*source)) {
      // We need to globalize the handle to the flattened string here, in
      // case it's not referenced from anywhere else.
      source_ = isolate->global_handles()->Create(*source);
      DisallowHeapAllocation no_allocation;
      String::FlatContent content = source->GetFlatContent();
      DCHECK(content.IsFlat());
      data =
          content.IsOneByte()
              ? reinterpret_cast<const void*>(content.ToOneByteVector().start())
              : reinterpret_cast<const void*>(content.ToUC16Vector().start());
    } else {
      // Otherwise, create a copy of the part of the string we'll parse in the
      // zone.
      length = (shared_->end_position() - shared_->start_position());
      offset = shared_->start_position();

      int byte_len = length * (source->IsOneByteRepresentation() ? 1 : 2);
      data = parse_info_->zone()->New(byte_len);

      DisallowHeapAllocation no_allocation;
      String::FlatContent content = source->GetFlatContent();
      DCHECK(content.IsFlat());
      if (content.IsOneByte()) {
        MemCopy(const_cast<void*>(data),
                &content.ToOneByteVector().at(shared_->start_position()),
                byte_len);
      } else {
        MemCopy(const_cast<void*>(data),
                &content.ToUC16Vector().at(shared_->start_position()),
                byte_len);
      }
    }
    Handle<String> wrapper;
    if (source->IsOneByteRepresentation()) {
      ExternalOneByteString::Resource* resource =
          new OneByteWrapper(data, length);
      source_wrapper_.reset(resource);
      wrapper = isolate->factory()
                    ->NewExternalStringFromOneByte(resource)
                    .ToHandleChecked();
    } else {
      ExternalTwoByteString::Resource* resource =
          new TwoByteWrapper(data, length);
      source_wrapper_.reset(resource);
      wrapper = isolate->factory()
                    ->NewExternalStringFromTwoByte(resource)
                    .ToHandleChecked();
    }
    wrapper_ = isolate->global_handles()->Create(*wrapper);

    character_stream_.reset(
        ScannerStream::For(wrapper_, shared_->start_position() - offset,
                           shared_->end_position() - offset));
  }
  parse_info_->InitFromIsolate(isolate);
  parse_info_->set_character_stream(character_stream_.get());
  parse_info_->set_hash_seed(isolate->heap()->HashSeed());
  parse_info_->set_is_named_expression(shared_->is_named_expression());
  parse_info_->set_compiler_hints(shared_->compiler_hints());
  parse_info_->set_start_position(shared_->start_position());
  parse_info_->set_end_position(shared_->end_position());
  parse_info_->set_unicode_cache(unicode_cache_.get());
  parse_info_->set_language_mode(shared_->language_mode());
  parse_info_->set_function_literal_id(shared_->function_literal_id());
  if (V8_UNLIKELY(FLAG_runtime_stats)) {
    parse_info_->set_runtime_call_stats(new (parse_info_->zone())
                                            RuntimeCallStats());
  }

  parser_.reset(new Parser(parse_info_.get()));
  MaybeHandle<ScopeInfo> outer_scope_info;
  if (!shared_->outer_scope_info()->IsTheHole(isolate) &&
      ScopeInfo::cast(shared_->outer_scope_info())->length() > 0) {
    outer_scope_info = handle(ScopeInfo::cast(shared_->outer_scope_info()));
  }
  parser_->DeserializeScopeChain(parse_info_.get(), outer_scope_info);

  Handle<String> name(shared_->name());
  parse_info_->set_function_name(
      parse_info_->ast_value_factory()->GetString(name));
  status_ = Status::kReadyToParse;
}

void UnoptimizedCompileJob::Parse() {
  DCHECK(status() == Status::kReadyToParse);
  COMPILER_DISPATCHER_TRACE_SCOPE_WITH_NUM(
      tracer_, kParse,
      parse_info_->end_position() - parse_info_->start_position());
  if (trace_compiler_dispatcher_jobs_) {
    PrintF("UnoptimizedCompileJob[%p]: Parsing\n", static_cast<void*>(this));
  }

  DisallowHeapAllocation no_allocation;
  DisallowHandleAllocation no_handles;
  DisallowHandleDereference no_deref;

  uintptr_t stack_limit = GetCurrentStackPosition() - max_stack_size_ * KB;

  parser_->set_stack_limit(stack_limit);
  parser_->ParseOnBackground(parse_info_.get());

  if (finish_callback_) {
    finish_callback_->ParseFinished(std::move(parse_info_));
    status_ = Status::kDone;
  } else {
    status_ = Status::kParsed;
  }
}

void UnoptimizedCompileJob::FinalizeParsingOnMainThread(Isolate* isolate) {
  DCHECK_EQ(ThreadId::Current().ToInteger(), main_thread_id_);
  DCHECK_EQ(isolate->thread_id().ToInteger(), main_thread_id_);
  DCHECK(status() == Status::kParsed);
  COMPILER_DISPATCHER_TRACE_SCOPE(tracer_, kFinalizeParsing);
  if (trace_compiler_dispatcher_jobs_) {
    PrintF("UnoptimizedCompileJob[%p]: Finalizing parsing\n",
           static_cast<void*>(this));
  }

  if (!source_.is_null()) {
    i::GlobalHandles::Destroy(Handle<Object>::cast(source_).location());
    source_ = Handle<String>::null();
  }
  if (!wrapper_.is_null()) {
    i::GlobalHandles::Destroy(Handle<Object>::cast(wrapper_).location());
    wrapper_ = Handle<String>::null();
  }

  Handle<Script> script(Script::cast(shared_->script()), isolate);
  parse_info_->set_script(script);
  if (parse_info_->literal() == nullptr) {
    parser_->ReportErrors(isolate, script);
    status_ = Status::kFailed;
  } else {
    status_ = Status::kReadyToAnalyze;
  }
  parser_->UpdateStatistics(isolate, script);
  parse_info_->UpdateStatisticsAfterBackgroundParse(isolate);

  if (!shared_->outer_scope_info()->IsTheHole(isolate) &&
      ScopeInfo::cast(shared_->outer_scope_info())->length() > 0) {
    Handle<ScopeInfo> outer_scope_info(
        handle(ScopeInfo::cast(shared_->outer_scope_info())));
    parse_info_->set_outer_scope_info(outer_scope_info);
  }

  parser_->HandleSourceURLComments(isolate, script);

  parse_info_->set_character_stream(nullptr);
  parse_info_->set_unicode_cache(nullptr);
  parser_.reset();
  unicode_cache_.reset();
  character_stream_.reset();
}

void UnoptimizedCompileJob::AnalyzeOnMainThread(Isolate* isolate) {
  DCHECK_EQ(ThreadId::Current().ToInteger(), main_thread_id_);
  DCHECK_EQ(isolate->thread_id().ToInteger(), main_thread_id_);
  DCHECK(status() == Status::kReadyToAnalyze);
  COMPILER_DISPATCHER_TRACE_SCOPE(tracer_, kAnalyze);
  if (trace_compiler_dispatcher_jobs_) {
    PrintF("UnoptimizedCompileJob[%p]: Analyzing\n", static_cast<void*>(this));
  }

  compile_zone_.reset(new Zone(isolate->allocator(), ZONE_NAME));
  compile_info_.reset(new CompilationInfo(
      compile_zone_.get(), parse_info_.get(), isolate,
      Handle<SharedFunctionInfo>::null(), Handle<JSFunction>::null()));

  DeferredHandleScope scope(isolate);
  {
    if (Compiler::Analyze(compile_info_.get())) {
      status_ = Status::kAnalyzed;
    } else {
      status_ = Status::kFailed;
      if (!isolate->has_pending_exception()) isolate->StackOverflow();
    }
  }
  compile_info_->set_deferred_handles(scope.Detach());
}

void UnoptimizedCompileJob::PrepareToCompileOnMainThread(Isolate* isolate) {
  DCHECK_EQ(ThreadId::Current().ToInteger(), main_thread_id_);
  DCHECK_EQ(isolate->thread_id().ToInteger(), main_thread_id_);
  DCHECK(status() == Status::kAnalyzed);
  COMPILER_DISPATCHER_TRACE_SCOPE(tracer_, kPrepareToCompile);

  compile_job_.reset(
      Compiler::PrepareUnoptimizedCompilationJob(compile_info_.get()));
  if (!compile_job_.get()) {
    if (!isolate->has_pending_exception()) isolate->StackOverflow();
    status_ = Status::kFailed;
    return;
  }

  CHECK(compile_job_->can_execute_on_background_thread());
  status_ = Status::kReadyToCompile;
}

void UnoptimizedCompileJob::Compile() {
  DCHECK(status() == Status::kReadyToCompile);
  COMPILER_DISPATCHER_TRACE_SCOPE(tracer_, kCompile);
  if (trace_compiler_dispatcher_jobs_) {
    PrintF("UnoptimizedCompileJob[%p]: Compiling\n", static_cast<void*>(this));
  }

  // Disallowing of handle dereference and heap access dealt with in
  // CompilationJob::ExecuteJob.

  uintptr_t stack_limit = GetCurrentStackPosition() - max_stack_size_ * KB;
  compile_job_->set_stack_limit(stack_limit);

  CompilationJob::Status status = compile_job_->ExecuteJob();
  USE(status);

  // Always transition to kCompiled - errors will be reported by
  // FinalizeCompilingOnMainThread.
  status_ = Status::kCompiled;
}

void UnoptimizedCompileJob::FinalizeCompilingOnMainThread(Isolate* isolate) {
  DCHECK_EQ(ThreadId::Current().ToInteger(), main_thread_id_);
  DCHECK_EQ(isolate->thread_id().ToInteger(), main_thread_id_);
  DCHECK(status() == Status::kCompiled);
  COMPILER_DISPATCHER_TRACE_SCOPE(tracer_, kFinalizeCompiling);
  if (trace_compiler_dispatcher_jobs_) {
    PrintF("UnoptimizedCompileJob[%p]: Finalizing compiling\n",
           static_cast<void*>(this));
  }

  {
    HandleScope scope(isolate);
    compile_info_->set_shared_info(shared_);
    if (compile_job_->state() == CompilationJob::State::kFailed ||
        !Compiler::FinalizeCompilationJob(compile_job_.release())) {
      if (!isolate->has_pending_exception()) isolate->StackOverflow();
      status_ = Status::kFailed;
      return;
    }
  }

  compile_job_.reset();
  compile_info_.reset();
  compile_zone_.reset();
  parse_info_.reset();

  status_ = Status::kDone;
}

void UnoptimizedCompileJob::ResetOnMainThread(Isolate* isolate) {
  if (trace_compiler_dispatcher_jobs_) {
    PrintF("UnoptimizedCompileJob[%p]: Resetting\n", static_cast<void*>(this));
  }

  compile_job_.reset();
  compile_info_.reset();
  compile_zone_.reset();
  parser_.reset();
  unicode_cache_.reset();
  character_stream_.reset();
  parse_info_.reset();
  finish_callback_ = nullptr;

  if (!source_.is_null()) {
    DCHECK_EQ(ThreadId::Current().ToInteger(), main_thread_id_);
    DCHECK_EQ(isolate->thread_id().ToInteger(), main_thread_id_);
    i::GlobalHandles::Destroy(Handle<Object>::cast(source_).location());
    source_ = Handle<String>::null();
  }
  if (!wrapper_.is_null()) {
    DCHECK_EQ(ThreadId::Current().ToInteger(), main_thread_id_);
    DCHECK_EQ(isolate->thread_id().ToInteger(), main_thread_id_);
    i::GlobalHandles::Destroy(Handle<Object>::cast(wrapper_).location());
    wrapper_ = Handle<String>::null();
  }

  status_ = Status::kInitial;
}

double UnoptimizedCompileJob::EstimateRuntimeOfNextStepInMs() const {
  switch (status()) {
    case Status::kInitial:
      return tracer_->EstimatePrepareToParseInMs();

    case Status::kReadyToParse:
      return tracer_->EstimateParseInMs(parse_info_->end_position() -
                                        parse_info_->start_position());

    case Status::kParsed:
      return tracer_->EstimateFinalizeParsingInMs();

    case Status::kReadyToAnalyze:
      return tracer_->EstimateAnalyzeInMs();

    case Status::kAnalyzed:
      return tracer_->EstimatePrepareToCompileInMs();

    case Status::kReadyToCompile:
      return tracer_->EstimateCompileInMs();

    case Status::kCompiled:
      return tracer_->EstimateFinalizeCompilingInMs();

    case Status::kFailed:
    case Status::kDone:
      return 0.0;
  }

  UNREACHABLE();
}

void UnoptimizedCompileJob::ShortPrintOnMainThread() {
  DCHECK_EQ(ThreadId::Current().ToInteger(), main_thread_id_);
  if (!shared_.is_null()) {
    shared_->ShortPrint();
  } else {
    // TODO(wiktorg) more useful info in those cases
    if (parse_info_) {
      PrintF("function at %d", parse_info_->start_position());
    } else {
      PrintF("parsed function");
    }
  }
}

}  // namespace internal
}  // namespace v8
