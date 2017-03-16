// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 19.3 Boolean Objects

// ES6 section 19.3.3.2 Boolean.prototype.toString ( )
TF_BUILTIN(BooleanPrototypeToString, CodeStubAssembler) {
  Node* receiver = Parameter(0);
  Node* context = Parameter(3);

  Node* value = ToThisValue(context, receiver, PrimitiveType::kBoolean,
                            "Boolean.prototype.toString");
  Node* result = LoadObjectField(value, Oddball::kToStringOffset);
  Return(result);
}

// ES6 section 19.3.3.3 Boolean.prototype.valueOf ( )
TF_BUILTIN(BooleanPrototypeValueOf, CodeStubAssembler) {
  Node* receiver = Parameter(0);
  Node* context = Parameter(3);

  Node* result = ToThisValue(context, receiver, PrimitiveType::kBoolean,
                             "Boolean.prototype.valueOf");
  Return(result);
}

}  // namespace internal
}  // namespace v8
