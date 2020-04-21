// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/parse-info.h"

#include "src/ast/ast-source-ranges.h"
#include "src/ast/ast-value-factory.h"
#include "src/ast/ast.h"
#include "src/common/globals.h"
#include "src/compiler-dispatcher/compiler-dispatcher.h"
#include "src/heap/heap-inl.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/numbers/hash-seed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/scope-info.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

UnoptimizedCompileFlags::UnoptimizedCompileFlags(Isolate* isolate,
                                                 int script_id) {
  // Zero initialize the fields. Ideally we'd use default initializers on the
  // fields themselves, but default bitfield values are not supported until
  // C++20.
  memset(this, 0, sizeof(*this));

  collect_type_profile = isolate->is_collecting_type_profile();
  coverage_enabled = !isolate->is_best_effort_code_coverage();
  block_coverage_enabled = isolate->is_block_code_coverage();
  might_always_opt = FLAG_always_opt || FLAG_prepare_always_opt;
  allow_natives_syntax = FLAG_allow_natives_syntax;
  allow_lazy_compile = FLAG_lazy;
  allow_harmony_dynamic_import = FLAG_harmony_dynamic_import;
  allow_harmony_import_meta = FLAG_harmony_import_meta;
  allow_harmony_private_methods = FLAG_harmony_private_methods;
  collect_source_positions = !FLAG_enable_lazy_source_positions ||
                             isolate->NeedsDetailedOptimizedCodeLineInfo();
  allow_harmony_top_level_await = FLAG_harmony_top_level_await;
  this->script_id = script_id;
  function_kind = FunctionKind::kNormalFunction;
  function_syntax_kind = FunctionSyntaxKind::kDeclaration;
}

// static
UnoptimizedCompileFlags UnoptimizedCompileFlags::ForFunctionCompile(
    Isolate* isolate, SharedFunctionInfo shared) {
  Script script = Script::cast(shared.script());

  UnoptimizedCompileFlags flags(isolate, script.id());

  flags.SetFlagsFromFunction(&shared);
  flags.SetFlagsForFunctionFromScript(script);

  flags.allow_lazy_parsing = true;
  flags.is_asm_wasm_broken = shared.is_asm_wasm_broken();
  flags.is_repl_mode = shared.is_repl_mode();

  // CollectTypeProfile uses its own feedback slots. If we have existing
  // FeedbackMetadata, we can only collect type profile if the feedback vector
  // has the appropriate slots.
  flags.collect_type_profile =
      isolate->is_collecting_type_profile() &&
      (shared.HasFeedbackMetadata()
           ? shared.feedback_metadata().HasTypeProfileSlot()
           : script.IsUserJavaScript());

  // Do not support re-parsing top-level function of a wrapped script.
  DCHECK_IMPLIES(flags.is_toplevel, !script.is_wrapped());

  return flags;
}

// static
UnoptimizedCompileFlags UnoptimizedCompileFlags::ForScriptCompile(
    Isolate* isolate, Script script) {
  UnoptimizedCompileFlags flags(isolate, script.id());

  flags.SetFlagsForFunctionFromScript(script);
  flags.SetFlagsForToplevelCompile(
      isolate->is_collecting_type_profile(), script.IsUserJavaScript(),
      flags.outer_language_mode, construct_repl_mode(script.is_repl_mode()));
  if (script.is_wrapped()) {
    flags.function_syntax_kind = FunctionSyntaxKind::kWrapped;
  }

  return flags;
}

// static
UnoptimizedCompileFlags UnoptimizedCompileFlags::ForToplevelCompile(
    Isolate* isolate, bool is_user_javascript, LanguageMode language_mode,
    REPLMode repl_mode) {
  UnoptimizedCompileFlags flags(isolate, isolate->GetNextScriptId());
  flags.SetFlagsForToplevelCompile(isolate->is_collecting_type_profile(),
                                   is_user_javascript, language_mode,
                                   repl_mode);

  LOG(isolate,
      ScriptEvent(Logger::ScriptEventType::kReserveId, flags.script_id));
  return flags;
}

// static
UnoptimizedCompileFlags UnoptimizedCompileFlags::ForToplevelFunction(
    const UnoptimizedCompileFlags toplevel_flags,
    const FunctionLiteral* literal) {
  DCHECK(toplevel_flags.is_toplevel);
  DCHECK(!literal->is_toplevel());

  // Replicate the toplevel flags, then setup the function-specific flags.
  UnoptimizedCompileFlags flags = toplevel_flags;
  flags.SetFlagsFromFunction(literal);

  return flags;
}

// static
UnoptimizedCompileFlags UnoptimizedCompileFlags::ForTest(Isolate* isolate) {
  return UnoptimizedCompileFlags(isolate, -1);
}

template <typename T>
void UnoptimizedCompileFlags::SetFlagsFromFunction(T function) {
  outer_language_mode = function->language_mode();
  function_kind = function->kind();
  function_syntax_kind = function->syntax_kind();
  requires_instance_members_initializer =
      function->requires_instance_members_initializer();
  class_scope_has_private_brand = function->class_scope_has_private_brand();
  has_static_private_methods_or_accessors =
      function->has_static_private_methods_or_accessors();
  is_toplevel = function->is_toplevel();
  is_oneshot_iife = function->is_oneshot_iife();
}

void UnoptimizedCompileFlags::SetFlagsForToplevelCompile(
    bool is_collecting_type_profile, bool is_user_javascript,
    LanguageMode language_mode, REPLMode repl_mode) {
  allow_lazy_parsing = true;
  is_toplevel = true;
  collect_type_profile = is_user_javascript && is_collecting_type_profile;
  outer_language_mode =
      stricter_language_mode(outer_language_mode, language_mode);
  is_repl_mode = (repl_mode == REPLMode::kYes);

  block_coverage_enabled = block_coverage_enabled && is_user_javascript;
}

void UnoptimizedCompileFlags::SetFlagsForFunctionFromScript(Script script) {
  DCHECK_EQ(script_id, script.id());

  is_eval = script.compilation_type() == Script::COMPILATION_TYPE_EVAL;
  is_module = script.origin_options().IsModule();
  DCHECK(!(is_eval && is_module));

  block_coverage_enabled = block_coverage_enabled && script.IsUserJavaScript();
}

ParseInfo::ParseInfo(AccountingAllocator* zone_allocator,
                     UnoptimizedCompileFlags flags)
    : flags_(flags),
      zone_(std::make_unique<Zone>(zone_allocator, ZONE_NAME)),
      extension_(nullptr),
      script_scope_(nullptr),
      stack_limit_(0),
      hash_seed_(0),
      parameters_end_pos_(kNoSourcePosition),
      max_function_literal_id_(kFunctionLiteralIdInvalid),
      character_stream_(nullptr),
      ast_value_factory_(nullptr),
      ast_string_constants_(nullptr),
      function_name_(nullptr),
      runtime_call_stats_(nullptr),
      source_range_map_(nullptr),
      literal_(nullptr),
      allow_eval_cache_(false),
      contains_asm_module_(false),
      language_mode_(flags.outer_language_mode) {
  if (flags.block_coverage_enabled) {
    AllocateSourceRangeMap();
  }
}

ParseInfo::ParseInfo(Isolate* isolate, UnoptimizedCompileFlags flags)
    : ParseInfo(isolate->allocator(), flags) {
  set_hash_seed(HashSeed(isolate));
  set_stack_limit(isolate->stack_guard()->real_climit());
  set_runtime_call_stats(isolate->counters()->runtime_call_stats());
  set_logger(isolate->logger());
  set_ast_string_constants(isolate->ast_string_constants());
  if (isolate->compiler_dispatcher()->IsEnabled()) {
    parallel_tasks_.reset(new ParallelTasks(isolate->compiler_dispatcher()));
  }
}

// static
std::unique_ptr<ParseInfo> ParseInfo::FromParent(
    const ParseInfo* outer_parse_info, const UnoptimizedCompileFlags flags,
    AccountingAllocator* zone_allocator, const FunctionLiteral* literal,
    const AstRawString* function_name) {
  std::unique_ptr<ParseInfo> result(new ParseInfo(zone_allocator, flags));

  // Replicate shared state of the outer_parse_info.
  result->set_logger(outer_parse_info->logger());
  result->set_ast_string_constants(outer_parse_info->ast_string_constants());
  result->set_hash_seed(outer_parse_info->hash_seed());

  DCHECK_EQ(outer_parse_info->parameters_end_pos(), kNoSourcePosition);
  DCHECK_NULL(outer_parse_info->extension());

  // Clone the function_name AstRawString into the ParseInfo's own
  // AstValueFactory.
  const AstRawString* cloned_function_name =
      result->GetOrCreateAstValueFactory()->CloneFromOtherFactory(
          function_name);

  // Setup function specific details.
  DCHECK(!literal->is_toplevel());
  result->set_function_name(cloned_function_name);

  return result;
}

ParseInfo::~ParseInfo() = default;

DeclarationScope* ParseInfo::scope() const { return literal()->scope(); }

template <typename LocalIsolate>
Handle<Script> ParseInfo::CreateScript(
    LocalIsolate* isolate, Handle<String> source,
    MaybeHandle<FixedArray> maybe_wrapped_arguments,
    ScriptOriginOptions origin_options, NativesFlag natives) {
  // Create a script object describing the script to be compiled.
  DCHECK_GE(flags().script_id, 0);
  Handle<Script> script =
      isolate->factory()->NewScriptWithId(source, flags().script_id);
  if (isolate->NeedsSourcePositionsForProfiling()) {
    Script::InitLineEnds(isolate, script);
  }
  switch (natives) {
    case EXTENSION_CODE:
      script->set_type(Script::TYPE_EXTENSION);
      break;
    case INSPECTOR_CODE:
      script->set_type(Script::TYPE_INSPECTOR);
      break;
    case NOT_NATIVES_CODE:
      break;
  }
  script->set_origin_options(origin_options);
  script->set_is_repl_mode(flags().is_repl_mode);

  DCHECK_EQ(is_wrapped_as_function(), !maybe_wrapped_arguments.is_null());
  if (is_wrapped_as_function()) {
    script->set_wrapped_arguments(*maybe_wrapped_arguments.ToHandleChecked());
  } else if (flags().is_eval) {
    script->set_compilation_type(Script::COMPILATION_TYPE_EVAL);
  }

  CheckFlagsForToplevelCompileFromScript(*script,
                                         isolate->is_collecting_type_profile());
  return script;
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<Script> ParseInfo::CreateScript(
        Isolate* isolate, Handle<String> source,
        MaybeHandle<FixedArray> maybe_wrapped_arguments,
        ScriptOriginOptions origin_options, NativesFlag natives);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<Script> ParseInfo::CreateScript(
        OffThreadIsolate* isolate, Handle<String> source,
        MaybeHandle<FixedArray> maybe_wrapped_arguments,
        ScriptOriginOptions origin_options, NativesFlag natives);

AstValueFactory* ParseInfo::GetOrCreateAstValueFactory() {
  if (!ast_value_factory_.get()) {
    ast_value_factory_.reset(
        new AstValueFactory(zone(), ast_string_constants(), hash_seed()));
  }
  return ast_value_factory();
}

void ParseInfo::AllocateSourceRangeMap() {
  DCHECK(flags().block_coverage_enabled);
  DCHECK_NULL(source_range_map());
  set_source_range_map(new (zone()) SourceRangeMap(zone()));
}

void ParseInfo::ResetCharacterStream() { character_stream_.reset(); }

void ParseInfo::set_character_stream(
    std::unique_ptr<Utf16CharacterStream> character_stream) {
  DCHECK_NULL(character_stream_);
  character_stream_.swap(character_stream);
}

void ParseInfo::CheckFlagsForToplevelCompileFromScript(
    Script script, bool is_collecting_type_profile) {
  CheckFlagsForFunctionFromScript(script);
  DCHECK(flags().allow_lazy_parsing);
  DCHECK(flags().is_toplevel);
  DCHECK_EQ(flags().collect_type_profile,
            is_collecting_type_profile && script.IsUserJavaScript());
  DCHECK_EQ(flags().is_repl_mode, script.is_repl_mode());

  if (script.is_wrapped()) {
    DCHECK_EQ(flags().function_syntax_kind, FunctionSyntaxKind::kWrapped);
  }
}

void ParseInfo::CheckFlagsForFunctionFromScript(Script script) {
  DCHECK_EQ(flags().script_id, script.id());
  // We set "is_eval" for wrapped scripts to get an outer declaration scope.
  // This is a bit hacky, but ok since we can't be both eval and wrapped.
  DCHECK_EQ(flags().is_eval && !script.is_wrapped(),
            script.compilation_type() == Script::COMPILATION_TYPE_EVAL);
  DCHECK_EQ(flags().is_module, script.origin_options().IsModule());
  DCHECK_IMPLIES(flags().block_coverage_enabled && script.IsUserJavaScript(),
                 source_range_map() != nullptr);
}

void ParseInfo::ParallelTasks::Enqueue(ParseInfo* outer_parse_info,
                                       const AstRawString* function_name,
                                       FunctionLiteral* literal) {
  base::Optional<CompilerDispatcher::JobId> job_id =
      dispatcher_->Enqueue(outer_parse_info, function_name, literal);
  if (job_id) {
    enqueued_jobs_.emplace_front(std::make_pair(literal, *job_id));
  }
}

}  // namespace internal
}  // namespace v8
