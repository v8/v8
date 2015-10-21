// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-generator.h"

#include <stack>

#include "src/compiler.h"
#include "src/interpreter/control-flow-builders.h"
#include "src/objects.h"
#include "src/parser.h"
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
  ContextScope(BytecodeGenerator* generator, Scope* scope,
               bool should_pop_context = true)
      : generator_(generator),
        scope_(scope),
        outer_(generator_->execution_context()),
        register_(generator_->NextContextRegister()),
        depth_(0),
        should_pop_context_(should_pop_context) {
    if (outer_) {
      depth_ = outer_->depth_ + 1;
      generator_->builder()->PushContext(register_);
    }
    generator_->set_execution_context(this);
  }

  ~ContextScope() {
    if (outer_ && should_pop_context_) {
      generator_->builder()->PopContext(outer_->reg());
    }
    generator_->set_execution_context(outer_);
  }

  // Returns the execution context for the given |scope| if it is a function
  // local execution context, otherwise returns nullptr.
  ContextScope* Previous(Scope* scope) {
    int depth = scope_->ContextChainLength(scope);
    if (depth > depth_) {
      return nullptr;
    }

    ContextScope* previous = this;
    for (int i = depth; i > 0; --i) {
      previous = previous->outer_;
    }
    DCHECK_EQ(previous->scope_, scope);
    return previous;
  }

  Scope* scope() const { return scope_; }
  Register reg() const { return register_; }

 private:
  BytecodeGenerator* generator_;
  Scope* scope_;
  ContextScope* outer_;
  Register register_;
  int depth_;
  bool should_pop_context_;
};


// Scoped class for tracking control statements entered by the
// visitor. The pattern derives AstGraphBuilder::ControlScope.
class BytecodeGenerator::ControlScope BASE_EMBEDDED {
 public:
  explicit ControlScope(BytecodeGenerator* generator)
      : generator_(generator), outer_(generator->execution_control()) {
    generator_->set_execution_control(this);
  }
  virtual ~ControlScope() { generator_->set_execution_control(outer()); }

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
    : isolate_(isolate),
      zone_(zone),
      builder_(isolate, zone),
      info_(nullptr),
      scope_(nullptr),
      globals_(0, zone),
      execution_control_(nullptr),
      execution_context_(nullptr) {
  InitializeAstVisitor(isolate);
}


BytecodeGenerator::~BytecodeGenerator() {}


Handle<BytecodeArray> BytecodeGenerator::MakeBytecode(CompilationInfo* info) {
  set_info(info);
  set_scope(info->scope());

  // Initialize the incoming context.
  ContextScope incoming_context(this, scope(), false);

  builder()->set_parameter_count(info->num_parameters_including_this());
  builder()->set_locals_count(scope()->num_stack_slots());
  builder()->set_context_count(scope()->MaxNestedContextChainLength());

  // Build function context only if there are context allocated variables.
  if (scope()->NeedsContext()) {
    // Push a new inner context scope for the function.
    VisitNewLocalFunctionContext();
    ContextScope local_function_context(this, scope(), false);
    VisitBuildLocalActivationContext();
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


void BytecodeGenerator::VisitBlock(Block* stmt) {
  builder()->EnterBlock();
  if (stmt->scope() == NULL) {
    // Visit statements in the same scope, no declarations.
    VisitStatements(stmt->statements());
  } else {
    // Visit declarations and statements in a block scope.
    if (stmt->scope()->NeedsContext()) {
      VisitNewLocalBlockContext(stmt->scope());
      ContextScope scope(this, stmt->scope());
      VisitDeclarations(stmt->scope()->declarations());
      VisitStatements(stmt->statements());
    } else {
      VisitDeclarations(stmt->scope()->declarations());
      VisitStatements(stmt->statements());
    }
  }
  builder()->LeaveBlock();
}


void BytecodeGenerator::VisitVariableDeclaration(VariableDeclaration* decl) {
  Variable* variable = decl->proxy()->var();
  VariableMode mode = decl->mode();
  // Const and let variables are initialized with the hole so that we can
  // check that they are only assigned once.
  bool hole_init = mode == CONST || mode == CONST_LEGACY || mode == LET;
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
    case VariableLocation::LOCAL:
      if (hole_init) {
        Register destination(variable->index());
        builder()->LoadTheHole().StoreAccumulatorInRegister(destination);
      }
      break;
    case VariableLocation::PARAMETER:
      if (hole_init) {
        // The parameter indices are shifted by 1 (receiver is variable
        // index -1 but is parameter index 0 in BytecodeArrayBuilder).
        Register destination(builder()->Parameter(variable->index() + 1));
        builder()->LoadTheHole().StoreAccumulatorInRegister(destination);
      }
      break;
    case VariableLocation::CONTEXT:
      if (hole_init) {
        builder()->LoadTheHole().StoreContextSlot(execution_context()->reg(),
                                                  variable->index());
      }
      break;
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
  execution_control()->Continue(stmt->target());
}


void BytecodeGenerator::VisitBreakStatement(BreakStatement* stmt) {
  execution_control()->Break(stmt->target());
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
  ControlScopeForIteration execution_control(this, stmt, &loop_builder);

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
  ControlScopeForIteration execution_control(this, stmt, &loop_builder);

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
  ControlScopeForIteration execution_control(this, stmt, &loop_builder);

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
  if (FLAG_ignition_fake_try_catch) {
    Visit(stmt->try_block());
    return;
  }
  UNIMPLEMENTED();
}


void BytecodeGenerator::VisitTryFinallyStatement(TryFinallyStatement* stmt) {
  if (FLAG_ignition_fake_try_catch) {
    Visit(stmt->try_block());
    Visit(stmt->finally_block());
    return;
  }
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


void BytecodeGenerator::VisitDoExpression(DoExpression* expr) {
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
  // Materialize a regular expression literal.
  TemporaryRegisterScope temporary_register_scope(builder());
  Register flags = temporary_register_scope.NewRegister();
  builder()
      ->LoadLiteral(expr->flags())
      .StoreAccumulatorInRegister(flags)
      .LoadLiteral(expr->pattern())
      .CreateRegExpLiteral(expr->literal_index(), flags);
}


void BytecodeGenerator::VisitObjectLiteral(ObjectLiteral* expr) {
  // Deep-copy the literal boilerplate.
  builder()
      ->LoadLiteral(expr->constant_properties())
      .CreateObjectLiteral(expr->literal_index(), expr->ComputeFlags(true));

  TemporaryRegisterScope temporary_register_scope(builder());
  Register literal;

  // Store computed values into the literal.
  bool literal_in_accumulator = true;
  int property_index = 0;
  AccessorTable accessor_table(zone());
  for (; property_index < expr->properties()->length(); property_index++) {
    TemporaryRegisterScope inner_temporary_register_scope(builder());
    ObjectLiteral::Property* property = expr->properties()->at(property_index);
    if (property->is_computed_name()) break;
    if (property->IsCompileTimeValue()) continue;

    if (literal_in_accumulator) {
      literal = temporary_register_scope.NewRegister();
      builder()->StoreAccumulatorInRegister(literal);
      literal_in_accumulator = false;
    }

    Literal* literal_key = property->key()->AsLiteral();
    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT:
        UNREACHABLE();
      case ObjectLiteral::Property::MATERIALIZED_LITERAL:
        DCHECK(!CompileTimeValue::IsCompileTimeValue(property->value()));
      // Fall through.
      case ObjectLiteral::Property::COMPUTED: {
        // It is safe to use [[Put]] here because the boilerplate already
        // contains computed properties with an uninitialized value.
        if (literal_key->value()->IsInternalizedString()) {
          if (property->emit_store()) {
            Register name = inner_temporary_register_scope.NewRegister();
            builder()
                ->LoadLiteral(literal_key->AsPropertyName())
                .StoreAccumulatorInRegister(name);
            Visit(property->value());
            builder()->StoreNamedProperty(literal, name,
                                          feedback_index(property->GetSlot(0)),
                                          language_mode());
          } else {
            Visit(property->value());
          }
        } else {
          Register key = inner_temporary_register_scope.NewRegister();
          Register value = inner_temporary_register_scope.NewRegister();
          Register language = inner_temporary_register_scope.NewRegister();
          DCHECK(Register::AreContiguous(literal, key, value, language));
          Visit(property->key());
          builder()->StoreAccumulatorInRegister(key);
          Visit(property->value());
          builder()->StoreAccumulatorInRegister(value);
          if (property->emit_store()) {
            builder()
                ->LoadLiteral(Smi::FromInt(SLOPPY))
                .StoreAccumulatorInRegister(language)
                .CallRuntime(Runtime::kSetProperty, literal, 4);
            VisitSetHomeObject(value, literal, property);
          }
        }
        break;
      }
      case ObjectLiteral::Property::PROTOTYPE: {
        DCHECK(property->emit_store());
        Register value = inner_temporary_register_scope.NewRegister();
        DCHECK(Register::AreContiguous(literal, value));
        Visit(property->value());
        builder()->StoreAccumulatorInRegister(value).CallRuntime(
            Runtime::kInternalSetPrototype, literal, 2);
        break;
      }
      case ObjectLiteral::Property::GETTER:
        if (property->emit_store()) {
          accessor_table.lookup(literal_key)->second->getter = property;
        }
        break;
      case ObjectLiteral::Property::SETTER:
        if (property->emit_store()) {
          accessor_table.lookup(literal_key)->second->setter = property;
        }
        break;
    }
  }

  // Define accessors, using only a single call to the runtime for each pair of
  // corresponding getters and setters.
  for (AccessorTable::Iterator it = accessor_table.begin();
       it != accessor_table.end(); ++it) {
    TemporaryRegisterScope inner_temporary_register_scope(builder());
    Register name = inner_temporary_register_scope.NewRegister();
    Register getter = inner_temporary_register_scope.NewRegister();
    Register setter = inner_temporary_register_scope.NewRegister();
    Register attr = inner_temporary_register_scope.NewRegister();
    DCHECK(Register::AreContiguous(literal, name, getter, setter, attr));
    Visit(it->first);
    builder()->StoreAccumulatorInRegister(name);
    VisitObjectLiteralAccessor(literal, it->second->getter, getter);
    VisitObjectLiteralAccessor(literal, it->second->setter, setter);
    builder()
        ->LoadLiteral(Smi::FromInt(NONE))
        .StoreAccumulatorInRegister(attr)
        .CallRuntime(Runtime::kDefineAccessorPropertyUnchecked, literal, 5);
  }

  // Object literals have two parts. The "static" part on the left contains no
  // computed property names, and so we can compute its map ahead of time; see
  // Runtime_CreateObjectLiteralBoilerplate. The second "dynamic" part starts
  // with the first computed property name and continues with all properties to
  // its right. All the code from above initializes the static component of the
  // object literal, and arranges for the map of the result to reflect the
  // static order in which the keys appear. For the dynamic properties, we
  // compile them into a series of "SetOwnProperty" runtime calls. This will
  // preserve insertion order.
  for (; property_index < expr->properties()->length(); property_index++) {
    ObjectLiteral::Property* property = expr->properties()->at(property_index);

    if (literal_in_accumulator) {
      literal = temporary_register_scope.NewRegister();
      builder()->StoreAccumulatorInRegister(literal);
      literal_in_accumulator = false;
    }

    if (property->kind() == ObjectLiteral::Property::PROTOTYPE) {
      DCHECK(property->emit_store());
      TemporaryRegisterScope inner_temporary_register_scope(builder());
      Register value = inner_temporary_register_scope.NewRegister();
      DCHECK(Register::AreContiguous(literal, value));
      Visit(property->value());
      builder()->StoreAccumulatorInRegister(value).CallRuntime(
          Runtime::kInternalSetPrototype, literal, 2);
      continue;
    }

    TemporaryRegisterScope inner_temporary_register_scope(builder());
    Register key = inner_temporary_register_scope.NewRegister();
    Register value = inner_temporary_register_scope.NewRegister();
    Register attr = inner_temporary_register_scope.NewRegister();
    DCHECK(Register::AreContiguous(literal, key, value, attr));

    Visit(property->key());
    builder()->CastAccumulatorToName().StoreAccumulatorInRegister(key);
    Visit(property->value());
    builder()->StoreAccumulatorInRegister(value);
    VisitSetHomeObject(value, literal, property);
    builder()->LoadLiteral(Smi::FromInt(NONE)).StoreAccumulatorInRegister(attr);
    Runtime::FunctionId function_id = static_cast<Runtime::FunctionId>(-1);
    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT:
      case ObjectLiteral::Property::COMPUTED:
      case ObjectLiteral::Property::MATERIALIZED_LITERAL:
        function_id = Runtime::kDefineDataPropertyUnchecked;
        break;
      case ObjectLiteral::Property::PROTOTYPE:
        UNREACHABLE();  // Handled specially above.
        break;
      case ObjectLiteral::Property::GETTER:
        function_id = Runtime::kDefineGetterPropertyUnchecked;
        break;
      case ObjectLiteral::Property::SETTER:
        function_id = Runtime::kDefineSetterPropertyUnchecked;
        break;
    }
    builder()->CallRuntime(function_id, literal, 4);
  }

  // Transform literals that contain functions to fast properties.
  if (expr->has_function()) {
    DCHECK(!literal_in_accumulator);
    builder()->CallRuntime(Runtime::kToFastProperties, literal, 1);
  }

  if (!literal_in_accumulator) {
    // Restore literal array into accumulator.
    builder()->LoadAccumulatorWithRegister(literal);
  }
}


void BytecodeGenerator::VisitArrayLiteral(ArrayLiteral* expr) {
  // Deep-copy the literal boilerplate.
  builder()
      ->LoadLiteral(expr->constant_elements())
      .CreateArrayLiteral(expr->literal_index(), expr->ComputeFlags(true));

  TemporaryRegisterScope temporary_register_scope(builder());
  Register index, literal;

  // Evaluate all the non-constant subexpressions and store them into the
  // newly cloned array.
  bool literal_in_accumulator = true;
  for (int array_index = 0; array_index < expr->values()->length();
       array_index++) {
    Expression* subexpr = expr->values()->at(array_index);
    if (CompileTimeValue::IsCompileTimeValue(subexpr)) continue;
    if (subexpr->IsSpread()) {
      // TODO(rmcilroy): Deal with spread expressions.
      UNIMPLEMENTED();
    }

    if (literal_in_accumulator) {
      index = temporary_register_scope.NewRegister();
      literal = temporary_register_scope.NewRegister();
      builder()->StoreAccumulatorInRegister(literal);
      literal_in_accumulator = false;
    }

    builder()
        ->LoadLiteral(Smi::FromInt(array_index))
        .StoreAccumulatorInRegister(index);
    Visit(subexpr);
    FeedbackVectorSlot slot = expr->LiteralFeedbackSlot();
    builder()->StoreKeyedProperty(literal, index, feedback_index(slot),
                                  language_mode());
  }

  if (!literal_in_accumulator) {
    // Restore literal array into accumulator.
    builder()->LoadAccumulatorWithRegister(literal);
  }
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
      // TODO(rmcilroy): Perform check for uninitialized legacy const, const and
      // let variables.
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
      builder()->LoadContextSlot(execution_context()->reg(),
                                 Context::GLOBAL_OBJECT_INDEX);
      builder()->StoreAccumulatorInRegister(obj);
      builder()->LoadLiteral(variable->name());
      builder()->LoadNamedProperty(obj, feedback_index(slot), language_mode());
      break;
    }
    case VariableLocation::CONTEXT: {
      ContextScope* context = execution_context()->Previous(variable->scope());
      if (context) {
        builder()->LoadContextSlot(context->reg(), variable->index());
      } else {
        UNIMPLEMENTED();
      }
      // TODO(rmcilroy): Perform check for uninitialized legacy const, const and
      // let variables.
      break;
    }
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
      builder()->LoadContextSlot(execution_context()->reg(),
                                 Context::GLOBAL_OBJECT_INDEX);
      builder()->StoreAccumulatorInRegister(obj);
      builder()->LoadLiteral(variable->name());
      builder()->StoreAccumulatorInRegister(name);
      builder()->LoadAccumulatorWithRegister(value);
      builder()->StoreNamedProperty(obj, name, feedback_index(slot),
                                    language_mode());
      break;
    }
    case VariableLocation::CONTEXT: {
      // TODO(rmcilroy): support const mode initialization.
      ContextScope* context = execution_context()->Previous(variable->scope());
      if (context) {
        builder()->StoreContextSlot(context->reg(), variable->index());
      } else {
        UNIMPLEMENTED();
      }
      break;
    }
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


void BytecodeGenerator::VisitThrow(Throw* expr) {
  TemporaryRegisterScope temporary_register_scope(builder());
  Visit(expr->exception());
  builder()->Throw();
}


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


Register BytecodeGenerator::VisitArguments(
    ZoneList<Expression*>* args, TemporaryRegisterScope* register_scope) {
  // Visit arguments and place in a contiguous block of temporary registers.
  // Return the first temporary register corresponding to the first argument.
  DCHECK_GT(args->length(), 0);
  Register first_arg = register_scope->NewRegister();
  Visit(args->at(0));
  builder()->StoreAccumulatorInRegister(first_arg);
  for (int i = 1; i < static_cast<int>(args->length()); i++) {
    Register ith_arg = register_scope->NewRegister();
    Visit(args->at(i));
    builder()->StoreAccumulatorInRegister(ith_arg);
    DCHECK(ith_arg.index() - i == first_arg.index());
  }

  return first_arg;
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
  if (args->length() > 0) {
    Register first_arg = VisitArguments(args, &temporary_register_scope);
    CHECK_EQ(first_arg.index(), receiver.index() + 1);
  }

  // TODO(rmcilroy): Deal with possible direct eval here?
  // TODO(rmcilroy): Use CallIC to allow call type feedback.
  builder()->Call(callee, receiver, args->length());
}


void BytecodeGenerator::VisitCallNew(CallNew* expr) {
  TemporaryRegisterScope temporary_register_scope(builder());
  Register constructor = temporary_register_scope.NewRegister();
  Visit(expr->expression());
  builder()->StoreAccumulatorInRegister(constructor);
  ZoneList<Expression*>* args = expr->arguments();
  if (args->length() > 0) {
    Register first_arg = VisitArguments(args, &temporary_register_scope);
    builder()->New(constructor, first_arg, args->length());
  } else {
    // The second argument here will be ignored as there are zero
    // arguments. Using the constructor register avoids avoid
    // allocating a temporary just to fill the operands.
    builder()->New(constructor, constructor, 0);
  }
}


void BytecodeGenerator::VisitCallRuntime(CallRuntime* expr) {
  if (expr->is_jsruntime()) {
    UNIMPLEMENTED();
  }

  // Evaluate all arguments to the runtime call.
  TemporaryRegisterScope temporary_register_scope(&builder_);

  // TODO(rmcilroy): support multiple return values.
  DCHECK_LE(expr->function()->result_size, 1);
  Runtime::FunctionId function_id = expr->function()->function_id;
  ZoneList<Expression*>* args = expr->arguments();
  Register first_arg;
  if (args->length() > 0) {
    first_arg = VisitArguments(args, &temporary_register_scope);
  } else {
    // Allocation here is just to fullfil the requirement that there
    // is a register operand for the start of the arguments though
    // there are zero when this is generated.
    first_arg = temporary_register_scope.NewRegister();
  }
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
      VisitCommaExpression(binop);
      break;
    case Token::OR:
      VisitLogicalOrExpression(binop);
      break;
    case Token::AND:
      VisitLogicalAndExpression(binop);
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
    DCHECK(Register::AreContiguous(closure, scope_info));
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
}


void BytecodeGenerator::VisitBuildLocalActivationContext() {
  Scope* scope = this->scope();

  if (scope->has_this_declaration() && scope->receiver()->IsContextSlot()) {
    UNIMPLEMENTED();
  }

  // Copy parameters into context if necessary.
  int num_parameters = scope->num_parameters();
  for (int i = 0; i < num_parameters; i++) {
    Variable* variable = scope->parameter(i);
    if (!variable->IsContextSlot()) continue;

    // The parameter indices are shifted by 1 (receiver is variable
    // index -1 but is parameter index 0 in BytecodeArrayBuilder).
    Register parameter(builder()->Parameter(i + 1));
    // Context variable (at bottom of the context chain).
    DCHECK_EQ(0, scope->ContextChainLength(variable->scope()));
    builder()->LoadAccumulatorWithRegister(parameter)
        .StoreContextSlot(execution_context()->reg(), variable->index());
  }
}


void BytecodeGenerator::VisitNewLocalBlockContext(Scope* scope) {
  DCHECK(scope->is_block_scope());

  // Allocate a new local block context.
  TemporaryRegisterScope temporary_register_scope(builder());
  Register scope_info = temporary_register_scope.NewRegister();
  Register closure = temporary_register_scope.NewRegister();
  DCHECK(Register::AreContiguous(scope_info, closure));
  builder()
      ->LoadLiteral(scope->GetScopeInfo(isolate()))
      .StoreAccumulatorInRegister(scope_info);
  VisitFunctionClosureForContext();
  builder()
      ->StoreAccumulatorInRegister(closure)
      .CallRuntime(Runtime::kPushBlockContext, scope_info, 2);
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


void BytecodeGenerator::VisitCommaExpression(BinaryOperation* binop) {
  Expression* left = binop->left();
  Expression* right = binop->right();

  Visit(left);
  Visit(right);
}


void BytecodeGenerator::VisitLogicalOrExpression(BinaryOperation* binop) {
  Expression* left = binop->left();
  Expression* right = binop->right();

  // Short-circuit evaluation- If it is known that left is always true,
  // no need to visit right
  if (left->ToBooleanIsTrue()) {
    Visit(left);
  } else {
    BytecodeLabel end_label;

    Visit(left);
    builder()->JumpIfToBooleanTrue(&end_label);
    Visit(right);
    builder()->Bind(&end_label);
  }
}


void BytecodeGenerator::VisitLogicalAndExpression(BinaryOperation* binop) {
  Expression* left = binop->left();
  Expression* right = binop->right();

  // Short-circuit evaluation- If it is known that left is always false,
  // no need to visit right
  if (left->ToBooleanIsFalse()) {
    Visit(left);
  } else {
    BytecodeLabel end_label;

    Visit(left);
    builder()->JumpIfToBooleanFalse(&end_label);
    Visit(right);
    builder()->Bind(&end_label);
  }
}


void BytecodeGenerator::VisitObjectLiteralAccessor(
    Register home_object, ObjectLiteralProperty* property, Register value_out) {
  // TODO(rmcilroy): Replace value_out with VisitForRegister();
  if (property == nullptr) {
    builder()->LoadNull().StoreAccumulatorInRegister(value_out);
  } else {
    Visit(property->value());
    builder()->StoreAccumulatorInRegister(value_out);
    VisitSetHomeObject(value_out, home_object, property);
  }
}


void BytecodeGenerator::VisitSetHomeObject(Register value, Register home_object,
                                           ObjectLiteralProperty* property,
                                           int slot_number) {
  Expression* expr = property->value();
  if (!FunctionLiteral::NeedsHomeObject(expr)) return;

  // TODO(rmcilroy): Remove UNIMPLEMENTED once we have tests for setting the
  // home object.
  UNIMPLEMENTED();

  TemporaryRegisterScope temporary_register_scope(builder());
  Register name = temporary_register_scope.NewRegister();
  isolate()->factory()->home_object_symbol();
  builder()
      ->LoadLiteral(isolate()->factory()->home_object_symbol())
      .StoreAccumulatorInRegister(name)
      .StoreNamedProperty(home_object, name,
                          feedback_index(property->GetSlot(slot_number)),
                          language_mode());
}


void BytecodeGenerator::VisitFunctionClosureForContext() {
  Scope* closure_scope = execution_context()->scope()->ClosureScope();
  if (closure_scope->is_script_scope() ||
      closure_scope->is_module_scope()) {
    // Contexts nested in the native context have a canonical empty function as
    // their closure, not the anonymous closure containing the global code.
    // Pass a SMI sentinel and let the runtime look up the empty function.
    builder()->LoadLiteral(Smi::FromInt(0));
  } else {
    DCHECK(closure_scope->is_function_scope());
    builder()->LoadAccumulatorWithRegister(Register::function_closure());
  }
}


Register BytecodeGenerator::NextContextRegister() const {
  if (execution_context() == nullptr) {
    // Return the incoming function context for the outermost execution context.
    return Register::function_context();
  }
  Register previous = execution_context()->reg();
  if (previous == Register::function_context()) {
    // If the previous context was the incoming function context, then the next
    // context register is the first local context register.
    return builder_.first_context_register();
  } else {
    // Otherwise use the next local context register.
    DCHECK_LT(previous.index(), builder_.last_context_register().index());
    return Register(previous.index() + 1);
  }
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
