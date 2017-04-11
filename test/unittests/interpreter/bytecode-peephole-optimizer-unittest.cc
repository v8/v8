// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/factory.h"
#include "src/interpreter/bytecode-label.h"
#include "src/interpreter/bytecode-peephole-optimizer.h"
#include "src/objects-inl.h"
#include "src/objects.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace interpreter {

class BytecodePeepholeOptimizerTest : public BytecodePipelineStage,
                                      public TestWithIsolateAndZone {
 public:
  BytecodePeepholeOptimizerTest()
      : peephole_optimizer_(this),
        last_written_(BytecodeNode::Illegal(BytecodeSourceInfo())) {}
  ~BytecodePeepholeOptimizerTest() override {}

  void Reset() {
    last_written_ = BytecodeNode::Illegal(BytecodeSourceInfo());
    write_count_ = 0;
  }

  void Write(BytecodeNode* node) override {
    write_count_++;
    last_written_ = *node;
  }

  void WriteJump(BytecodeNode* node, BytecodeLabel* label) override {
    write_count_++;
    last_written_ = *node;
  }

  void BindLabel(BytecodeLabel* label) override {}
  void BindLabel(const BytecodeLabel& target, BytecodeLabel* label) override {}
  Handle<BytecodeArray> ToBytecodeArray(
      Isolate* isolate, int fixed_register_count, int parameter_count,
      Handle<FixedArray> handle_table) override {
    return Handle<BytecodeArray>();
  }

  void Flush() {
    optimizer()->ToBytecodeArray(isolate(), 0, 0,
                                 factory()->empty_fixed_array());
  }

  BytecodePeepholeOptimizer* optimizer() { return &peephole_optimizer_; }

  int write_count() const { return write_count_; }
  const BytecodeNode& last_written() const { return last_written_; }

 private:
  BytecodePeepholeOptimizer peephole_optimizer_;

  int write_count_ = 0;
  BytecodeNode last_written_;
};

// Sanity tests.

TEST_F(BytecodePeepholeOptimizerTest, FlushOnJump) {
  CHECK_EQ(write_count(), 0);

  BytecodeNode add(Bytecode::kAdd, Register(0).ToOperand(), 1);
  optimizer()->Write(&add);
  CHECK_EQ(write_count(), 0);

  BytecodeLabel target;
  BytecodeNode jump(Bytecode::kJump, 0);
  optimizer()->WriteJump(&jump, &target);
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(jump, last_written());
}

TEST_F(BytecodePeepholeOptimizerTest, FlushOnBind) {
  CHECK_EQ(write_count(), 0);

  BytecodeNode add(Bytecode::kAdd, Register(0).ToOperand(), 1);
  optimizer()->Write(&add);
  CHECK_EQ(write_count(), 0);

  BytecodeLabel target;
  optimizer()->BindLabel(&target);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(add, last_written());
}

// Tests covering BytecodePeepholeOptimizer::UpdateLastAndCurrentBytecodes().

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
