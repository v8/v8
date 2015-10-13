// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-generator.h"

#include <stack>

#include "src/compiler.h"
#include "src/interpreter/control-flow-builders.h"
#include "src/objects.h"
#include "src/scopes.h"
#include "src/token.h"

namespace v8 {
namespace internal {
namespace interpreter {


// Scoped class tracking context objects created by the visitor. Represents
// mutations of the context chain within the function body, allowing pushing and
// popping of the current {context_register} during visitation.
class BytecodeGenerator::ContextScope BASE_EMBEDDED {
 public:
  explicit ContextScope(BytecodeGenerator* generator,
                        bool is_function_context = false)
      : generator_(generator),
        outer_(generator_->current_context()),
        is_function_context_(is_function_context) {
    DCHECK(!is_function_context ||
           outer_.index() == Register::function_context().index());
    Register new_context_reg = NewContextRegister();
    generator_->builder()->PushContext(new_context_reg);
    generator_->set_current_context(new_context_reg);
  }

  ~ContextScope() {
    if (!is_function_context_) {
      generator_->builder()->PopContext(outer_);
    }
    generator_->set_current_context(outer_);
  }

 private:
  Register NewContextRegister() const {
    if (outer_.index() == Register::function_context().index()) {
      return generator_->builder()->first_context_register();
    } else {
      DCHECK_LT(outer_.index(),
                generator_->builder()->last_context_register().index());
      return Register(outer_.index() + 1);
    }
  }

  BytecodeGenerator* generator_;
  Register outer_;
  bool is_function_context_;
};


// Scoped class for tracking control statements entered by the
// visitor. The pattern derives AstGraphBuilder::ControlScope.
class BytecodeGenerator::ControlScope BASE_EMBEDDED {
 public:
  explicit ControlScope(BytecodeGenerator* generator)
      : generator_(generator), outer_(generator->control_scope()) {
    generator_->set_control_scope(this);
  }
  virtual ~ControlScope() { generator_->set_control_scope(outer()); }

  void Break(Statement* stmt) { PerformCommand(CMD_BREAK, stmt); }
  void Continue(Statement* stmt) { PerformCommand(CMD_CONTINUE, stmt); }

 protected:
  enum Command { CMD_BREAK, CMD_CONTINUE };
  void PerformCommand(Command command, Statement* statement);
  virtual bool Execute(Command command, Statement* statement) = 0;

  BytecodeGenerator* generator() const { return generator_; }
  ControlScope* outer() const { return outer_; }

 private:
  BytecodeGenerator* generator_;
  ControlScope* outer_;

  DISALLOW_COPY_AND_ASSIGN(ControlScope);
};


// Scoped class for enabling 'break' and 'continue' in iteration
// constructs, e.g. do...while, while..., for...
class BytecodeGenerator::ControlScopeForIteration
    : public BytecodeGenerator::ControlScope {
 public:
  ControlScopeForIteration(BytecodeGenerator* generator,
                           IterationStatement* statement,
                           LoopBuilder* loop_builder)
      : ControlScope(generator),
        statement_(statement),
        loop_builder_(loop_builder) {}

 protected:
  virtual bool Execute(Command command, Statement* statement) {
    if (statement != statement_) return false;
    switch (command) {
      case CMD_BREAK:
        loop_builder_->Break();
        return true;
      case CMD_CONTINUE:
        loop_builder_->Continue();
        return true;
    }
    return false;
  }

 private:
  Statement* statement_;
  LoopBuilder* loop_builder_;
};


void BytecodeGenerator::ControlScope::PerformCommand(Command command,
                                                     Statement* statement) {
  ControlScope* current = this;
  do {
    if (current->Execute(command, statement)) return;
    current = current->outer();
  } while (current != nullptr);
  UNREACHABLE();
}


BytecodeGenerator::BytecodeGenerator(Isolate* isolate, Zone* zone)
    : builder_(isolate, zone),
      info_(nullptr),
      scope_(nullptr),
      globals_(0, zone),
      control_scope_(nullptr),
      current_context_(Register::function_context()) {
  InitializeAstVisitor(isolate, zone);
}


BytecodeGenerator::~BytecodeGenerator() {}


Handle<BytecodeArray> BytecodeGenerator::MakeBytecode(CompilationInfo* info) {
  set_info(info);
  set_scope(info->scope());

  builder()->set_parameter_count(info->num_parameters_including_this());
  builder()->set_locals_count(scope()->num_stack_slots());
  // TODO(rmcilroy): Set correct context count.
  builder()->set_context_count(info->num_heap_slots() > 0 ? 1 : 0);

  // Build function context only if there are context allocated variables.
  if (info->num_heap_slots() > 0) {
    // Push a new inner context scope for the function.
    VisitNewLocalFunctionContext();
    ContextScope top_context(this, true);
    MakeBytecodeBody();
  } else {
    MakeBytecodeBody();
  }

  set_scope(nullptr);
  set_info(nullptr);
  return builder_.ToBytecodeArray();
}


void BytecodeGenerator::MakeBytecodeBody() {
  // Visit declarations within the function scope.
  VisitDeclarations(scope()->declarations());

  // Visit statements in the function body.
  VisitStatements(info()->literal()->body());
}


void BytecodeGenerator::VisitBlock(Block* node) {
  builder()->EnterBlock();
  if (node->scope() == NULL) {
    // Visit statements in the same scope, no declarations.
    VisitStatements(node->statements());
  } else {
    // Visit declarations and statements in a block scope.
    if (node->scope()->ContextLocalCount() > 0) {
      UNIMPLEMENTED();
    } else {
      VisitDeclarations(node->scope()->declarations());
      VisitStatements(node->statements());
    }
  }
  builder()->LeaveBlock();
}


void BytecodeGenerator::VisitVariableDeclaration(VariableDeclaration* decl) {
  Variable* variable = decl->proxy()->var();
  VariableMode mode = decl->mode();
  bool hole_init = mode == CONST || mode == CONST_LEGACY || mode == LET;
  if (hole_init) {
    UNIMPLEMENTED();
  }
  switch (variable->location()) {
    case VariableLocation::GLOBAL:
    case VariableLocation::UNALLOCATED: {
      Handle<Oddball> value = variable->binding_needs_init()
                                  ? isolate()->factory()->the_hole_value()
                                  : isolate()->factory()->undefined_value();
      globals()->push_back(variable->name());
      globals()->push_back(value);
      break;
    }
    case VariableLocation::PARAMETER:
    case VariableLocation::LOCAL:
      // Details stored in scope, i.e. variable index.
      break;
    case VariableLocation::CONTEXT:
    case VariableLocation::LOOKUP:
      UNIMPLEMENTED();
      break;
  }
}


void BytecodeGenerator::VisitFunctionDeclaration(FunctionDeclaration* decl) {
  Variable* variable = decl->proxy()->var();
  switch (variable->location()) {
    case VariableLocation::GLOBAL:
    case VariableLocation::UNALLOCATED: {
      Handle<SharedFunctionInfo> function = Compiler::GetSharedFunctionInfo(
          decl->fun(), info()->script(), info());
      // Check for stack-overflow exception.
      if (function.is_null()) return SetStackOverflow();
      globals()->push_back(variable->name());
      globals()->push_back(function);
      break;
    }
    case VariableLocation::PARAMETER:
    case VariableLocation::LOCAL:
    case VariableLocation::CONTEXT:
    case VariableLocation::LOOKUP:
      UNIMPLEMENTED();
  }
}


void BytecodeGenerator::VisitImportDeclaration(ImportDeclaration* decl) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitExportDeclaration(ExportDeclaration* decl) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitDeclarations(
    ZoneList<Declaration*>* declarations) {
  DCHECK(globals()->empty());
  AstVisitor::VisitDeclarations(declarations);
  if (globals()->empty()) return;
  int array_index = 0;
  Handle<FixedArray> data = isolate()->factory()->NewFixedArray(
      static_cast<int>(globals()->size()), TENURED);
  for (Handle<Object> obj : *globals()) data->set(array_index++, *obj);
  int encoded_flags = DeclareGlobalsEvalFlag::encode(info()->is_eval()) |
                      DeclareGlobalsNativeFlag::encode(info()->is_native()) |
                      DeclareGlobalsLanguageMode::encode(language_mode());

  TemporaryRegisterScope temporary_register_scope(builder());
  Register pairs = temporary_register_scope.NewRegister();
  builder()->LoadLiteral(data);
  builder()->StoreAccumulatorInRegister(pairs);

  Register flags = temporary_register_scope.NewRegister();
  builder()->LoadLiteral(Smi::FromInt(encoded_flags));
  builder()->StoreAccumulatorInRegister(flags);
  DCHECK(flags.index() == pairs.index() + 1);

  builder()->CallRuntime(Runtime::kDeclareGlobals, pairs, 2);
  globals()->clear();
}


void BytecodeGenerator::VisitExpressionStatement(ExpressionStatement* stmt) {
  Visit(stmt->expression());
}


void BytecodeGenerator::VisitEmptyStatement(EmptyStatement* stmt) {
  // TODO(oth): For control-flow it could be useful to signal empty paths here.
}


void BytecodeGenerator::VisitIfStatement(IfStatement* stmt) {
  // TODO(oth): Spot easy cases where there code would not need to
  // emit the then block or the else block, e.g. condition is
  // obviously true/1/false/0.

  BytecodeLabel else_label, end_label;

  Visit(stmt->condition());
  builder()->CastAccumulatorToBoolean();
  builder()->JumpIfFalse(&else_label);
  Visit(stmt->then_statement());
  if (stmt->HasElseStatement()) {
    builder()->Jump(&end_label);
    builder()->Bind(&else_label);
    Visit(stmt->else_statement());
  } else {
    builder()->Bind(&else_label);
  }
  builder()->Bind(&end_label);
}


void BytecodeGenerator::VisitSloppyBlockFunctionStatement(
    SloppyBlockFunctionStatement* stmt) {
  Visit(stmt->statement());
}


void BytecodeGenerator::VisitContinueStatement(ContinueStatement* stmt) {
  control_scope()->Continue(stmt->target());
}


void BytecodeGenerator::VisitBreakStatement(BreakStatement* stmt) {
  control_scope()->Break(stmt->target());
}


void BytecodeGenerator::VisitReturnStatement(ReturnStatement* stmt) {
  Visit(stmt->expression());
  builder()->Return();
}


void BytecodeGenerator::VisitWithStatement(WithStatement* stmt) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitSwitchStatement(SwitchStatement* stmt) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitCaseClause(CaseClause* clause) { UNIMPLEMENTED(); }


void BytecodeGenerator::VisitDoWhileStatement(DoWhileStatement* stmt) {
  LoopBuilder loop_builder(builder());
  ControlScopeForIteration control_scope(this, stmt, &loop_builder);

  BytecodeLabel body_label, condition_label, done_label;
  builder()->Bind(&body_label);
  Visit(stmt->body());
  builder()->Bind(&condition_label);
  Visit(stmt->cond());
  builder()->JumpIfTrue(&body_label);
  builder()->Bind(&done_label);

  loop_builder.SetBreakTarget(done_label);
  loop_builder.SetContinueTarget(condition_label);
}


void BytecodeGenerator::VisitWhileStatement(WhileStatement* stmt) {
  LoopBuilder loop_builder(builder());
  ControlScopeForIteration control_scope(this, stmt, &loop_builder);

  BytecodeLabel body_label, condition_label, done_label;
  builder()->Jump(&condition_label);
  builder()->Bind(&body_label);
  Visit(stmt->body());
  builder()->Bind(&condition_label);
  Visit(stmt->cond());
  builder()->JumpIfTrue(&body_label);
  builder()->Bind(&done_label);

  loop_builder.SetBreakTarget(done_label);
  loop_builder.SetContinueTarget(condition_label);
}


void BytecodeGenerator::VisitForStatement(ForStatement* stmt) {
  LoopBuilder loop_builder(builder());
  ControlScopeForIteration control_scope(this, stmt, &loop_builder);

  if (stmt->init() != nullptr) {
    Visit(stmt->init());
  }

  BytecodeLabel body_label, condition_label, next_label, done_label;
  if (stmt->cond() != nullptr) {
    builder()->Jump(&condition_label);
  }
  builder()->Bind(&body_label);
  Visit(stmt->body());
  builder()->Bind(&next_label);
  if (stmt->next() != nullptr) {
    Visit(stmt->next());
  }
  if (stmt->cond()) {
    builder()->Bind(&condition_label);
    Visit(stmt->cond());
    builder()->JumpIfTrue(&body_label);
  } else {
    builder()->Jump(&body_label);
  }
  builder()->Bind(&done_label);

  loop_builder.SetBreakTarget(done_label);
  loop_builder.SetContinueTarget(next_label);
}


void BytecodeGenerator::VisitForInStatement(ForInStatement* stmt) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitForOfStatement(ForOfStatement* stmt) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitTryCatchStatement(TryCatchStatement* stmt) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitTryFinallyStatement(TryFinallyStatement* stmt) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitDebuggerStatement(DebuggerStatement* stmt) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitFunctionLiteral(FunctionLiteral* expr) {
  // Find or build a shared function info.
  Handle<SharedFunctionInfo> shared_info =
      Compiler::GetSharedFunctionInfo(expr, info()->script(), info());
  CHECK(!shared_info.is_null());  // TODO(rmcilroy): Set stack overflow?

  builder()
      ->LoadLiteral(shared_info)
      .CreateClosure(expr->pretenure() ? TENURED : NOT_TENURED);
}


void BytecodeGenerator::VisitClassLiteral(ClassLiteral* expr) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitNativeFunctionLiteral(
    NativeFunctionLiteral* expr) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitConditional(Conditional* expr) { UNIMPLEMENTED(); }


void BytecodeGenerator::VisitLiteral(Literal* expr) {
  Handle<Object> value = expr->value();
  if (value->IsSmi()) {
    builder()->LoadLiteral(Smi::cast(*value));
  } else if (value->IsUndefined()) {
    builder()->LoadUndefined();
  } else if (value->IsTrue()) {
    builder()->LoadTrue();
  } else if (value->IsFalse()) {
    builder()->LoadFalse();
  } else if (value->IsNull()) {
    builder()->LoadNull();
  } else if (value->IsTheHole()) {
    builder()->LoadTheHole();
  } else {
    builder()->LoadLiteral(value);
  }
}


void BytecodeGenerator::VisitRegExpLiteral(RegExpLiteral* expr) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitObjectLiteral(ObjectLiteral* expr) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitArrayLiteral(ArrayLiteral* expr) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitVariableProxy(VariableProxy* proxy) {
  VisitVariableLoad(proxy->var(), proxy->VariableFeedbackSlot());
}


void BytecodeGenerator::VisitVariableLoad(Variable* variable,
                                          FeedbackVectorSlot slot) {
  switch (variable->location()) {
    case VariableLocation::LOCAL: {
      Register source(variable->index());
      builder()->LoadAccumulatorWithRegister(source);
      break;
    }
    case VariableLocation::PARAMETER: {
      // The parameter indices are shifted by 1 (receiver is variable
      // index -1 but is parameter index 0 in BytecodeArrayBuilder).
      Register source(builder()->Parameter(variable->index() + 1));
      builder()->LoadAccumulatorWithRegister(source);
      break;
    }
    case VariableLocation::GLOBAL: {
      // Global var, const, or let variable.
      // TODO(rmcilroy): If context chain depth is short enough, do this using
      // a generic version of LoadGlobalViaContextStub rather than calling the
      // runtime.
      DCHECK(variable->IsStaticGlobalObjectProperty());
      builder()->LoadGlobal(variable->index());
      break;
    }
    case VariableLocation::UNALLOCATED: {
      TemporaryRegisterScope temporary_register_scope(builder());
      Register obj = temporary_register_scope.NewRegister();
      builder()->LoadContextSlot(current_context(),
                                 Context::GLOBAL_OBJECT_INDEX);
      builder()->StoreAccumulatorInRegister(obj);
      builder()->LoadLiteral(variable->name());
      builder()->LoadNamedProperty(obj, feedback_index(slot), language_mode());
      break;
    }
    case VariableLocation::CONTEXT:
    case VariableLocation::LOOKUP:
      UNIMPLEMENTED();
  }
}


void BytecodeGenerator::VisitVariableAssignment(Variable* variable,
                                                FeedbackVectorSlot slot) {
  switch (variable->location()) {
    case VariableLocation::LOCAL: {
      // TODO(rmcilroy): support const mode initialization.
      Register destination(variable->index());
      builder()->StoreAccumulatorInRegister(destination);
      break;
    }
    case VariableLocation::PARAMETER: {
      // The parameter indices are shifted by 1 (receiver is variable
      // index -1 but is parameter index 0 in BytecodeArrayBuilder).
      Register destination(builder()->Parameter(variable->index() + 1));
      builder()->StoreAccumulatorInRegister(destination);
      break;
    }
    case VariableLocation::GLOBAL: {
      // Global var, const, or let variable.
      // TODO(rmcilroy): If context chain depth is short enough, do this using
      // a generic version of LoadGlobalViaContextStub rather than calling the
      // runtime.
      DCHECK(variable->IsStaticGlobalObjectProperty());
      builder()->StoreGlobal(variable->index(), language_mode());
      break;
    }
    case VariableLocation::UNALLOCATED: {
      TemporaryRegisterScope temporary_register_scope(builder());
      Register value = temporary_register_scope.NewRegister();
      Register obj = temporary_register_scope.NewRegister();
      Register name = temporary_register_scope.NewRegister();
      // TODO(rmcilroy): Investigate whether we can avoid having to stash the
      // value in a register.
      builder()->StoreAccumulatorInRegister(value);
      builder()->LoadContextSlot(current_context(),
                                 Context::GLOBAL_OBJECT_INDEX);
      builder()->StoreAccumulatorInRegister(obj);
      builder()->LoadLiteral(variable->name());
      builder()->StoreAccumulatorInRegister(name);
      builder()->LoadAccumulatorWithRegister(value);
      builder()->StoreNamedProperty(obj, name, feedback_index(slot),
                                    language_mode());
      break;
    }
    case VariableLocation::CONTEXT:
    case VariableLocation::LOOKUP:
      UNIMPLEMENTED();
  }
}


void BytecodeGenerator::VisitAssignment(Assignment* expr) {
  DCHECK(expr->target()->IsValidReferenceExpression());
  TemporaryRegisterScope temporary_register_scope(builder());
  Register object, key;

  // Left-hand side can only be a property, a global or a variable slot.
  Property* property = expr->target()->AsProperty();
  LhsKind assign_type = Property::GetAssignType(property);

  // Evaluate LHS expression.
  switch (assign_type) {
    case VARIABLE:
      // Nothing to do to evaluate variable assignment LHS.
      break;
    case NAMED_PROPERTY:
      object = temporary_register_scope.NewRegister();
      key = temporary_register_scope.NewRegister();
      Visit(property->obj());
      builder()->StoreAccumulatorInRegister(object);
      builder()->LoadLiteral(property->key()->AsLiteral()->AsPropertyName());
      builder()->StoreAccumulatorInRegister(key);
      break;
    case KEYED_PROPERTY:
      object = temporary_register_scope.NewRegister();
      key = temporary_register_scope.NewRegister();
      Visit(property->obj());
      builder()->StoreAccumulatorInRegister(object);
      Visit(property->key());
      builder()->StoreAccumulatorInRegister(key);
      break;
    case NAMED_SUPER_PROPERTY:
    case KEYED_SUPER_PROPERTY:
      UNIMPLEMENTED();
  }

  // Evaluate the value and potentially handle compound assignments by loading
  // the left-hand side value and performing a binary operation.
  if (expr->is_compound()) {
    UNIMPLEMENTED();
  } else {
    Visit(expr->value());
  }

  // Store the value.
  FeedbackVectorSlot slot = expr->AssignmentSlot();
  switch (assign_type) {
    case VARIABLE: {
      Variable* variable = expr->target()->AsVariableProxy()->var();
      VisitVariableAssignment(variable, slot);
      break;
    }
    case NAMED_PROPERTY:
      builder()->StoreNamedProperty(object, key, feedback_index(slot),
                                    language_mode());
      break;
    case KEYED_PROPERTY:
      builder()->StoreKeyedProperty(object, key, feedback_index(slot),
                                    language_mode());
      break;
    case NAMED_SUPER_PROPERTY:
    case KEYED_SUPER_PROPERTY:
      UNIMPLEMENTED();
  }
}


void BytecodeGenerator::VisitYield(Yield* expr) { UNIMPLEMENTED(); }


void BytecodeGenerator::VisitThrow(Throw* expr) { UNIMPLEMENTED(); }


void BytecodeGenerator::VisitPropertyLoad(Register obj, Property* expr) {
  LhsKind property_kind = Property::GetAssignType(expr);
  FeedbackVectorSlot slot = expr->PropertyFeedbackSlot();
  switch (property_kind) {
    case VARIABLE:
      UNREACHABLE();
    case NAMED_PROPERTY: {
      builder()->LoadLiteral(expr->key()->AsLiteral()->AsPropertyName());
      builder()->LoadNamedProperty(obj, feedback_index(slot), language_mode());
      break;
    }
    case KEYED_PROPERTY: {
      Visit(expr->key());
      builder()->LoadKeyedProperty(obj, feedback_index(slot), language_mode());
      break;
    }
    case NAMED_SUPER_PROPERTY:
    case KEYED_SUPER_PROPERTY:
      UNIMPLEMENTED();
  }
}


void BytecodeGenerator::VisitProperty(Property* expr) {
  TemporaryRegisterScope temporary_register_scope(builder());
  Register obj = temporary_register_scope.NewRegister();
  Visit(expr->obj());
  builder()->StoreAccumulatorInRegister(obj);
  VisitPropertyLoad(obj, expr);
}


void BytecodeGenerator::VisitCall(Call* expr) {
  Expression* callee_expr = expr->expression();
  Call::CallType call_type = expr->GetCallType(isolate());

  // Prepare the callee and the receiver to the function call. This depends on
  // the semantics of the underlying call type.
  TemporaryRegisterScope temporary_register_scope(builder());
  Register callee = temporary_register_scope.NewRegister();
  Register receiver = temporary_register_scope.NewRegister();

  switch (call_type) {
    case Call::PROPERTY_CALL: {
      Property* property = callee_expr->AsProperty();
      if (property->IsSuperAccess()) {
        UNIMPLEMENTED();
      }
      Visit(property->obj());
      builder()->StoreAccumulatorInRegister(receiver);
      // Perform a property load of the callee.
      VisitPropertyLoad(receiver, property);
      builder()->StoreAccumulatorInRegister(callee);
      break;
    }
    case Call::GLOBAL_CALL: {
      // Receiver is undefined for global calls.
      builder()->LoadUndefined().StoreAccumulatorInRegister(receiver);
      // Load callee as a global variable.
      VariableProxy* proxy = callee_expr->AsVariableProxy();
      VisitVariableLoad(proxy->var(), proxy->VariableFeedbackSlot());
      builder()->StoreAccumulatorInRegister(callee);
      break;
    }
    case Call::OTHER_CALL: {
      builder()->LoadUndefined().StoreAccumulatorInRegister(receiver);
      Visit(callee_expr);
      builder()->StoreAccumulatorInRegister(callee);
      break;
    }
    case Call::LOOKUP_SLOT_CALL:
    case Call::SUPER_CALL:
    case Call::POSSIBLY_EVAL_CALL:
      UNIMPLEMENTED();
  }

  // Evaluate all arguments to the function call and store in sequential
  // registers.
  ZoneList<Expression*>* args = expr->arguments();
  for (int i = 0; i < args->length(); ++i) {
    Visit(args->at(i));
    Register arg = temporary_register_scope.NewRegister();
    DCHECK(arg.index() - i == receiver.index() + 1);
    builder()->StoreAccumulatorInRegister(arg);
  }

  // TODO(rmcilroy): Deal with possible direct eval here?
  // TODO(rmcilroy): Use CallIC to allow call type feedback.
  builder()->Call(callee, receiver, args->length());
}


void BytecodeGenerator::VisitCallNew(CallNew* expr) { UNIMPLEMENTED(); }


void BytecodeGenerator::VisitCallRuntime(CallRuntime* expr) {
  if (expr->is_jsruntime()) {
    UNIMPLEMENTED();
  }

  // Evaluate all arguments to the runtime call.
  ZoneList<Expression*>* args = expr->arguments();
  TemporaryRegisterScope temporary_register_scope(builder());
  // Ensure we always have a valid first_arg register even if there are no
  // arguments to pass.
  Register first_arg = temporary_register_scope.NewRegister();
  for (int i = 0; i < args->length(); ++i) {
    Register arg =
        (i == 0) ? first_arg : temporary_register_scope.NewRegister();
    Visit(args->at(i));
    DCHECK_EQ(arg.index() - i, first_arg.index());
    builder()->StoreAccumulatorInRegister(arg);
  }

  // TODO(rmcilroy): support multiple return values.
  DCHECK_LE(expr->function()->result_size, 1);
  Runtime::FunctionId function_id = expr->function()->function_id;
  builder()->CallRuntime(function_id, first_arg, args->length());
}


void BytecodeGenerator::VisitVoid(UnaryOperation* expr) {
  Visit(expr->expression());
  builder()->LoadUndefined();
}


void BytecodeGenerator::VisitTypeOf(UnaryOperation* expr) {
  Visit(expr->expression());
  builder()->TypeOf();
}


void BytecodeGenerator::VisitNot(UnaryOperation* expr) {
  Visit(expr->expression());
  builder()->LogicalNot();
}


void BytecodeGenerator::VisitUnaryOperation(UnaryOperation* expr) {
  switch (expr->op()) {
    case Token::Value::NOT:
      VisitNot(expr);
      break;
    case Token::Value::TYPEOF:
      VisitTypeOf(expr);
      break;
    case Token::Value::VOID:
      VisitVoid(expr);
      break;
    case Token::Value::BIT_NOT:
    case Token::Value::DELETE:
      UNIMPLEMENTED();
    default:
      UNREACHABLE();
  }
}


void BytecodeGenerator::VisitCountOperation(CountOperation* expr) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitBinaryOperation(BinaryOperation* binop) {
  switch (binop->op()) {
    case Token::COMMA:
    case Token::OR:
    case Token::AND:
      UNIMPLEMENTED();
      break;
    default:
      VisitArithmeticExpression(binop);
      break;
  }
}


void BytecodeGenerator::VisitCompareOperation(CompareOperation* expr) {
  Token::Value op = expr->op();
  Expression* left = expr->left();
  Expression* right = expr->right();

  TemporaryRegisterScope temporary_register_scope(builder());
  Register temporary = temporary_register_scope.NewRegister();

  Visit(left);
  builder()->StoreAccumulatorInRegister(temporary);
  Visit(right);
  builder()->CompareOperation(op, temporary, language_mode_strength());
}


void BytecodeGenerator::VisitSpread(Spread* expr) { UNREACHABLE(); }


void BytecodeGenerator::VisitEmptyParentheses(EmptyParentheses* expr) {
  UNREACHABLE();
}


void BytecodeGenerator::VisitThisFunction(ThisFunction* expr) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitSuperCallReference(SuperCallReference* expr) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitSuperPropertyReference(
    SuperPropertyReference* expr) {
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitNewLocalFunctionContext() {
  Scope* scope = this->scope();

  // Allocate a new local context.
  if (scope->is_script_scope()) {
    TemporaryRegisterScope temporary_register_scope(builder());
    Register closure = temporary_register_scope.NewRegister();
    Register scope_info = temporary_register_scope.NewRegister();
    DCHECK_EQ(closure.index() + 1, scope_info.index());
    builder()
        ->LoadAccumulatorWithRegister(Register::function_closure())
        .StoreAccumulatorInRegister(closure)
        .LoadLiteral(scope->GetScopeInfo(isolate()))
        .StoreAccumulatorInRegister(scope_info)
        .CallRuntime(Runtime::kNewScriptContext, closure, 2);
  } else {
    builder()->CallRuntime(Runtime::kNewFunctionContext,
                           Register::function_closure(), 1);
  }

  if (scope->has_this_declaration() && scope->receiver()->IsContextSlot()) {
    UNIMPLEMENTED();
  }

  // Copy parameters into context if necessary.
  int num_parameters = scope->num_parameters();
  for (int i = 0; i < num_parameters; i++) {
    Variable* variable = scope->parameter(i);
    if (variable->IsContextSlot()) {
      UNIMPLEMENTED();
    }
  }
}


void BytecodeGenerator::VisitArithmeticExpression(BinaryOperation* binop) {
  Token::Value op = binop->op();
  Expression* left = binop->left();
  Expression* right = binop->right();

  TemporaryRegisterScope temporary_register_scope(builder());
  Register temporary = temporary_register_scope.NewRegister();

  Visit(left);
  builder()->StoreAccumulatorInRegister(temporary);
  Visit(right);
  builder()->BinaryOperation(op, temporary, language_mode_strength());
}


LanguageMode BytecodeGenerator::language_mode() const {
  return info()->language_mode();
}


Strength BytecodeGenerator::language_mode_strength() const {
  return strength(language_mode());
}


int BytecodeGenerator::feedback_index(FeedbackVectorSlot slot) const {
  return info()->feedback_vector()->GetIndex(slot);
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
