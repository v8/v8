// Copyright 2006-2008 Google Inc. All Rights Reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "bootstrapper.h"
#include "codegen-inl.h"
#include "compiler.h"
#include "debug.h"
#include "scopes.h"
#include "rewriter.h"
#include "usage-analyzer.h"

namespace v8 { namespace internal {

DEFINE_bool(strict, false, "strict error checking");
DEFINE_int(min_preparse_length, 1024,
           "Minimum length for automatic enable preparsing");
DECLARE_bool(debug_info);

#ifdef DEBUG
DEFINE_bool(print_builtin_scopes, false, "print scopes for builtins");
DEFINE_bool(print_scopes, false, "print scopes");
#endif


// Helper class to keep track of compilation nesting and to do proper
// cleanups of generated ASTs.
class CompilationTracker BASE_EMBEDDED {
 public:
  CompilationTracker() {
    ++nesting_;
  }

  ~CompilationTracker() {
    // If we're leaving the top-level compilation, we must make sure
    // to get rid of all generated ASTs.
    if (--nesting_ == 0) Zone::DeleteAll();
  }

 private:
  static int nesting_;
};


int CompilationTracker::nesting_ = 0;


static Handle<Code> MakeCode(FunctionLiteral* literal,
                             Handle<Script> script,
                             bool is_eval) {
  ASSERT(literal != NULL);

  // Rewrite the AST by introducing .result assignments where needed.
  if (!Rewriter::Process(literal) || !AnalyzeVariableUsage(literal)) {
    Top::StackOverflow();
    return Handle<Code>::null();
  }

  // Compute top scope and allocate variables. For lazy compilation
  // the top scope only contains the single lazily compiled function,
  // so this doesn't re-allocate variables repeatedly.
  Scope* top = literal->scope();
  while (top->outer_scope() != NULL) top = top->outer_scope();
  top->AllocateVariables();

#ifdef DEBUG
  if (Bootstrapper::IsActive() ?
      FLAG_print_builtin_scopes :
      FLAG_print_scopes) {
    literal->scope()->Print();
  }
#endif

  // Generate code and return it.
  Handle<Code> result = CodeGenerator::MakeCode(literal, script, is_eval);
  return result;
}


static Handle<JSFunction> MakeFunction(bool is_global,
                                       bool is_eval,
                                       Handle<Script> script,
                                       v8::Extension* extension,
                                       ScriptDataImpl* pre_data) {
  CompilationTracker tracker;

  // Make sure we have an initial stack limit.
  StackGuard guard;
  StackGuard::DisableInterrupts();

  // Notify debugger
  Debugger::OnBeforeCompile(script);

  // Only allow non-global compiles for eval.
  ASSERT(is_eval || is_global);

  // Build AST.
  FunctionLiteral* lit = MakeAST(is_global, script, extension, pre_data);

  // Measure how long it takes to do the compilation; only take the
  // rest of the function into account to avoid overlap with the
  // parsing statistics.
  StatsRate* rate = is_eval
      ? &Counters::compile_eval
      : &Counters::compile;
  StatsRateScope timer(rate);

  // Compile the code.
  Handle<Code> code = Handle<Code>::null();
  if (lit != NULL) code = MakeCode(lit, script, is_eval);

  // Check for stack overflow.
  if (code.is_null()) {
    ASSERT(Top::has_pending_exception());
    StackGuard::EnableInterrupts();
    return Handle<JSFunction>::null();
  }

  if (script->name()->IsString()) {
    SmartPointer<char> data =
        String::cast(script->name())->ToCString(DISALLOW_NULLS);
    LOG(CodeCreateEvent(is_eval ? "Eval" : "Script", *code, *data));
  } else {
    LOG(CodeCreateEvent(is_eval ? "Eval" : "Script", *code, ""));
  }

  // Allocate function.
  Handle<JSFunction> fun =
      Factory::NewFunctionBoilerplate(lit->name(),
                                      lit->materialized_literal_count(),
                                      code);

  CodeGenerator::SetFunctionInfo(fun, lit->scope()->num_parameters(),
                                 kNoPosition,
                                 lit->start_position(), lit->end_position(),
                                 lit->is_expression(), true, script);

  // Hint to the runtime system used when allocating space for initial
  // property space by setting the expected number of properties for
  // the instances of the function.
  SetExpectedNofPropertiesFromEstimate(fun, lit->expected_property_count());

  StackGuard::EnableInterrupts();

  // Notify debugger
  Debugger::OnAfterCompile(script, fun);

  return fun;
}


static StaticResource<SafeStringInputBuffer> safe_string_input_buffer;


Handle<JSFunction> Compiler::Compile(Handle<String> source,
                                     Handle<String> script_name,
                                     int line_offset, int column_offset,
                                     v8::Extension* extension,
                                     ScriptDataImpl* input_pre_data) {
  Counters::total_load_size.Increment(source->length());
  Counters::total_compile_size.Increment(source->length());

  // The VM is in the COMPILER state until exiting this function.
  VMState state(COMPILER);

  ScriptDataImpl* pre_data = input_pre_data;
  if (pre_data == NULL && source->length() >= FLAG_min_preparse_length) {
    Access<SafeStringInputBuffer> buf(&safe_string_input_buffer);
    buf->Reset(source.location());
    pre_data = PreParse(buf.value(), extension);
  }

  // Create a script object describing the script to be compiled.
  Handle<Script> script = Factory::NewScript(source);
  if (!script_name.is_null()) {
    script->set_name(*script_name);
    script->set_line_offset(Smi::FromInt(line_offset));
    script->set_column_offset(Smi::FromInt(column_offset));
  }

  Handle<JSFunction> result =
      MakeFunction(true, false, script, extension, pre_data);

  if (input_pre_data == NULL && pre_data != NULL)
    delete pre_data;

  return result;
}


Handle<JSFunction> Compiler::CompileEval(bool is_global,
                                         Handle<String> source) {
  Counters::total_eval_size.Increment(source->length());
  Counters::total_compile_size.Increment(source->length());

  // The VM is in the COMPILER state until exiting this function.
  VMState state(COMPILER);

  // Create a script object describing the script to be compiled.
  Handle<Script> script = Factory::NewScript(source);
  return MakeFunction(is_global, true, script, NULL, NULL);
}


bool Compiler::CompileLazy(Handle<SharedFunctionInfo> shared) {
  CompilationTracker tracker;

  // The VM is in the COMPILER state until exiting this function.
  VMState state(COMPILER);

  // Make sure we have an initial stack limit.
  StackGuard guard;
  StackGuard::DisableInterrupts();

  // Compute name, source code and script data.
  Handle<String> name(String::cast(shared->name()));
  Handle<Script> script(Script::cast(shared->script()));

  int start_position = shared->start_position();
  int end_position = shared->end_position();
  bool is_expression = shared->is_expression();
  Counters::total_compile_size.Increment(end_position - start_position);

  // Generate the AST for the lazily compiled function. The AST may be
  // NULL in case of parser stack overflow.
  FunctionLiteral* lit = MakeLazyAST(script, name,
                                     start_position,
                                     end_position,
                                     is_expression);

  // Measure how long it takes to do the lazy compilation; only take
  // the rest of the function into account to avoid overlap with the
  // lazy parsing statistics.
  StatsRateScope timer(&Counters::compile_lazy);

  // Compile the code (if we have a syntax tree).
  Handle<Code> code = Handle<Code>::null();
  if (lit != NULL) code = MakeCode(lit, script, false);

  // Check for stack-overflow during compilation.
  if (code.is_null()) {
    ASSERT(Top::has_pending_exception());
    StackGuard::EnableInterrupts();
    return false;
  }

  // Generate the code, update the function info, and return the code.
  LOG(CodeCreateEvent("LazyCompile", *code, *lit->name()));

  // Update the shared function info with the compiled code.
  shared->set_code(*code);

  // Set the expected number of properties for instances.
  SetExpectedNofPropertiesFromEstimate(shared, lit->expected_property_count());

  // Check the function has compiled code.
  ASSERT(shared->is_compiled());
  StackGuard::EnableInterrupts();
  return true;
}


} }  // namespace v8::internal
