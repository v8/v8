// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/hydrogen-dehoist.h"

namespace v8 {
namespace internal {

static void DehoistArrayIndex(ArrayInstructionInterface* array_operation) {
  HValue* index = array_operation->GetKey()->ActualValue();
  if (!index->representation().IsSmiOrInteger32()) return;
  if (!index->IsAdd() && !index->IsSub()) return;

  HConstant* constant;
  HValue* subexpression;
  HBinaryOperation* binary_operation = HBinaryOperation::cast(index);
  if (binary_operation->left()->IsConstant() && index->IsAdd()) {
    subexpression = binary_operation->right();
    constant = HConstant::cast(binary_operation->left());
  } else if (binary_operation->right()->IsConstant()) {
    subexpression = binary_operation->left();
    constant = HConstant::cast(binary_operation->right());
  } else {
    return;
  }

  if (!constant->HasInteger32Value()) return;
  int32_t sign = binary_operation->IsSub() ? -1 : 1;
  int32_t value = constant->Integer32Value() * sign;
  if (value < 0) return;

  // Check for overflow.
  // TODO(mvstanton): replace with safe_math.h operations when that code is
  // integrated.
  int32_t shift_amount =
      1 << ElementsKindToShiftSize(array_operation->elements_kind());
  int32_t multiplication_result = value * shift_amount;
  if ((multiplication_result >> shift_amount) != value) return;
  value = multiplication_result;

  // Ensure that the array operation can add value to existing base offset
  // without overflowing.
  if (!array_operation->CanIncreaseBaseOffset(value)) return;
  array_operation->SetKey(subexpression);
  if (binary_operation->HasNoUses()) {
    binary_operation->DeleteAndReplaceWith(NULL);
  }
  array_operation->IncreaseBaseOffset(value);
  array_operation->SetDehoisted(true);
}


void HDehoistIndexComputationsPhase::Run() {
  const ZoneList<HBasicBlock*>* blocks(graph()->blocks());
  for (int i = 0; i < blocks->length(); ++i) {
    for (HInstructionIterator it(blocks->at(i)); !it.Done(); it.Advance()) {
      HInstruction* instr = it.Current();
      if (instr->IsLoadKeyed()) {
        DehoistArrayIndex(HLoadKeyed::cast(instr));
      } else if (instr->IsStoreKeyed()) {
        DehoistArrayIndex(HStoreKeyed::cast(instr));
      }
    }
  }
}

} }  // namespace v8::internal
