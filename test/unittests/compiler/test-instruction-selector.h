// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_COMPILER_INSTRUCTION_SELECTOR_TEST_H_
#define V8_UNITTESTS_COMPILER_INSTRUCTION_SELECTOR_TEST_H_

#include <deque>

#include "src/compiler/instruction-selector.h"
#include "src/compiler/raw-machine-assembler.h"
#include "test/unittests/test-zone.h"

namespace v8 {
namespace internal {
namespace compiler {

class InstructionSelectorTest : public ContextTest, public ZoneTest {
 public:
  InstructionSelectorTest() {}

 protected:
  class Stream;

  enum StreamBuilderMode { kAllInstructions, kTargetInstructions };

  class StreamBuilder : public RawMachineAssembler {
   public:
    StreamBuilder(InstructionSelectorTest* test,
                  MachineRepresentation return_type)
        : RawMachineAssembler(new (test->zone()) Graph(test->zone()),
                              CallDescriptorBuilder(test->zone(), return_type)),
          test_(test) {}

    Stream Build(StreamBuilderMode mode = kTargetInstructions);

   private:
    MachineCallDescriptorBuilder* CallDescriptorBuilder(
        Zone* zone, MachineRepresentation return_type) {
      return new (zone) MachineCallDescriptorBuilder(return_type, 0, NULL);
    }

   private:
    InstructionSelectorTest* test_;
  };

  class Stream {
   public:
    size_t size() const { return instructions_.size(); }
    const Instruction* operator[](size_t index) const {
      EXPECT_LT(index, size());
      return instructions_[index];
    }

    int32_t ToInt32(const InstructionOperand* operand) const {
      return ToConstant(operand).ToInt32();
    }

   private:
    Constant ToConstant(const InstructionOperand* operand) const {
      ConstantMap::const_iterator i;
      if (operand->IsConstant()) {
        i = constants_.find(operand->index());
        EXPECT_NE(constants_.end(), i);
      } else {
        EXPECT_EQ(InstructionOperand::IMMEDIATE, operand->kind());
        i = immediates_.find(operand->index());
        EXPECT_NE(immediates_.end(), i);
      }
      EXPECT_EQ(operand->index(), i->first);
      return i->second;
    }

    friend class StreamBuilder;

    typedef std::map<int, Constant> ConstantMap;

    ConstantMap constants_;
    ConstantMap immediates_;
    std::deque<Instruction*> instructions_;
  };
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_UNITTESTS_COMPILER_INSTRUCTION_SELECTOR_TEST_H_
