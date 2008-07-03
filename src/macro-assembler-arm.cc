// Copyright 2006-2008 Google Inc. All Rights Reserved.
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

#include "bootstrapper.h"
#include "codegen-inl.h"
#include "debug.h"
#include "runtime.h"

namespace v8 { namespace internal {

DECLARE_bool(debug_code);
DECLARE_bool(optimize_locals);


// Give alias names to registers
Register cp = {  8 };  // JavaScript context pointer
Register pp = { 10 };  // parameter pointer


MacroAssembler::MacroAssembler(void* buffer, int size)
    : Assembler(buffer, size),
      unresolved_(0),
      generating_stub_(false) {
}


// We always generate arm code, never thumb code, even if V8 is compiled to
// thumb, so we require inter-working support
#if defined(__thumb__) && !defined(__THUMB_INTERWORK__)
#error "flag -mthumb-interwork missing"
#endif


// We do not support thumb inter-working with an arm architecture not supporting
// the blx instruction (below v5t)
#if defined(__THUMB_INTERWORK__)
#if !defined(__ARM_ARCH_5T__) && !defined(__ARM_ARCH_5TE__)
// add tests for other versions above v5t as required
#error "for thumb inter-working we require architecture v5t or above"
#endif
#endif


// Using blx may yield better code, so use it when required or when available
#if defined(__THUMB_INTERWORK__) || defined(__ARM_ARCH_5__)
#define USE_BLX 1
#endif

// Using bx does not yield better code, so use it only when required
#if defined(__THUMB_INTERWORK__)
#define USE_BX 1
#endif


void MacroAssembler::Jump(Register target, Condition cond) {
#if USE_BX
  bx(target, cond);
#else
  mov(pc, Operand(target), LeaveCC, cond);
#endif
}


void MacroAssembler::Jump(intptr_t target, RelocMode rmode, Condition cond) {
#if USE_BX
  mov(ip, Operand(target, rmode), LeaveCC, cond);
  bx(ip, cond);
#else
  mov(pc, Operand(target, rmode), LeaveCC, cond);
#endif
}


void MacroAssembler::Jump(byte* target, RelocMode rmode, Condition cond) {
  ASSERT(!is_code_target(rmode));
  Jump(reinterpret_cast<intptr_t>(target), rmode, cond);
}


void MacroAssembler::Jump(Handle<Code> code, RelocMode rmode, Condition cond) {
  ASSERT(is_code_target(rmode));
  // 'code' is always generated ARM code, never THUMB code
  Jump(reinterpret_cast<intptr_t>(code.location()), rmode, cond);
}


void MacroAssembler::Call(Register target, Condition cond) {
#if USE_BLX
  blx(target, cond);
#else
  // set lr for return at current pc + 8
  mov(lr, Operand(pc), LeaveCC, cond);
  mov(pc, Operand(target), LeaveCC, cond);
#endif
}


void MacroAssembler::Call(intptr_t target, RelocMode rmode, Condition cond) {
#if !defined(__arm__)
  if (rmode == runtime_entry) {
    mov(r2, Operand(target, rmode), LeaveCC, cond);
    // Set lr for return at current pc + 8.
    mov(lr, Operand(pc), LeaveCC, cond);
    // Emit a ldr<cond> pc, [pc + offset of target in constant pool].
    // Notify the simulator of the transition to C code.
    swi(assembler::arm::call_rt_r2);
  } else {
    // set lr for return at current pc + 8
    mov(lr, Operand(pc), LeaveCC, cond);
    // emit a ldr<cond> pc, [pc + offset of target in constant pool]
    mov(pc, Operand(target, rmode), LeaveCC, cond);
  }
#else
  // Set lr for return at current pc + 8.
  mov(lr, Operand(pc), LeaveCC, cond);
  // Emit a ldr<cond> pc, [pc + offset of target in constant pool].
  mov(pc, Operand(target, rmode), LeaveCC, cond);
#endif  // !defined(__arm__)
  // If USE_BLX is defined, we could emit a 'mov ip, target', followed by a
  // 'blx ip'; however, the code would not be shorter than the above sequence
  // and the target address of the call would be referenced by the first
  // instruction rather than the second one, which would make it harder to patch
  // (two instructions before the return address, instead of one).
  ASSERT(kTargetAddrToReturnAddrDist == sizeof(Instr));
}


void MacroAssembler::Call(byte* target, RelocMode rmode, Condition cond) {
  ASSERT(!is_code_target(rmode));
  Call(reinterpret_cast<intptr_t>(target), rmode, cond);
}


void MacroAssembler::Call(Handle<Code> code, RelocMode rmode, Condition cond) {
  ASSERT(is_code_target(rmode));
  // 'code' is always generated ARM code, never THUMB code
  Call(reinterpret_cast<intptr_t>(code.location()), rmode, cond);
}


void MacroAssembler::Ret() {
#if USE_BX
  bx(lr);
#else
  mov(pc, Operand(lr));
#endif
}


void MacroAssembler::Push(const Operand& src) {
  push(r0);
  mov(r0, src);
}


void MacroAssembler::Push(const MemOperand& src) {
  push(r0);
  ldr(r0, src);
}


void MacroAssembler::Pop(Register dst) {
  mov(dst, Operand(r0));
  pop(r0);
}


void MacroAssembler::Pop(const MemOperand& dst) {
  str(r0, dst);
  pop(r0);
}


// Will clobber 4 registers: object, offset, scratch, ip.  The
// register 'object' contains a heap object pointer.  The heap object
// tag is shifted away.
void MacroAssembler::RecordWrite(Register object, Register offset,
                                 Register scratch) {
  // This is how much we shift the remembered set bit offset to get the
  // offset of the word in the remembered set.  We divide by kBitsPerInt (32,
  // shift right 5) and then multiply by kIntSize (4, shift left 2).
  const int kRSetWordShift = 3;

  Label fast, done;

  // First, test that the start address is not in the new space.  We cannot
  // set remembered set bits in the new space.
  and_(scratch, object, Operand(Heap::NewSpaceMask()));
  cmp(scratch, Operand(ExternalReference::new_space_start()));
  b(eq, &done);

  mov(ip, Operand(Page::kPageAlignmentMask));  // load mask only once
  // Compute the bit offset in the remembered set.
  and_(scratch, object, Operand(ip));
  add(offset, scratch, Operand(offset));
  mov(offset, Operand(offset, LSR, kObjectAlignmentBits));

  // Compute the page address from the heap object pointer.
  bic(object, object, Operand(ip));

  // If the bit offset lies beyond the normal remembered set range, it is in
  // the extra remembered set area of a large object.
  cmp(offset, Operand(Page::kPageSize / kPointerSize));
  b(lt, &fast);

  // Adjust the bit offset to be relative to the start of the extra
  // remembered set and the start address to be the address of the extra
  // remembered set.
  sub(offset, offset, Operand(Page::kPageSize / kPointerSize));
  // Load the array length into 'scratch' and multiply by four to get the
  // size in bytes of the elements.
  ldr(scratch, MemOperand(object, Page::kObjectStartOffset
                                  + FixedArray::kLengthOffset));
  mov(scratch, Operand(scratch, LSL, kObjectAlignmentBits));
  // Add the page header (including remembered set), array header, and array
  // body size to the page address.
  add(object, object, Operand(Page::kObjectStartOffset
                              + Array::kHeaderSize));
  add(object, object, Operand(scratch));

  bind(&fast);
  // Now object is the address of the start of the remembered set and offset
  // is the bit offset from that start.
  // Get address of the rset word.
  add(object, object, Operand(offset, LSR, kRSetWordShift));
  // Get bit offset in the word.
  and_(offset, offset, Operand(kBitsPerInt - 1));

  ldr(scratch, MemOperand(object));
  mov(ip, Operand(1));
  orr(scratch, scratch, Operand(ip, LSL, offset));
  str(scratch, MemOperand(object));

  bind(&done);
}


void MacroAssembler::EnterJSFrame(int argc, RegList callee_saved) {
  // Generate code entering a JS function called from a JS function
  // stack: receiver, arguments
  // r0: number of arguments (not including function, nor receiver)
  // r1: preserved
  // sp: stack pointer
  // fp: frame pointer
  // cp: callee's context
  // pp: caller's parameter pointer
  // lr: return address

  // compute parameter pointer before making changes
  // ip = sp + kPointerSize*(args_len+1);  // +1 for receiver
  add(ip, sp, Operand(r0, LSL, kPointerSizeLog2));
  add(ip, ip, Operand(kPointerSize));

  // push extra parameters if we don't have enough
  // (this can only happen if argc > 0 to begin with)
  if (argc > 0) {
    Label loop, done;

    // assume enough arguments to be the most common case
    sub(r2, r0, Operand(argc), SetCC);  // number of missing arguments
    b(ge, &done);  // enough arguments

    // not enough arguments
    mov(r3, Operand(Factory::undefined_value()));
    bind(&loop);
    push(r3);
    add(r2, r2, Operand(1), SetCC);
    b(lt, &loop);

    bind(&done);
  }

  mov(r3, Operand(r0));  // args_len to be saved
  mov(r2, Operand(cp));  // context to be saved

  // Make sure there are no instructions between both stm instructions, because
  // the callee_saved list is obtained during stack unwinding by decoding the
  // first stmdb instruction, which is found (or not) at a constant offset from
  // the pc saved by the second stmdb instruction.
  if (callee_saved != 0) {
    stm(db_w, sp, callee_saved);
  }

  // push in reverse order: context (r2), args_len (r3), caller_pp, caller_fp,
  // sp_on_exit (ip == pp, may be patched on exit), return address, prolog_pc
  stm(db_w, sp, r2.bit() | r3.bit() | pp.bit() | fp.bit() |
      ip.bit() | lr.bit() | pc.bit());

  // Setup new frame pointer.
  add(fp, sp, Operand(-StandardFrameConstants::kContextOffset));
  mov(pp, Operand(ip));  // setup new parameter pointer
  mov(r0, Operand(0));  // spare slot to store caller code object during GC
  // r0: TOS (code slot == 0)
  // r1: preserved
}


void MacroAssembler::ExitJSFrame(ExitJSFlag flag, RegList callee_saved) {
  // r0: result
  // sp: stack pointer
  // fp: frame pointer
  // pp: parameter pointer

  if (callee_saved != 0 || flag == DO_NOT_RETURN) {
    add(r3, fp, Operand(JavaScriptFrameConstants::kSavedRegistersOffset));
  }

  if (callee_saved != 0) {
    ldm(ia_w, r3, callee_saved);
  }

  if (flag == DO_NOT_RETURN) {
    // restore sp as caller_sp (not as pp)
    str(r3, MemOperand(fp, JavaScriptFrameConstants::kSPOnExitOffset));
  }

  if (flag == DO_NOT_RETURN && generating_stub()) {
    // If we're generating a stub, we need to preserve the link
    // register to be able to return to the place the stub was called
    // from.
    mov(ip, Operand(lr));
  }

  mov(sp, Operand(fp));  // respect ABI stack constraint
  ldm(ia, sp, pp.bit() | fp.bit() | sp.bit() |
      ((flag == RETURN) ? pc.bit() : lr.bit()));

  if (flag == DO_NOT_RETURN && generating_stub()) {
    // Return to the place where the stub was called without
    // clobbering the value of the link register.
    mov(pc, Operand(ip));
  }

  // r0: result
  // sp: points to function arg (if return) or to last arg (if no return)
  // fp: restored frame pointer
  // pp: restored parameter pointer
}


void MacroAssembler::SaveRegistersToMemory(RegList regs) {
  ASSERT((regs & ~kJSCallerSaved) == 0);
  // Copy the content of registers to memory location.
  for (int i = 0; i < kNumJSCallerSaved; i++) {
    int r = JSCallerSavedCode(i);
    if ((regs & (1 << r)) != 0) {
      Register reg = { r };
      mov(ip, Operand(ExternalReference(Debug_Address::Register(i))));
      str(reg, MemOperand(ip));
    }
  }
}


void MacroAssembler::RestoreRegistersFromMemory(RegList regs) {
  ASSERT((regs & ~kJSCallerSaved) == 0);
  // Copy the content of memory location to registers.
  for (int i = kNumJSCallerSaved; --i >= 0;) {
    int r = JSCallerSavedCode(i);
    if ((regs & (1 << r)) != 0) {
      Register reg = { r };
      mov(ip, Operand(ExternalReference(Debug_Address::Register(i))));
      ldr(reg, MemOperand(ip));
    }
  }
}


void MacroAssembler::CopyRegistersFromMemoryToStack(Register base,
                                                    RegList regs) {
  ASSERT((regs & ~kJSCallerSaved) == 0);
  // Copy the content of the memory location to the stack and adjust base.
  for (int i = kNumJSCallerSaved; --i >= 0;) {
    int r = JSCallerSavedCode(i);
    if ((regs & (1 << r)) != 0) {
      mov(ip, Operand(ExternalReference(Debug_Address::Register(i))));
      ldr(ip, MemOperand(ip));
      str(ip, MemOperand(base, 4, NegPreIndex));
    }
  }
}


void MacroAssembler::CopyRegistersFromStackToMemory(Register base,
                                                    Register scratch,
                                                    RegList regs) {
  ASSERT((regs & ~kJSCallerSaved) == 0);
  // Copy the content of the stack to the memory location and adjust base.
  for (int i = 0; i < kNumJSCallerSaved; i++) {
    int r = JSCallerSavedCode(i);
    if ((regs & (1 << r)) != 0) {
      mov(ip, Operand(ExternalReference(Debug_Address::Register(i))));
      ldr(scratch, MemOperand(base, 4, PostIndex));
      str(scratch, MemOperand(ip));
    }
  }
}


void MacroAssembler::PushTryHandler(CodeLocation try_location,
                                    HandlerType type) {
  ASSERT(StackHandlerConstants::kSize == 6 * kPointerSize);  // adjust this code
  // The pc (return address) is passed in register lr.
  if (try_location == IN_JAVASCRIPT) {
    mov(r0, Operand(Smi::FromInt(StackHandler::kCodeNotPresent)));  // new TOS
    stm(db_w, sp, pp.bit() | fp.bit() | lr.bit());
    if (type == TRY_CATCH_HANDLER) {
      mov(r3, Operand(StackHandler::TRY_CATCH));
    } else {
      mov(r3, Operand(StackHandler::TRY_FINALLY));
    }
    push(r3);  // state
    mov(r3, Operand(ExternalReference(Top::k_handler_address)));
    ldr(r1, MemOperand(r3));
    push(r1);  // next sp
    str(sp, MemOperand(r3));  // chain handler
    // TOS is r0
  } else {
    // Must preserve r0-r3, r5-r7 are available.
    ASSERT(try_location == IN_JS_ENTRY);
    // The parameter pointer is meaningless here and fp does not point to a JS
    // frame. So we save NULL for both pp and fp. We expect the code throwing an
    // exception to check fp before dereferencing it to restore the context.
    mov(r5, Operand(Smi::FromInt(StackHandler::kCodeNotPresent)));  // new TOS
    mov(pp, Operand(0));  // set pp to NULL
    mov(ip, Operand(0));  // to save a NULL fp
    stm(db_w, sp, pp.bit() | ip.bit() | lr.bit());
    mov(r6, Operand(StackHandler::ENTRY));
    push(r6);  // state
    mov(r7, Operand(ExternalReference(Top::k_handler_address)));
    ldr(r6, MemOperand(r7));
    push(r6);  // next sp
    str(sp, MemOperand(r7));  // chain handler
    push(r5);  // flush TOS
  }
}


Register MacroAssembler::CheckMaps(JSObject* object, Register object_reg,
                                   JSObject* holder, Register holder_reg,
                                   Register scratch,
                                   Label* miss) {
  // Make sure there's no overlap between scratch and the other
  // registers.
  ASSERT(!scratch.is(object_reg) && !scratch.is(holder_reg));

  // Keep track of the current object in register reg.
  Register reg = object_reg;
  int depth = 1;

  // Check the maps in the prototype chain.
  // Traverse the prototype chain from the object and do map checks.
  while (object != holder) {
    depth++;

    // Only global objects and objects that do not require access
    // checks are allowed in stubs.
    ASSERT(object->IsJSGlobalObject() || !object->IsAccessCheckNeeded());

    // Get the map of the current object.
    ldr(scratch, FieldMemOperand(reg, HeapObject::kMapOffset));
    cmp(scratch, Operand(Handle<Map>(object->map())));

    // Branch on the result of the map check.
    b(ne, miss);

    // Check access rights to the global object.  This has to happen
    // after the map check so that we know that the object is
    // actually a global object.
    if (object->IsJSGlobalObject()) {
      CheckAccessGlobal(reg, scratch, miss);
      // Restore scratch register to be the map of the object.  In the
      // new space case below, we load the prototype from the map in
      // the scratch register.
      ldr(scratch, FieldMemOperand(reg, HeapObject::kMapOffset));
    }

    reg = holder_reg;  // from now the object is in holder_reg
    JSObject* prototype = JSObject::cast(object->GetPrototype());
    if (Heap::InNewSpace(prototype)) {
      // The prototype is in new space; we cannot store a reference
      // to it in the code. Load it from the map.
      ldr(reg, FieldMemOperand(scratch, Map::kPrototypeOffset));
    } else {
      // The prototype is in old space; load it directly.
      mov(reg, Operand(Handle<JSObject>(prototype)));
    }

    // Go to the next object in the prototype chain.
    object = prototype;
  }

  // Check the holder map.
  ldr(scratch, FieldMemOperand(reg, HeapObject::kMapOffset));
  cmp(scratch, Operand(Handle<Map>(object->map())));
  b(ne, miss);

  // Log the check depth.
  LOG(IntEvent("check-maps-depth", depth));

  // Perform security check for access to the global object and return
  // the holder register.
  ASSERT(object == holder);
  ASSERT(object->IsJSGlobalObject() || !object->IsAccessCheckNeeded());
  if (object->IsJSGlobalObject()) {
    CheckAccessGlobal(reg, scratch, miss);
  }
  return reg;
}


void MacroAssembler::CheckAccessGlobal(Register holder_reg,
                                       Register scratch,
                                       Label* miss) {
  ASSERT(!holder_reg.is(scratch));

  // Load the security context.
  mov(scratch, Operand(Top::security_context_address()));
  ldr(scratch, MemOperand(scratch));
  // In debug mode, make sure the security context is set.
  if (kDebug) {
    cmp(scratch, Operand(0));
    Check(ne, "we should not have an empty security context");
  }

  // Load the global object of the security context.
  int offset = Context::kHeaderSize + Context::GLOBAL_INDEX * kPointerSize;
  ldr(scratch, FieldMemOperand(scratch, offset));
  // Check that the security token in the calling global object is
  // compatible with the security token in the receiving global
  // object.
  ldr(scratch, FieldMemOperand(scratch, JSGlobalObject::kSecurityTokenOffset));
  ldr(ip, FieldMemOperand(holder_reg, JSGlobalObject::kSecurityTokenOffset));
  cmp(scratch, Operand(ip));
  b(ne, miss);
}


void MacroAssembler::CallStub(CodeStub* stub) {
  ASSERT(!generating_stub());  // stub calls are not allowed in stubs
  Call(stub->GetCode(), code_target);
}


void MacroAssembler::CallJSExitStub(CodeStub* stub) {
  ASSERT(!generating_stub());  // stub calls are not allowed in stubs
  Call(stub->GetCode(), exit_js_frame);
}


void MacroAssembler::StubReturn(int argc) {
  ASSERT(argc >= 1 && generating_stub());
  if (argc > 1)
    add(sp, sp, Operand((argc - 1) * kPointerSize));
  Ret();
}

void MacroAssembler::CallRuntime(Runtime::Function* f, int num_arguments) {
  ASSERT(num_arguments >= 1);  // must have receiver for call

  if (f->nargs < 0) {
    // The number of arguments is not constant for this call, or we don't
    // have an entry stub that pushes the value. Push it before the call.
    push(r0);
    // Receiver does not count as an argument.
    mov(r0, Operand(num_arguments - 1));
  } else {
    ASSERT(f->nargs == num_arguments);
  }

  RuntimeStub stub((Runtime::FunctionId) f->stub_id);
  CallStub(&stub);
}


void MacroAssembler::CallRuntime(Runtime::FunctionId fid, int num_arguments) {
  CallRuntime(Runtime::FunctionForId(fid), num_arguments);
}


void MacroAssembler::TailCallRuntime(Runtime::Function* f) {
  // TODO(1236192): Most runtime routines don't need the number of
  // arguments passed in because it is constant. At some point we
  // should remove this need and make the runtime routine entry code
  // smarter.
  if (f->nargs >= 0) {
    // The number of arguments is fixed for this call.
    // Set r0 correspondingly.
    push(r0);
    mov(r0, Operand(f->nargs - 1));  // receiver does not count as an argument
  }
  JumpToBuiltin(ExternalReference(f));  // tail call to runtime routine
}


void MacroAssembler::JumpToBuiltin(const ExternalReference& builtin) {
#if defined(__thumb__)
  // Thumb mode builtin.
  ASSERT((reinterpret_cast<intptr_t>(builtin.address()) & 1) == 1);
#endif
  mov(r1, Operand(builtin));
  CEntryStub stub;
  Jump(stub.GetCode(), code_target);
}


void MacroAssembler::InvokeBuiltin(const char* name,
                                   int argc,
                                   InvokeJSFlags flags) {
  Handle<String> symbol = Factory::LookupAsciiSymbol(name);
  Object* object = Top::security_context_builtins()->GetProperty(*symbol);
  bool unresolved = true;
  Code* code = Builtins::builtin(Builtins::Illegal);

  if (object->IsJSFunction()) {
    Handle<JSFunction> function(JSFunction::cast(object));
    if (function->is_compiled() || CompileLazy(function, CLEAR_EXCEPTION)) {
      code = function->code();
      unresolved = false;
    }
  }

  if (flags == CALL_JS) {
    Call(Handle<Code>(code), code_target);
  } else {
    ASSERT(flags == JUMP_JS);
    Jump(Handle<Code>(code), code_target);
  }

  if (unresolved) {
    uint32_t flags =
        Bootstrapper::FixupFlagsArgumentsCount::encode(argc) |
        Bootstrapper::FixupFlagsIsPCRelative::encode(false);
    Unresolved entry = { pc_offset() - sizeof(Instr), flags, name };
    unresolved_.Add(entry);
  }
}


void MacroAssembler::Assert(Condition cc, const char* msg) {
  if (FLAG_debug_code)
    Check(cc, msg);
}


void MacroAssembler::Check(Condition cc, const char* msg) {
  Label L;
  b(cc, &L);
  Abort(msg);
  // will not return here
  bind(&L);
}


void MacroAssembler::Abort(const char* msg) {
  // We want to pass the msg string like a smi to avoid GC
  // problems, however msg is not guaranteed to be aligned
  // properly. Instead, we pass an aligned pointer that is
  // a proper v8 smi, but also pass the aligment difference
  // from the real pointer as a smi.
  intptr_t p1 = reinterpret_cast<intptr_t>(msg);
  intptr_t p0 = (p1 & ~kSmiTagMask) + kSmiTag;
  ASSERT(reinterpret_cast<Object*>(p0)->IsSmi());
#ifdef DEBUG
  if (msg != NULL) {
    RecordComment("Abort message: ");
    RecordComment(msg);
  }
#endif
  push(r0);
  mov(r0, Operand(p0));
  push(r0);
  mov(r0, Operand(Smi::FromInt(p1 - p0)));
  CallRuntime(Runtime::kAbort, 2);
  // will not return here
}

} }  // namespace v8::internal
