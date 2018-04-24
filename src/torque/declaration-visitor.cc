// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/declaration-visitor.h"

namespace v8 {
namespace internal {
namespace torque {

void DeclarationVisitor::Visit(Expression* expr) {
  switch (expr->kind) {
#define ENUM_ITEM(name)        \
  case AstNode::Kind::k##name: \
    return Visit(name::cast(expr));
    AST_EXPRESSION_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
    default:
      UNIMPLEMENTED();
  }
}

void DeclarationVisitor::Visit(Statement* stmt) {
  switch (stmt->kind) {
#define ENUM_ITEM(name)        \
  case AstNode::Kind::k##name: \
    return Visit(name::cast(stmt));
    AST_STATEMENT_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
    default:
      UNIMPLEMENTED();
  }
}

void DeclarationVisitor::Visit(Declaration* decl) {
  switch (decl->kind) {
#define ENUM_ITEM(name)        \
  case AstNode::Kind::k##name: \
    return Visit(name::cast(decl));
    AST_DECLARATION_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
    default:
      UNIMPLEMENTED();
  }
}

void DeclarationVisitor::Visit(BuiltinDeclaration* decl) {
  if (global_context_.verbose()) {
    std::cout << "found declaration of builtin " << decl->name;
  }
  Scope* enclosing_scope = TopScope();

  Signature signature = MakeSignature(decl->parameters, decl->return_type, {});

  Scope* new_scope = new Scope(global_context_);
  Scope::Activator s(new_scope);
  if (signature.types().size() == 0 ||
      !signature.types()[0].Is(CONTEXT_TYPE_STRING)) {
    std::stringstream stream;
    stream << "first parameter to builtin " << decl->name
           << " is not a context but should be at "
           << PositionAsString(decl->pos);
    ReportError(stream.str());
  }
  if (global_context_.verbose()) {
    std::cout << decl->name << " with signature " << signature << std::endl;
  }

  const bool javascript = decl->javascript_linkage;
  const bool varargs = decl->parameters.has_varargs;
  if (varargs && !javascript) {
    std::stringstream stream;
    stream << "builtin " << decl->name
           << " with rest parameters must be a JavaScript builtin at "
           << PositionAsString(decl->pos);
    ReportError(stream.str());
  }
  if (javascript) {
    if (signature.types().size() < 2 ||
        !signature.types()[1].Is(OBJECT_TYPE_STRING)) {
      std::stringstream stream;
      stream << "second parameter to javascript builtin " << decl->name
             << " is not a receiver type but should be at "
             << PositionAsString(decl->pos);
      ReportError(stream.str());
    }
  }

  if (varargs) {
    TopScope()->DeclareConstant(decl->pos, decl->parameters.arguments_variable,
                                GetTypeOracle().GetArgumentsType(),
                                "arguments");
  }

  Builtin::Kind kind = !javascript ? Builtin::kStub
                                   : varargs ? Builtin::kVarArgsJavaScript
                                             : Builtin::kFixedArgsJavaScript;
  Builtin* builtin = enclosing_scope->DeclareBuiltin(
      decl->pos, decl->name, kind, new_scope, signature);
  defined_builtins_.push_back(builtin);
  DeclareParameterList(decl->pos, signature, {});
  CurrentCallActivator activator(global_context_, builtin);
  Visit(decl->body);
}

void DeclarationVisitor::Visit(MacroDeclaration* decl) {
  if (global_context_.verbose()) {
    std::cout << "found declaration of macro " << decl->name;
  }
  Scope* enclosing_scope = TopScope();
  Scope* new_scope = new Scope(global_context_);
  Scope::Activator s(new_scope);

  PushControlSplit();

  Signature signature =
      MakeSignature(decl->parameters, decl->return_type, decl->labels);

  if (!signature.return_type.IsVoidOrNever()) {
    TopScope()->DeclareVariable(decl->pos, kReturnValueVariable,
                                signature.return_type);
  }

  if (global_context_.verbose()) {
    std::cout << " resulting in signature " << signature << "\n";
  }

  Macro* macro = enclosing_scope->DeclareMacro(decl->pos, decl->name, new_scope,
                                               signature);
  DeclareParameterList(decl->pos, signature, decl->labels);
  CurrentCallActivator activator(global_context_, macro);
  Visit(decl->body);

  auto changed_vars = PopControlSplit();
  global_context_.AddControlSplitChangedVariables(decl, changed_vars);
}

void DeclarationVisitor::Visit(ReturnStatement* stmt) {
  const Callable* callable = global_context_.GetCurrentCallable();
  if (callable->IsMacro() && callable->HasReturnValue()) {
    MarkVariableModified(
        Variable::cast(LookupValue(stmt->pos, kReturnValueVariable)));
  }
}

void DeclarationVisitor::Visit(ForOfLoopStatement* stmt) {
  // Scope for for iteration variable
  Scope::Activator s(global_context_, stmt);
  Visit(stmt->var_declaration);
  Visit(stmt->iterable);
  if (stmt->begin) Visit(*stmt->begin);
  if (stmt->end) Visit(*stmt->end);
  PushControlSplit();
  Visit(stmt->body);
  auto changed_vars = PopControlSplit();
  global_context_.AddControlSplitChangedVariables(stmt, changed_vars);
}

void DeclarationVisitor::Visit(TryCatchStatement* stmt) {
  // Activate a new scope to declare catch handler labels, they should not be
  // visible outside the catch.
  {
    Scope::Activator s(global_context_, stmt);

    // Declare catch labels
    for (LabelBlock* block : stmt->label_blocks) {
      Label* shared_label = TopScope()->DeclareLabel(stmt->pos, block->label);
      {
        Scope::Activator s(global_context_, block->body);
        if (block->parameters.has_varargs) {
          std::stringstream stream;
          stream << "cannot use ... for label parameters  at "
                 << PositionAsString(stmt->pos);
          ReportError(stream.str());
        }

        size_t i = 0;
        for (auto p : block->parameters.names) {
          shared_label->AddVariable(TopScope()->DeclareVariable(
              stmt->pos, p,
              GetTypeOracle().GetType(block->parameters.types[i])));
          ++i;
        }
      }
      if (global_context_.verbose()) {
        std::cout << " declaring catch for exception " << block->label << "\n";
      }
    }

    // Try catch not supported yet
    DCHECK_EQ(stmt->catch_blocks.size(), 0);

    Visit(stmt->try_block);
  }

  for (CatchBlock* block : stmt->catch_blocks) {
    Visit(block->body);
  }

  for (LabelBlock* block : stmt->label_blocks) {
    Visit(block->body);
  }
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
