// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGISTER_ALLOCATOR_VERIFIER_H_
#define V8_REGISTER_ALLOCATOR_VERIFIER_H_

#include "src/v8.h"
#include "src/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

class InstructionOperand;
class InstructionSequence;

class RegisterAllocatorVerifier FINAL : public ZoneObject {
 public:
  RegisterAllocatorVerifier(Zone* zone, const InstructionSequence* sequence);

  void VerifyAssignment();

 private:
  enum ConstraintType {
    kConstant,
    kImmediate,
    kRegister,
    kFixedRegister,
    kDoubleRegister,
    kFixedDoubleRegister,
    kFixedSlot,
    kNone,
    kNoneDouble,
    kSameAsFirst
  };

  struct OperandConstraint {
    ConstraintType type_;
    int value_;  // subkind index when relevant
  };

  struct InstructionConstraint {
    const Instruction* instruction_;
    size_t operand_constaints_size_;
    OperandConstraint* operand_constraints_;
  };

  typedef ZoneVector<InstructionConstraint> Constraints;

  const InstructionSequence* sequence() const { return sequence_; }
  Constraints* constraints() { return &constraints_; }
  void BuildConstraint(const InstructionOperand* op,
                       OperandConstraint* constraint);
  void CheckConstraint(const InstructionOperand* op,
                       const OperandConstraint* constraint);

  const InstructionSequence* const sequence_;
  Constraints constraints_;

  DISALLOW_COPY_AND_ASSIGN(RegisterAllocatorVerifier);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif
