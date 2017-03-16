// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 19.4 Symbol Objects

// ES6 section 19.4.3.4 Symbol.prototype [ @@toPrimitive ] ( hint )
TF_BUILTIN(SymbolPrototypeToPrimitive, CodeStubAssembler) {
  Node* receiver = Parameter(0);
  Node* context = Parameter(4);

  Node* result = ToThisValue(context, receiver, PrimitiveType::kSymbol,
                             "Symbol.prototype [ @@toPrimitive ]");
  Return(result);
}

// ES6 section 19.4.3.2 Symbol.prototype.toString ( )
TF_BUILTIN(SymbolPrototypeToString, CodeStubAssembler) {
  Node* receiver = Parameter(0);
  Node* context = Parameter(3);

  Node* value = ToThisValue(context, receiver, PrimitiveType::kSymbol,
                            "Symbol.prototype.toString");
  Node* result = CallRuntime(Runtime::kSymbolDescriptiveString, context, value);
  Return(result);
}

// ES6 section 19.4.3.3 Symbol.prototype.valueOf ( )
TF_BUILTIN(SymbolPrototypeValueOf, CodeStubAssembler) {
  Node* receiver = Parameter(0);
  Node* context = Parameter(3);

  Node* result = ToThisValue(context, receiver, PrimitiveType::kSymbol,
                             "Symbol.prototype.valueOf");
  Return(result);
}

}  // namespace internal
}  // namespace v8
