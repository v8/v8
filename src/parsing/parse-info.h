// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PARSE_INFO_H_
#define V8_PARSING_PARSE_INFO_H_

#include <map>
#include <memory>
#include <vector>

#include "include/v8.h"
#include "src/base/export-template.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/objects/function-kind.h"
#include "src/objects/function-syntax-kind.h"
#include "src/objects/script.h"
#include "src/parsing/pending-compilation-error-handler.h"
#include "src/parsing/preparse-data.h"

namespace v8 {

class Extension;

namespace internal {

class AccountingAllocator;
class AstRawString;
class AstStringConstants;
class AstValueFactory;
class CompilerDispatcher;
class DeclarationScope;
class FunctionLiteral;
class RuntimeCallStats;
class Logger;
class SourceRangeMap;
class Utf16CharacterStream;
class Zone;

// The flags for a parse + unoptimized compile operation.
class V8_EXPORT_PRIVATE UnoptimizedCompileFlags {
 public:
  // Default constructor uses default initialization on the fields, setting them
  // to zero rather than undefined values.
  UnoptimizedCompileFlags() = default;

  // Set-up flags for a toplevel compilation.
  static UnoptimizedCompileFlags ForToplevelCompile(Isolate* isolate,
                                                    bool is_user_javascript,
                                                    LanguageMode language_mode,
                                                    REPLMode repl_mode);

  // Set-up flags for a compiling a particular function (either a lazy compile
  // or a recompile).
  static UnoptimizedCompileFlags ForFunctionCompile(Isolate* isolate,
                                                    SharedFunctionInfo shared);

  // Set-up flags for a full compilation of a given script.
  static UnoptimizedCompileFlags ForScriptCompile(Isolate* isolate,
                                                  Script script);

  // Set-up flags for a parallel toplevel function compilation, based on the
  // flags of an existing toplevel compilation.
  static UnoptimizedCompileFlags ForToplevelFunction(
      const UnoptimizedCompileFlags toplevel_flags,
      const FunctionLiteral* literal);

  // Create flags for a test.
  static UnoptimizedCompileFlags ForTest(Isolate* isolate);

  bool is_toplevel : 1;
  bool is_eager : 1;
  bool is_eval : 1;
  LanguageMode outer_language_mode : 1;
  ParseRestriction parse_restriction : 1;
  bool is_module : 1;
  bool allow_lazy_parsing : 1;
  bool is_lazy_compile : 1;
  bool collect_type_profile : 1;
  bool coverage_enabled : 1;
  bool block_coverage_enabled : 1;
  bool is_asm_wasm_broken : 1;
  bool class_scope_has_private_brand : 1;
  bool requires_instance_members_initializer : 1;
  bool has_static_private_methods_or_accessors : 1;
  bool might_always_opt : 1;
  bool allow_natives_syntax : 1;
  bool allow_lazy_compile : 1;
  bool allow_harmony_dynamic_import : 1;
  bool allow_harmony_import_meta : 1;
  bool allow_harmony_optional_chaining : 1;
  bool allow_harmony_private_methods : 1;
  bool is_oneshot_iife : 1;
  bool collect_source_positions : 1;
  bool allow_harmony_nullish : 1;
  bool allow_harmony_top_level_await : 1;
  bool is_repl_mode : 1;

  int script_id;
  FunctionKind function_kind;
  FunctionSyntaxKind function_syntax_kind;

 private:
  UnoptimizedCompileFlags(Isolate* isolate, int script_id);

  // Set function info flags based on those in either FunctionLiteral or
  // SharedFunctionInfo |function|
  template <typename T>
  void SetFlagsFromFunction(T function);
  void SetFlagsForToplevelCompile(bool is_collecting_type_profile,
                                  bool is_user_javascript,
                                  LanguageMode language_mode,
                                  REPLMode repl_mode);
  void SetFlagsForFunctionFromScript(Script script);
};

// A container for the inputs, configuration options, and outputs of parsing.
class V8_EXPORT_PRIVATE ParseInfo {
 public:
  ParseInfo(Isolate*, const UnoptimizedCompileFlags flags);

  // Creates a new parse info based on parent top-level |outer_parse_info| for
  // function |literal|.
  static std::unique_ptr<ParseInfo> FromParent(
      const ParseInfo* outer_parse_info, const UnoptimizedCompileFlags flags,
      AccountingAllocator* zone_allocator, const FunctionLiteral* literal,
      const AstRawString* function_name);

  ~ParseInfo();

  template <typename LocalIsolate>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  Handle<Script> CreateScript(LocalIsolate* isolate, Handle<String> source,
                              MaybeHandle<FixedArray> maybe_wrapped_arguments,
                              ScriptOriginOptions origin_options,
                              NativesFlag natives = NOT_NATIVES_CODE);

  // Either returns the ast-value-factory associcated with this ParseInfo, or
  // creates and returns a new factory if none exists.
  AstValueFactory* GetOrCreateAstValueFactory();

  Zone* zone() const { return zone_.get(); }

  const UnoptimizedCompileFlags& flags() const { return flags_; }

  // Accessor methods for output flags.
  bool allow_eval_cache() const { return allow_eval_cache_; }
  void set_allow_eval_cache(bool value) { allow_eval_cache_ = value; }
  bool contains_asm_module() const { return contains_asm_module_; }
  void set_contains_asm_module(bool value) { contains_asm_module_ = value; }
  LanguageMode language_mode() const { return language_mode_; }
  void set_language_mode(LanguageMode value) { language_mode_ = value; }

  Utf16CharacterStream* character_stream() const {
    return character_stream_.get();
  }
  void set_character_stream(
      std::unique_ptr<Utf16CharacterStream> character_stream);
  void ResetCharacterStream();

  v8::Extension* extension() const { return extension_; }
  void set_extension(v8::Extension* extension) { extension_ = extension; }

  void set_consumed_preparse_data(std::unique_ptr<ConsumedPreparseData> data) {
    consumed_preparse_data_.swap(data);
  }
  ConsumedPreparseData* consumed_preparse_data() {
    return consumed_preparse_data_.get();
  }

  DeclarationScope* script_scope() const { return script_scope_; }
  void set_script_scope(DeclarationScope* script_scope) {
    script_scope_ = script_scope;
  }

  AstValueFactory* ast_value_factory() const {
    DCHECK(ast_value_factory_.get());
    return ast_value_factory_.get();
  }

  const AstRawString* function_name() const { return function_name_; }
  void set_function_name(const AstRawString* function_name) {
    function_name_ = function_name;
  }

  FunctionLiteral* literal() const { return literal_; }
  void set_literal(FunctionLiteral* literal) { literal_ = literal; }

  DeclarationScope* scope() const;

  uintptr_t stack_limit() const { return stack_limit_; }
  void set_stack_limit(uintptr_t stack_limit) { stack_limit_ = stack_limit; }

  uint64_t hash_seed() const { return hash_seed_; }
  void set_hash_seed(uint64_t hash_seed) { hash_seed_ = hash_seed; }

  int parameters_end_pos() const { return parameters_end_pos_; }
  void set_parameters_end_pos(int parameters_end_pos) {
    parameters_end_pos_ = parameters_end_pos;
  }

  bool is_wrapped_as_function() const {
    return flags().function_syntax_kind == FunctionSyntaxKind::kWrapped;
  }

  int max_function_literal_id() const { return max_function_literal_id_; }
  void set_max_function_literal_id(int max_function_literal_id) {
    max_function_literal_id_ = max_function_literal_id;
  }

  const AstStringConstants* ast_string_constants() const {
    return ast_string_constants_;
  }
  void set_ast_string_constants(
      const AstStringConstants* ast_string_constants) {
    ast_string_constants_ = ast_string_constants;
  }

  RuntimeCallStats* runtime_call_stats() const { return runtime_call_stats_; }
  void set_runtime_call_stats(RuntimeCallStats* runtime_call_stats) {
    runtime_call_stats_ = runtime_call_stats;
  }
  Logger* logger() const { return logger_; }
  void set_logger(Logger* logger) { logger_ = logger; }

  void AllocateSourceRangeMap();
  SourceRangeMap* source_range_map() const { return source_range_map_; }
  void set_source_range_map(SourceRangeMap* source_range_map) {
    source_range_map_ = source_range_map;
  }

  PendingCompilationErrorHandler* pending_error_handler() {
    return &pending_error_handler_;
  }

  class ParallelTasks {
   public:
    explicit ParallelTasks(CompilerDispatcher* compiler_dispatcher)
        : dispatcher_(compiler_dispatcher) {
      DCHECK(dispatcher_);
    }

    void Enqueue(ParseInfo* outer_parse_info, const AstRawString* function_name,
                 FunctionLiteral* literal);

    using EnqueuedJobsIterator =
        std::forward_list<std::pair<FunctionLiteral*, uintptr_t>>::iterator;

    EnqueuedJobsIterator begin() { return enqueued_jobs_.begin(); }
    EnqueuedJobsIterator end() { return enqueued_jobs_.end(); }

    CompilerDispatcher* dispatcher() { return dispatcher_; }

   private:
    CompilerDispatcher* dispatcher_;
    std::forward_list<std::pair<FunctionLiteral*, uintptr_t>> enqueued_jobs_;
  };

  ParallelTasks* parallel_tasks() { return parallel_tasks_.get(); }

  void CheckFlagsForFunctionFromScript(Script script);

 private:
  ParseInfo(AccountingAllocator* zone_allocator, UnoptimizedCompileFlags flags);

  void CheckFlagsForToplevelCompileFromScript(Script script,
                                              bool is_collecting_type_profile);

  //------------- Inputs to parsing and scope analysis -----------------------
  const UnoptimizedCompileFlags flags_;
  std::unique_ptr<Zone> zone_;
  v8::Extension* extension_;
  DeclarationScope* script_scope_;
  uintptr_t stack_limit_;
  uint64_t hash_seed_;
  int parameters_end_pos_;
  int max_function_literal_id_;

  //----------- Inputs+Outputs of parsing and scope analysis -----------------
  std::unique_ptr<Utf16CharacterStream> character_stream_;
  std::unique_ptr<ConsumedPreparseData> consumed_preparse_data_;
  std::unique_ptr<AstValueFactory> ast_value_factory_;
  const class AstStringConstants* ast_string_constants_;
  const AstRawString* function_name_;
  RuntimeCallStats* runtime_call_stats_;
  Logger* logger_;
  SourceRangeMap* source_range_map_;  // Used when block coverage is enabled.
  std::unique_ptr<ParallelTasks> parallel_tasks_;

  //----------- Output of parsing and scope analysis ------------------------
  FunctionLiteral* literal_;
  PendingCompilationErrorHandler pending_error_handler_;
  bool allow_eval_cache_ : 1;
  bool contains_asm_module_ : 1;
  LanguageMode language_mode_ : 1;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_PARSE_INFO_H_
