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

#include "frames-inl.h"
#include "scopeinfo.h"
#include "string-stream.h"
#include "top.h"
#include "zone-inl.h"

namespace v8 { namespace internal {


DEFINE_int(max_stack_trace_source_length, 300,
           "maximum length of function source code printed in a stack trace.");


// -------------------------------------------------------------------------


// Iterator that supports traversing the stack handlers of a
// particular frame. Needs to know the top of the handler chain.
class StackHandlerIterator BASE_EMBEDDED {
 public:
  StackHandlerIterator(const StackFrame* frame, StackHandler* handler)
      : limit_(frame->fp()), handler_(handler) {
    // Make sure the handler has already been unwound to this frame.
    ASSERT(frame->sp() <= handler->address());
  }

  StackHandler* handler() const { return handler_; }

  bool done() { return handler_->address() > limit_; }
  void Advance() {
    ASSERT(!done());
    handler_ = handler_->next();
  }

 private:
  const Address limit_;
  StackHandler* handler_;
};


// -------------------------------------------------------------------------


#define INITIALIZE_SINGLETON(type, field) field##_(this),
StackFrameIterator::StackFrameIterator()
    : STACK_FRAME_TYPE_LIST(INITIALIZE_SINGLETON)
      frame_(NULL), handler_(NULL), thread(Top::GetCurrentThread()) {
  Reset();
}
StackFrameIterator::StackFrameIterator(ThreadLocalTop* t)
    : STACK_FRAME_TYPE_LIST(INITIALIZE_SINGLETON)
      frame_(NULL), handler_(NULL), thread(t) {
  Reset();
}
#undef INITIALIZE_SINGLETON


void StackFrameIterator::Advance() {
  ASSERT(!done());
  // Compute the state of the calling frame before restoring
  // callee-saved registers and unwinding handlers. This allows the
  // frame code that computes the caller state to access the top
  // handler and the value of any callee-saved register if needed.
  StackFrame::State state;
  StackFrame::Type type = frame_->GetCallerState(&state);

  // Restore any callee-saved registers to the register buffer. Avoid
  // the virtual call if the platform doesn't have any callee-saved
  // registers.
  if (kNumJSCalleeSaved > 0) {
    frame_->RestoreCalleeSavedRegisters(register_buffer());
  }

  // Unwind handlers corresponding to the current frame.
  StackHandlerIterator it(frame_, handler_);
  while (!it.done()) it.Advance();
  handler_ = it.handler();

  // Advance to the calling frame.
  frame_ = SingletonFor(type, &state);

  // When we're done iterating over the stack frames, the handler
  // chain must have been completely unwound.
  ASSERT(!done() || handler_ == NULL);
}


void StackFrameIterator::Reset() {
  Address fp = Top::c_entry_fp(thread);
  StackFrame::State state;
  StackFrame::Type type = ExitFrame::GetStateForFramePointer(fp, &state);
  frame_ = SingletonFor(type, &state);
  handler_ = StackHandler::FromAddress(Top::handler(thread));
  // Zap the register buffer in debug mode.
  if (kDebug) {
    Object** buffer = register_buffer();
    for (int i = 0; i < kNumJSCalleeSaved; i++) {
      buffer[i] = reinterpret_cast<Object*>(kZapValue);
    }
  }
}


Object** StackFrameIterator::RestoreCalleeSavedForTopHandler(Object** buffer) {
  ASSERT(kNumJSCalleeSaved > 0);
  // Traverse the frames until we find the frame containing the top
  // handler. Such a frame is guaranteed to always exists by the
  // callers of this function.
  for (StackFrameIterator it; true; it.Advance()) {
    StackHandlerIterator handlers(it.frame(), it.handler());
    if (!handlers.done()) {
      memcpy(buffer, it.register_buffer(), kNumJSCalleeSaved * kPointerSize);
      return buffer;
    }
  }
}


StackFrame* StackFrameIterator::SingletonFor(StackFrame::Type type,
                                             StackFrame::State* state) {
#define FRAME_TYPE_CASE(type, field) \
  case StackFrame::type: result = &field##_; break;

  StackFrame* result = NULL;
  switch (type) {
    case StackFrame::NONE: return NULL;
    STACK_FRAME_TYPE_LIST(FRAME_TYPE_CASE)
    default: break;
  }
  ASSERT(result != NULL);
  result->state_ = *state;
  return result;

#undef FRAME_TYPE_CASE
}


// -------------------------------------------------------------------------


JavaScriptFrameIterator::JavaScriptFrameIterator(StackFrame::Id id) {
  while (true) {
    Advance();
    if (frame()->id() == id) return;
  }
}


void JavaScriptFrameIterator::Advance() {
  do {
    iterator_.Advance();
  } while (!iterator_.done() && !iterator_.frame()->is_java_script());
}


void JavaScriptFrameIterator::AdvanceToArgumentsFrame() {
  if (!frame()->has_adapted_arguments()) return;
  iterator_.Advance();
  ASSERT(iterator_.frame()->is_arguments_adaptor());
}


void JavaScriptFrameIterator::Reset() {
  iterator_.Reset();
  Advance();
}


// -------------------------------------------------------------------------


void StackHandler::Cook(Code* code) {
  ASSERT(code->contains(pc()));
  set_pc(AddressFrom<Address>(pc() - code->instruction_start()));
}


void StackHandler::Uncook(Code* code) {
  set_pc(code->instruction_start() + OffsetFrom(pc()));
  ASSERT(code->contains(pc()));
}


// -------------------------------------------------------------------------


bool StackFrame::HasHandler() const {
  StackHandlerIterator it(this, top_handler());
  return !it.done();
}


void StackFrame::CookFramesForThread(ThreadLocalTop* thread) {
  ASSERT(!thread->stack_is_cooked());
  for (StackFrameIterator it(thread); !it.done(); it.Advance()) {
    it.frame()->Cook();
  }
  thread->set_stack_is_cooked(true);
}


void StackFrame::UncookFramesForThread(ThreadLocalTop* thread) {
  ASSERT(thread->stack_is_cooked());
  for (StackFrameIterator it(thread); !it.done(); it.Advance()) {
    it.frame()->Uncook();
  }
  thread->set_stack_is_cooked(false);
}


void StackFrame::Cook() {
  Code* code = FindCode();
  for (StackHandlerIterator it(this, top_handler()); !it.done(); it.Advance()) {
    it.handler()->Cook(code);
  }
  ASSERT(code->contains(pc()));
  set_pc(AddressFrom<Address>(pc() - code->instruction_start()));
}


void StackFrame::Uncook() {
  Code* code = FindCode();
  for (StackHandlerIterator it(this, top_handler()); !it.done(); it.Advance()) {
    it.handler()->Uncook(code);
  }
  set_pc(code->instruction_start() + OffsetFrom(pc()));
  ASSERT(code->contains(pc()));
}


Code* EntryFrame::FindCode() const {
  return Heap::js_entry_code();
}


StackFrame::Type EntryFrame::GetCallerState(State* state) const {
  const int offset = EntryFrameConstants::kCallerFPOffset;
  Address fp = Memory::Address_at(this->fp() + offset);
  return ExitFrame::GetStateForFramePointer(fp, state);
}


Code* EntryConstructFrame::FindCode() const {
  return Heap::js_construct_entry_code();
}


Code* ExitFrame::FindCode() const {
  return Heap::c_entry_code();
}


StackFrame::Type ExitFrame::GetCallerState(State* state) const {
  // Setup the caller state.
  state->sp = pp();
  state->fp = Memory::Address_at(fp() + ExitFrameConstants::kCallerFPOffset);
#ifdef USE_OLD_CALLING_CONVENTIONS
  state->pp = Memory::Address_at(fp() + ExitFrameConstants::kCallerPPOffset);
#endif
  state->pc_address
      = reinterpret_cast<Address*>(fp() + ExitFrameConstants::kCallerPCOffset);
  return ComputeType(state);
}


Address ExitFrame::GetCallerStackPointer() const {
  return fp() + ExitFrameConstants::kPPDisplacement;
}


Code* ExitDebugFrame::FindCode() const {
  return Heap::c_entry_debug_break_code();
}


RegList ExitFrame::FindCalleeSavedRegisters() const {
  // Exit frames save all - if any - callee-saved registers.
  return kJSCalleeSaved;
}


Address StandardFrame::GetExpressionAddress(int n) const {
  ASSERT(0 <= n && n < ComputeExpressionsCount());
  if (kNumJSCalleeSaved > 0 && n < kNumJSCalleeSaved) {
    return reinterpret_cast<Address>(top_register_buffer() + n);
  } else {
    const int offset = StandardFrameConstants::kExpressionsOffset;
    return fp() + offset - (n - kNumJSCalleeSaved) * kPointerSize;
  }
}


int StandardFrame::ComputeExpressionsCount() const {
  const int offset =
      StandardFrameConstants::kExpressionsOffset + kPointerSize;
  Address base = fp() + offset;
  Address limit = sp();
  ASSERT(base >= limit);  // stack grows downwards
  // Include register-allocated locals in number of expressions.
  return (base - limit) / kPointerSize + kNumJSCalleeSaved;
}


StackFrame::Type StandardFrame::GetCallerState(State* state) const {
  state->sp = caller_sp();
  state->fp = caller_fp();
#ifdef USE_OLD_CALLING_CONVENTIONS
  state->pp = caller_pp();
#endif
  state->pc_address = reinterpret_cast<Address*>(ComputePCAddress(fp()));
  return ComputeType(state);
}


bool StandardFrame::IsExpressionInsideHandler(int n) const {
  Address address = GetExpressionAddress(n);
  for (StackHandlerIterator it(this, top_handler()); !it.done(); it.Advance()) {
    if (it.handler()->includes(address)) return true;
  }
  return false;
}


Object* JavaScriptFrame::GetParameter(int index) const {
  ASSERT(index >= 0 && index < ComputeParametersCount());
  const int offset = JavaScriptFrameConstants::kParam0Offset;
  return Memory::Object_at(pp() + offset - (index * kPointerSize));
}


int JavaScriptFrame::ComputeParametersCount() const {
  Address base  = pp() + JavaScriptFrameConstants::kReceiverOffset;
  Address limit = fp() + JavaScriptFrameConstants::kSavedRegistersOffset;
  int result = (base - limit) / kPointerSize;
  if (kNumJSCalleeSaved > 0) {
    return result - NumRegs(FindCalleeSavedRegisters());
  } else {
    return result;
  }
}


bool JavaScriptFrame::IsConstructor() const {
  Address pc = has_adapted_arguments()
      ? Memory::Address_at(ComputePCAddress(caller_fp()))
      : caller_pc();
  return IsConstructTrampolineFrame(pc);
}


Code* ArgumentsAdaptorFrame::FindCode() const {
  return Builtins::builtin(Builtins::ArgumentsAdaptorTrampoline);
}


Code* InternalFrame::FindCode() const {
  const int offset = InternalFrameConstants::kCodeOffset;
  Object* code = Memory::Object_at(fp() + offset);
  if (code == NULL) {
    // The code object isn't set; find it and set it.
    code = Heap::FindCodeObject(pc());
    ASSERT(!code->IsFailure());
    Memory::Object_at(fp() + offset) = code;
  }
  ASSERT(code != NULL);
  return Code::cast(code);
}


void StackFrame::PrintIndex(StringStream* accumulator,
                            PrintMode mode,
                            int index) {
  accumulator->Add((mode == OVERVIEW) ? "%5d: " : "[%d]: ", index);
}


void JavaScriptFrame::Print(StringStream* accumulator,
                            PrintMode mode,
                            int index) const {
  HandleScope scope;
  Object* receiver = this->receiver();
  Object* function = this->function();

  accumulator->PrintSecurityTokenIfChanged(function);
  PrintIndex(accumulator, mode, index);
  Code* code = NULL;
  if (IsConstructor()) accumulator->Add("new ");
  accumulator->PrintFunction(function, receiver, &code);
  accumulator->Add("(this=%o", receiver);

  // Get scope information for nicer output, if possible. If code is
  // NULL, or doesn't contain scope info, info will return 0 for the
  // number of parameters, stack slots, or context slots.
  ScopeInfo<PreallocatedStorage> info(code);

  // Print the parameters.
  int parameters_count = ComputeParametersCount();
  for (int i = 0; i < parameters_count; i++) {
    accumulator->Add(",");
    // If we have a name for the parameter we print it. Nameless
    // parameters are either because we have more actual parameters
    // than formal parameters or because we have no scope information.
    if (i < info.number_of_parameters()) {
      accumulator->PrintName(*info.parameter_name(i));
      accumulator->Add("=");
    }
    accumulator->Add("%o", GetParameter(i));
  }

  accumulator->Add(")");
  if (mode == OVERVIEW) {
    accumulator->Add("\n");
    return;
  }
  accumulator->Add(" {\n");

  // Compute the number of locals and expression stack elements.
  int stack_locals_count = info.number_of_stack_slots();
  int heap_locals_count = info.number_of_context_slots();
  int expressions_count = ComputeExpressionsCount();

  // Print stack-allocated local variables.
  if (stack_locals_count > 0) {
    accumulator->Add("  // stack-allocated locals\n");
  }
  for (int i = 0; i < stack_locals_count; i++) {
    accumulator->Add("  var ");
    accumulator->PrintName(*info.stack_slot_name(i));
    accumulator->Add(" = ");
    if (i < expressions_count) {
      accumulator->Add("%o", GetExpression(i));
    } else {
      accumulator->Add("// no expression found - inconsistent frame?");
    }
    accumulator->Add("\n");
  }

  // Try to get hold of the context of this frame.
  Context* context = NULL;
  if (this->context() != NULL && this->context()->IsContext()) {
    context = Context::cast(this->context());
  }

  // Print heap-allocated local variables.
  if (heap_locals_count > Context::MIN_CONTEXT_SLOTS) {
    accumulator->Add("  // heap-allocated locals\n");
  }
  for (int i = Context::MIN_CONTEXT_SLOTS; i < heap_locals_count; i++) {
    accumulator->Add("  var ");
    accumulator->PrintName(*info.context_slot_name(i));
    accumulator->Add(" = ");
    if (context != NULL) {
      if (i < context->length()) {
        accumulator->Add("%o", context->get(i));
      } else {
        accumulator->Add(
            "// warning: missing context slot - inconsistent frame?");
      }
    } else {
      accumulator->Add("// warning: no context found - inconsistent frame?");
    }
    accumulator->Add("\n");
  }

  // Print the expression stack.
  int expressions_start = Max(stack_locals_count, kNumJSCalleeSaved);
  if (expressions_start < expressions_count) {
    accumulator->Add("  // expression stack (top to bottom)\n");
  }
  for (int i = expressions_count - 1; i >= expressions_start; i--) {
    if (IsExpressionInsideHandler(i)) continue;
    accumulator->Add("  [%02d] : %o\n", i, GetExpression(i));
  }

  // Print details about the function.
  if (FLAG_max_stack_trace_source_length != 0 && code != NULL) {
    SharedFunctionInfo* shared = JSFunction::cast(function)->shared();
    accumulator->Add("--------- s o u r c e   c o d e ---------\n");
    shared->SourceCodePrint(accumulator, FLAG_max_stack_trace_source_length);
    accumulator->Add("\n-----------------------------------------\n");
  }

  accumulator->Add("}\n\n");
}


void ArgumentsAdaptorFrame::Print(StringStream* accumulator,
                                  PrintMode mode,
                                  int index) const {
  int actual = ComputeParametersCount();
  int expected = -1;
  Object* function = this->function();
  if (function->IsJSFunction()) {
    expected = JSFunction::cast(function)->shared()->formal_parameter_count();
  }

  PrintIndex(accumulator, mode, index);
  accumulator->Add("arguments adaptor frame: %d->%d", actual, expected);
  if (mode == OVERVIEW) {
    accumulator->Add("\n");
    return;
  }
  accumulator->Add(" {\n");

  // Print actual arguments.
  if (actual > 0) accumulator->Add("  // actual arguments\n");
  for (int i = 0; i < actual; i++) {
    accumulator->Add("  [%02d] : %o", i, GetParameter(i));
    if (expected != -1 && i >= expected) {
      accumulator->Add("  // not passed to callee");
    }
    accumulator->Add("\n");
  }

  accumulator->Add("}\n\n");
}


void EntryFrame::Iterate(ObjectVisitor* v) const {
  StackHandlerIterator it(this, top_handler());
  ASSERT(!it.done());
  StackHandler* handler = it.handler();
  ASSERT(handler->is_entry());
  handler->Iterate(v);
  // Make sure that there's the entry frame does not contain more than
  // one stack handler.
  if (kDebug) {
    it.Advance();
    ASSERT(it.done());
  }
}


void StandardFrame::IterateExpressions(ObjectVisitor* v) const {
  const int offset = StandardFrameConstants::kContextOffset;
  Object** base = &Memory::Object_at(sp());
  Object** limit = &Memory::Object_at(fp() + offset) + 1;
  for (StackHandlerIterator it(this, top_handler()); !it.done(); it.Advance()) {
    StackHandler* handler = it.handler();
    // Traverse pointers down to - but not including - the next
    // handler in the handler chain. Update the base to skip the
    // handler and allow the handler to traverse its own pointers.
    const Address address = handler->address();
    v->VisitPointers(base, reinterpret_cast<Object**>(address));
    base = reinterpret_cast<Object**>(address + StackHandlerConstants::kSize);
    // Traverse the pointers in the handler itself.
    handler->Iterate(v);
  }
  v->VisitPointers(base, limit);
}


void JavaScriptFrame::Iterate(ObjectVisitor* v) const {
  IterateExpressions(v);

  // Traverse callee-saved registers, receiver, and parameters.
  const int kBaseOffset = JavaScriptFrameConstants::kSavedRegistersOffset;
  const int kLimitOffset = JavaScriptFrameConstants::kReceiverOffset;
  Object** base = &Memory::Object_at(fp() + kBaseOffset);
  Object** limit = &Memory::Object_at(pp() + kLimitOffset) + 1;
  v->VisitPointers(base, limit);
}


void InternalFrame::Iterate(ObjectVisitor* v) const {
  // Internal frames only have object pointers on the expression stack
  // as they never have any arguments.
  IterateExpressions(v);
}


// -------------------------------------------------------------------------


JavaScriptFrame* StackFrameLocator::FindJavaScriptFrame(int n) {
  ASSERT(n >= 0);
  for (int i = 0; i <= n; i++) {
    while (!iterator_.frame()->is_java_script()) iterator_.Advance();
    if (i == n) return JavaScriptFrame::cast(iterator_.frame());
    iterator_.Advance();
  }
  UNREACHABLE();
  return NULL;
}


// -------------------------------------------------------------------------


int NumRegs(RegList reglist) {
  int n = 0;
  while (reglist != 0) {
    n++;
    reglist &= reglist - 1;  // clear one bit
  }
  return n;
}


int JSCallerSavedCode(int n) {
  static int reg_code[kNumJSCallerSaved];
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    int i = 0;
    for (int r = 0; r < kNumRegs; r++)
      if ((kJSCallerSaved & (1 << r)) != 0)
        reg_code[i++] = r;

    ASSERT(i == kNumJSCallerSaved);
  }
  ASSERT(0 <= n && n < kNumJSCallerSaved);
  return reg_code[n];
}


int JSCalleeSavedCode(int n) {
  static int reg_code[kNumJSCalleeSaved + 1];  // avoid zero-size array error
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    int i = 0;
    for (int r = 0; r < kNumRegs; r++)
      if ((kJSCalleeSaved & (1 << r)) != 0)
        reg_code[i++] = r;

    ASSERT(i == kNumJSCalleeSaved);
  }
  ASSERT(0 <= n && n < kNumJSCalleeSaved);
  return reg_code[n];
}


RegList JSCalleeSavedList(int n) {
  // avoid zero-size array error
  static RegList reg_list[kNumJSCalleeSaved + 1];
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    reg_list[0] = 0;
    for (int i = 0; i < kNumJSCalleeSaved; i++)
      reg_list[i+1] = reg_list[i] + (1 << JSCalleeSavedCode(i));
  }
  ASSERT(0 <= n && n <= kNumJSCalleeSaved);
  return reg_list[n];
}


} }  // namespace v8::internal
