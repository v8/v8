// Copyright 2007-2008 the V8 project authors. All rights reserved.
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

#include "codegen-inl.h"
#include "debug.h"
#include "runtime.h"
#include "stub-cache.h"

namespace v8 { namespace internal {

DeferredCode::DeferredCode(CodeGenerator* generator)
  : masm_(generator->masm()),
    generator_(generator),
    position_(masm_->last_position()),
    position_is_statement_(masm_->last_position_is_statement()) {
  generator->AddDeferred(this);
#ifdef DEBUG
  comment_ = "";
#endif
}


void CodeGenerator::ProcessDeferred() {
  while (!deferred_.is_empty()) {
    DeferredCode* code = deferred_.RemoveLast();
    MacroAssembler* masm = code->masm();
    // Record position of deferred code stub.
    if (code->position_is_statement()) {
      masm->RecordStatementPosition(code->position());
    } else {
      masm->RecordPosition(code->position());
    }
    // Bind labels and generate the code.
    masm->bind(code->enter());
    Comment cmnt(masm, code->comment());
    code->Generate();
    if (code->exit()->is_bound()) {
      masm->jmp(code->exit());  // platform independent?
    }
  }
}


// Sets the function info on a function.
// The start_position points to the first '(' character after the function name
// in the full script source. When counting characters in the script source the
// the first character is number 0 (not 1).
void CodeGenerator::SetFunctionInfo(Handle<JSFunction> fun,
                                    int length,
                                    int function_token_position,
                                    int start_position,
                                    int end_position,
                                    bool is_expression,
                                    bool is_toplevel,
                                    Handle<Script> script) {
  fun->shared()->set_length(length);
  fun->shared()->set_formal_parameter_count(length);
  fun->shared()->set_script(*script);
  fun->shared()->set_function_token_position(function_token_position);
  fun->shared()->set_start_position(start_position);
  fun->shared()->set_end_position(end_position);
  fun->shared()->set_is_expression(is_expression);
  fun->shared()->set_is_toplevel(is_toplevel);
}


static Handle<Code> ComputeLazyCompile(int argc) {
  CALL_HEAP_FUNCTION(StubCache::ComputeLazyCompile(argc), Code);
}


Handle<JSFunction> CodeGenerator::BuildBoilerplate(FunctionLiteral* node) {
  // Determine if the function can be lazily compiled. This is
  // necessary to allow some of our builtin JS files to be lazily
  // compiled. These builtins cannot be handled lazily by the parser,
  // since we have to know if a function uses the special natives
  // syntax, which is something the parser records.
  bool allow_lazy = node->AllowsLazyCompilation();

  // Generate code
  Handle<Code> code;
  if (FLAG_lazy && allow_lazy) {
    code = ComputeLazyCompile(node->num_parameters());
  } else {
    code = MakeCode(node, script_, false);

    // Check for stack-overflow exception.
    if (code.is_null()) {
      SetStackOverflow();
      return Handle<JSFunction>::null();
    }

    // Function compilation complete.
    LOG(CodeCreateEvent("Function", *code, *node->name()));
  }

  // Create a boilerplate function.
  Handle<JSFunction> function =
      Factory::NewFunctionBoilerplate(node->name(),
                                      node->materialized_literal_count(),
                                      node->contains_array_literal(),
                                      code);
  CodeGenerator::SetFunctionInfo(function, node->num_parameters(),
                                 node->function_token_position(),
                                 node->start_position(), node->end_position(),
                                 node->is_expression(), false, script_);

  // Notify debugger that a new function has been added.
  Debugger::OnNewFunction(function);

  // Set the expected number of properties for instances and return
  // the resulting function.
  SetExpectedNofPropertiesFromEstimate(function,
                                       node->expected_property_count());
  return function;
}


Handle<Code> CodeGenerator::ComputeCallInitialize(int argc) {
  CALL_HEAP_FUNCTION(StubCache::ComputeCallInitialize(argc), Code);
}


void CodeGenerator::ProcessDeclarations(ZoneList<Declaration*>* declarations) {
  int length = declarations->length();
  int globals = 0;
  for (int i = 0; i < length; i++) {
    Declaration* node = declarations->at(i);
    Variable* var = node->proxy()->var();
    Slot* slot = var->slot();

    // If it was not possible to allocate the variable at compile
    // time, we need to "declare" it at runtime to make sure it
    // actually exists in the local context.
    if ((slot != NULL && slot->type() == Slot::LOOKUP) || !var->is_global()) {
      VisitDeclaration(node);
    } else {
      // Count global variables and functions for later processing
      globals++;
    }
  }

  // Return in case of no declared global functions or variables.
  if (globals == 0) return;

  // Compute array of global variable and function declarations.
  Handle<FixedArray> array = Factory::NewFixedArray(2 * globals, TENURED);
  for (int j = 0, i = 0; i < length; i++) {
    Declaration* node = declarations->at(i);
    Variable* var = node->proxy()->var();
    Slot* slot = var->slot();

    if ((slot != NULL && slot->type() == Slot::LOOKUP) || !var->is_global()) {
      // Skip - already processed.
    } else {
      array->set(j++, *(var->name()));
      if (node->fun() == NULL) {
        if (var->mode() == Variable::CONST) {
          // In case this is const property use the hole.
          array->set_the_hole(j++);
        } else {
          array->set_undefined(j++);
        }
      } else {
        Handle<JSFunction> function = BuildBoilerplate(node->fun());
        // Check for stack-overflow exception.
        if (HasStackOverflow()) return;
        array->set(j++, *function);
      }
    }
  }

  // Invoke the platform-dependent code generator to do the actual
  // declaration the global variables and functions.
  DeclareGlobals(array);
}


struct InlineRuntimeLUT {
  void (CodeGenerator::*method)(ZoneList<Expression*>*);
  const char* name;
};


bool CodeGenerator::CheckForInlineRuntimeCall(CallRuntime* node) {
  ZoneList<Expression*>* args = node->arguments();
  // Special cases: These 'runtime calls' manipulate the current
  // frame and are only used 1 or two places, so we generate them
  // inline instead of generating calls to them.  They are used
  // for implementing Function.prototype.call() and
  // Function.prototype.apply().
  static const InlineRuntimeLUT kInlineRuntimeLUT[] = {
    {&v8::internal::CodeGenerator::GenerateIsSmi,
     "_IsSmi"},
    {&v8::internal::CodeGenerator::GenerateIsNonNegativeSmi,
     "_IsNonNegativeSmi"},
    {&v8::internal::CodeGenerator::GenerateIsArray,
     "_IsArray"},
    {&v8::internal::CodeGenerator::GenerateArgumentsLength,
     "_ArgumentsLength"},
    {&v8::internal::CodeGenerator::GenerateArgumentsAccess,
     "_Arguments"},
    {&v8::internal::CodeGenerator::GenerateValueOf,
     "_ValueOf"},
    {&v8::internal::CodeGenerator::GenerateSetValueOf,
     "_SetValueOf"},
    {&v8::internal::CodeGenerator::GenerateFastCharCodeAt,
     "_FastCharCodeAt"},
    {&v8::internal::CodeGenerator::GenerateObjectEquals,
     "_ObjectEquals"}
  };
  if (node->name()->length() > 0 && node->name()->Get(0) == '_') {
    for (unsigned i = 0;
         i < sizeof(kInlineRuntimeLUT) / sizeof(InlineRuntimeLUT);
         i++) {
      const InlineRuntimeLUT* entry = kInlineRuntimeLUT + i;
      if (node->name()->IsEqualTo(CStrVector(entry->name))) {
        ((*this).*(entry->method))(args);
        return true;
      }
    }
  }
  return false;
}


const char* RuntimeStub::GetName() {
  return Runtime::FunctionForId(id_)->stub_name;
}


void RuntimeStub::Generate(MacroAssembler* masm) {
  masm->TailCallRuntime(ExternalReference(id_), num_arguments_);
}


} }  // namespace v8::internal
