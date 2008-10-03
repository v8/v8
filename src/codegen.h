// Copyright 2006-2008 the V8 project authors. All rights reserved.
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

#ifndef V8_CODEGEN_H_

#include "ast.h"
#include "code-stubs.h"
#include "runtime.h"

#define V8_CODEGEN_H_

namespace v8 { namespace internal {


// Use lazy compilation; defaults to true.
// NOTE: Do not remove non-lazy compilation until we can properly
//       install extensions with lazy compilation enabled. At the
//       moment, this doesn't work for the extensions in Google3,
//       and we can only run the tests with --nolazy.


// Forward declaration.
class CodeGenerator;


// Deferred code objects are small pieces of code that are compiled
// out of line. They are used to defer the compilation of uncommon
// paths thereby avoiding expensive jumps around uncommon code parts.
class DeferredCode: public ZoneObject {
 public:
  explicit DeferredCode(CodeGenerator* generator);
  virtual ~DeferredCode() { }

  virtual void Generate() = 0;

  MacroAssembler* masm() const { return masm_; }
  CodeGenerator* generator() const { return generator_; }

  Label* enter() { return &enter_; }
  Label* exit() { return &exit_; }

  int statement_position() const { return statement_position_; }
  int position() const { return position_; }

#ifdef DEBUG
  void set_comment(const char* comment) { comment_ = comment; }
  const char* comment() const { return comment_; }
#else
  inline void set_comment(const char* comment) { }
  const char* comment() const { return ""; }
#endif

 protected:
  // The masm_ field is manipulated when compiling stubs with the
  // BEGIN_STUB and END_STUB macros. For that reason, it cannot be
  // constant.
  MacroAssembler* masm_;

 private:
  CodeGenerator* const generator_;
  Label enter_;
  Label exit_;
  int statement_position_;
  int position_;
#ifdef DEBUG
  const char* comment_;
#endif
  DISALLOW_COPY_AND_ASSIGN(DeferredCode);
};


// A superclass for gode generators.  The implementations of methods
// declared in this class are partially in codegen.c and partially in
// codegen_<arch>.c.
class CodeGenerator: public Visitor {
 public:
  CodeGenerator(bool is_eval,
                Handle<Script> script)
      : is_eval_(is_eval),
        script_(script),
        deferred_(8) { }


  // The code generator: Takes a function literal, generates code for it,
  // and assembles it all into a Code* object. This function should only
  // be called by compiler.cc.
  static Handle<Code> MakeCode(FunctionLiteral* fun,
                               Handle<Script> script,
                               bool is_eval);

  static void SetFunctionInfo(Handle<JSFunction> fun,
                              int length,
                              int function_token_position,
                              int start_position,
                              int end_position,
                              bool is_expression,
                              bool is_toplevel,
                              Handle<Script> script);

  virtual MacroAssembler* masm() = 0;

  virtual Scope* scope() const = 0;

  void AddDeferred(DeferredCode* code) { deferred_.Add(code); }
  void ProcessDeferred();

  // Accessors for is_eval.
  bool is_eval() { return is_eval_; }

  // Abstract node visitors.
#define DEF_VISIT(type)                         \
  virtual void Visit##type(type* node) = 0;
  NODE_LIST(DEF_VISIT)
#undef DEF_VISIT

 protected:
  bool CheckForInlineRuntimeCall(CallRuntime* node);
  Handle<JSFunction> BuildBoilerplate(FunctionLiteral* node);
  void ProcessDeclarations(ZoneList<Declaration*>* declarations);

  Handle<Code> ComputeCallInitialize(int argc);

  // Declare global variables and functions in the given array of
  // name/value pairs.
  virtual void DeclareGlobals(Handle<FixedArray> pairs) = 0;

  // Support for type checks.
  virtual void GenerateIsSmi(ZoneList<Expression*>* args) = 0;
  virtual void GenerateIsNonNegativeSmi(ZoneList<Expression*>* args) = 0;
  virtual void GenerateIsArray(ZoneList<Expression*>* args) = 0;

  // Support for arguments.length and arguments[?].
  virtual void GenerateArgumentsLength(ZoneList<Expression*>* args) = 0;
  virtual void GenerateArgumentsAccess(ZoneList<Expression*>* args) = 0;

  // Support for accessing the value field of an object (used by Date).
  virtual void GenerateValueOf(ZoneList<Expression*>* args) = 0;
  virtual void GenerateSetValueOf(ZoneList<Expression*>* args) = 0;

  // Fast support for charCodeAt(n).
  virtual void GenerateFastCharCodeAt(ZoneList<Expression*>* args) = 0;

  // Fast support for object equality testing.
  virtual void GenerateObjectEquals(ZoneList<Expression*>* args) = 0;


  // Multiple methods for fast case switch statement support.

  // The limit of the range of a fast-case switch, as a factor of the number
  // of cases of the switch. Each platform should return a value that
  // is optimal compared to the default code generated for a switch statement
  // on that platform.
  virtual int FastCaseSwitchMaxOverheadFactor() = 0;

  // The minimal number of cases in a switch before the fast-case switch
  // optimization is enabled. Each platform should return a value that
  // is optimal compared to the default code generated for a switch statement
  // on that platform.
  virtual int FastCaseSwitchMinCaseCount() = 0;

  // Allocate a jump table and create code to jump through it.
  // Should call GenerateFastCaseSwitchCases to generate the code for
  // all the cases at the appropriate point.
  virtual void GenerateFastCaseSwitchJumpTable(
      SwitchStatement* node, int min_index, int range, Label *fail_label,
      SmartPointer<Label*> &case_targets, SmartPointer<Label>& case_labels) = 0;

  // Generate the code for cases for the fast case switch.
  // Called by GenerateFastCaseSwitchJumpTable.
  virtual void GenerateFastCaseSwitchCases(
      SwitchStatement* node, SmartPointer<Label> &case_labels);

  // Fast support for constant-Smi switches.
  virtual void GenerateFastCaseSwitchStatement(
      SwitchStatement *node, int min_index, int range, int default_index);

  // Fast support for constant-Smi switches. Tests whether switch statement
  // permits optimization and calls GenerateFastCaseSwitch if it does.
  // Returns true if the fast-case switch was generated, and false if not.
  virtual bool TryGenerateFastCaseSwitchStatement(SwitchStatement *node);


 private:
  bool is_eval_;  // Tells whether code is generated for eval.
  Handle<Script> script_;
  List<DeferredCode*> deferred_;
};


// RuntimeStub models code stubs calling entry points in the Runtime class.
class RuntimeStub : public CodeStub {
 public:
  explicit RuntimeStub(Runtime::FunctionId id, int num_arguments)
      : id_(id), num_arguments_(num_arguments) { }

  void Generate(MacroAssembler* masm);

  // Disassembler support.  It is useful to be able to print the name
  // of the runtime function called through this stub.
  static const char* GetNameFromMinorKey(int minor_key) {
    return Runtime::FunctionForId(IdField::decode(minor_key))->stub_name;
  }

 private:
  Runtime::FunctionId id_;
  int num_arguments_;

  class ArgumentField: public BitField<int,  0, 16> {};
  class IdField: public BitField<Runtime::FunctionId, 16, kMinorBits - 16> {};

  Major MajorKey() { return Runtime; }
  int MinorKey() {
    return IdField::encode(id_) | ArgumentField::encode(num_arguments_);
  }

  const char* GetName();

#ifdef DEBUG
  void Print() {
    PrintF("RuntimeStub (id %s)\n", Runtime::FunctionForId(id_)->name);
  }
#endif
};


class StackCheckStub : public CodeStub {
 public:
  StackCheckStub() { }

  void Generate(MacroAssembler* masm);

 private:

  const char* GetName() { return "StackCheckStub"; }

  Major MajorKey() { return StackCheck; }
  int MinorKey() { return 0; }
};


class UnarySubStub : public CodeStub {
 public:
  UnarySubStub() { }

 private:
  Major MajorKey() { return UnarySub; }
  int MinorKey() { return 0; }
  void Generate(MacroAssembler* masm);

  const char* GetName() { return "UnarySubStub"; }
};


class CEntryStub : public CodeStub {
 public:
  CEntryStub() { }

  void Generate(MacroAssembler* masm) { GenerateBody(masm, false); }

 protected:
  void GenerateBody(MacroAssembler* masm, bool is_debug_break);
  void GenerateCore(MacroAssembler* masm,
                    Label* throw_normal_exception,
                    Label* throw_out_of_memory_exception,
                    StackFrame::Type frame_type,
                    bool do_gc);
  void GenerateThrowTOS(MacroAssembler* masm);
  void GenerateThrowOutOfMemory(MacroAssembler* masm);

 private:
  Major MajorKey() { return CEntry; }
  int MinorKey() { return 0; }

  const char* GetName() { return "CEntryStub"; }
};


class CEntryDebugBreakStub : public CEntryStub {
 public:
  CEntryDebugBreakStub() { }

  void Generate(MacroAssembler* masm) { GenerateBody(masm, true); }

 private:
  int MinorKey() { return 1; }

  const char* GetName() { return "CEntryDebugBreakStub"; }
};


class JSEntryStub : public CodeStub {
 public:
  JSEntryStub() { }

  void Generate(MacroAssembler* masm) { GenerateBody(masm, false); }

 protected:
  void GenerateBody(MacroAssembler* masm, bool is_construct);

 private:
  Major MajorKey() { return JSEntry; }
  int MinorKey() { return 0; }

  const char* GetName() { return "JSEntryStub"; }
};


class JSConstructEntryStub : public JSEntryStub {
 public:
  JSConstructEntryStub() { }

  void Generate(MacroAssembler* masm) { GenerateBody(masm, true); }

 private:
  int MinorKey() { return 1; }

  const char* GetName() { return "JSConstructEntryStub"; }
};


}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_H_
