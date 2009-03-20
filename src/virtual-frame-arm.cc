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

#include "v8.h"

#include "codegen-inl.h"
#include "register-allocator-inl.h"
#include "scopes.h"

namespace v8 { namespace internal {

// -------------------------------------------------------------------------
// VirtualFrame implementation.

#define __ masm_->

// On entry to a function, the virtual frame already contains the
// receiver and the parameters.  All initial frame elements are in
// memory.
VirtualFrame::VirtualFrame(CodeGenerator* cgen)
    : cgen_(cgen),
      masm_(cgen->masm()),
      elements_(0),
      parameter_count_(cgen->scope()->num_parameters()),
      local_count_(0),
      stack_pointer_(parameter_count_),  // 0-based index of TOS.
      frame_pointer_(kIllegalIndex) {
  for (int i = 0; i < parameter_count_ + 1; i++) {
    elements_.Add(FrameElement::MemoryElement());
  }
}


// Clear the dirty bit for the element at a given index if it is a
// valid element.  The stack address corresponding to the element must
// be allocated on the physical stack, or the first element above the
// stack pointer so it can be allocated by a single push instruction.
void VirtualFrame::RawSyncElementAt(int index) {
  FrameElement element = elements_[index];

  if (!element.is_valid() || element.is_synced()) return;

  if (index <= stack_pointer_) {
    // Emit code to write elements below the stack pointer to their
    // (already allocated) stack address.
    switch (element.type()) {
      case FrameElement::INVALID:  // Fall through.
      case FrameElement::MEMORY:
        // There was an early bailout for invalid and synced elements
        // (memory elements are always synced).
        UNREACHABLE();
        break;

      case FrameElement::REGISTER:
        __ str(element.reg(), MemOperand(fp, fp_relative(index)));
        break;

      case FrameElement::CONSTANT: {
        Result temp = cgen_->allocator()->Allocate();
        ASSERT(temp.is_valid());
        __ mov(temp.reg(), Operand(element.handle()));
        __ str(temp.reg(), MemOperand(fp, fp_relative(index)));
        break;
      }

      case FrameElement::COPY: {
        int backing_index = element.index();
        FrameElement backing_element = elements_[backing_index];
        if (backing_element.is_memory()) {
          Result temp = cgen_->allocator()->Allocate();
          ASSERT(temp.is_valid());
          __ ldr(temp.reg(), MemOperand(fp, fp_relative(backing_index)));
          __ str(temp.reg(), MemOperand(fp, fp_relative(index)));
        } else {
          ASSERT(backing_element.is_register());
          __ str(backing_element.reg(), MemOperand(fp, fp_relative(index)));
        }
        break;
      }
    }

  } else {
    // Push elements above the stack pointer to allocate space and
    // sync them.  Space should have already been allocated in the
    // actual frame for all the elements below this one.
    ASSERT(index == stack_pointer_ + 1);
    stack_pointer_++;
    switch (element.type()) {
      case FrameElement::INVALID:  // Fall through.
      case FrameElement::MEMORY:
        // There was an early bailout for invalid and synced elements
        // (memory elements are always synced).
        UNREACHABLE();
        break;

      case FrameElement::REGISTER:
        __ push(element.reg());
        break;

      case FrameElement::CONSTANT: {
        Result temp = cgen_->allocator()->Allocate();
        ASSERT(temp.is_valid());
        __ mov(temp.reg(), Operand(element.handle()));
        __ push(temp.reg());
        break;
      }

      case FrameElement::COPY: {
        int backing_index = element.index();
        FrameElement backing = elements_[backing_index];
        ASSERT(backing.is_memory() || backing.is_register());
        if (backing.is_memory()) {
          Result temp = cgen_->allocator()->Allocate();
          ASSERT(temp.is_valid());
          __ ldr(temp.reg(), MemOperand(fp, fp_relative(backing_index)));
          __ push(temp.reg());
        } else {
          __ push(backing.reg());
        }
        break;
      }
    }
  }

  elements_[index].set_sync();
}


void VirtualFrame::MergeTo(VirtualFrame* expected) {
  Comment cmnt(masm_, "[ Merge frame");
  // We should always be merging the code generator's current frame to an
  // expected frame.
  ASSERT(cgen_->frame() == this);

  // Adjust the stack pointer upward (toward the top of the virtual
  // frame) if necessary.
  if (stack_pointer_ < expected->stack_pointer_) {
    int difference = expected->stack_pointer_ - stack_pointer_;
    stack_pointer_ = expected->stack_pointer_;
    __ sub(sp, sp, Operand(difference * kPointerSize));
  }

  MergeMoveRegistersToMemory(expected);
  MergeMoveRegistersToRegisters(expected);
  MergeMoveMemoryToRegisters(expected);

  // Fix any sync bit problems from the bottom-up, stopping when we
  // hit the stack pointer or the top of the frame if the stack
  // pointer is floating above the frame.
  int limit = Min(stack_pointer_, elements_.length() - 1);
  for (int i = 0; i <= limit; i++) {
    FrameElement source = elements_[i];
    FrameElement target = expected->elements_[i];
    if (source.is_synced() && !target.is_synced()) {
      elements_[i].clear_sync();
    } else if (!source.is_synced() && target.is_synced()) {
      SyncElementAt(i);
    }
  }

  // Adjust the stack point downard if necessary.
  if (stack_pointer_ > expected->stack_pointer_) {
    int difference = stack_pointer_ - expected->stack_pointer_;
    stack_pointer_ = expected->stack_pointer_;
    __ add(sp, sp, Operand(difference * kPointerSize));
  }

  // At this point, the frames should be identical.
  ASSERT(Equals(expected));
}


void VirtualFrame::MergeMoveRegistersToMemory(VirtualFrame* expected) {
  ASSERT(stack_pointer_ >= expected->stack_pointer_);

  // Move registers, constants, and copies to memory.  Perform moves
  // from the top downward in the frame in order to leave the backing
  // stores of copies in registers.
  // On ARM, all elements are in memory.

#ifdef DEBUG
  int start = Min(stack_pointer_, elements_.length() - 1);
  for (int i = start; i >= 0; i--) {
    ASSERT(elements_[i].is_memory());
    ASSERT(expected->elements_[i].is_memory());
  }
#endif
}


void VirtualFrame::MergeMoveRegistersToRegisters(VirtualFrame* expected) {
}


void VirtualFrame::MergeMoveMemoryToRegisters(VirtualFrame *expected) {
}


void VirtualFrame::Enter() {
  Comment cmnt(masm_, "[ Enter JS frame");

#ifdef DEBUG
  // Verify that r1 contains a JS function.  The following code relies
  // on r2 being available for use.
  { Label map_check, done;
    __ tst(r1, Operand(kSmiTagMask));
    __ b(ne, &map_check);
    __ stop("VirtualFrame::Enter - r1 is not a function (smi check).");
    __ bind(&map_check);
    __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));
    __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
    __ cmp(r2, Operand(JS_FUNCTION_TYPE));
    __ b(eq, &done);
    __ stop("VirtualFrame::Enter - r1 is not a function (map check).");
    __ bind(&done);
  }
#endif  // DEBUG

  // We are about to push four values to the frame.
  Adjust(4);
  __ stm(db_w, sp, r1.bit() | cp.bit() | fp.bit() | lr.bit());
  // Adjust FP to point to saved FP.
  frame_pointer_ = elements_.length() - 2;
  __ add(fp, sp, Operand(2 * kPointerSize));
  cgen_->allocator()->Unuse(r1);
  cgen_->allocator()->Unuse(lr);
}


void VirtualFrame::Exit() {
  Comment cmnt(masm_, "[ Exit JS frame");
  // Drop the execution stack down to the frame pointer and restore the caller
  // frame pointer and return address.
  __ mov(sp, fp);
  __ ldm(ia_w, sp, fp.bit() | lr.bit());
}


void VirtualFrame::AllocateStackSlots(int count) {
  ASSERT(height() == 0);
  local_count_ = count;
  Adjust(count);
  if (count > 0) {
    Comment cmnt(masm_, "[ Allocate space for locals");
      // Initialize stack slots with 'undefined' value.
    __ mov(ip, Operand(Factory::undefined_value()));
    for (int i = 0; i < count; i++) {
      __ push(ip);
    }
  }
}


void VirtualFrame::SaveContextRegister() {
  UNIMPLEMENTED();
}


void VirtualFrame::RestoreContextRegister() {
  UNIMPLEMENTED();
}


void VirtualFrame::PushReceiverSlotAddress() {
  UNIMPLEMENTED();
}


// Before changing an element which is copied, adjust so that the
// first copy becomes the new backing store and all the other copies
// are updated.  If the original was in memory, the new backing store
// is allocated to a register.  Return a copy of the new backing store
// or an invalid element if the original was not a copy.
FrameElement VirtualFrame::AdjustCopies(int index) {
  UNIMPLEMENTED();
  return FrameElement::InvalidElement();
}


void VirtualFrame::TakeFrameSlotAt(int index) {
  UNIMPLEMENTED();
}


void VirtualFrame::StoreToFrameSlotAt(int index) {
  UNIMPLEMENTED();
}


void VirtualFrame::PushTryHandler(HandlerType type) {
  // Grow the expression stack by handler size less one (the return address
  // is already pushed by a call instruction).
  Adjust(kHandlerSize - 1);
  __ PushTryHandler(IN_JAVASCRIPT, type);
}


Result VirtualFrame::RawCallStub(CodeStub* stub, int frame_arg_count) {
  ASSERT(cgen_->HasValidEntryRegisters());
  __ CallStub(stub);
  Result result = cgen_->allocator()->Allocate(r0);
  ASSERT(result.is_valid());
  return result;
}


Result VirtualFrame::CallRuntime(Runtime::Function* f,
                                 int frame_arg_count) {
  PrepareForCall(frame_arg_count, frame_arg_count);
  ASSERT(cgen_->HasValidEntryRegisters());
  __ CallRuntime(f, frame_arg_count);
  Result result = cgen_->allocator()->Allocate(r0);
  ASSERT(result.is_valid());
  return result;
}


Result VirtualFrame::CallRuntime(Runtime::FunctionId id,
                                 int frame_arg_count) {
  PrepareForCall(frame_arg_count, frame_arg_count);
  ASSERT(cgen_->HasValidEntryRegisters());
  __ CallRuntime(id, frame_arg_count);
  Result result = cgen_->allocator()->Allocate(r0);
  ASSERT(result.is_valid());
  return result;
}


Result VirtualFrame::InvokeBuiltin(Builtins::JavaScript id,
                                   InvokeJSFlags flags,
                                   Result* arg_count_register,
                                   int frame_arg_count) {
  ASSERT(arg_count_register->reg().is(r0));
  PrepareForCall(frame_arg_count, frame_arg_count);
  arg_count_register->Unuse();
  __ InvokeBuiltin(id, flags);
  Result result = cgen_->allocator()->Allocate(r0);
  return result;
}


Result VirtualFrame::RawCallCodeObject(Handle<Code> code,
                                       RelocInfo::Mode rmode) {
  ASSERT(cgen_->HasValidEntryRegisters());
  __ Call(code, rmode);
  Result result = cgen_->allocator()->Allocate(r0);
  ASSERT(result.is_valid());
  return result;
}


Result VirtualFrame::CallCodeObject(Handle<Code> code,
                                    RelocInfo::Mode rmode,
                                    Result* arg,
                                    int dropped_args) {
  int spilled_args = 0;
  switch (code->kind()) {
    case Code::LOAD_IC:
      ASSERT(arg->reg().is(r2));
      ASSERT(dropped_args == 0);
      spilled_args = 1;
      break;
    case Code::KEYED_STORE_IC:
      ASSERT(arg->reg().is(r0));
      ASSERT(dropped_args == 0);
      spilled_args = 2;
      break;
    default:
      // No other types of code objects are called with values
      // in exactly one register.
      UNREACHABLE();
      break;
  }
  PrepareForCall(spilled_args, dropped_args);
  arg->Unuse();
  return RawCallCodeObject(code, rmode);
}


Result VirtualFrame::CallCodeObject(Handle<Code> code,
                                    RelocInfo::Mode rmode,
                                    Result* arg0,
                                    Result* arg1,
                                    int dropped_args) {
  int spilled_args = 1;
  switch (code->kind()) {
    case Code::STORE_IC:
      ASSERT(arg0->reg().is(r0));
      ASSERT(arg1->reg().is(r2));
      ASSERT(dropped_args == 0);
      spilled_args = 1;
      break;
    case Code::BUILTIN:
      ASSERT(*code == Builtins::builtin(Builtins::JSConstructCall));
      ASSERT(arg0->reg().is(r0));
      ASSERT(arg1->reg().is(r1));
      spilled_args = dropped_args + 1;
      break;
    default:
      // No other types of code objects are called with values
      // in exactly two registers.
      UNREACHABLE();
      break;
  }
  PrepareForCall(spilled_args, dropped_args);
  arg0->Unuse();
  arg1->Unuse();
  return RawCallCodeObject(code, rmode);
}


void VirtualFrame::Drop(int count) {
  ASSERT(height() >= count);
  int num_virtual_elements = (elements_.length() - 1) - stack_pointer_;

  // Emit code to lower the stack pointer if necessary.
  if (num_virtual_elements < count) {
    int num_dropped = count - num_virtual_elements;
    stack_pointer_ -= num_dropped;
    __ add(sp, sp, Operand(num_dropped * kPointerSize));
  }

  // Discard elements from the virtual frame and free any registers.
  for (int i = 0; i < count; i++) {
    FrameElement dropped = elements_.RemoveLast();
    if (dropped.is_register()) {
      Unuse(dropped.reg());
    }
  }
}


Result VirtualFrame::Pop() {
  UNIMPLEMENTED();
  Result invalid(cgen_);
  return invalid;
}


void VirtualFrame::EmitPop(Register reg) {
  ASSERT(stack_pointer_ == elements_.length() - 1);
  stack_pointer_--;
  elements_.RemoveLast();
  __ pop(reg);
}


void VirtualFrame::EmitPush(Register reg) {
  ASSERT(stack_pointer_ == elements_.length() - 1);
  elements_.Add(FrameElement::MemoryElement());
  stack_pointer_++;
  __ push(reg);
}


#undef __

} }  // namespace v8::internal
