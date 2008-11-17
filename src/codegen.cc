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

#include "bootstrapper.h"
#include "codegen-inl.h"
#include "debug.h"
#include "prettyprinter.h"
#include "scopeinfo.h"
#include "runtime.h"
#include "stub-cache.h"

namespace v8 { namespace internal {

DeferredCode::DeferredCode(CodeGenerator* generator)
  : masm_(generator->masm()),
    generator_(generator),
    statement_position_(masm_->last_statement_position()),
    position_(masm_->last_position()) {
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
    if (code->statement_position() != RelocInfo::kNoPosition) {
      masm->RecordStatementPosition(code->statement_position());
    }
    if (code->position() != RelocInfo::kNoPosition) {
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


// Generate the code. Takes a function literal, generates code for it, assemble
// all the pieces into a Code object. This function is only to be called by
// the compiler.cc code.
Handle<Code> CodeGenerator::MakeCode(FunctionLiteral* flit,
                                     Handle<Script> script,
                                     bool is_eval) {
#ifdef ENABLE_DISASSEMBLER
  bool print_code = FLAG_print_code && !Bootstrapper::IsActive();
#endif

#ifdef DEBUG
  bool print_source = false;
  bool print_ast = false;
  const char* ftype;

  if (Bootstrapper::IsActive()) {
    print_source = FLAG_print_builtin_source;
    print_ast = FLAG_print_builtin_ast;
    print_code = FLAG_print_builtin_code;
    ftype = "builtin";
  } else {
    print_source = FLAG_print_source;
    print_ast = FLAG_print_ast;
    ftype = "user-defined";
  }

  if (FLAG_trace_codegen || print_source || print_ast) {
    PrintF("*** Generate code for %s function: ", ftype);
    flit->name()->ShortPrint();
    PrintF(" ***\n");
  }

  if (print_source) {
    PrintF("--- Source from AST ---\n%s\n", PrettyPrinter().PrintProgram(flit));
  }

  if (print_ast) {
    PrintF("--- AST ---\n%s\n", AstPrinter().PrintProgram(flit));
  }
#endif  // DEBUG

  // Generate code.
  const int initial_buffer_size = 4 * KB;
  CodeGenerator cgen(initial_buffer_size, script, is_eval);
  cgen.GenCode(flit);
  if (cgen.HasStackOverflow()) {
    ASSERT(!Top::has_pending_exception());
    return Handle<Code>::null();
  }

  // Process any deferred code.
  cgen.ProcessDeferred();

  // Allocate and install the code.
  CodeDesc desc;
  cgen.masm()->GetCode(&desc);
  ScopeInfo<> sinfo(flit->scope());
  Code::Flags flags = Code::ComputeFlags(Code::FUNCTION);
  Handle<Code> code = Factory::NewCode(desc, &sinfo, flags);

  // Add unresolved entries in the code to the fixup list.
  Bootstrapper::AddFixup(*code, cgen.masm());

#ifdef ENABLE_DISASSEMBLER
  if (print_code) {
    // Print the source code if available.
    if (!script->IsUndefined() && !script->source()->IsUndefined()) {
      PrintF("--- Raw source ---\n");
      StringInputBuffer stream(String::cast(script->source()));
      stream.Seek(flit->start_position());
      // flit->end_position() points to the last character in the stream. We
      // need to compensate by adding one to calculate the length.
      int source_len = flit->end_position() - flit->start_position() + 1;
      for (int i = 0; i < source_len; i++) {
        if (stream.has_more()) PrintF("%c", stream.GetNext());
      }
      PrintF("\n\n");
    }
    PrintF("--- Code ---\n");
    code->Disassemble();
  }
#endif  // ENABLE_DISASSEMBLER

  if (!code.is_null()) {
    Counters::total_compiled_code_size.Increment(code->instruction_size());
  }

  return code;
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


Handle<Code> CodeGenerator::ComputeCallInitializeInLoop(int argc) {
  // Force the creation of the corresponding stub outside loops,
  // because it will be used when clearing the ICs later - when we
  // don't know if we're inside a loop or not.
  ComputeCallInitialize(argc);
  CALL_HEAP_FUNCTION(StubCache::ComputeCallInitializeInLoop(argc), Code);
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
  Handle<String> name = node->name();
  StringShape shape(*name);
  if (name->length(shape) > 0 && name->Get(shape, 0) == '_') {
    for (unsigned i = 0;
         i < sizeof(kInlineRuntimeLUT) / sizeof(InlineRuntimeLUT);
         i++) {
      const InlineRuntimeLUT* entry = kInlineRuntimeLUT + i;
      if (name->IsEqualTo(CStrVector(entry->name))) {
        ((*this).*(entry->method))(args);
        return true;
      }
    }
  }
  return false;
}


void CodeGenerator::GenerateFastCaseSwitchStatement(SwitchStatement* node,
                                                    int min_index,
                                                    int range,
                                                    int default_index) {
  ZoneList<CaseClause*>* cases = node->cases();
  int length = cases->length();

  // Label pointer per number in range
  SmartPointer<Label*> case_targets(NewArray<Label*>(range));

  // Label per switch case
  SmartPointer<Label> case_labels(NewArray<Label>(length));

  Label* fail_label = default_index >= 0 ? &(case_labels[default_index])
                                         : node->break_target();

  // Populate array of label pointers for each number in the range.
  // Initally put the failure label everywhere.
  for (int i = 0; i < range; i++) {
    case_targets[i] = fail_label;
  }

  // Overwrite with label of a case for the number value of that case.
  // (In reverse order, so that if the same label occurs twice, the
  // first one wins).
  for (int i = length-1; i >= 0 ; i--) {
    CaseClause* clause = cases->at(i);
    if (!clause->is_default()) {
      Object* label_value = *(clause->label()->AsLiteral()->handle());
      int case_value = Smi::cast(label_value)->value();
      case_targets[case_value - min_index] = &(case_labels[i]);
    }
  }

  GenerateFastCaseSwitchJumpTable(node,
                                  min_index,
                                  range,
                                  fail_label,
                                  Vector<Label*>(*case_targets, range),
                                  Vector<Label>(*case_labels, length));
}


void CodeGenerator::GenerateFastCaseSwitchCases(
    SwitchStatement* node,
    Vector<Label> case_labels) {
  ZoneList<CaseClause*>* cases = node->cases();
  int length = cases->length();

  for (int i = 0; i < length; i++) {
    Comment cmnt(masm(), "[ Case clause");
    masm()->bind(&(case_labels[i]));
    VisitStatements(cases->at(i)->statements());
  }

  masm()->bind(node->break_target());
}


bool CodeGenerator::TryGenerateFastCaseSwitchStatement(SwitchStatement* node) {
  ZoneList<CaseClause*>* cases = node->cases();
  int length = cases->length();

  if (length < FastCaseSwitchMinCaseCount()) {
    return false;
  }

  // Test whether fast-case should be used.
  int default_index = -1;
  int min_index = Smi::kMaxValue;
  int max_index = Smi::kMinValue;
  for (int i = 0; i < length; i++) {
    CaseClause* clause = cases->at(i);
    if (clause->is_default()) {
      if (default_index >= 0) {
        return false;  // More than one default label:
                       // Defer to normal case for error.
    }
      default_index = i;
    } else {
      Expression* label = clause->label();
      Literal* literal = label->AsLiteral();
      if (literal == NULL) {
        return false;  // fail fast case
      }
      Object* value = *(literal->handle());
      if (!value->IsSmi()) {
        return false;
      }
      int smi = Smi::cast(value)->value();
      if (smi < min_index) { min_index = smi; }
      if (smi > max_index) { max_index = smi; }
    }
  }

  // All labels are known to be Smis.
  int range = max_index - min_index + 1;  // |min..max| inclusive
  if (range / FastCaseSwitchMaxOverheadFactor() > length) {
    return false;  // range of labels is too sparse
  }

  // Optimization accepted, generate code.
  GenerateFastCaseSwitchStatement(node, min_index, range, default_index);
  return true;
}


const char* RuntimeStub::GetName() {
  return Runtime::FunctionForId(id_)->stub_name;
}


void RuntimeStub::Generate(MacroAssembler* masm) {
  masm->TailCallRuntime(ExternalReference(id_), num_arguments_);
}


void ArgumentsAccessStub::Generate(MacroAssembler* masm) {
  switch (type_) {
    case READ_LENGTH: GenerateReadLength(masm); break;
    case READ_ELEMENT: GenerateReadElement(masm); break;
    case NEW_OBJECT: GenerateNewObject(masm); break;
  }
}


} }  // namespace v8::internal
