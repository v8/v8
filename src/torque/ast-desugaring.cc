// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/ast-desugaring.h"

#include "src/torque/ast-visitor.h"
#include "src/torque/declarations.h"
#include "src/torque/torque-parser.h"

namespace v8::internal::torque {

class AstDesugaring : public AstVisitor<AstDesugaring> {
 public:
  AstNode* VisitTypeswitchStatement(TypeswitchStatement* stmt) {
    // Recursively visit first, to potentially desugar nested stuff.
    stmt =
        TypeswitchStatement::cast(AstVisitor::VisitTypeswitchStatement(stmt));

    Expression* expression = stmt->expr;
    auto& cases = stmt->cases;

    CurrentSourcePosition::Scope typeswitch_source_position(stmt->pos);
    // typeswitch (expression) case (x1 : T1) {
    //   ...b1
    // } case (x2 : T2) {
    //   ...b2
    // } case (x3 : T3) {
    //   ...b3
    // }
    //
    // desugars to
    //
    // {
    //   const _value = expression;
    //   try {
    //     const x1 : T1 = cast<T1>(_value) otherwise _NextCase;
    //     ...b1
    //   } label _NextCase {
    //     try {
    //       const x2 : T2 = cast<T2>(%assume_impossible<T1>(_value)) otherwise
    //       _NextCase;
    //       ...b2
    //     } label _NextCase {
    //       const x3 : T3 = %assume_impossible<T1|T2>(_value);
    //       ...b3
    //     }
    //   }
    // }

    BlockStatement* current_block = MakeNode<BlockStatement>();
    Statement* result = current_block;
    {
      CurrentSourcePosition::Scope current_source_position(expression->pos);
      current_block->statements.push_back(MakeNode<VarDeclarationStatement>(
          true, MakeNode<Identifier>("__value"), std::nullopt, expression));
    }

    TypeExpression* accumulated_types;
    for (size_t i = 0; i < cases.size(); ++i) {
      CurrentSourcePosition::Scope current_source_position(cases[i].pos);
      Expression* value =
          MakeNode<IdentifierExpression>(MakeNode<Identifier>("__value"));
      if (i >= 1) {
        value =
            MakeNode<AssumeTypeImpossibleExpression>(accumulated_types, value);
      }
      BlockStatement* case_block;
      if (i < cases.size() - 1) {
        value = MakeCall(MakeNode<Identifier>("Cast"),
                         std::vector<TypeExpression*>{cases[i].type},
                         std::vector<Expression*>{value},
                         std::vector<Statement*>{MakeNode<ExpressionStatement>(
                             MakeNode<IdentifierExpression>(
                                 MakeNode<Identifier>(kNextCaseLabelName)))});
        case_block = MakeNode<BlockStatement>();
      } else {
        case_block = current_block;
      }
      Identifier* name =
          cases[i].name ? *cases[i].name : MakeNode<Identifier>("__case_value");
      case_block->statements.push_back(
          MakeNode<VarDeclarationStatement>(true, name, cases[i].type, value));
      case_block->statements.push_back(cases[i].block);
      if (i < cases.size() - 1) {
        BlockStatement* next_block = MakeNode<BlockStatement>();
        current_block->statements.push_back(
            MakeNode<ExpressionStatement>(MakeNode<TryLabelExpression>(
                MakeNode<StatementExpression>(case_block),
                MakeNode<TryHandler>(TryHandler::HandlerKind::kLabel,
                                     MakeNode<Identifier>(kNextCaseLabelName),
                                     ParameterList::Empty(), next_block))));
        current_block = next_block;
      }
      accumulated_types = i > 0 ? MakeNode<UnionTypeExpression>(
                                      accumulated_types, cases[i].type)
                                : cases[i].type;
    }
    return result;
  }
};

void DesugarAst(Ast& ast) {
  AstDesugaring desugaring;
  desugaring.Run(ast);
}

}  // namespace v8::internal::torque
