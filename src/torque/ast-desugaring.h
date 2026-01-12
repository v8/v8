// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_AST_DESUGARING_H_
#define V8_TORQUE_AST_DESUGARING_H_

#include "src/torque/ast.h"

namespace v8::internal::torque {

void DesugarAst(Ast& ast);

}  // namespace v8::internal::torque

#endif  // V8_TORQUE_AST_DESUGARING_H_
