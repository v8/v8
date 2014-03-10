// Copyright 2013 the V8 project authors. All rights reserved.
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

#include "hydrogen-minus-zero.h"

namespace v8 {
namespace internal {

void HComputeMinusZeroChecksPhase::Run() {
  const ZoneList<HBasicBlock*>* blocks(graph()->blocks());
  for (int i = 0; i < blocks->length(); ++i) {
    for (HInstructionIterator it(blocks->at(i)); !it.Done(); it.Advance()) {
      HInstruction* current = it.Current();
      if (current->IsChange()) {
        HChange* change = HChange::cast(current);
        // Propagate flags for negative zero checks upwards from conversions
        // int32-to-tagged and int32-to-double.
        Representation from = change->value()->representation();
        ASSERT(from.Equals(change->from()));
        if (from.IsSmiOrInteger32()) {
          ASSERT(change->to().IsTagged() ||
                 change->to().IsDouble() ||
                 change->to().IsSmiOrInteger32());
          PropagateMinusZeroChecks(change->value());
        }
      } else if (current->IsCompareMinusZeroAndBranch()) {
        HCompareMinusZeroAndBranch* check =
            HCompareMinusZeroAndBranch::cast(current);
        if (check->value()->representation().IsSmiOrInteger32()) {
          PropagateMinusZeroChecks(check->value());
        }
      }
    }
  }
}


void HComputeMinusZeroChecksPhase::PropagateMinusZeroChecks(HValue* value) {
  ASSERT(worklist_.is_empty());
  ASSERT(in_worklist_.IsEmpty());

  AddToWorklist(value);
  while (!worklist_.is_empty()) {
    value = worklist_.RemoveLast();

    if (value->IsPhi()) {
      // For phis, we must propagate the check to all of its inputs.
      HPhi* phi = HPhi::cast(value);
      for (int i = 0; i < phi->OperandCount(); ++i) {
        AddToWorklist(phi->OperandAt(i));
      }
    } else if (value->IsUnaryMathOperation()) {
      HUnaryMathOperation* instr = HUnaryMathOperation::cast(value);
      if (instr->representation().IsSmiOrInteger32() &&
          !instr->value()->representation().Equals(instr->representation())) {
        if (instr->value()->range() == NULL ||
            instr->value()->range()->CanBeMinusZero()) {
          instr->SetFlag(HValue::kBailoutOnMinusZero);
        }
      }
      if (instr->RequiredInputRepresentation(0).IsSmiOrInteger32() &&
          instr->representation().Equals(
              instr->RequiredInputRepresentation(0))) {
        AddToWorklist(instr->value());
      }
    } else if (value->IsChange()) {
      HChange* instr = HChange::cast(value);
      if (!instr->from().IsSmiOrInteger32() &&
          !instr->CanTruncateToInt32() &&
          (instr->value()->range() == NULL ||
           instr->value()->range()->CanBeMinusZero())) {
        instr->SetFlag(HValue::kBailoutOnMinusZero);
      }
    } else if (value->IsForceRepresentation()) {
      HForceRepresentation* instr = HForceRepresentation::cast(value);
      AddToWorklist(instr->value());
    } else if (value->IsMod()) {
      HMod* instr = HMod::cast(value);
      if (instr->range() == NULL || instr->range()->CanBeMinusZero()) {
        instr->SetFlag(HValue::kBailoutOnMinusZero);
        AddToWorklist(instr->left());
      }
    } else if (value->IsDiv() || value->IsMul()) {
      HBinaryOperation* instr = HBinaryOperation::cast(value);
      if (instr->range() == NULL || instr->range()->CanBeMinusZero()) {
        instr->SetFlag(HValue::kBailoutOnMinusZero);
      }
      AddToWorklist(instr->right());
      AddToWorklist(instr->left());
    } else if (value->IsMathFloorOfDiv()) {
      HMathFloorOfDiv* instr = HMathFloorOfDiv::cast(value);
      instr->SetFlag(HValue::kBailoutOnMinusZero);
    } else if (value->IsAdd() || value->IsSub()) {
      HBinaryOperation* instr = HBinaryOperation::cast(value);
      if (instr->range() == NULL || instr->range()->CanBeMinusZero()) {
        // Propagate to the left argument. If the left argument cannot be -0,
        // then the result of the add/sub operation cannot be either.
        AddToWorklist(instr->left());
      }
    } else if (value->IsMathMinMax()) {
      HMathMinMax* instr = HMathMinMax::cast(value);
      AddToWorklist(instr->right());
      AddToWorklist(instr->left());
    }
  }

  in_worklist_.Clear();
  ASSERT(in_worklist_.IsEmpty());
  ASSERT(worklist_.is_empty());
}

} }  // namespace v8::internal
