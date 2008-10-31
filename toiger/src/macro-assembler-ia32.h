// Copyright 2006-2008 the V8 project authors. All rights reserved.
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

#ifndef V8_MACRO_ASSEMBLER_IA32_H_
#define V8_MACRO_ASSEMBLER_IA32_H_

#include "assembler.h"

namespace v8 { namespace internal {


// Helper types to make flags easier to read at call sites.
enum InvokeFlag {
  CALL_FUNCTION,
  JUMP_FUNCTION
};

enum CodeLocation {
  IN_JAVASCRIPT,
  IN_JS_ENTRY,
  IN_C_ENTRY
};

enum HandlerType {
  TRY_CATCH_HANDLER,
  TRY_FINALLY_HANDLER,
  JS_ENTRY_HANDLER
};


// -------------------------------------------------------------------------
// Virtual frames
//
// The virtual frame is an abstraction of the physical stack frame.  It
// encapsulates the parameters, frame-allocated locals, and the expression
// stack.  It supports push/pop operations on the expression stack, as well
// as random access to the expression stack elements, locals, and
// parameters.

class VirtualFrame : public Malloced {
 public:
  // Construct a virtual frame with the given code generator used to
  // generate code.
  explicit VirtualFrame(CodeGenerator* cgen);

  // Construct a virtual frame that is a clone of an existing one, initially
  // with an identical state.
  explicit VirtualFrame(VirtualFrame* original);

  // The height of the virtual expression stack.  Always non-negative.
  int height() const { return height_; }

  // Forget elements from the top of the expression stack.  This is
  // used when the stack pointer is manually lowered to pop values
  // left by statements (eg, for...in, try...finally) that have been
  // escaped from.
  void Forget(int count);

  // Make this virtual frame have a state identical to an expected virtual
  // frame.  As a side effect, code may be emitted to make this frame match
  // the expected one.
  void MergeTo(VirtualFrame* expected);

  // Emit code for the physical JS entry and exit frame sequences.  After
  // calling Enter, the virtual frame is ready for use; and after calling
  // Exit it should not be used.  Note that Enter does not allocate space in
  // the physical frame for storing frame-allocated locals.
  void Enter();
  void Exit();

  // Allocate and initialize the frame-allocated locals.  The number of
  // locals is known from the frame's code generator's state (specifically
  // its scope).  As a side effect, code may be emitted.
  void AllocateLocals();

  // The current top of the expression stack as an assembly operand.
  Operand Top() const { return Operand(esp, 0); }

  // An element of the expression stack as an assembly operand.
  Operand Element(int index) const {
    return Operand(esp, index * kPointerSize);
  }

  // A frame-allocated local as an assembly operand.
  Operand Local(int index) const {
    ASSERT(0 <= index && index < frame_local_count_);
    return Operand(ebp, kLocal0Offset - index * kPointerSize);
  }

  // The function frame slot.
  Operand Function() const { return Operand(ebp, kFunctionOffset); }

  // The context frame slot.
  Operand Context() const { return Operand(ebp, kContextOffset); }

  // A parameter as an assembly operand.
  Operand Parameter(int index) const {
    ASSERT(-1 <= index && index < parameter_count_);
    return Operand(ebp, (1 + parameter_count_ - index) * kPointerSize);
  }

  // The receiver frame slot.
  Operand Receiver() const { return Parameter(-1); }

  // Push a try-catch or try-finally handler on top of the virtual frame.
  inline void PushTryHandler(HandlerType type);

  // Call a code stub, given the number of arguments it expects on (and
  // removes from) the top of the physical frame.
  inline void CallStub(CodeStub* stub, int frame_arg_count);

  // Call the runtime, given the number of arguments expected on (and
  // removed from) the top of the physical frame.
  inline void CallRuntime(Runtime::Function* f, int frame_arg_count);
  inline void CallRuntime(Runtime::FunctionId id, int frame_arg_count);

  // Invoke a builtin, given the number of arguments it expects on (and
  // removes from) the top of the physical frame.
  inline void InvokeBuiltin(Builtins::JavaScript id,
                            InvokeFlag flag,
                            int frame_arg_count);

  // Call into a JS code object, given the number of arguments it expects on
  // (and removes from) the top of the physical frame.
  inline void CallCode(Handle<Code> ic,
                       RelocInfo::Mode rmode,
                       int frame_arg_count);

  // Drop a number of elements from the top of the expression stack.  May
  // emit code to effect the physical frame.
  inline void Drop(int count);

  // Pop and discard an element from the top of the expression stack.
  // Specifically does not clobber any registers excepting possibly the
  // stack pointer.
  inline void Pop();

  // Pop and save an element from the top of the expression stack.  May emit
  // code.
  inline void Pop(Register reg);
  inline void Pop(Operand operand);

  // Push an element on top of the expression stack.  May emit code.
  inline void Push(Register reg);
  inline void Push(Operand operand);
  inline void Push(Immediate immediate);

 private:
  static const int kLocal0Offset = JavaScriptFrameConstants::kLocal0Offset;
  static const int kFunctionOffset = JavaScriptFrameConstants::kFunctionOffset;
  static const int kContextOffset = StandardFrameConstants::kContextOffset;

  static const int kHandlerSize = StackHandlerConstants::kSize / kPointerSize;

  MacroAssembler* masm_;

  // The number of frame-allocated locals and parameters respectively.
  int frame_local_count_;
  int parameter_count_;

  // The height of the expression stack.
  int height_;

  // The JumpTarget class explicitly sets the height_ field of the expected
  // frame at the actual return target.
  friend class JumpTarget;
};


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
  CodeGenerator* code_generator() const { return code_generator_; }

  MacroAssembler* masm() const { return masm_; }

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

  // True if this jump target is the (non-shadowed) target of the return
  // from the code generator's current function.
  bool IsActualFunctionReturn();

  // Emit a jump to the target.  If there is no expected frame, the code
  // generator's current frame becomes the expected one.  If there is
  // already an expected frame, code may be emitted to merge the current
  // frame to the expected one.  After the jump, the code generate has no
  // current frame (because control flow does not fall through from a jump).
  // A new current frame can be picked up by, eg, binding a jump target with
  // an expected frame.
  void Jump();

  // Emit a conditional branch to the target.  If there is no expected
  // frame, a clone of the code generator's current frame becomes the
  // expected one.  If there is already an expected frame, code may be
  // emitted to merge the current frame to the expected one.
  void Branch(Condition cc, Hint hint = no_hint);

  // Bind a jump target.  If there is no expected frame and there is a
  // current frame (ie, control flow is falling through to the target), then
  // a clone of the current frame becomes the expected one.  If there is a
  // current frame and an expected one (eg, control flow is falling through
  // to a target that has already been reached via a jump or branch), then
  // code may be emitted to merge the frames.  A jump target that already
  // has an expected frame can be bound even if there is no current
  // frame---in that case, the new current frame is picked up from the jump
  // target.
  void Bind();

  // Call a jump target.  A clone of the current frame, with a return
  // address pushed on top of it, becomes the expected frame at the target.
  // The current frame after the site of the call (ie, after the return) is
  // expected to be the same as before the call.  This operation is only
  // supported when there is a current frame and when there is no expected
  // frame at the label.
  void Call();

 protected:
  // The encapsulated assembler label.
  Label label_;

  // The expected frame where the label is bound, or NULL.
  VirtualFrame* expected_frame_;

 private:
  // The code generator gives access to the current frame.
  CodeGenerator* code_generator_;

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


// MacroAssembler implements a collection of frequently used macros.
class MacroAssembler: public Assembler {
 public:
  MacroAssembler(void* buffer, int size);

  // ---------------------------------------------------------------------------
  // GC Support

  // Set the remembered set bit for [object+offset].
  // object is the object being stored into, value is the object being stored.
  // If offset is zero, then the scratch register contains the array index into
  // the elements array represented as a Smi.
  // All registers are clobbered by the operation.
  void RecordWrite(Register object,
                   int offset,
                   Register value,
                   Register scratch);


  // ---------------------------------------------------------------------------
  // Debugger Support

  void SaveRegistersToMemory(RegList regs);
  void RestoreRegistersFromMemory(RegList regs);
  void PushRegistersFromMemory(RegList regs);
  void PopRegistersToMemory(RegList regs);
  void CopyRegistersFromStackToMemory(Register base,
                                      Register scratch,
                                      RegList regs);


  // ---------------------------------------------------------------------------
  // Activation frames

  void EnterInternalFrame() { EnterFrame(StackFrame::INTERNAL); }
  void LeaveInternalFrame() { LeaveFrame(StackFrame::INTERNAL); }

  void EnterConstructFrame() { EnterFrame(StackFrame::CONSTRUCT); }
  void LeaveConstructFrame() { LeaveFrame(StackFrame::CONSTRUCT); }

  // Enter specific kind of exit frame; either EXIT or
  // EXIT_DEBUG. Expects the number of arguments in register eax and
  // sets up the number of arguments in register edi and the pointer
  // to the first argument in register esi.
  void EnterExitFrame(StackFrame::Type type);

  // Leave the current exit frame. Expects the return value in
  // register eax:edx (untouched) and the pointer to the first
  // argument in register esi.
  void LeaveExitFrame(StackFrame::Type type);


  // ---------------------------------------------------------------------------
  // JavaScript invokes

  // Invoke the JavaScript function code by either calling or jumping.
  void InvokeCode(const Operand& code,
                  const ParameterCount& expected,
                  const ParameterCount& actual,
                  InvokeFlag flag);

  void InvokeCode(Handle<Code> code,
                  const ParameterCount& expected,
                  const ParameterCount& actual,
                  RelocInfo::Mode rmode,
                  InvokeFlag flag);

  // Invoke the JavaScript function in the given register. Changes the
  // current context to the context in the function before invoking.
  void InvokeFunction(Register function,
                      const ParameterCount& actual,
                      InvokeFlag flag);

  // Invoke specified builtin JavaScript function. Adds an entry to
  // the unresolved list if the name does not resolve.
  void InvokeBuiltin(Builtins::JavaScript id, InvokeFlag flag);

  // Store the code object for the given builtin in the target register.
  void GetBuiltinEntry(Register target, Builtins::JavaScript id);

  // Expression support
  void Set(Register dst, const Immediate& x);
  void Set(const Operand& dst, const Immediate& x);

  // FCmp is similar to integer cmp, but requires unsigned
  // jcc instructions (je, ja, jae, jb, jbe, je, and jz).
  void FCmp();

  // ---------------------------------------------------------------------------
  // Exception handling

  // Push a new try handler and link into try handler chain.
  // The return address must be pushed before calling this helper.
  // On exit, eax contains TOS (next_sp).
  void PushTryHandler(CodeLocation try_location, HandlerType type);


  // ---------------------------------------------------------------------------
  // Inline caching support

  // Generates code that verifies that the maps of objects in the
  // prototype chain of object hasn't changed since the code was
  // generated and branches to the miss label if any map has. If
  // necessary the function also generates code for security check
  // in case of global object holders. The scratch and holder
  // registers are always clobbered, but the object register is only
  // clobbered if it the same as the holder register. The function
  // returns a register containing the holder - either object_reg or
  // holder_reg.
  Register CheckMaps(JSObject* object, Register object_reg,
                     JSObject* holder, Register holder_reg,
                     Register scratch, Label* miss);

  // Generate code for checking access rights - used for security checks
  // on access to global objects across environments. The holder register
  // is left untouched, but the scratch register is clobbered.
  void CheckAccessGlobalProxy(Register holder_reg,
                              Register scratch,
                              Label* miss);


  // ---------------------------------------------------------------------------
  // Support functions.

  // Check if result is zero and op is negative.
  void NegativeZeroTest(Register result, Register op, Label* then_label);

  // Check if result is zero and any of op1 and op2 are negative.
  // Register scratch is destroyed, and it must be different from op2.
  void NegativeZeroTest(Register result, Register op1, Register op2,
                        Register scratch, Label* then_label);

  // Try to get function prototype of a function and puts the value in
  // the result register. Checks that the function really is a
  // function and jumps to the miss label if the fast checks fail. The
  // function register will be untouched; the other registers may be
  // clobbered.
  void TryGetFunctionPrototype(Register function,
                               Register result,
                               Register scratch,
                               Label* miss);

  // Generates code for reporting that an illegal operation has
  // occurred.
  void IllegalOperation(int num_arguments);

  // ---------------------------------------------------------------------------
  // Runtime calls

  // Call a code stub.
  void CallStub(CodeStub* stub);

  // Return from a code stub after popping its arguments.
  void StubReturn(int argc);

  // Call a runtime routine.
  // Eventually this should be used for all C calls.
  void CallRuntime(Runtime::Function* f, int num_arguments);

  // Convenience function: Same as above, but takes the fid instead.
  void CallRuntime(Runtime::FunctionId id, int num_arguments);

  // Tail call of a runtime routine (jump).
  // Like JumpToBuiltin, but also takes care of passing the number
  // of arguments.
  void TailCallRuntime(const ExternalReference& ext, int num_arguments);

  // Jump to the builtin routine.
  void JumpToBuiltin(const ExternalReference& ext);


  // ---------------------------------------------------------------------------
  // Utilities

  void Ret();

  struct Unresolved {
    int pc;
    uint32_t flags;  // see Bootstrapper::FixupFlags decoders/encoders.
    const char* name;
  };
  List<Unresolved>* unresolved() { return &unresolved_; }


  // ---------------------------------------------------------------------------
  // StatsCounter support

  void SetCounter(StatsCounter* counter, int value);
  void IncrementCounter(StatsCounter* counter, int value);
  void DecrementCounter(StatsCounter* counter, int value);


  // ---------------------------------------------------------------------------
  // Debugging

  // Calls Abort(msg) if the condition cc is not satisfied.
  // Use --debug_code to enable.
  void Assert(Condition cc, const char* msg);

  // Like Assert(), but always enabled.
  void Check(Condition cc, const char* msg);

  // Print a message to stdout and abort execution.
  void Abort(const char* msg);

  // Verify restrictions about code generated in stubs.
  void set_generating_stub(bool value) { generating_stub_ = value; }
  bool generating_stub() { return generating_stub_; }
  void set_allow_stub_calls(bool value) { allow_stub_calls_ = value; }
  bool allow_stub_calls() { return allow_stub_calls_; }

 private:
  List<Unresolved> unresolved_;
  bool generating_stub_;
  bool allow_stub_calls_;

  // Helper functions for generating invokes.
  void InvokePrologue(const ParameterCount& expected,
                      const ParameterCount& actual,
                      Handle<Code> code_constant,
                      const Operand& code_operand,
                      Label* done,
                      InvokeFlag flag);

  // Get the code for the given builtin. Returns if able to resolve
  // the function in the 'resolved' flag.
  Handle<Code> ResolveBuiltin(Builtins::JavaScript id, bool* resolved);

  // Activation support.
  void EnterFrame(StackFrame::Type type);
  void LeaveFrame(StackFrame::Type type);
};


// The code patcher is used to patch (typically) small parts of code e.g. for
// debugging and other types of instrumentation. When using the code patcher
// the exact number of bytes specified must be emitted. Is not legal to emit
// relocation information. If any of these constraints are violated it causes
// an assertion.
class CodePatcher {
 public:
  CodePatcher(byte* address, int size);
  virtual ~CodePatcher();

  // Macro assembler to emit code.
  MacroAssembler* masm() { return &masm_; }

 private:
  byte* address_;  // The address of the code being patched.
  int size_;  // Number of bytes of the expected patch size.
  MacroAssembler masm_;  // Macro assembler used to generate the code.
};

// -----------------------------------------------------------------------------
// Static helper functions.

// Generate an Operand for loading a field from an object.
static inline Operand FieldOperand(Register object, int offset) {
  return Operand(object, offset - kHeapObjectTag);
}


// Generate an Operand for loading an indexed field from an object.
static inline Operand FieldOperand(Register object,
                                   Register index,
                                   ScaleFactor scale,
                                   int offset) {
  return Operand(object, index, scale, offset - kHeapObjectTag);
}


// -------------------------------------------------------------------------
// VirtualFrame inline functions.

#define __ masm_->

void VirtualFrame::PushTryHandler(HandlerType type) {
  // Grow the expression stack by handler size less two (the return address
  // is already pushed by a call instruction, and PushTryHandler from the
  // macro assembler will leave the top of stack in the eax register to be
  // pushed separately).
  height_ += (kHandlerSize - 2);
  __ PushTryHandler(IN_JAVASCRIPT, type);
  // TODO(1222589): remove the reliance of PushTryHandler on a cached TOS
  Push(eax);
}


void VirtualFrame::CallStub(CodeStub* stub, int frame_arg_count) {
  ASSERT(frame_arg_count >= 0);
  ASSERT(height_ >= frame_arg_count);
  height_ -= frame_arg_count;
  __ CallStub(stub);
}


void VirtualFrame::CallRuntime(Runtime::Function* f, int frame_arg_count) {
  ASSERT(frame_arg_count >= 0);
  ASSERT(height_ >= frame_arg_count);
  height_ -= frame_arg_count;
  __ CallRuntime(f, frame_arg_count);
}


void VirtualFrame::CallRuntime(Runtime::FunctionId id, int frame_arg_count) {
  ASSERT(frame_arg_count >= 0);
  ASSERT(height_ >= frame_arg_count);
  height_ -= frame_arg_count;
  __ CallRuntime(id, frame_arg_count);
}


void VirtualFrame::InvokeBuiltin(Builtins::JavaScript id,
                                 InvokeFlag flag,
                                 int frame_arg_count) {
  ASSERT(frame_arg_count >= 0);
  ASSERT(height_ >= frame_arg_count);
  height_ -= frame_arg_count;
  __ InvokeBuiltin(id, flag);
}


void VirtualFrame::CallCode(Handle<Code> code,
                            RelocInfo::Mode rmode,
                            int frame_arg_count) {
  ASSERT(frame_arg_count >= 0);
  ASSERT(height_ >= frame_arg_count);
  height_ -= frame_arg_count;
  __ call(code, rmode);
}


void VirtualFrame::Drop(int count) {
  ASSERT(count >= 0);
  ASSERT(height_ >= count);
  if (count > 0) {
    __ add(Operand(esp), Immediate(count * kPointerSize));
    height_ -= count;
  }
}


void VirtualFrame::Pop() {
  ASSERT(height_ > 0);
  __ add(Operand(esp), Immediate(kPointerSize));
  height_--;
}


void VirtualFrame::Pop(Register reg) {
  ASSERT(height_ > 0);
  __ pop(reg);
  height_--;
}


void VirtualFrame::Pop(Operand operand) {
  ASSERT(height_ > 0);
  __ pop(operand);
  height_--;
}


void VirtualFrame::Push(Register reg) {
  height_++;
  __ push(reg);
}


void VirtualFrame::Push(Operand operand) {
  height_++;
  __ push(operand);
}


void VirtualFrame::Push(Immediate immediate) {
  height_++;
  __ push(immediate);
}

#undef __

} }  // namespace v8::internal

#endif  // V8_MACRO_ASSEMBLER_IA32_H_
