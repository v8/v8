// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/backend/mid-tier-register-allocator.h"

#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/optional.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/register-configuration.h"
#include "src/codegen/tick-counter.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/linkage.h"
#include "src/logging/counters.h"
#include "src/utils/bit-vector.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

class RegisterState;

MidTierRegisterAllocationData::MidTierRegisterAllocationData(
    const RegisterConfiguration* config, Zone* zone, Frame* frame,
    InstructionSequence* code, TickCounter* tick_counter,
    const char* debug_name)
    : RegisterAllocationData(Type::kMidTier),
      allocation_zone_(zone),
      frame_(frame),
      code_(code),
      debug_name_(debug_name),
      config_(config),
      virtual_register_data_(code->VirtualRegisterCount(), allocation_zone()),
      reference_map_instructions_(allocation_zone()),
      tick_counter_(tick_counter) {}

MachineRepresentation MidTierRegisterAllocationData::RepresentationFor(
    int virtual_register) {
  if (virtual_register == InstructionOperand::kInvalidVirtualRegister) {
    return InstructionSequence::DefaultRepresentation();
  } else {
    DCHECK_LT(virtual_register, code()->VirtualRegisterCount());
    return code()->GetRepresentation(virtual_register);
  }
}

// VirtualRegisterData stores data specific to a particular virtual register,
// and tracks spilled operands for that virtual register.
class VirtualRegisterData final {
 public:
  VirtualRegisterData() = default;

  // Define VirtualRegisterData with the type of output that produces this
  // virtual register.
  void DefineAsUnallocatedOperand(int virtual_register, int instr_index);
  void DefineAsFixedSpillOperand(AllocatedOperand* operand,
                                 int virtual_register, int instr_index);
  void DefineAsConstantOperand(ConstantOperand* operand, int instr_index);
  void DefineAsPhi(int virtual_register, int instr_index);

  int vreg() const { return vreg_; }
  int output_instr_index() const { return output_instr_index_; }
  bool is_constant() const { return is_constant_; }

  bool is_phi() const { return is_phi_; }
  void set_is_phi(bool value) { is_phi_ = value; }

 private:
  void Initialize(int virtual_register, InstructionOperand* spill_operand,
                  int instr_index, bool is_phi, bool is_constant);

  InstructionOperand* spill_operand_;
  int output_instr_index_;

  int vreg_;
  bool is_phi_ : 1;
  bool is_constant_ : 1;
};

VirtualRegisterData& MidTierRegisterAllocationData::VirtualRegisterDataFor(
    int virtual_register) {
  DCHECK_GE(virtual_register, 0);
  DCHECK_LT(virtual_register, virtual_register_data_.size());
  return virtual_register_data_[virtual_register];
}

void VirtualRegisterData::Initialize(int virtual_register,
                                     InstructionOperand* spill_operand,
                                     int instr_index, bool is_phi,
                                     bool is_constant) {
  vreg_ = virtual_register;
  spill_operand_ = spill_operand;
  output_instr_index_ = instr_index;
  is_phi_ = is_phi;
  is_constant_ = is_constant;
}

void VirtualRegisterData::DefineAsConstantOperand(ConstantOperand* operand,
                                                  int instr_index) {
  Initialize(operand->virtual_register(), operand, instr_index, false, true);
}

void VirtualRegisterData::DefineAsFixedSpillOperand(AllocatedOperand* operand,
                                                    int virtual_register,
                                                    int instr_index) {
  Initialize(virtual_register, operand, instr_index, false, false);
}

void VirtualRegisterData::DefineAsUnallocatedOperand(int virtual_register,
                                                     int instr_index) {
  Initialize(virtual_register, nullptr, instr_index, false, false);
}

void VirtualRegisterData::DefineAsPhi(int virtual_register, int instr_index) {
  Initialize(virtual_register, nullptr, instr_index, true, false);
}

MidTierRegisterAllocator::MidTierRegisterAllocator(
    MidTierRegisterAllocationData* data)
    : data_(data) {}

MidTierRegisterAllocator::~MidTierRegisterAllocator() = default;

void MidTierRegisterAllocator::DefineOutputs() {
  for (const InstructionBlock* block :
       base::Reversed(code()->instruction_blocks())) {
    data_->tick_counter()->DoTick();

    DefineOutputs(block);
  }
}

void MidTierRegisterAllocator::DefineOutputs(const InstructionBlock* block) {
  int block_start = block->first_instruction_index();
  for (int index = block->last_instruction_index(); index >= block_start;
       index--) {
    Instruction* instr = code()->InstructionAt(index);

    // For each instruction, define details of the output with the associated
    // virtual register data.
    for (size_t i = 0; i < instr->OutputCount(); i++) {
      InstructionOperand* output = instr->OutputAt(i);
      if (output->IsConstant()) {
        ConstantOperand* constant_operand = ConstantOperand::cast(output);
        int virtual_register = constant_operand->virtual_register();
        VirtualRegisterDataFor(virtual_register)
            .DefineAsConstantOperand(constant_operand, index);
      } else {
        DCHECK(output->IsUnallocated());
        UnallocatedOperand* unallocated_operand =
            UnallocatedOperand::cast(output);
        int virtual_register = unallocated_operand->virtual_register();
        if (unallocated_operand->HasFixedSlotPolicy()) {
          // If output has a fixed slot policy, allocate its spill operand now
          // so that the register allocator can use this knowledge.
          MachineRepresentation rep = RepresentationFor(virtual_register);
          AllocatedOperand* fixed_spill_operand = AllocatedOperand::New(
              allocation_zone(), AllocatedOperand::STACK_SLOT, rep,
              unallocated_operand->fixed_slot_index());
          VirtualRegisterDataFor(virtual_register)
              .DefineAsFixedSpillOperand(fixed_spill_operand, virtual_register,
                                         index);
        } else {
          VirtualRegisterDataFor(virtual_register)
              .DefineAsUnallocatedOperand(virtual_register, index);
        }
      }
    }

    // Mark any instructions that require reference maps for later reference map
    // processing.
    if (instr->HasReferenceMap()) {
      data()->reference_map_instructions().push_back(index);
    }
  }

  // Define phi output operands.
  for (PhiInstruction* phi : block->phis()) {
    int virtual_register = phi->virtual_register();
    VirtualRegisterDataFor(virtual_register)
        .DefineAsPhi(virtual_register, block->first_instruction_index());
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
