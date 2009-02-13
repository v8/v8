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

#ifndef V8_VIRTUAL_FRAME_ARM_H_
#define V8_VIRTUAL_FRAME_ARM_H_

#include "macro-assembler.h"
#include "register-allocator.h"

namespace v8 { namespace internal {

// -------------------------------------------------------------------------
// Virtual frame elements
//
// The internal elements of the virtual frames.  There are several kinds of
// elements:
//   * Invalid: elements that are uninitialized or not actually part
//     of the virtual frame.  They should not be read.
//   * Memory: an element that resides in the actual frame.  Its address is
//     given by its position in the virtual frame.
//   * Register: an element that resides in a register.
//   * Constant: an element whose value is known at compile time.

class FrameElement BASE_EMBEDDED {
 public:
  enum SyncFlag {
    SYNCED,
    NOT_SYNCED
  };

  // The default constructor creates an invalid frame element.
  FrameElement() {
    type_ = TypeField::encode(INVALID) | SyncField::encode(NOT_SYNCED);
    data_.reg_ = no_reg;
  }

  // Factory function to construct an invalid frame element.
  static FrameElement InvalidElement() {
    FrameElement result;
    return result;
  }

  // Factory function to construct an in-memory frame element.
  static FrameElement MemoryElement() {
    FrameElement result;
    result.type_ = TypeField::encode(MEMORY) | SyncField::encode(SYNCED);
    // In-memory elements have no useful data.
    result.data_.reg_ = no_reg;
    return result;
  }

  // Factory function to construct an in-register frame element.
  static FrameElement RegisterElement(Register reg, SyncFlag is_synced) {
    FrameElement result;
    result.type_ = TypeField::encode(REGISTER) | SyncField::encode(is_synced);
    result.data_.reg_ = reg;
    return result;
  }

  // Factory function to construct a frame element whose value is known at
  // compile time.
  static FrameElement ConstantElement(Handle<Object> value,
                                      SyncFlag is_synced) {
    FrameElement result;
    result.type_ = TypeField::encode(CONSTANT) | SyncField::encode(is_synced);
    result.data_.handle_ = value.location();
    return result;
  }

  bool is_synced() const { return SyncField::decode(type_) == SYNCED; }

  void set_sync() {
    ASSERT(type() != MEMORY);
    type_ = (type_ & ~SyncField::mask()) | SyncField::encode(SYNCED);
  }

  void clear_sync() {
    ASSERT(type() != MEMORY);
    type_ = (type_ & ~SyncField::mask()) | SyncField::encode(NOT_SYNCED);
  }

  bool is_valid() const { return type() != INVALID; }
  bool is_memory() const { return type() == MEMORY; }
  bool is_register() const { return type() == REGISTER; }
  bool is_constant() const { return type() == CONSTANT; }
  bool is_copy() const { return type() == COPY; }

  Register reg() const {
    ASSERT(is_register());
    return data_.reg_;
  }

  Handle<Object> handle() const {
    ASSERT(is_constant());
    return Handle<Object>(data_.handle_);
  }

  int index() const {
    ASSERT(is_copy());
    return data_.index_;
  }

#ifdef DEBUG
  bool Equals(FrameElement other);
#endif

 private:
  enum Type {
    INVALID,
    MEMORY,
    REGISTER,
    CONSTANT,
    COPY
  };

  // BitField is <type, shift, size>.
  class SyncField : public BitField<SyncFlag, 0, 1> {};
  class TypeField : public BitField<Type, 1, 32 - 1> {};

  Type type() const { return TypeField::decode(type_); }

  // The element's type and a dirty bit.  The dirty bit can be cleared
  // for non-memory elements to indicate that the element agrees with
  // the value in memory in the actual frame.
  int type_;

  union {
    Register reg_;
    Object** handle_;
    int index_;
  } data_;

  friend class VirtualFrame;
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

  int height() const {
    return elements_.length() - expression_base_index();
  }

  // Add extra in-memory elements to the top of the frame without generating
  // code.
  void Adjust(int count);

  // Forget frame elements without generating code.
  void Forget(int count);

  // Ensure that this frame is in a state where an arbitrary frame of the
  // right size could be merged to it.  May emit code.
  void MakeMergable() { }

  // Make this virtual frame have a state identical to an expected virtual
  // frame.  As a side effect, code may be emitted to make this frame match
  // the expected one.
  void MergeTo(VirtualFrame* expected);

  // Detach a frame from its code generator, perhaps temporarily.  This
  // tells the register allocator that it is free to use frame-internal
  // registers.  Used when the code generator's frame is switched from this
  // one to NULL by an unconditional jump.
  void DetachFromCodeGenerator();

  // (Re)attach a frame to its code generator.  This informs the register
  // allocator that the frame-internal register references are active again.
  // Used when a code generator's frame is switched from NULL to this one by
  // binding a label.
  void AttachToCodeGenerator();

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
  MemOperand Top() const { return MemOperand(sp, 0); }

  // An element of the expression stack as an assembly operand.
  MemOperand ElementAt(int index) const {
    return MemOperand(sp, index * kPointerSize);
  }

  // A frame-allocated local as an assembly operand.
  MemOperand LocalAt(int index) const {
    ASSERT(0 <= index);
    ASSERT(index < local_count_);
    return MemOperand(fp, kLocal0Offset - index * kPointerSize);
  }

  // The function frame slot.
  MemOperand Function() const { return MemOperand(fp, kFunctionOffset); }

  // The context frame slot.
  MemOperand Context() const { return MemOperand(fp, kContextOffset); }

  // A parameter as an assembly operand.
  MemOperand ParameterAt(int index) const {
    // Index -1 corresponds to the receiver.
    ASSERT(-1 <= index && index <= parameter_count_);
    return MemOperand(fp, (1 + parameter_count_ - index) * kPointerSize);
  }

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
                     InvokeJSFlags flags,
                     int frame_arg_count);

  // Call into a JS code object, given the number of arguments it expects on
  // (and removes from) the top of the physical frame.
  void CallCodeObject(Handle<Code> ic,
                      RelocInfo::Mode rmode,
                      int frame_arg_count);

  // Drop a number of elements from the top of the expression stack.  May
  // emit code to affect the physical frame.  Does not clobber any registers
  // excepting possibly the stack pointer.
  void Drop(int count);

  // Drop one element.
  void Drop();

  // Pop and save an element from the top of the expression stack.  May emit
  // code.
  void Pop(Register reg);

  // Push an element on top of the expression stack and emit a corresponding
  // push instruction.
  void EmitPush(Register reg);

 private:
  static const int kLocal0Offset = JavaScriptFrameConstants::kLocal0Offset;
  static const int kFunctionOffset = JavaScriptFrameConstants::kFunctionOffset;
  static const int kContextOffset = StandardFrameConstants::kContextOffset;

  static const int kHandlerSize = StackHandlerConstants::kSize / kPointerSize;

  MacroAssembler* masm_;

  List<FrameElement> elements_;

  // The number of frame-allocated locals and parameters respectively.
  int parameter_count_;
  int local_count_;

  int frame_pointer_;

  // The index of the first parameter.  The receiver lies below the first
  // parameter.
  int param0_index() const { return 1; }

  // The index of the first local.  Between the parameters and the locals
  // lie the return address, the saved frame pointer, the context, and the
  // function.
  int local0_index() const { return param0_index() + parameter_count_ + 4; }

  // The index of the base of the expression stack.
  int expression_base_index() const { return local0_index() + local_count_; }
};


} }  // namespace v8::internal

#endif  // V8_VIRTUAL_FRAME_ARM_H_
