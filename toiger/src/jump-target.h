// Copyright 2008 the V8 project authors. All rights reserved.
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

#ifndef V8_JUMP_TARGET_H_
#define V8_JUMP_TARGET_H_

#include "virtual-frame.h"

namespace v8 { namespace internal {

// -------------------------------------------------------------------------
// Jump targets
//
// A jump target is an abstraction of a control-flow target in generated
// code.  It encapsulates an assembler label and an expected virtual frame
// layout at that label.  The first time control flow reaches the target,
// either via jumping or branching or by binding the target, the expected
// frame is set.  If control flow subsequently reaches the target, code may
// be emitted to ensure that the current frame matches the expected frame.
//
// A jump target must have been reached via control flow (either by jumping,
// branching, or falling through) when it is bound.  In particular, this
// means that at least one of the control-flow graph edges reaching the
// target must be a forward edge and must be compiled before any backward
// edges.

class JumpTarget : public ZoneObject {  // Shadows are dynamically allocated.
 public:
  // Construct a jump target with a given code generator used to generate
  // code and to provide access to a current frame.
  explicit JumpTarget(CodeGenerator* cgen);

  // Construct a jump target without a code generator.  A code generator
  // must be supplied before using the jump target as a label.  This is
  // useful, eg, when jump targets are embedded in AST nodes.
  JumpTarget();

  virtual ~JumpTarget() { delete expected_frame_; }

  // Supply a code generator.  This function expects to be given a non-null
  // code generator, and to be called only when the code generator is not
  // yet set.
  void set_code_generator(CodeGenerator* cgen);

  // Accessors.
  CodeGenerator* code_generator() const { return cgen_; }

  Label* label() { return &label_; }

  VirtualFrame* expected_frame() const { return expected_frame_; }
  void set_expected_frame(VirtualFrame* frame) {
    expected_frame_ = frame;
  }

  // Predicates testing the state of the encapsulated label.
  bool is_bound() const { return label_.is_bound(); }
  bool is_linked() const { return label_.is_linked(); }
  bool is_unused() const { return label_.is_unused(); }

  // Treat the jump target as a fresh one---the label is unused and the
  // expected frame if any is reset.
  void Unuse() {
    label_.Unuse();
    delete expected_frame_;
    expected_frame_ = NULL;
  }

  // Emit a jump to the target.  There must be a current frame before the
  // jump and there will be no current frame after the jump.
  void Jump();
  void Jump(Result* arg);

  // Emit a conditional branch to the target.  If there is no current frame,
  // there must be one expected at the target.
  void Branch(Condition cc, Hint hint = no_hint);
  void Branch(Condition cc, Result* arg, Hint hint = no_hint);
  void Branch(Condition cc, Result* arg0, Result* arg1, Hint hint = no_hint);

  // Bind a jump target.  There must be a current frame and no expected
  // frame at the target (targets are only bound once).
  void Bind();
  void Bind(Result* arg);
  void Bind(Result* arg0, Result* arg1);

  // Emit a call to a jump target.  There must be a current frame.  The
  // frame at the target is the same as the current frame except for an
  // extra return address on top of it.
  void Call();

 protected:
  // The encapsulated assembler label.
  Label label_;

  // The expected frame where the label is bound, or NULL.
  VirtualFrame* expected_frame_;

 private:
  // The code generator gives access to the current frame.
  CodeGenerator* cgen_;

  // Used to emit code.
  MacroAssembler* masm_;
};


// -------------------------------------------------------------------------
// Shadow jump targets
//
// Shadow jump targets represent a jump target that is temporarily shadowed
// by another one (represented by the original during shadowing).  They are
// used to catch jumps to labels in certain contexts, e.g. try blocks.
// After shadowing ends, the formerly shadowed target is again represented
// by the original and the ShadowTarget can be used as a jump target in its
// own right, representing the formerly shadowing target.

class ShadowTarget : public JumpTarget {
 public:
  // Construct a shadow a jump target.  After construction, the original
  // jump target shadows the former target, which is hidden as the
  // newly-constructed shadow target.
  explicit ShadowTarget(JumpTarget* original);

  virtual ~ShadowTarget() {
    ASSERT(!is_shadowing_);
  }

  // End shadowing.  After shadowing ends, the original jump target gives
  // access to the formerly shadowed target and the shadow target object
  // gives access to the formerly shadowing target.
  void StopShadowing();

  // During shadowing, the currently shadowing target.  After shadowing, the
  // target that was shadowed.
  JumpTarget* original_target() const { return original_target_; }

 private:
  // During shadowing, the currently shadowing target.  After shadowing, the
  // target that was shadowed.
  JumpTarget* original_target_;

  // During shadowing, the saved state of the shadowed target's label.
  int original_pos_;

  // During shadowing, the saved state of the shadowed target's expected
  // frame.
  VirtualFrame* original_expected_frame_;

#ifdef DEBUG
  bool is_shadowing_;
#endif
};


} }  // namespace v8::internal

#endif  // V8_JUMP_TARGET_H_
