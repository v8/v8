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

#ifndef V8_VIRTUAL_FRAME_IA32_H_
#define V8_VIRTUAL_FRAME_IA32_H_

#include "macro-assembler.h"

namespace v8 { namespace internal {

// -------------------------------------------------------------------------
// Virtual frame elements
//
// The internal elements of the virtual frames.  Elements are (currently) of
// only one kind, in-memory.  Their actual location is given by their
// position in the virtual frame.

class Element BASE_EMBEDDED {
 public:
  Element() {}

  bool matches(const Element& other) { return true; }
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
  int height() const {
    return virtual_stack_pointer_ - expression_base_index() + 1;
  }

  // Add extra in-memory elements to the top of the frame without generating
  // code.
  void Adjust(int count);

  // Forget frame elements without generating code.
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
  void AllocateStackSlots(int count);

  // The current top of the expression stack as an assembly operand.
  Operand Top() const { return Operand(esp, 0); }

  // An element of the expression stack as an assembly operand.
  Operand ElementAt(int index) const {
    return Operand(esp, index * kPointerSize);
  }

  // A frame-allocated local as an assembly operand.
  Operand Local(int index) const {
    ASSERT(0 <= index);
    ASSERT(index < local_count_);
    return Operand(ebp, kLocal0Offset - index * kPointerSize);
  }

  // The function frame slot.
  Operand Function() const { return Operand(ebp, kFunctionOffset); }

  // The context frame slot.
  Operand Context() const { return Operand(ebp, kContextOffset); }

  // A parameter as an assembly operand.
  Operand Parameter(int index) const {
    ASSERT(-1 <= index);
    ASSERT(index < parameter_count_);
    return Operand(ebp, (1 + parameter_count_ - index) * kPointerSize);
  }

  // The receiver frame slot.
  Operand Receiver() const { return Parameter(-1); }

  // Push a try-catch or try-finally handler on top of the virtual frame.
  void PushTryHandler(HandlerType type);

  // Call a code stub, given the number of arguments it expects on (and
  // removes from) the top of the physical frame.
  void CallStub(CodeStub* stub, int frame_arg_count);

  // Call the runtime, given the number of arguments expected on (and
  // removed from) the top of the physical frame.
  void CallRuntime(Runtime::Function* f, int frame_arg_count);
  void CallRuntime(Runtime::FunctionId id, int frame_arg_count);

  // Invoke a builtin, given the number of arguments it expects on (and
  // removes from) the top of the physical frame.
  void InvokeBuiltin(Builtins::JavaScript id,
                     InvokeFlag flag,
                     int frame_arg_count);

  // Call into a JS code object, given the number of arguments it expects on
  // (and removes from) the top of the physical frame.
  void CallCode(Handle<Code> ic, RelocInfo::Mode rmode, int frame_arg_count);

  // Drop a number of elements from the top of the expression stack.  May
  // emit code to affect the physical frame.  Does not clobber any registers
  // excepting possibly the stack pointer.
  void Drop(int count);

  // Drop one element.
  void Drop();

  // Pop and save an element from the top of the expression stack.  May emit
  // code.
  void Pop(Register reg);
  void Pop(Operand operand);

  // Push an element on top of the expression stack.  May emit code.
  void Push(Register reg);
  void Push(Operand operand);
  void Push(Immediate immediate);

 private:
  static const int kLocal0Offset = JavaScriptFrameConstants::kLocal0Offset;
  static const int kFunctionOffset = JavaScriptFrameConstants::kFunctionOffset;
  static const int kContextOffset = StandardFrameConstants::kContextOffset;

  static const int kHandlerSize = StackHandlerConstants::kSize / kPointerSize;

  MacroAssembler* masm_;

  List<Element> elements_;

  // The virtual stack pointer is the index of the top element of the stack.
  int virtual_stack_pointer_;

  int virtual_frame_pointer_;

  int parameter_count_;
  int local_count_;

  // The index of the first parameter.  The receiver lies below the first
  // parameter.
  int param0_index() const { return 1; }

  // The index of the first local.  Between the parameters and the locals
  // lie the return address, the saved frame pointer, the context, and the
  // function.
  int local0_index() const { return param0_index() + parameter_count_ + 4; }

  // The index of the base of the expression stack.
  int expression_base_index() const { return local0_index() + local_count_; }

  void AddElement(const Element& element) {
    virtual_stack_pointer_++;
    elements_.Add(element);
  }

  Element RemoveElement() {
    virtual_stack_pointer_--;
    return elements_.RemoveLast();
  }

  // The JumpTarget class explicitly sets the height_ field of the expected
  // frame at the actual return target.
  friend class JumpTarget;
};


} }  // namespace v8::internal

#endif  // V8_VIRTUAL_FRAME_IA32_H_
