// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_CONTROL_FLOW_BUILDERS_H_
#define V8_INTERPRETER_CONTROL_FLOW_BUILDERS_H_

#include "src/interpreter/bytecode-array-builder.h"

#include "src/zone-containers.h"

namespace v8 {
namespace internal {
namespace interpreter {

class ControlFlowBuilder BASE_EMBEDDED {
 public:
  explicit ControlFlowBuilder(BytecodeArrayBuilder* builder)
      : builder_(builder) {}
  virtual ~ControlFlowBuilder() {}

 protected:
  BytecodeArrayBuilder* builder() const { return builder_; }

 private:
  BytecodeArrayBuilder* builder_;

  DISALLOW_COPY_AND_ASSIGN(ControlFlowBuilder);
};

// A class to help with co-ordinating break and continue statements with
// their loop.
// TODO(oth): add support for TF branch/merge info.
class LoopBuilder : public ControlFlowBuilder {
 public:
  explicit LoopBuilder(BytecodeArrayBuilder* builder)
      : ControlFlowBuilder(builder),
        continue_sites_(builder->zone()),
        break_sites_(builder->zone()) {}
  ~LoopBuilder();

  // These methods should be called by the LoopBuilder owner before
  // destruction to update sites that emit jumps for break/continue.
  void SetContinueTarget(const BytecodeLabel& continue_target);
  void SetBreakTarget(const BytecodeLabel& break_target);

  // These methods are called when visiting break and continue
  // statements in the AST.  Inserts a jump to a unbound label that is
  // patched when the corresponding SetContinueTarget/SetBreakTarget
  // is called.
  void Break() { EmitJump(&break_sites_); }
  void Continue() { EmitJump(&continue_sites_); }

 private:
  void BindLabels(const BytecodeLabel& target, ZoneVector<BytecodeLabel>* site);
  void EmitJump(ZoneVector<BytecodeLabel>* labels);

  // Unbound labels that identify jumps for continue/break statements
  // in the code.
  ZoneVector<BytecodeLabel> continue_sites_;
  ZoneVector<BytecodeLabel> break_sites_;
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_CONTROL_FLOW_BUILDERS_H_
