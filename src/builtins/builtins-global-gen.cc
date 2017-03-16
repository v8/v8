// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

// ES6 section 18.2.2 isFinite ( number )
TF_BUILTIN(GlobalIsFinite, CodeStubAssembler) {
  Node* context = Parameter(4);

  Label return_true(this), return_false(this);

  // We might need to loop once for ToNumber conversion.
  Variable var_num(this, MachineRepresentation::kTagged);
  Label loop(this, &var_num);
  var_num.Bind(Parameter(1));
  Goto(&loop);
  Bind(&loop);
  {
    Node* num = var_num.value();

    // Check if {num} is a Smi or a HeapObject.
    GotoIf(TaggedIsSmi(num), &return_true);

    // Check if {num} is a HeapNumber.
    Label if_numisheapnumber(this),
        if_numisnotheapnumber(this, Label::kDeferred);
    Branch(IsHeapNumberMap(LoadMap(num)), &if_numisheapnumber,
           &if_numisnotheapnumber);

    Bind(&if_numisheapnumber);
    {
      // Check if {num} contains a finite, non-NaN value.
      Node* num_value = LoadHeapNumberValue(num);
      BranchIfFloat64IsNaN(Float64Sub(num_value, num_value), &return_false,
                           &return_true);
    }

    Bind(&if_numisnotheapnumber);
    {
      // Need to convert {num} to a Number first.
      Callable callable = CodeFactory::NonNumberToNumber(isolate());
      var_num.Bind(CallStub(callable, context, num));
      Goto(&loop);
    }
  }

  Bind(&return_true);
  Return(BooleanConstant(true));

  Bind(&return_false);
  Return(BooleanConstant(false));
}

// ES6 section 18.2.3 isNaN ( number )
TF_BUILTIN(GlobalIsNaN, CodeStubAssembler) {
  Node* context = Parameter(4);

  Label return_true(this), return_false(this);

  // We might need to loop once for ToNumber conversion.
  Variable var_num(this, MachineRepresentation::kTagged);
  Label loop(this, &var_num);
  var_num.Bind(Parameter(1));
  Goto(&loop);
  Bind(&loop);
  {
    Node* num = var_num.value();

    // Check if {num} is a Smi or a HeapObject.
    GotoIf(TaggedIsSmi(num), &return_false);

    // Check if {num} is a HeapNumber.
    Label if_numisheapnumber(this),
        if_numisnotheapnumber(this, Label::kDeferred);
    Branch(IsHeapNumberMap(LoadMap(num)), &if_numisheapnumber,
           &if_numisnotheapnumber);

    Bind(&if_numisheapnumber);
    {
      // Check if {num} contains a NaN.
      Node* num_value = LoadHeapNumberValue(num);
      BranchIfFloat64IsNaN(num_value, &return_true, &return_false);
    }

    Bind(&if_numisnotheapnumber);
    {
      // Need to convert {num} to a Number first.
      Callable callable = CodeFactory::NonNumberToNumber(isolate());
      var_num.Bind(CallStub(callable, context, num));
      Goto(&loop);
    }
  }

  Bind(&return_true);
  Return(BooleanConstant(true));

  Bind(&return_false);
  Return(BooleanConstant(false));
}

}  // namespace internal
}  // namespace v8
