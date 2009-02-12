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
// A jump target is an abstraction of a basic-block entry in generated
// code.  It collects all the virtual frames reaching the block by
// forward jumps and pairs them with labels for the merge code along
// all forward-reaching paths.  When bound, an expected frame for the
// block is determined and code is generated to merge to the expected
// frame.  For backward jumps, the merge code is generated at the edge
// leaving the predecessor block.
//
// A jump target must have been reached via control flow (either by
// jumping, branching, or falling through) at the time it is bound.
// In particular, this means that at least one of the control-flow
// graph edges reaching the target must be a forward edge.

class JumpTarget : public ZoneObject {  // Shadows are dynamically allocated.
 public:
  // Forward-only jump targets can only be reached by forward CFG edges.
  enum Directionality { FORWARD_ONLY, BIDIRECTIONAL };

  // Construct a jump target with a given code generator used to generate
  // code and to provide access to a current frame.
  explicit JumpTarget(CodeGenerator* cgen,
                      Directionality direction = FORWARD_ONLY);

  // Construct a jump target without a code generator.  A code generator
  // must be supplied before using the jump target as a label.  This is
  // useful, eg, when jump targets are embedded in AST nodes.
  JumpTarget();

  virtual ~JumpTarget() { Unuse(); }

  // Supply a code generator and directionality to an already
  // constructed jump target.  This function expects to be given a
  // non-null code generator, and to be called only when the code
  // generator is not yet set.
  void Initialize(CodeGenerator* cgen,
                  Directionality direction = FORWARD_ONLY);

  // Accessors.
  CodeGenerator* code_generator() const { return cgen_; }

  Label* entry_label() { return &entry_label_; }

  VirtualFrame* entry_frame() const { return entry_frame_; }
  void set_entry_frame(VirtualFrame* frame) {
    entry_frame_ = frame;
  }

  // Predicates testing the state of the encapsulated label.
  bool is_bound() const { return is_bound_; }
  bool is_linked() const { return is_linked_; }
  bool is_unused() const { return !is_bound() && !is_linked(); }

  // Treat the jump target as a fresh one.  The expected frame if any
  // will be deallocated and there should be no dangling jumps to the
  // target (thus no reaching frames).
  void Unuse();

  // Reset the internal state of this jump target.  Pointed-to virtual
  // frames are not deallocated and dangling jumps to the target are
  // left dangling.
  void Reset();

  // Copy the state of this jump target to the destination.  The lists
  // of forward-reaching frames and merge-point labels are copied.
  // All virtual frame pointers are copied, not the pointed-to frames.
  // The previous state of the destination is overwritten, without
  // deallocating pointed-to virtual frames.
  void CopyTo(JumpTarget* destination);

  // Emit a jump to the target.  There must be a current frame at the
  // jump and there will be no current frame after the jump.
  void Jump();
  void Jump(Result* arg);
  void Jump(Result* arg0, Result* arg1);
  void Jump(Result* arg0, Result* arg1, Result* arg2);

  // Emit a conditional branch to the target.  There must be a current
  // frame at the branch.  The current frame will fall through to the
  // code after the branch.
  void Branch(Condition cc, Hint hint = no_hint);
  void Branch(Condition cc, Result* arg, Hint hint = no_hint);
  void Branch(Condition cc, Result* arg0, Result* arg1, Hint hint = no_hint);
  void Branch(Condition cc,
              Result* arg0,
              Result* arg1,
              Result* arg2,
              Hint hint = no_hint);
  void Branch(Condition cc,
              Result* arg0,
              Result* arg1,
              Result* arg2,
              Result* arg3,
              Hint hint = no_hint);

  // Bind a jump target.  If there is no current frame at the binding
  // site, there must be at least one frame reaching via a forward
  // jump.  This frame will be used to establish an expected frame for
  // the block, which will be the current frame after the bind.
  void Bind();
  void Bind(Result* arg);
  void Bind(Result* arg0, Result* arg1);
  void Bind(Result* arg0, Result* arg1, Result* arg2);
  void Bind(Result* arg0, Result* arg1, Result* arg2, Result* arg3);

  // Emit a call to a jump target.  There must be a current frame at
  // the call.  The frame at the target is the same as the current
  // frame except for an extra return address on top of it.  The frame
  // after the call is the same as the frame before the call.
  void Call();

 protected:
  // The code generator gives access to its current frame.
  CodeGenerator* cgen_;

  // Used to emit code.
  MacroAssembler* masm_;

 private:
  // Directionality flag set at initialization time.
  Directionality direction_;

  // A list of frames reaching this block via forward jumps.
  List<VirtualFrame*> reaching_frames_;

  // A parallel list of labels for merge code.
  List<Label> merge_labels_;

  // The frame used on entry to the block and expected at backward
  // jumps to the block.  Set when the jump target is bound, but may
  // or may not be set for forward-only blocks.
  VirtualFrame* entry_frame_;

  // The actual entry label of the block.
  Label entry_label_;

  // A target is bound if its Bind member function has been called.
  // It is linked if it is not bound but its Jump, Branch, or Call
  // member functions have been called.
  bool is_bound_;
  bool is_linked_;

  // Add a virtual frame reaching this labeled block via a forward
  // jump, and a fresh label for its merge code.
  void AddReachingFrame(VirtualFrame* frame);

  // Choose an element from a pair of frame elements to be in the
  // expected frame.  Return null if they are incompatible.
  FrameElement* Combine(FrameElement* left, FrameElement* right);

  // Compute a frame to use for entry to this block.
  void ComputeEntryFrame();

  DISALLOW_COPY_AND_ASSIGN(JumpTarget);
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
  // Construct a shadow jump target.  After construction the shadow
  // target object holds the state of the original jump target, and
  // the original target is actually a fresh one that intercepts jumps
  // intended for the shadowed one.
  explicit ShadowTarget(JumpTarget* shadowed);

  virtual ~ShadowTarget() {
    ASSERT(!is_shadowing_);
  }

  // End shadowing.  After shadowing ends, the original jump target
  // again gives access to the formerly shadowed target and the shadow
  // target object gives access to the formerly shadowing target.
  void StopShadowing();

  // During shadowing, the currently shadowing target.  After
  // shadowing, the target that was shadowed.
  JumpTarget* other_target() const { return other_target_; }

 private:
  // During shadowing, the currently shadowing target.  After
  // shadowing, the target that was shadowed.
  JumpTarget* other_target_;

#ifdef DEBUG
  bool is_shadowing_;
#endif

  DISALLOW_COPY_AND_ASSIGN(ShadowTarget);
};


} }  // namespace v8::internal

#endif  // V8_JUMP_TARGET_H_
