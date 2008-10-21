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

#ifndef V8_CODEGEN_IA32_H_
#define V8_CODEGEN_IA32_H_

#include "scopes.h"

namespace v8 { namespace internal {

// Forward declarations
class DeferredCode;

// Mode to overwrite BinaryExpression values.
enum OverwriteMode { NO_OVERWRITE, OVERWRITE_LEFT, OVERWRITE_RIGHT };

enum InitState { CONST_INIT, NOT_CONST_INIT };
enum TypeofState { INSIDE_TYPEOF, NOT_INSIDE_TYPEOF };


// -------------------------------------------------------------------------
// Virtual frame

class VirtualFrame BASE_EMBEDDED {
 public:
  explicit VirtualFrame(CodeGenerator* cgen);

  void Enter();
  void Exit();

  void AllocateLocals();

  Operand Top() const { return Operand(esp, 0); }

  Operand Element(int index) const {
    return Operand(esp, index * kPointerSize);
  }

  Operand Local(int index) const {
    ASSERT(0 <= index && index < frame_local_count_);
    return Operand(ebp, kLocal0Offset - index * kPointerSize);
  }

  Operand Function() const { return Operand(ebp, kFunctionOffset); }

  Operand Context() const { return Operand(ebp, kContextOffset); }

  Operand Parameter(int index) const {
    ASSERT(-1 <= index && index < parameter_count_);
    return Operand(ebp, (1 + parameter_count_ - index) * kPointerSize);
  }

  Operand Receiver() const { return Parameter(-1); }

  inline void Drop(int count);

  inline void Pop();
  inline void Pop(Register reg);
  inline void Pop(Operand operand);

  inline void Push(Register reg);
  inline void Push(Operand operand);
  inline void Push(Immediate immediate);

 private:
  static const int kLocal0Offset = JavaScriptFrameConstants::kLocal0Offset;
  static const int kFunctionOffset = JavaScriptFrameConstants::kFunctionOffset;
  static const int kContextOffset = StandardFrameConstants::kContextOffset;

  MacroAssembler* masm_;
  int frame_local_count_;
  int parameter_count_;
};


// -------------------------------------------------------------------------
// Reference support

// A reference is a C++ stack-allocated object that keeps an ECMA
// reference on the execution stack while in scope. For variables
// the reference is empty, indicating that it isn't necessary to
// store state on the stack for keeping track of references to those.
// For properties, we keep either one (named) or two (indexed) values
// on the execution stack to represent the reference.

class Reference BASE_EMBEDDED {
 public:
  // The values of the types is important, see size().
  enum Type { ILLEGAL = -1, SLOT = 0, NAMED = 1, KEYED = 2 };
  Reference(CodeGenerator* cgen, Expression* expression);
  ~Reference();

  Expression* expression() const { return expression_; }
  Type type() const { return type_; }
  void set_type(Type value) {
    ASSERT(type_ == ILLEGAL);
    type_ = value;
  }

  // The size of the reference or -1 if the reference is illegal.
  int size() const { return type_; }

  bool is_illegal() const { return type_ == ILLEGAL; }
  bool is_slot() const { return type_ == SLOT; }
  bool is_property() const { return type_ == NAMED || type_ == KEYED; }

  // Return the name.  Only valid for named property references.
  Handle<String> GetName();

  // Generate code to push the value of the reference on top of the
  // expression stack.  The reference is expected to be already on top of
  // the expression stack, and it is left in place with its value above it.
  void GetValue(TypeofState typeof_state);

  // Generate code to store the value on top of the expression stack in the
  // reference.  The reference is expected to be immediately below the value
  // on the expression stack.  The stored value is left in place (with the
  // reference intact below it) to support chained assignments.
  void SetValue(InitState init_state);

 private:
  CodeGenerator* cgen_;
  Expression* expression_;
  Type type_;
};


// -------------------------------------------------------------------------
// Code generation state

// The state is passed down the AST by the code generator (and back up, in
// the form of the state of the label pair).  It is threaded through the
// call stack.  Constructing a state implicitly pushes it on the owning code
// generator's stack of states, and destroying one implicitly pops it.

class CodeGenState BASE_EMBEDDED {
 public:
  // Create an initial code generator state.  Destroying the initial state
  // leaves the code generator with a NULL state.
  explicit CodeGenState(CodeGenerator* owner);

  // Create a code generator state based on a code generator's current
  // state.  The new state has its own access type and pair of branch
  // labels, and no reference.
  CodeGenState(CodeGenerator* owner,
               TypeofState typeof_state,
               Label* true_target,
               Label* false_target);

  // Destroy a code generator state and restore the owning code generator's
  // previous state.
  ~CodeGenState();

  TypeofState typeof_state() const { return typeof_state_; }
  Label* true_target() const { return true_target_; }
  Label* false_target() const { return false_target_; }

 private:
  CodeGenerator* owner_;
  TypeofState typeof_state_;
  Label* true_target_;
  Label* false_target_;
  CodeGenState* previous_;
};




// -------------------------------------------------------------------------
// CodeGenerator

class CodeGenerator: public Visitor {
 public:
  // Takes a function literal, generates code for it. This function should only
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

  // Accessors
  MacroAssembler* masm() { return masm_; }

  VirtualFrame* frame() const { return frame_; }

  CodeGenState* state() { return state_; }
  void set_state(CodeGenState* state) { state_ = state; }

  void AddDeferred(DeferredCode* code) { deferred_.Add(code); }

 private:
  // Construction/Destruction
  CodeGenerator(int buffer_size, Handle<Script> script, bool is_eval);
  virtual ~CodeGenerator() { delete masm_; }

  // Accessors
  Scope* scope() const { return scope_; }

  void ProcessDeferred();

  bool is_eval() { return is_eval_; }

  // State
  bool has_cc() const  { return cc_reg_ >= 0; }
  TypeofState typeof_state() const { return state_->typeof_state(); }
  Label* true_target() const  { return state_->true_target(); }
  Label* false_target() const  { return state_->false_target(); }


  // Node visitors.
#define DEF_VISIT(type) \
  void Visit##type(type* node);
  NODE_LIST(DEF_VISIT)
#undef DEF_VISIT

  // Main code generation function
  void GenCode(FunctionLiteral* fun);

  // The following are used by class Reference.
  void LoadReference(Reference* ref);
  void UnloadReference(Reference* ref);

  Operand ContextOperand(Register context, int index) const {
    return Operand(context, Context::SlotOffset(index));
  }

  Operand SlotOperand(Slot* slot, Register tmp);


  // Expressions
  Operand GlobalObject() const {
    return ContextOperand(esi, Context::GLOBAL_INDEX);
  }

  void LoadCondition(Expression* x,
                     TypeofState typeof_state,
                     Label* true_target,
                     Label* false_target,
                     bool force_cc);
  void Load(Expression* x, TypeofState typeof_state = NOT_INSIDE_TYPEOF);
  void LoadGlobal();
  void LoadGlobalReceiver(Register scratch);

  // Read a value from a slot and leave it on top of the expression stack.
  void LoadFromSlot(Slot* slot, TypeofState typeof_state);

  // Special code for typeof expressions: Unfortunately, we must
  // be careful when loading the expression in 'typeof'
  // expressions. We are not allowed to throw reference errors for
  // non-existing properties of the global object, so we must make it
  // look like an explicit property access, instead of an access
  // through the context chain.
  void LoadTypeofExpression(Expression* x);

  void ToBoolean(Label* true_target, Label* false_target);

  void GenericBinaryOperation(Token::Value op,
      const OverwriteMode overwrite_mode = NO_OVERWRITE);
  void Comparison(Condition cc, bool strict = false);

  // Inline small integer literals. To prevent long attacker-controlled byte
  // sequences, we only inline small Smi:s.
  static const int kMaxSmiInlinedBits = 16;
  bool IsInlineSmi(Literal* literal);
  void SmiComparison(Condition cc,  Handle<Object> value, bool strict = false);
  void SmiOperation(Token::Value op,
                    Handle<Object> value,
                    bool reversed,
                    OverwriteMode overwrite_mode);

  void CallWithArguments(ZoneList<Expression*>* arguments, int position);

  // Control flow
  void Branch(bool if_true, Label* L);
  void CheckStack();
  void CleanStack(int num_bytes);

  bool CheckForInlineRuntimeCall(CallRuntime* node);
  Handle<JSFunction> BuildBoilerplate(FunctionLiteral* node);
  void ProcessDeclarations(ZoneList<Declaration*>* declarations);

  Handle<Code> ComputeCallInitialize(int argc);

  // Declare global variables and functions in the given array of
  // name/value pairs.
  void DeclareGlobals(Handle<FixedArray> pairs);

  // Instantiate the function boilerplate.
  void InstantiateBoilerplate(Handle<JSFunction> boilerplate);

  // Support for type checks.
  void GenerateIsSmi(ZoneList<Expression*>* args);
  void GenerateIsNonNegativeSmi(ZoneList<Expression*>* args);
  void GenerateIsArray(ZoneList<Expression*>* args);

  // Support for arguments.length and arguments[?].
  void GenerateArgumentsLength(ZoneList<Expression*>* args);
  void GenerateArgumentsAccess(ZoneList<Expression*>* args);

  // Support for accessing the value field of an object (used by Date).
  void GenerateValueOf(ZoneList<Expression*>* args);
  void GenerateSetValueOf(ZoneList<Expression*>* args);

  // Fast support for charCodeAt(n).
  void GenerateFastCharCodeAt(ZoneList<Expression*>* args);

  // Fast support for object equality testing.
  void GenerateObjectEquals(ZoneList<Expression*>* args);


  // Methods and constants for fast case switch statement support.
  //
  // Only allow fast-case switch if the range of labels is at most
  // this factor times the number of case labels.
  // Value is derived from comparing the size of code generated by the normal
  // switch code for Smi-labels to the size of a single pointer. If code
  // quality increases this number should be decreased to match.
  static const int kFastSwitchMaxOverheadFactor = 5;

  // Minimal number of switch cases required before we allow jump-table
  // optimization.
  static const int kFastSwitchMinCaseCount = 5;

  // The limit of the range of a fast-case switch, as a factor of the number
  // of cases of the switch. Each platform should return a value that
  // is optimal compared to the default code generated for a switch statement
  // on that platform.
  int FastCaseSwitchMaxOverheadFactor();

  // The minimal number of cases in a switch before the fast-case switch
  // optimization is enabled. Each platform should return a value that
  // is optimal compared to the default code generated for a switch statement
  // on that platform.
  int FastCaseSwitchMinCaseCount();

  // Allocate a jump table and create code to jump through it.
  // Should call GenerateFastCaseSwitchCases to generate the code for
  // all the cases at the appropriate point.
  void GenerateFastCaseSwitchJumpTable(SwitchStatement* node, int min_index,
                                       int range, Label *fail_label,
                                       SmartPointer<Label*> &case_targets,
                                       SmartPointer<Label>& case_labels);

  // Generate the code for cases for the fast case switch.
  // Called by GenerateFastCaseSwitchJumpTable.
  void GenerateFastCaseSwitchCases(SwitchStatement* node,
                                   SmartPointer<Label> &case_labels);

  // Fast support for constant-Smi switches.
  void GenerateFastCaseSwitchStatement(SwitchStatement *node, int min_index,
                                       int range, int default_index);

  // Fast support for constant-Smi switches. Tests whether switch statement
  // permits optimization and calls GenerateFastCaseSwitch if it does.
  // Returns true if the fast-case switch was generated, and false if not.
  bool TryGenerateFastCaseSwitchStatement(SwitchStatement *node);


  // Bottle-neck interface to call the Assembler to generate the statement
  // position. This allows us to easily control whether statement positions
  // should be generated or not.
  void RecordStatementPosition(Node* node);

  bool is_eval_;  // Tells whether code is generated for eval.
  Handle<Script> script_;
  List<DeferredCode*> deferred_;

  // Assembler
  MacroAssembler* masm_;  // to generate code

  // Code generation state
  Scope* scope_;
  VirtualFrame* frame_;
  Condition cc_reg_;
  CodeGenState* state_;
  bool is_inside_try_;
  int break_stack_height_;

  // Labels
  Label function_return_;

  friend class VirtualFrame;
  friend class Reference;

  DISALLOW_COPY_AND_ASSIGN(CodeGenerator);
};


} }  // namespace v8::internal

#endif  // V8_CODEGEN_IA32_H_
