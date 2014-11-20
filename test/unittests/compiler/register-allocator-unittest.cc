// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/utils/random-number-generator.h"
#include "src/compiler/instruction.h"
#include "src/compiler/pipeline.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace v8 {
namespace internal {
namespace compiler {

typedef BasicBlock::RpoNumber Rpo;

static const char*
    general_register_names_[RegisterConfiguration::kMaxGeneralRegisters];
static const char*
    double_register_names_[RegisterConfiguration::kMaxDoubleRegisters];
static char register_names_[10 * (RegisterConfiguration::kMaxGeneralRegisters +
                                  RegisterConfiguration::kMaxDoubleRegisters)];

static void InitializeRegisterNames() {
  char* loc = register_names_;
  for (int i = 0; i < RegisterConfiguration::kMaxGeneralRegisters; ++i) {
    general_register_names_[i] = loc;
    loc += base::OS::SNPrintF(loc, 100, "gp_%d", i);
    *loc++ = 0;
  }
  for (int i = 0; i < RegisterConfiguration::kMaxDoubleRegisters; ++i) {
    double_register_names_[i] = loc;
    loc += base::OS::SNPrintF(loc, 100, "fp_%d", i) + 1;
    *loc++ = 0;
  }
}


class RegisterAllocatorTest : public TestWithZone {
 public:
  static const int kDefaultNRegs = 4;
  static const int kNoValue = kMinInt;

  struct VReg {
    VReg() : value_(kNoValue) {}
    VReg(PhiInstruction* phi) : value_(phi->virtual_register()) {}  // NOLINT
    explicit VReg(int value) : value_(value) {}
    int value_;
  };

  enum TestOperandType {
    kInvalid,
    kSameAsFirst,
    kRegister,
    kFixedRegister,
    kSlot,
    kFixedSlot,
    kImmediate,
    kNone
  };

  struct TestOperand {
    TestOperand() : type_(kInvalid), vreg_(), value_(kNoValue) {}
    TestOperand(TestOperandType type, int imm)
        : type_(type), vreg_(), value_(imm) {}
    TestOperand(TestOperandType type, VReg vreg, int value = kNoValue)
        : type_(type), vreg_(vreg), value_(value) {}

    TestOperandType type_;
    VReg vreg_;
    int value_;
  };

  static TestOperand Same() { return TestOperand(kSameAsFirst, VReg()); }

  static TestOperand Reg(VReg vreg, int index = kNoValue) {
    TestOperandType type = kRegister;
    if (index != kNoValue) type = kFixedRegister;
    return TestOperand(type, vreg, index);
  }

  static TestOperand Reg(int index = kNoValue) { return Reg(VReg(), index); }

  static TestOperand Slot(VReg vreg, int index = kNoValue) {
    TestOperandType type = kSlot;
    if (index != kNoValue) type = kFixedSlot;
    return TestOperand(type, vreg, index);
  }

  static TestOperand Slot(int index = kNoValue) { return Slot(VReg(), index); }

  static TestOperand Use(VReg vreg) { return TestOperand(kNone, vreg); }

  static TestOperand Use() { return Use(VReg()); }

  enum BlockCompletionType { kBlockEnd, kFallThrough, kBranch, kJump };

  struct BlockCompletion {
    BlockCompletionType type_;
    TestOperand op_;
    int offset_0_;
    int offset_1_;
  };

  static BlockCompletion FallThrough() {
    BlockCompletion completion = {kFallThrough, TestOperand(), 1, kNoValue};
    return completion;
  }

  static BlockCompletion Jump(int offset) {
    BlockCompletion completion = {kJump, TestOperand(), offset, kNoValue};
    return completion;
  }

  static BlockCompletion Branch(TestOperand op, int left_offset,
                                int right_offset) {
    BlockCompletion completion = {kBranch, op, left_offset, right_offset};
    return completion;
  }

  static BlockCompletion Last() {
    BlockCompletion completion = {kBlockEnd, TestOperand(), kNoValue, kNoValue};
    return completion;
  }

  RegisterAllocatorTest()
      : frame_(nullptr),
        sequence_(nullptr),
        num_general_registers_(kDefaultNRegs),
        num_double_registers_(kDefaultNRegs),
        instruction_blocks_(zone()),
        current_instruction_index_(-1),
        current_block_(nullptr),
        block_returns_(false) {
    InitializeRegisterNames();
  }

  void SetNumRegs(int num_general_registers, int num_double_registers) {
    CHECK(config_.is_empty());
    CHECK(instructions_.empty());
    CHECK(instruction_blocks_.empty());
    num_general_registers_ = num_general_registers;
    num_double_registers_ = num_double_registers;
  }

  RegisterConfiguration* config() {
    if (config_.is_empty()) {
      config_.Reset(new RegisterConfiguration(
          num_general_registers_, num_double_registers_, num_double_registers_,
          general_register_names_, double_register_names_));
    }
    return config_.get();
  }

  Frame* frame() {
    if (frame_ == nullptr) {
      frame_ = new (zone()) Frame();
    }
    return frame_;
  }

  InstructionSequence* sequence() {
    if (sequence_ == nullptr) {
      sequence_ =
          new (zone()) InstructionSequence(zone(), &instruction_blocks_);
    }
    return sequence_;
  }

  void StartLoop(int loop_blocks) {
    CHECK(current_block_ == nullptr);
    if (!loop_blocks_.empty()) {
      CHECK(!loop_blocks_.back().loop_header_.IsValid());
    }
    LoopData loop_data = {Rpo::Invalid(), loop_blocks};
    loop_blocks_.push_back(loop_data);
  }

  void EndLoop() {
    CHECK(current_block_ == nullptr);
    CHECK(!loop_blocks_.empty());
    CHECK_EQ(0, loop_blocks_.back().expected_blocks_);
    loop_blocks_.pop_back();
  }

  void StartBlock() {
    block_returns_ = false;
    NewBlock();
  }

  int EndBlock(BlockCompletion completion = FallThrough()) {
    int instruction_index = kMinInt;
    if (block_returns_) {
      CHECK(completion.type_ == kBlockEnd || completion.type_ == kFallThrough);
      completion.type_ = kBlockEnd;
    }
    switch (completion.type_) {
      case kBlockEnd:
        break;
      case kFallThrough:
        instruction_index = EmitFallThrough();
        break;
      case kJump:
        CHECK(!block_returns_);
        instruction_index = EmitJump();
        break;
      case kBranch:
        CHECK(!block_returns_);
        instruction_index = EmitBranch(completion.op_);
        break;
    }
    completions_.push_back(completion);
    CHECK(current_block_ != nullptr);
    sequence()->EndBlock(current_block_->rpo_number());
    current_block_ = nullptr;
    return instruction_index;
  }

  void Allocate() {
    CHECK_EQ(nullptr, current_block_);
    WireBlocks();
    Pipeline::AllocateRegistersForTesting(config(), sequence(), true);
  }

  TestOperand Imm(int32_t imm = 0) {
    int index = sequence()->AddImmediate(Constant(imm));
    return TestOperand(kImmediate, index);
  }

  VReg Parameter(TestOperand output_op = Reg()) {
    VReg vreg = NewReg();
    InstructionOperand* outputs[1]{ConvertOutputOp(vreg, output_op)};
    Emit(vreg.value_, kArchNop, 1, outputs);
    return vreg;
  }

  int Return(TestOperand input_op_0) {
    block_returns_ = true;
    InstructionOperand* inputs[1]{ConvertInputOp(input_op_0)};
    return Emit(NewIndex(), kArchRet, 0, nullptr, 1, inputs);
  }

  int Return(VReg vreg) { return Return(Reg(vreg, 0)); }

  PhiInstruction* Phi(VReg incoming_vreg) {
    PhiInstruction* phi =
        new (zone()) PhiInstruction(zone(), NewReg().value_, 10);
    phi->Extend(zone(), incoming_vreg.value_);
    current_block_->AddPhi(phi);
    return phi;
  }

  PhiInstruction* Phi(VReg incoming_vreg_0, VReg incoming_vreg_1) {
    auto phi = Phi(incoming_vreg_0);
    Extend(phi, incoming_vreg_1);
    return phi;
  }

  void Extend(PhiInstruction* phi, VReg vreg) {
    phi->Extend(zone(), vreg.value_);
  }

  VReg DefineConstant(int32_t imm = 0) {
    VReg vreg = NewReg();
    sequence()->AddConstant(vreg.value_, Constant(imm));
    InstructionOperand* outputs[1]{
        ConstantOperand::Create(vreg.value_, zone())};
    Emit(vreg.value_, kArchNop, 1, outputs);
    return vreg;
  }

  VReg EmitOII(TestOperand output_op, TestOperand input_op_0,
               TestOperand input_op_1) {
    VReg output_vreg = NewReg();
    InstructionOperand* outputs[1]{ConvertOutputOp(output_vreg, output_op)};
    InstructionOperand* inputs[2]{ConvertInputOp(input_op_0),
                                  ConvertInputOp(input_op_1)};
    Emit(output_vreg.value_, kArchNop, 1, outputs, 2, inputs);
    return output_vreg;
  }

  VReg EmitCall(TestOperand output_op, size_t input_size, TestOperand* inputs) {
    VReg output_vreg = NewReg();
    InstructionOperand* outputs[1]{ConvertOutputOp(output_vreg, output_op)};
    InstructionOperand** mapped_inputs =
        zone()->NewArray<InstructionOperand*>(static_cast<int>(input_size));
    for (size_t i = 0; i < input_size; ++i) {
      mapped_inputs[i] = ConvertInputOp(inputs[i]);
    }
    Emit(output_vreg.value_, kArchCallCodeObject, 1, outputs, input_size,
         mapped_inputs);
    return output_vreg;
  }

  // Get defining instruction vreg or value returned at instruction creation
  // time when there is no return value.
  const Instruction* GetInstruction(int instruction_index) {
    auto it = instructions_.find(instruction_index);
    CHECK(it != instructions_.end());
    return it->second;
  }

 private:
  VReg NewReg() { return VReg(sequence()->NextVirtualRegister()); }
  int NewIndex() { return current_instruction_index_--; }

  static TestOperand Invalid() { return TestOperand(kInvalid, VReg()); }

  int EmitBranch(TestOperand input_op) {
    InstructionOperand* inputs[4]{ConvertInputOp(input_op),
                                  ConvertInputOp(Imm()), ConvertInputOp(Imm()),
                                  ConvertInputOp(Imm())};
    InstructionCode opcode = kArchJmp | FlagsModeField::encode(kFlags_branch) |
                             FlagsConditionField::encode(kEqual);
    auto instruction =
        NewInstruction(opcode, 0, nullptr, 4, inputs)->MarkAsControl();
    return AddInstruction(NewIndex(), instruction);
  }

  int EmitFallThrough() {
    auto instruction = NewInstruction(kArchNop, 0, nullptr)->MarkAsControl();
    return AddInstruction(NewIndex(), instruction);
  }

  int EmitJump() {
    InstructionOperand* inputs[1]{ConvertInputOp(Imm())};
    auto instruction =
        NewInstruction(kArchJmp, 0, nullptr, 1, inputs)->MarkAsControl();
    return AddInstruction(NewIndex(), instruction);
  }

  Instruction* NewInstruction(InstructionCode code, size_t outputs_size,
                              InstructionOperand** outputs,
                              size_t inputs_size = 0,
                              InstructionOperand* *inputs = nullptr,
                              size_t temps_size = 0,
                              InstructionOperand* *temps = nullptr) {
    CHECK_NE(nullptr, current_block_);
    return Instruction::New(zone(), code, outputs_size, outputs, inputs_size,
                            inputs, temps_size, temps);
  }

  InstructionOperand* Unallocated(TestOperand op,
                                  UnallocatedOperand::ExtendedPolicy policy) {
    auto unallocated = new (zone()) UnallocatedOperand(policy);
    unallocated->set_virtual_register(op.vreg_.value_);
    return unallocated;
  }

  InstructionOperand* Unallocated(TestOperand op,
                                  UnallocatedOperand::ExtendedPolicy policy,
                                  UnallocatedOperand::Lifetime lifetime) {
    auto unallocated = new (zone()) UnallocatedOperand(policy, lifetime);
    unallocated->set_virtual_register(op.vreg_.value_);
    return unallocated;
  }

  InstructionOperand* Unallocated(TestOperand op,
                                  UnallocatedOperand::ExtendedPolicy policy,
                                  int index) {
    auto unallocated = new (zone()) UnallocatedOperand(policy, index);
    unallocated->set_virtual_register(op.vreg_.value_);
    return unallocated;
  }

  InstructionOperand* Unallocated(TestOperand op,
                                  UnallocatedOperand::BasicPolicy policy,
                                  int index) {
    auto unallocated = new (zone()) UnallocatedOperand(policy, index);
    unallocated->set_virtual_register(op.vreg_.value_);
    return unallocated;
  }

  InstructionOperand* ConvertInputOp(TestOperand op) {
    if (op.type_ == kImmediate) {
      CHECK_EQ(op.vreg_.value_, kNoValue);
      return ImmediateOperand::Create(op.value_, zone());
    }
    CHECK_NE(op.vreg_.value_, kNoValue);
    switch (op.type_) {
      case kNone:
        return Unallocated(op, UnallocatedOperand::NONE,
                           UnallocatedOperand::USED_AT_START);
      case kRegister:
        return Unallocated(op, UnallocatedOperand::MUST_HAVE_REGISTER,
                           UnallocatedOperand::USED_AT_START);
      case kFixedRegister:
        CHECK(0 <= op.value_ && op.value_ < num_general_registers_);
        return Unallocated(op, UnallocatedOperand::FIXED_REGISTER, op.value_);
      default:
        break;
    }
    CHECK(false);
    return NULL;
  }

  InstructionOperand* ConvertOutputOp(VReg vreg, TestOperand op) {
    CHECK_EQ(op.vreg_.value_, kNoValue);
    op.vreg_ = vreg;
    switch (op.type_) {
      case kSameAsFirst:
        return Unallocated(op, UnallocatedOperand::SAME_AS_FIRST_INPUT);
      case kRegister:
        return Unallocated(op, UnallocatedOperand::MUST_HAVE_REGISTER);
      case kFixedSlot:
        return Unallocated(op, UnallocatedOperand::FIXED_SLOT, op.value_);
      case kFixedRegister:
        CHECK(0 <= op.value_ && op.value_ < num_general_registers_);
        return Unallocated(op, UnallocatedOperand::FIXED_REGISTER, op.value_);
      default:
        break;
    }
    CHECK(false);
    return NULL;
  }

  InstructionBlock* NewBlock() {
    CHECK(current_block_ == nullptr);
    auto block_id = BasicBlock::Id::FromSize(instruction_blocks_.size());
    Rpo rpo = Rpo::FromInt(block_id.ToInt());
    Rpo loop_header = Rpo::Invalid();
    Rpo loop_end = Rpo::Invalid();
    if (!loop_blocks_.empty()) {
      auto& loop_data = loop_blocks_.back();
      // This is a loop header.
      if (!loop_data.loop_header_.IsValid()) {
        loop_end = Rpo::FromInt(block_id.ToInt() + loop_data.expected_blocks_);
        loop_data.expected_blocks_--;
        loop_data.loop_header_ = rpo;
      } else {
        // This is a loop body.
        CHECK_NE(0, loop_data.expected_blocks_);
        // TODO(dcarney): handle nested loops.
        loop_data.expected_blocks_--;
        loop_header = loop_data.loop_header_;
      }
    }
    // Construct instruction block.
    auto instruction_block = new (zone()) InstructionBlock(
        zone(), block_id, rpo, rpo, loop_header, loop_end, false);
    instruction_blocks_.push_back(instruction_block);
    current_block_ = instruction_block;
    sequence()->StartBlock(rpo);
    return instruction_block;
  }

  void WireBlocks() {
    CHECK(instruction_blocks_.size() == completions_.size());
    size_t offset = 0;
    for (const auto& completion : completions_) {
      switch (completion.type_) {
        case kBlockEnd:
          break;
        case kFallThrough:  // Fallthrough.
        case kJump:
          WireBlock(offset, completion.offset_0_);
          break;
        case kBranch:
          WireBlock(offset, completion.offset_0_);
          WireBlock(offset, completion.offset_1_);
          break;
      }
      ++offset;
    }
  }

  void WireBlock(size_t block_offset, int jump_offset) {
    size_t target_block_offset =
        block_offset + static_cast<size_t>(jump_offset);
    CHECK(block_offset < instruction_blocks_.size());
    CHECK(target_block_offset < instruction_blocks_.size());
    auto block = instruction_blocks_[block_offset];
    auto target = instruction_blocks_[target_block_offset];
    block->successors().push_back(target->rpo_number());
    target->predecessors().push_back(block->rpo_number());
  }

  int Emit(int instruction_index, InstructionCode code, size_t outputs_size,
           InstructionOperand** outputs, size_t inputs_size = 0,
           InstructionOperand* *inputs = nullptr, size_t temps_size = 0,
           InstructionOperand* *temps = nullptr) {
    auto instruction = NewInstruction(code, outputs_size, outputs, inputs_size,
                                      inputs, temps_size, temps);
    return AddInstruction(instruction_index, instruction);
  }

  int AddInstruction(int instruction_index, Instruction* instruction) {
    sequence()->AddInstruction(instruction);
    return instruction_index;
  }

  struct LoopData {
    Rpo loop_header_;
    int expected_blocks_;
  };

  typedef std::vector<LoopData> LoopBlocks;
  typedef std::map<int, const Instruction*> Instructions;
  typedef std::vector<BlockCompletion> Completions;

  SmartPointer<RegisterConfiguration> config_;
  Frame* frame_;
  InstructionSequence* sequence_;
  int num_general_registers_;
  int num_double_registers_;

  // Block building state.
  InstructionBlocks instruction_blocks_;
  Instructions instructions_;
  int current_instruction_index_;
  Completions completions_;
  LoopBlocks loop_blocks_;
  InstructionBlock* current_block_;
  bool block_returns_;
};


TEST_F(RegisterAllocatorTest, CanAllocateThreeRegisters) {
  // return p0 + p1;
  StartBlock();
  auto a_reg = Parameter();
  auto b_reg = Parameter();
  auto c_reg = EmitOII(Reg(1), Reg(a_reg, 1), Reg(b_reg, 0));
  Return(c_reg);
  EndBlock(Last());

  Allocate();
}


TEST_F(RegisterAllocatorTest, SimpleLoop) {
  // i = K;
  // while(true) { i++ }
  StartBlock();
  auto i_reg = DefineConstant();
  EndBlock();

  {
    StartLoop(1);

    StartBlock();
    auto phi = Phi(i_reg);
    auto ipp = EmitOII(Same(), Reg(phi), Use(DefineConstant()));
    Extend(phi, ipp);
    EndBlock(Jump(0));

    EndLoop();
  }

  Allocate();
}


TEST_F(RegisterAllocatorTest, SimpleBranch) {
  // return i ? K1 : K2
  StartBlock();
  auto i = DefineConstant();
  EndBlock(Branch(Reg(i), 1, 2));

  StartBlock();
  Return(DefineConstant());
  EndBlock(Last());

  StartBlock();
  Return(DefineConstant());
  EndBlock(Last());

  Allocate();
}


TEST_F(RegisterAllocatorTest, SimpleDiamond) {
  // return p0 ? p0 : p0
  StartBlock();
  auto param = Parameter();
  EndBlock(Branch(Reg(param), 1, 2));

  StartBlock();
  EndBlock(Jump(2));

  StartBlock();
  EndBlock(Jump(1));

  StartBlock();
  Return(param);
  EndBlock();

  Allocate();
}


TEST_F(RegisterAllocatorTest, SimpleDiamondPhi) {
  // return i ? K1 : K2
  StartBlock();
  EndBlock(Branch(Reg(DefineConstant()), 1, 2));

  StartBlock();
  auto t_val = DefineConstant();
  EndBlock(Jump(2));

  StartBlock();
  auto f_val = DefineConstant();
  EndBlock(Jump(1));

  StartBlock();
  Return(Reg(Phi(t_val, f_val)));
  EndBlock();

  Allocate();
}


TEST_F(RegisterAllocatorTest, DiamondManyPhis) {
  const int kPhis = kDefaultNRegs * 2;

  StartBlock();
  EndBlock(Branch(Reg(DefineConstant()), 1, 2));

  StartBlock();
  VReg t_vals[kPhis];
  for (int i = 0; i < kPhis; ++i) {
    t_vals[i] = DefineConstant();
  }
  EndBlock(Jump(2));

  StartBlock();
  VReg f_vals[kPhis];
  for (int i = 0; i < kPhis; ++i) {
    f_vals[i] = DefineConstant();
  }
  EndBlock(Jump(1));

  StartBlock();
  TestOperand merged[kPhis];
  for (int i = 0; i < kPhis; ++i) {
    merged[i] = Use(Phi(t_vals[i], f_vals[i]));
  }
  Return(EmitCall(Reg(), kPhis, merged));
  EndBlock();

  Allocate();
}


TEST_F(RegisterAllocatorTest, DoubleDiamondManyRedundantPhis) {
  const int kPhis = kDefaultNRegs * 2;

  // First diamond.
  StartBlock();
  VReg vals[kPhis];
  for (int i = 0; i < kPhis; ++i) {
    vals[i] = Parameter(Slot(i));
  }
  EndBlock(Branch(Reg(DefineConstant()), 1, 2));

  StartBlock();
  EndBlock(Jump(2));

  StartBlock();
  EndBlock(Jump(1));

  // Second diamond.
  StartBlock();
  EndBlock(Branch(Reg(DefineConstant()), 1, 2));

  StartBlock();
  EndBlock(Jump(2));

  StartBlock();
  EndBlock(Jump(1));

  StartBlock();
  TestOperand merged[kPhis];
  for (int i = 0; i < kPhis; ++i) {
    merged[i] = Use(Phi(vals[i], vals[i]));
  }
  Return(EmitCall(Reg(), kPhis, merged));
  EndBlock();

  Allocate();
}


TEST_F(RegisterAllocatorTest, RegressionPhisNeedTooManyRegisters) {
  const size_t kNumRegs = 3;
  const size_t kParams = kNumRegs + 1;
  // Override number of registers.
  SetNumRegs(kNumRegs, kNumRegs);

  StartBlock();
  auto constant = DefineConstant();
  VReg parameters[kParams];
  for (size_t i = 0; i < arraysize(parameters); ++i) {
    parameters[i] = DefineConstant();
  }
  EndBlock();

  PhiInstruction* phis[kParams];
  {
    StartLoop(2);

    // Loop header.
    StartBlock();

    for (size_t i = 0; i < arraysize(parameters); ++i) {
      phis[i] = Phi(parameters[i]);
    }

    // Perform some computations.
    // something like phi[i] += const
    for (size_t i = 0; i < arraysize(parameters); ++i) {
      auto result = EmitOII(Same(), Reg(phis[i]), Use(constant));
      Extend(phis[i], result);
    }

    EndBlock(Branch(Reg(DefineConstant()), 1, 2));

    // Jump back to loop header.
    StartBlock();
    EndBlock(Jump(-1));

    EndLoop();
  }

  StartBlock();
  Return(DefineConstant());
  EndBlock();

  Allocate();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
