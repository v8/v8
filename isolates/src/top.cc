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

#include "v8.h"

#include "api.h"
#include "bootstrapper.h"
#include "debug.h"
#include "execution.h"
#include "messages.h"
#include "platform.h"
#include "simulator.h"
#include "string-stream.h"

namespace v8 {
namespace internal {


v8::TryCatch* ThreadLocalTop::TryCatchHandler() {
  return TRY_CATCH_FROM_ADDRESS(try_catch_handler_address());
}


void ThreadLocalTop::Initialize() {
  c_entry_fp_ = 0;
  handler_ = 0;
#ifdef ENABLE_LOGGING_AND_PROFILING
  js_entry_sp_ = 0;
#endif
  stack_is_cooked_ = false;
  try_catch_handler_address_ = NULL;
  context_ = NULL;
  int id = Isolate::Current()->thread_manager()->CurrentId();
  thread_id_ = (id == 0) ? ThreadManager::kInvalidId : id;
  external_caught_exception_ = false;
  failed_access_check_callback_ = NULL;
  save_context_ = NULL;
  catcher_ = NULL;
}


Address Isolate::get_address_from_id(Isolate::AddressId id) {
  return isolate_addresses_[id];
}


char* Isolate::Iterate(ObjectVisitor* v, char* thread_storage) {
  ThreadLocalTop* thread = reinterpret_cast<ThreadLocalTop*>(thread_storage);
  Iterate(v, thread);
  return thread_storage + sizeof(ThreadLocalTop);
}


void Isolate::IterateThread(ThreadVisitor* v) {
  v->VisitThread(thread_local_top());
}


void Isolate::IterateThread(ThreadVisitor* v, char* t) {
  ThreadLocalTop* thread = reinterpret_cast<ThreadLocalTop*>(t);
  v->VisitThread(thread);
}


void Isolate::Iterate(ObjectVisitor* v, ThreadLocalTop* thread) {
  v->VisitPointer(&(thread->pending_exception_));
  v->VisitPointer(&(thread->pending_message_obj_));
  v->VisitPointer(
      BitCast<Object**, Script**>(&(thread->pending_message_script_)));
  v->VisitPointer(BitCast<Object**, Context**>(&(thread->context_)));
  v->VisitPointer(&(thread->scheduled_exception_));

  for (v8::TryCatch* block = thread->TryCatchHandler();
       block != NULL;
       block = TRY_CATCH_FROM_ADDRESS(block->next_)) {
    v->VisitPointer(BitCast<Object**, void**>(&(block->exception_)));
    v->VisitPointer(BitCast<Object**, void**>(&(block->message_)));
  }

  // Iterate over pointers on native execution stack.
  for (StackFrameIterator it(thread); !it.done(); it.Advance()) {
    it.frame()->Iterate(v);
  }
}


void Isolate::Iterate(ObjectVisitor* v) {
  ThreadLocalTop* current_t = thread_local_top();
  Iterate(v, current_t);
}


void Isolate::RegisterTryCatchHandler(v8::TryCatch* that) {
  // The ARM simulator has a separate JS stack.  We therefore register
  // the C++ try catch handler with the simulator and get back an
  // address that can be used for comparisons with addresses into the
  // JS stack.  When running without the simulator, the address
  // returned will be the address of the C++ try catch handler itself.
  Address address = reinterpret_cast<Address>(
      SimulatorStack::RegisterCTryCatch(reinterpret_cast<uintptr_t>(that)));
  thread_local_top()->set_try_catch_handler_address(address);
}


void Isolate::UnregisterTryCatchHandler(v8::TryCatch* that) {
  ASSERT(thread_local_top()->TryCatchHandler() == that);
  thread_local_top()->set_try_catch_handler_address(
      reinterpret_cast<Address>(that->next_));
  thread_local_top()->catcher_ = NULL;
  SimulatorStack::UnregisterCTryCatch();
}


void Isolate::MarkCompactPrologue(bool is_compacting) {
  MarkCompactPrologue(is_compacting, thread_local_top());
}


void Isolate::MarkCompactPrologue(bool is_compacting, char* data) {
  MarkCompactPrologue(is_compacting, reinterpret_cast<ThreadLocalTop*>(data));
}


void Isolate::MarkCompactPrologue(bool is_compacting, ThreadLocalTop* thread) {
  if (is_compacting) {
    StackFrame::CookFramesForThread(thread);
  }
}


void Isolate::MarkCompactEpilogue(bool is_compacting, char* data) {
  MarkCompactEpilogue(is_compacting, reinterpret_cast<ThreadLocalTop*>(data));
}


void Isolate::MarkCompactEpilogue(bool is_compacting) {
  MarkCompactEpilogue(is_compacting, thread_local_top());
}


void Isolate::MarkCompactEpilogue(bool is_compacting, ThreadLocalTop* thread) {
  if (is_compacting) {
    StackFrame::UncookFramesForThread(thread);
  }
}


Handle<String> Isolate::StackTraceString() {
  if (stack_trace_nesting_level_ == 0) {
    stack_trace_nesting_level_++;
    HeapStringAllocator allocator;
    StringStream::ClearMentionedObjectCache();
    StringStream accumulator(&allocator);
    incomplete_message_ = &accumulator;
    PrintStack(&accumulator);
    Handle<String> stack_trace = accumulator.ToString();
    incomplete_message_ = NULL;
    stack_trace_nesting_level_ = 0;
    return stack_trace;
  } else if (stack_trace_nesting_level_ == 1) {
    stack_trace_nesting_level_++;
    OS::PrintError(
      "\n\nAttempt to print stack while printing stack (double fault)\n");
    OS::PrintError(
      "If you are lucky you may find a partial stack dump on stdout.\n\n");
    incomplete_message_->OutputToStdOut();
    return Factory::empty_symbol();
  } else {
    OS::Abort();
    // Unreachable
    return Factory::empty_symbol();
  }
}


Local<StackTrace> Isolate::CaptureCurrentStackTrace(
    int frame_limit, StackTrace::StackTraceOptions options) {
  v8::HandleScope scope;
  // Ensure no negative values.
  int limit = Max(frame_limit, 0);
  Handle<JSArray> stack_trace = Factory::NewJSArray(frame_limit);

  Handle<String> column_key =  Factory::LookupAsciiSymbol("column");
  Handle<String> line_key =  Factory::LookupAsciiSymbol("lineNumber");
  Handle<String> script_key =  Factory::LookupAsciiSymbol("scriptName");
  Handle<String> function_key =  Factory::LookupAsciiSymbol("functionName");
  Handle<String> eval_key =  Factory::LookupAsciiSymbol("isEval");
  Handle<String> constructor_key =  Factory::LookupAsciiSymbol("isConstructor");

  StackTraceFrameIterator it;
  int frames_seen = 0;
  while (!it.done() && (frames_seen < limit)) {
    // Create a JSObject to hold the information for the StackFrame.
    Handle<JSObject> stackFrame = Factory::NewJSObject(object_function());

    JavaScriptFrame* frame = it.frame();
    JSFunction* fun(JSFunction::cast(frame->function()));
    Script* script = Script::cast(fun->shared()->script());

    if (options & StackTrace::kLineNumber) {
      int script_line_offset = script->line_offset()->value();
      int position = frame->code()->SourcePosition(frame->pc());
      int line_number = GetScriptLineNumber(Handle<Script>(script), position);
      // line_number is already shifted by the script_line_offset.
      int relative_line_number = line_number - script_line_offset;
      if (options & StackTrace::kColumnOffset && relative_line_number >= 0) {
        Handle<FixedArray> line_ends(FixedArray::cast(script->line_ends()));
        int start = (relative_line_number == 0) ? 0 :
            Smi::cast(line_ends->get(relative_line_number - 1))->value() + 1;
        int column_offset = position - start;
        if (relative_line_number == 0) {
          // For the case where the code is on the same line as the script tag.
          column_offset += script->column_offset()->value();
        }
        SetProperty(stackFrame, column_key,
                    Handle<Smi>(Smi::FromInt(column_offset + 1)), NONE);
      }
      SetProperty(stackFrame, line_key,
                  Handle<Smi>(Smi::FromInt(line_number + 1)), NONE);
    }

    if (options & StackTrace::kScriptName) {
      Handle<Object> script_name(script->name());
      SetProperty(stackFrame, script_key, script_name, NONE);
    }

    if (options & StackTrace::kFunctionName) {
      Handle<Object> fun_name(fun->shared()->name());
      if (fun_name->ToBoolean()->IsFalse()) {
        fun_name = Handle<Object>(fun->shared()->inferred_name());
      }
      SetProperty(stackFrame, function_key, fun_name, NONE);
    }

    if (options & StackTrace::kIsEval) {
      int type = Smi::cast(script->compilation_type())->value();
      Handle<Object> is_eval = (type == Script::COMPILATION_TYPE_EVAL) ?
          Factory::true_value() : Factory::false_value();
      SetProperty(stackFrame, eval_key, is_eval, NONE);
    }

    if (options & StackTrace::kIsConstructor) {
      Handle<Object> is_constructor = (frame->IsConstructor()) ?
          Factory::true_value() : Factory::false_value();
      SetProperty(stackFrame, constructor_key, is_constructor, NONE);
    }

    FixedArray::cast(stack_trace->elements())->set(frames_seen, *stackFrame);
    frames_seen++;
    it.Advance();
  }

  stack_trace->set_length(Smi::FromInt(frames_seen));
  return scope.Close(Utils::StackTraceToLocal(stack_trace));
}


void Isolate::PrintStack() {
  if (stack_trace_nesting_level_ == 0) {
    stack_trace_nesting_level_++;

    StringAllocator* allocator;
    if (preallocated_message_space_ == NULL) {
      allocator = new HeapStringAllocator();
    } else {
      allocator = preallocated_message_space_;
    }

    NativeAllocationChecker allocation_checker(
      !FLAG_preallocate_message_memory ?
      NativeAllocationChecker::ALLOW :
      NativeAllocationChecker::DISALLOW);

    StringStream::ClearMentionedObjectCache();
    StringStream accumulator(allocator);
    incomplete_message_ = &accumulator;
    PrintStack(&accumulator);
    accumulator.OutputToStdOut();
    accumulator.Log();
    incomplete_message_ = NULL;
    stack_trace_nesting_level_ = 0;
    if (preallocated_message_space_ == NULL) {
      // Remove the HeapStringAllocator created above.
      delete allocator;
    }
  } else if (stack_trace_nesting_level_ == 1) {
    stack_trace_nesting_level_++;
    OS::PrintError(
      "\n\nAttempt to print stack while printing stack (double fault)\n");
    OS::PrintError(
      "If you are lucky you may find a partial stack dump on stdout.\n\n");
    incomplete_message_->OutputToStdOut();
  }
}


static void PrintFrames(StringStream* accumulator,
                        StackFrame::PrintMode mode) {
  StackFrameIterator it;
  for (int i = 0; !it.done(); it.Advance()) {
    it.frame()->Print(accumulator, mode, i++);
  }
}


void Isolate::PrintStack(StringStream* accumulator) {
  // The MentionedObjectCache is not GC-proof at the moment.
  AssertNoAllocation nogc;
  ASSERT(StringStream::IsMentionedObjectCacheClear());

  // Avoid printing anything if there are no frames.
  if (c_entry_fp(thread_local_top()) == 0) return;

  accumulator->Add(
      "\n==== Stack trace ============================================\n\n");
  PrintFrames(accumulator, StackFrame::OVERVIEW);

  accumulator->Add(
      "\n==== Details ================================================\n\n");
  PrintFrames(accumulator, StackFrame::DETAILS);

  accumulator->PrintMentionedObjectCache();
  accumulator->Add("=====================\n\n");
}


void Isolate::SetFailedAccessCheckCallback(
    v8::FailedAccessCheckCallback callback) {
  ASSERT(thread_local_top()->failed_access_check_callback_ == NULL);
  thread_local_top()->failed_access_check_callback_ = callback;
}


void Isolate::ReportFailedAccessCheck(JSObject* receiver, v8::AccessType type) {
  if (!thread_local_top()->failed_access_check_callback_) return;

  ASSERT(receiver->IsAccessCheckNeeded());
  ASSERT(context());
  // The callers of this method are not expecting a GC.
  AssertNoAllocation no_gc;

  // Get the data object from access check info.
  JSFunction* constructor = JSFunction::cast(receiver->map()->constructor());
  if (!constructor->shared()->IsApiFunction()) return;
  Object* data_obj =
      constructor->shared()->get_api_func_data()->access_check_info();
  if (data_obj == heap_.undefined_value()) return;

  HandleScope scope;
  Handle<JSObject> receiver_handle(receiver);
  Handle<Object> data(AccessCheckInfo::cast(data_obj)->data());
  thread_local_top()->failed_access_check_callback_(
    v8::Utils::ToLocal(receiver_handle),
    type,
    v8::Utils::ToLocal(data));
}


enum MayAccessDecision {
  YES, NO, UNKNOWN
};


static MayAccessDecision MayAccessPreCheck(Isolate* isolate,
                                           JSObject* receiver,
                                           v8::AccessType type) {
  // During bootstrapping, callback functions are not enabled yet.
  if (isolate->bootstrapper()->IsActive()) return YES;

  if (receiver->IsJSGlobalProxy()) {
    Object* receiver_context = JSGlobalProxy::cast(receiver)->context();
    if (!receiver_context->IsContext()) return NO;

    // Get the global context of current top context.
    // avoid using Isolate::global_context() because it uses Handle.
    Context* global_context = isolate->context()->global()->global_context();
    if (receiver_context == global_context) return YES;

    if (Context::cast(receiver_context)->security_token() ==
        global_context->security_token())
      return YES;
  }

  return UNKNOWN;
}


bool Isolate::MayNamedAccess(JSObject* receiver, Object* key,
                             v8::AccessType type) {
  ASSERT(receiver->IsAccessCheckNeeded());

  // The callers of this method are not expecting a GC.
  AssertNoAllocation no_gc;

  // Skip checks for hidden properties access.  Note, we do not
  // require existence of a context in this case.
  if (key == heap_.hidden_symbol()) return true;

  // Check for compatibility between the security tokens in the
  // current lexical context and the accessed object.
  ASSERT(context());

  MayAccessDecision decision = MayAccessPreCheck(this, receiver, type);
  if (decision != UNKNOWN) return decision == YES;

  // Get named access check callback
  JSFunction* constructor = JSFunction::cast(receiver->map()->constructor());
  if (!constructor->shared()->IsApiFunction()) return false;

  Object* data_obj =
     constructor->shared()->get_api_func_data()->access_check_info();
  if (data_obj == heap_.undefined_value()) return false;

  Object* fun_obj = AccessCheckInfo::cast(data_obj)->named_callback();
  v8::NamedSecurityCallback callback =
      v8::ToCData<v8::NamedSecurityCallback>(fun_obj);

  if (!callback) return false;

  HandleScope scope;
  Handle<JSObject> receiver_handle(receiver);
  Handle<Object> key_handle(key);
  Handle<Object> data(AccessCheckInfo::cast(data_obj)->data());
  LOG(ApiNamedSecurityCheck(key));
  bool result = false;
  {
    // Leaving JavaScript.
    VMState state(EXTERNAL);
    result = callback(v8::Utils::ToLocal(receiver_handle),
                      v8::Utils::ToLocal(key_handle),
                      type,
                      v8::Utils::ToLocal(data));
  }
  return result;
}


bool Isolate::MayIndexedAccess(JSObject* receiver,
                               uint32_t index,
                               v8::AccessType type) {
  ASSERT(receiver->IsAccessCheckNeeded());
  // Check for compatibility between the security tokens in the
  // current lexical context and the accessed object.
  ASSERT(context());
  // The callers of this method are not expecting a GC.
  AssertNoAllocation no_gc;

  MayAccessDecision decision = MayAccessPreCheck(this, receiver, type);
  if (decision != UNKNOWN) return decision == YES;

  // Get indexed access check callback
  JSFunction* constructor = JSFunction::cast(receiver->map()->constructor());
  if (!constructor->shared()->IsApiFunction()) return false;

  Object* data_obj =
      constructor->shared()->get_api_func_data()->access_check_info();
  if (data_obj == heap_.undefined_value()) return false;

  Object* fun_obj = AccessCheckInfo::cast(data_obj)->indexed_callback();
  v8::IndexedSecurityCallback callback =
      v8::ToCData<v8::IndexedSecurityCallback>(fun_obj);

  if (!callback) return false;

  HandleScope scope;
  Handle<JSObject> receiver_handle(receiver);
  Handle<Object> data(AccessCheckInfo::cast(data_obj)->data());
  LOG(ApiIndexedSecurityCheck(index));
  bool result = false;
  {
    // Leaving JavaScript.
    VMState state(EXTERNAL);
    result = callback(v8::Utils::ToLocal(receiver_handle),
                      index,
                      type,
                      v8::Utils::ToLocal(data));
  }
  return result;
}


const char* Isolate::kStackOverflowMessage =
  "Uncaught RangeError: Maximum call stack size exceeded";


Failure* Isolate::StackOverflow() {
  HandleScope scope;
  Handle<String> key = Factory::stack_overflow_symbol();
  Handle<JSObject> boilerplate =
      Handle<JSObject>::cast(GetProperty(js_builtins_object(), key));
  Handle<Object> exception = Copy(boilerplate);
  // TODO(1240995): To avoid having to call JavaScript code to compute
  // the message for stack overflow exceptions which is very likely to
  // double fault with another stack overflow exception, we use a
  // precomputed message. This is somewhat problematic in that it
  // doesn't use ReportUncaughtException to determine the location
  // from where the exception occurred. It should probably be
  // reworked.
  DoThrow(*exception, NULL, kStackOverflowMessage);
  return Failure::Exception();
}


Failure* Isolate::TerminateExecution() {
  DoThrow(heap_.termination_exception(), NULL, NULL);
  return Failure::Exception();
}


Failure* Isolate::Throw(Object* exception, MessageLocation* location) {
  DoThrow(exception, location, NULL);
  return Failure::Exception();
}


Failure* Isolate::ReThrow(Object* exception, MessageLocation* location) {
  // Set the exception being re-thrown.
  set_pending_exception(exception);
  return Failure::Exception();
}


Failure* Isolate::ThrowIllegalOperation() {
  return Throw(heap_.illegal_access_symbol());
}


void Isolate::ScheduleThrow(Object* exception) {
  // When scheduling a throw we first throw the exception to get the
  // error reporting if it is uncaught before rescheduling it.
  Throw(exception);
  thread_local_top()->scheduled_exception_ = pending_exception();
  thread_local_top()->external_caught_exception_ = false;
  clear_pending_exception();
}


Object* Isolate::PromoteScheduledException() {
  Object* thrown = scheduled_exception();
  clear_scheduled_exception();
  // Re-throw the exception to avoid getting repeated error reporting.
  return ReThrow(thrown);
}


void Isolate::PrintCurrentStackTrace(FILE* out) {
  StackTraceFrameIterator it;
  while (!it.done()) {
    HandleScope scope;
    // Find code position if recorded in relocation info.
    JavaScriptFrame* frame = it.frame();
    int pos = frame->code()->SourcePosition(frame->pc());
    Handle<Object> pos_obj(Smi::FromInt(pos));
    // Fetch function and receiver.
    Handle<JSFunction> fun(JSFunction::cast(frame->function()));
    Handle<Object> recv(frame->receiver());
    // Advance to the next JavaScript frame and determine if the
    // current frame is the top-level frame.
    it.Advance();
    Handle<Object> is_top_level = it.done()
        ? Factory::true_value()
        : Factory::false_value();
    // Generate and print stack trace line.
    Handle<String> line =
        Execution::GetStackTraceLine(recv, fun, pos_obj, is_top_level);
    if (line->length() > 0) {
      line->PrintOn(out);
      fprintf(out, "\n");
    }
  }
}


void Isolate::ComputeLocation(MessageLocation* target) {
  *target = MessageLocation(Handle<Script>(heap_.empty_script()), -1, -1);
  StackTraceFrameIterator it;
  if (!it.done()) {
    JavaScriptFrame* frame = it.frame();
    JSFunction* fun = JSFunction::cast(frame->function());
    Object* script = fun->shared()->script();
    if (script->IsScript() &&
        !(Script::cast(script)->source()->IsUndefined())) {
      int pos = frame->code()->SourcePosition(frame->pc());
      // Compute the location from the function and the reloc info.
      Handle<Script> casted_script(Script::cast(script));
      *target = MessageLocation(casted_script, pos, pos + 1);
    }
  }
}


void Isolate::ReportUncaughtException(Handle<Object> exception,
                                      MessageLocation* location,
                                      Handle<String> stack_trace) {
  Handle<Object> message;
  if (!bootstrapper()->IsActive()) {
    // It's not safe to try to make message objects while the bootstrapper
    // is active since the infrastructure may not have been properly
    // initialized.
    message =
      MessageHandler::MakeMessageObject("uncaught_exception",
                                        location,
                                        HandleVector<Object>(&exception, 1),
                                        stack_trace);
  }
  // Report the uncaught exception.
  MessageHandler::ReportMessage(location, message);
}


bool Isolate::ShouldReturnException(bool* is_caught_externally,
                                    bool catchable_by_javascript) {
  // Find the top-most try-catch handler.
  StackHandler* handler =
      StackHandler::FromAddress(Isolate::handler(thread_local_top()));
  while (handler != NULL && !handler->is_try_catch()) {
    handler = handler->next();
  }

  // Get the address of the external handler so we can compare the address to
  // determine which one is closer to the top of the stack.
  Address external_handler_address =
      thread_local_top()->try_catch_handler_address();

  // The exception has been externally caught if and only if there is
  // an external handler which is on top of the top-most try-catch
  // handler.
  *is_caught_externally = external_handler_address != NULL &&
      (handler == NULL || handler->address() > external_handler_address ||
       !catchable_by_javascript);

  if (*is_caught_externally) {
    // Only report the exception if the external handler is verbose.
    return thread_local_top()->TryCatchHandler()->is_verbose_;
  } else {
    // Report the exception if it isn't caught by JavaScript code.
    return handler == NULL;
  }
}


void Isolate::DoThrow(Object* exception,
                      MessageLocation* location,
                      const char* message) {
  ASSERT(!has_pending_exception());

  HandleScope scope;
  Handle<Object> exception_handle(exception);

  // Determine reporting and whether the exception is caught externally.
  bool is_caught_externally = false;
  bool is_out_of_memory = exception == Failure::OutOfMemoryException();
  bool is_termination_exception = exception == heap_.termination_exception();
  bool catchable_by_javascript = !is_termination_exception && !is_out_of_memory;
  bool should_return_exception =
      ShouldReturnException(&is_caught_externally, catchable_by_javascript);
  bool report_exception = catchable_by_javascript && should_return_exception;

#ifdef ENABLE_DEBUGGER_SUPPORT
  // Notify debugger of exception.
  if (catchable_by_javascript) {
    debugger_->OnException(exception_handle, report_exception);
  }
#endif

  // Generate the message.
  Handle<Object> message_obj;
  MessageLocation potential_computed_location;
  bool try_catch_needs_message =
      is_caught_externally &&
      thread_local_top()->TryCatchHandler()->capture_message_;
  if (report_exception || try_catch_needs_message) {
    if (location == NULL) {
      // If no location was specified we use a computed one instead
      ComputeLocation(&potential_computed_location);
      location = &potential_computed_location;
    }
    if (!bootstrapper()->IsActive()) {
      // It's not safe to try to make message objects or collect stack
      // traces while the bootstrapper is active since the infrastructure
      // may not have been properly initialized.
      Handle<String> stack_trace;
      if (FLAG_trace_exception) stack_trace = StackTraceString();
      message_obj = MessageHandler::MakeMessageObject("uncaught_exception",
          location, HandleVector<Object>(&exception_handle, 1), stack_trace);
    }
  }

  // Save the message for reporting if the the exception remains uncaught.
  thread_local_top()->has_pending_message_ = report_exception;
  thread_local_top()->pending_message_ = message;
  if (!message_obj.is_null()) {
    thread_local_top()->pending_message_obj_ = *message_obj;
    if (location != NULL) {
      thread_local_top()->pending_message_script_ = *location->script();
      thread_local_top()->pending_message_start_pos_ = location->start_pos();
      thread_local_top()->pending_message_end_pos_ = location->end_pos();
    }
  }

  if (is_caught_externally) {
    thread_local_top()->catcher_ = thread_local_top()->TryCatchHandler();
  }

  // NOTE: Notifying the debugger or generating the message
  // may have caused new exceptions. For now, we just ignore
  // that and set the pending exception to the original one.
  set_pending_exception(*exception_handle);
}


void Isolate::ReportPendingMessages() {
  ASSERT(has_pending_exception());
  setup_external_caught();
  // If the pending exception is OutOfMemoryException set out_of_memory in
  // the global context.  Note: We have to mark the global context here
  // since the GenerateThrowOutOfMemory stub cannot make a RuntimeCall to
  // set it.
  bool external_caught = thread_local_top()->external_caught_exception_;
  HandleScope scope;
  if (thread_local_top()->pending_exception_ ==
      Failure::OutOfMemoryException()) {
    context()->mark_out_of_memory();
  } else if (thread_local_top()->pending_exception_ ==
             heap_.termination_exception()) {
    if (external_caught) {
      thread_local_top()->TryCatchHandler()->can_continue_ = false;
      thread_local_top()->TryCatchHandler()->exception_ = heap_.null_value();
    }
  } else {
    Handle<Object> exception(pending_exception());
    thread_local_top()->external_caught_exception_ = false;
    if (external_caught) {
      thread_local_top()->TryCatchHandler()->can_continue_ = true;
      thread_local_top()->TryCatchHandler()->exception_ =
        thread_local_top()->pending_exception_;
      if (!thread_local_top()->pending_message_obj_->IsTheHole()) {
        try_catch_handler()->message_ =
            thread_local_top()->pending_message_obj_;
      }
    }
    if (thread_local_top()->has_pending_message_) {
      thread_local_top()->has_pending_message_ = false;
      if (thread_local_top()->pending_message_ != NULL) {
        MessageHandler::ReportMessage(thread_local_top()->pending_message_);
      } else if (!thread_local_top()->pending_message_obj_->IsTheHole()) {
        Handle<Object> message_obj(thread_local_top()->pending_message_obj_);
        if (thread_local_top()->pending_message_script_ != NULL) {
          Handle<Script> script(thread_local_top()->pending_message_script_);
          int start_pos = thread_local_top()->pending_message_start_pos_;
          int end_pos = thread_local_top()->pending_message_end_pos_;
          MessageLocation location(script, start_pos, end_pos);
          MessageHandler::ReportMessage(&location, message_obj);
        } else {
          MessageHandler::ReportMessage(NULL, message_obj);
        }
      }
    }
    thread_local_top()->external_caught_exception_ = external_caught;
    set_pending_exception(*exception);
  }
  clear_pending_message();
}


void Isolate::TraceException(bool flag) {
  FLAG_trace_exception = flag;  // TODO(isolates): This is an unfortunate use.
}


bool Isolate::OptionalRescheduleException(bool is_bottom_call) {
  // Allways reschedule out of memory exceptions.
  if (!is_out_of_memory()) {
    bool is_termination_exception =
        pending_exception() == heap_.termination_exception();

    // Do not reschedule the exception if this is the bottom call.
    bool clear_exception = is_bottom_call;

    if (is_termination_exception) {
      if (is_bottom_call) {
        thread_local_top()->external_caught_exception_ = false;
        clear_pending_exception();
        return false;
      }
    } else if (thread_local_top()->external_caught_exception_) {
      // If the exception is externally caught, clear it if there are no
      // JavaScript frames on the way to the C++ frame that has the
      // external handler.
      ASSERT(thread_local_top()->try_catch_handler_address() != NULL);
      Address external_handler_address =
          thread_local_top()->try_catch_handler_address();
      JavaScriptFrameIterator it;
      if (it.done() || (it.frame()->sp() > external_handler_address)) {
        clear_exception = true;
      }
    }

    // Clear the exception if needed.
    if (clear_exception) {
      thread_local_top()->external_caught_exception_ = false;
      clear_pending_exception();
      return false;
    }
  }

  // Reschedule the exception.
  thread_local_top()->scheduled_exception_ = pending_exception();
  clear_pending_exception();
  return true;
}


bool Isolate::is_out_of_memory() {
  if (has_pending_exception()) {
    Object* e = pending_exception();
    if (e->IsFailure() && Failure::cast(e)->IsOutOfMemoryException()) {
      return true;
    }
  }
  if (has_scheduled_exception()) {
    Object* e = scheduled_exception();
    if (e->IsFailure() && Failure::cast(e)->IsOutOfMemoryException()) {
      return true;
    }
  }
  return false;
}


Handle<Context> Isolate::global_context() {
  GlobalObject* global = thread_local_top()->context_->global();
  return Handle<Context>(global->global_context());
}


Handle<Context> Isolate::GetCallingGlobalContext() {
  JavaScriptFrameIterator it;
#ifdef ENABLE_DEBUGGER_SUPPORT
  if (Debug::InDebugger()) {
    while (!it.done()) {
      JavaScriptFrame* frame = it.frame();
      Context* context = Context::cast(frame->context());
      if (context->global_context() == *Debug::debug_context()) {
        it.Advance();
      } else {
        break;
      }
    }
  }
#endif  // ENABLE_DEBUGGER_SUPPORT
  if (it.done()) return Handle<Context>::null();
  JavaScriptFrame* frame = it.frame();
  Context* context = Context::cast(frame->context());
  return Handle<Context>(context->global_context());
}


char* Isolate::ArchiveThread(char* to) {
  memcpy(to, reinterpret_cast<char*>(thread_local_top()),
      sizeof(ThreadLocalTop));
  InitializeThreadLocal();
  return to + sizeof(ThreadLocalTop);
}


char* Isolate::RestoreThread(char* from) {
  memcpy(reinterpret_cast<char*>(thread_local_top()), from,
      sizeof(ThreadLocalTop));
  return from + sizeof(ThreadLocalTop);
}


ExecutionAccess::ExecutionAccess() {
  Isolate::Current()->break_access()->Lock();
}


ExecutionAccess::~ExecutionAccess() {
  Isolate::Current()->break_access()->Unlock();
}


} }  // namespace v8::internal
