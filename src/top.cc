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

#include "api.h"
#include "bootstrapper.h"
#include "debug.h"
#include "execution.h"
#include "string-stream.h"
#include "platform.h"

namespace v8 { namespace internal {

DEFINE_bool(trace_exception, false,
            "print stack trace when throwing exceptions");
DEFINE_bool(preallocate_message_memory, false,
            "preallocate some memory to build stack traces.");

ThreadLocalTop Top::thread_local_;
Mutex* Top::break_access_ = OS::CreateMutex();
StackFrame::Id Top::break_frame_id_;
int Top::break_count_;
int Top::break_id_;

NoAllocationStringAllocator* preallocated_message_space = NULL;

Address top_addresses[] = {
#define C(name) reinterpret_cast<Address>(Top::name()),
    TOP_ADDRESS_LIST(C)
#undef C
    NULL
};

Address Top::get_address_from_id(Top::AddressId id) {
  return top_addresses[id];
}

char* Top::Iterate(ObjectVisitor* v, char* thread_storage) {
  ThreadLocalTop* thread = reinterpret_cast<ThreadLocalTop*>(thread_storage);
  Iterate(v, thread);
  return thread_storage + sizeof(ThreadLocalTop);
}


#define VISIT(field) v->VisitPointer(reinterpret_cast<Object**>(&(field)));

void Top::Iterate(ObjectVisitor* v, ThreadLocalTop* thread) {
  VISIT(thread->pending_exception_);
  VISIT(thread->security_context_);
  VISIT(thread->context_);
  VISIT(thread->scheduled_exception_);

  for (v8::TryCatch* block = thread->try_catch_handler_;
       block != NULL;
       block = block->next_) {
    VISIT(reinterpret_cast<Object*&>(block->exception_));
  }

  // Iterate over pointers on native execution stack.
  for (StackFrameIterator it(thread); !it.done(); it.Advance()) {
    it.frame()->Iterate(v);
  }
}
#undef VISIT


void Top::Iterate(ObjectVisitor* v) {
  ThreadLocalTop* current_t = &thread_local_;
  Iterate(v, current_t);
}


void Top::InitializeThreadLocal() {
  thread_local_.c_entry_fp_ = 0;
  thread_local_.handler_ = 0;
  thread_local_.stack_is_cooked_ = false;
  thread_local_.try_catch_handler_ = NULL;
  thread_local_.security_context_ = NULL;
  thread_local_.context_ = NULL;
  thread_local_.external_caught_exception_ = false;
  thread_local_.failed_access_check_callback_ = NULL;
  clear_pending_exception();
  clear_scheduled_exception();
  thread_local_.save_context_ = NULL;
}


// Create a dummy thread that will wait forever on a semaphore. The only
// purpose for this thread is to have some stack area to save essential data
// into for use by a stacks only core dump (aka minidump).
class PreallocatedMemoryThread: public Thread {
 public:
  PreallocatedMemoryThread() : keep_running_(true) {
    wait_for_ever_semaphore_ = OS::CreateSemaphore(0);
    data_ready_semaphore_ = OS::CreateSemaphore(0);
  }

  // When the thread starts running it will allocate a fixed number of bytes
  // on the stack and publish the location of this memory for others to use.
  void Run() {
    char local_buffer[16 * 1024];

    // Initialize the buffer with a known good value.
    strncpy(local_buffer, "Trace data was not generated.\n",
            sizeof(local_buffer));

    // Publish the local buffer and signal its availability.
    data_ = &local_buffer[0];
    length_ = sizeof(local_buffer);
    data_ready_semaphore_->Signal();

    while (keep_running_) {
      // This thread will wait here until the end of time.
      wait_for_ever_semaphore_->Wait();
    }

    // Make sure we access the buffer after the wait to remove all possibility
    // of it being optimized away.
    strncpy(local_buffer, "PreallocatedMemoryThread shutting down.\n",
            sizeof(local_buffer));
  }

  static char* data() {
    if (data_ready_semaphore_ != NULL) {
      // Initial access is guarded until the data has been published.
      data_ready_semaphore_->Wait();
      delete data_ready_semaphore_;
      data_ready_semaphore_ = NULL;
    }
    return data_;
  }

  static unsigned length() {
    if (data_ready_semaphore_ != NULL) {
      // Initial access is guarded until the data has been published.
      data_ready_semaphore_->Wait();
      delete data_ready_semaphore_;
      data_ready_semaphore_ = NULL;
    }
    return length_;
  }

  static void StartThread() {
    if (the_thread_ != NULL) return;

    the_thread_ = new PreallocatedMemoryThread();
    the_thread_->Start();
  }

  // Stop the PreallocatedMemoryThread and release its resources.
  static void StopThread() {
    if (the_thread_ == NULL) return;

    the_thread_->keep_running_ = false;
    wait_for_ever_semaphore_->Signal();

    // Wait for the thread to terminate.
    the_thread_->Join();

    if (data_ready_semaphore_ != NULL) {
      delete data_ready_semaphore_;
      data_ready_semaphore_ = NULL;
    }

    delete wait_for_ever_semaphore_;
    wait_for_ever_semaphore_ = NULL;

    // Done with the thread entirely.
    delete the_thread_;
    the_thread_ = NULL;
  }

 private:
  // Used to make sure that the thread keeps looping even for spurious wakeups.
  bool keep_running_;

  // The preallocated memory thread singleton.
  static PreallocatedMemoryThread* the_thread_;
  // This semaphore is used by the PreallocatedMemoryThread to wait for ever.
  static Semaphore* wait_for_ever_semaphore_;
  // Semaphore to signal that the data has been initialized.
  static Semaphore* data_ready_semaphore_;

  // Location and size of the preallocated memory block.
  static char* data_;
  static unsigned length_;

  DISALLOW_EVIL_CONSTRUCTORS(PreallocatedMemoryThread);
};

PreallocatedMemoryThread* PreallocatedMemoryThread::the_thread_ = NULL;
Semaphore* PreallocatedMemoryThread::wait_for_ever_semaphore_ = NULL;
Semaphore* PreallocatedMemoryThread::data_ready_semaphore_ = NULL;
char* PreallocatedMemoryThread::data_ = NULL;
unsigned PreallocatedMemoryThread::length_ = 0;

static bool initialized = false;

void Top::Initialize() {
  CHECK(!initialized);

  InitializeThreadLocal();

  break_frame_id_ = StackFrame::NO_ID;
  break_count_ = 0;
  break_id_ = 0;

  // Only preallocate on the first initialization.
  if (FLAG_preallocate_message_memory && (preallocated_message_space == NULL)) {
    // Start the thread which will set aside some memory.
    PreallocatedMemoryThread::StartThread();
    preallocated_message_space =
        new NoAllocationStringAllocator(PreallocatedMemoryThread::data(),
                                        PreallocatedMemoryThread::length());
    PreallocatedStorage::Init(PreallocatedMemoryThread::length() / 4);
  }
  initialized = true;
}


void Top::TearDown() {
  if (initialized) {
    // Remove the external reference to the preallocated stack memory.
    if (preallocated_message_space != NULL) {
      delete preallocated_message_space;
      preallocated_message_space = NULL;
    }

    PreallocatedMemoryThread::StopThread();
    initialized = false;
  }
}


void Top::RegisterTryCatchHandler(v8::TryCatch* that) {
  thread_local_.try_catch_handler_ = that;
}


void Top::UnregisterTryCatchHandler(v8::TryCatch* that) {
  ASSERT(thread_local_.try_catch_handler_ == that);
  thread_local_.try_catch_handler_ = that->next_;
}


void Top::new_break(StackFrame::Id break_frame_id) {
  ExecutionAccess access;
  break_frame_id_ = break_frame_id;
  break_id_ = ++break_count_;
}


void Top::set_break(StackFrame::Id break_frame_id, int break_id) {
  ExecutionAccess access;
  break_frame_id_ = break_frame_id;
  break_id_ = break_id;
}


bool Top::check_break(int break_id) {
  ExecutionAccess access;
  return break_id == break_id_;
}


bool Top::is_break() {
  ExecutionAccess access;
  return is_break_no_lock();
}


bool Top::is_break_no_lock() {
  return break_id_ != 0;
}

StackFrame::Id Top::break_frame_id() {
  ExecutionAccess access;
  return break_frame_id_;
}


int Top::break_id() {
  ExecutionAccess access;
  return break_id_;
}


void Top::MarkCompactPrologue() {
  MarkCompactPrologue(&thread_local_);
}


void Top::MarkCompactPrologue(char* data) {
  MarkCompactPrologue(reinterpret_cast<ThreadLocalTop*>(data));
}


void Top::MarkCompactPrologue(ThreadLocalTop* thread) {
  StackFrame::CookFramesForThread(thread);
}


void Top::MarkCompactEpilogue(char* data) {
  MarkCompactEpilogue(reinterpret_cast<ThreadLocalTop*>(data));
}


void Top::MarkCompactEpilogue() {
  MarkCompactEpilogue(&thread_local_);
}


void Top::MarkCompactEpilogue(ThreadLocalTop* thread) {
  StackFrame::UncookFramesForThread(thread);
}


static int stack_trace_nesting_level = 0;
static StringStream* incomplete_message = NULL;


Handle<String> Top::StackTrace() {
  if (stack_trace_nesting_level == 0) {
    stack_trace_nesting_level++;
    HeapStringAllocator allocator;
    StringStream::ClearMentionedObjectCache();
    StringStream accumulator(&allocator);
    incomplete_message = &accumulator;
    PrintStack(&accumulator);
    Handle<String> stack_trace = accumulator.ToString();
    incomplete_message = NULL;
    stack_trace_nesting_level = 0;
    return stack_trace;
  } else if (stack_trace_nesting_level == 1) {
    stack_trace_nesting_level++;
    OS::PrintError(
      "\n\nAttempt to print stack while printing stack (double fault)\n");
    OS::PrintError(
      "If you are lucky you may find a partial stack dump on stdout.\n\n");
    incomplete_message->OutputToStdOut();
    return Factory::empty_symbol();
  } else {
    OS::Abort();
    // Unreachable
    return Factory::empty_symbol();
  }
}


void Top::PrintStack() {
  if (stack_trace_nesting_level == 0) {
    stack_trace_nesting_level++;

    StringAllocator* allocator;
    if (preallocated_message_space == NULL) {
      allocator = new HeapStringAllocator();
    } else {
      allocator = preallocated_message_space;
    }

    NativeAllocationChecker allocation_checker(
      !FLAG_preallocate_message_memory ?
      NativeAllocationChecker::ALLOW :
      NativeAllocationChecker::DISALLOW);

    StringStream::ClearMentionedObjectCache();
    StringStream accumulator(allocator);
    incomplete_message = &accumulator;
    PrintStack(&accumulator);
    accumulator.OutputToStdOut();
    accumulator.Log();
    incomplete_message = NULL;
    stack_trace_nesting_level = 0;
    if (preallocated_message_space == NULL) {
      // Remove the HeapStringAllocator created above.
      delete allocator;
    }
  } else if (stack_trace_nesting_level == 1) {
    stack_trace_nesting_level++;
    OS::PrintError(
      "\n\nAttempt to print stack while printing stack (double fault)\n");
    OS::PrintError(
      "If you are lucky you may find a partial stack dump on stdout.\n\n");
    incomplete_message->OutputToStdOut();
  }
}


static void PrintFrames(StringStream* accumulator,
                        StackFrame::PrintMode mode) {
  StackFrameIterator it;
  for (int i = 0; !it.done(); it.Advance()) {
    it.frame()->Print(accumulator, mode, i++);
  }
}


void Top::PrintStack(StringStream* accumulator) {
  // The MentionedObjectCache is not GC-proof at the moment.
  AssertNoAllocation nogc;
  ASSERT(StringStream::IsMentionedObjectCacheClear());

  // Avoid printing anything if there are no frames.
  if (c_entry_fp(GetCurrentThread()) == 0) return;

  accumulator->Add(
      "\n==== Stack trace ============================================\n\n");
  PrintFrames(accumulator, StackFrame::OVERVIEW);

  accumulator->Add(
      "\n==== Details ================================================\n\n");
  PrintFrames(accumulator, StackFrame::DETAILS);

  accumulator->PrintMentionedObjectCache();
  accumulator->Add("=====================\n\n");
}


void Top::SetFailedAccessCheckCallback(v8::FailedAccessCheckCallback callback) {
  ASSERT(thread_local_.failed_access_check_callback_ == NULL);
  thread_local_.failed_access_check_callback_ = callback;
}


void Top::ReportFailedAccessCheck(JSObject* receiver, v8::AccessType type) {
  if (!thread_local_.failed_access_check_callback_) return;

  ASSERT(receiver->IsAccessCheckNeeded());
  ASSERT(Top::security_context());
  // The callers of this method are not expecting a GC.
  AssertNoAllocation no_gc;

  // Get the data object from access check info.
  JSFunction* constructor = JSFunction::cast(receiver->map()->constructor());
  Object* info = constructor->shared()->function_data();
  if (info == Heap::undefined_value()) return;

  Object* data_obj = FunctionTemplateInfo::cast(info)->access_check_info();
  if (data_obj == Heap::undefined_value()) return;

  HandleScope scope;
  Handle<JSObject> receiver_handle(receiver);
  Handle<Object> data(AccessCheckInfo::cast(data_obj)->data());
  thread_local_.failed_access_check_callback_(
    v8::Utils::ToLocal(receiver_handle),
    type,
    v8::Utils::ToLocal(data));
}

bool Top::MayNamedAccess(JSObject* receiver, Object* key, v8::AccessType type) {
  ASSERT(receiver->IsAccessCheckNeeded());
  // Check for compatibility between the security tokens in the
  // current security context and the accessed object.
  ASSERT(Top::security_context());
  // The callers of this method are not expecting a GC.
  AssertNoAllocation no_gc;

  // During bootstrapping, callback functions are not enabled yet.
  if (Bootstrapper::IsActive()) return true;

  if (receiver->IsJSGlobalObject()) {
    JSGlobalObject* global = JSGlobalObject::cast(receiver);
    JSGlobalObject* current =
        JSGlobalObject::cast(Top::security_context()->global());
    if (current->security_token() == global->security_token()) return true;
  }

  // Get named access check callback
  JSFunction* constructor = JSFunction::cast(receiver->map()->constructor());
  Object* info = constructor->shared()->function_data();
  if (info == Heap::undefined_value()) return false;

  Object* data_obj = FunctionTemplateInfo::cast(info)->access_check_info();
  if (data_obj == Heap::undefined_value()) return false;

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
    VMState state(OTHER);
    result = callback(v8::Utils::ToLocal(receiver_handle),
                      v8::Utils::ToLocal(key_handle),
                      type,
                      v8::Utils::ToLocal(data));
  }
  return result;
}


bool Top::MayIndexedAccess(JSObject* receiver,
                           uint32_t index,
                           v8::AccessType type) {
  ASSERT(receiver->IsAccessCheckNeeded());
  // Check for compatibility between the security tokens in the
  // current security context and the accessed object.
  ASSERT(Top::security_context());
  // The callers of this method are not expecting a GC.
  AssertNoAllocation no_gc;

  // During bootstrapping, callback functions are not enabled yet.
  if (Bootstrapper::IsActive()) return true;

  if (receiver->IsJSGlobalObject()) {
    JSGlobalObject* global = JSGlobalObject::cast(receiver);
    JSGlobalObject* current =
      JSGlobalObject::cast(Top::security_context()->global());
    if (current->security_token() == global->security_token()) return true;
  }

  // Get indexed access check callback
  JSFunction* constructor = JSFunction::cast(receiver->map()->constructor());
  Object* info = constructor->shared()->function_data();
  if (info == Heap::undefined_value()) return false;

  Object* data_obj = FunctionTemplateInfo::cast(info)->access_check_info();
  if (data_obj == Heap::undefined_value()) return false;

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
    VMState state(OTHER);
    result = callback(v8::Utils::ToLocal(receiver_handle),
                      index,
                      type,
                      v8::Utils::ToLocal(data));
  }
  return result;
}


Failure* Top::StackOverflow() {
  HandleScope scope;
  Handle<String> key = Factory::stack_overflow_symbol();
  Handle<JSObject> boilerplate =
      Handle<JSObject>::cast(
          GetProperty(Top::security_context_builtins(), key));
  Handle<Object> exception = Copy(boilerplate);
  // TODO(1240995): To avoid having to call JavaScript code to compute
  // the message for stack overflow exceptions which is very likely to
  // double fault with another stack overflow exception, we use a
  // precomputed message. This is somewhat problematic in that it
  // doesn't use ReportUncaughtException to determine the location
  // from where the exception occurred. It should probably be
  // reworked.
  static const char* kMessage =
      "Uncaught RangeError: Maximum call stack size exceeded";
  DoThrow(*exception, NULL, kMessage, false);
  return Failure::Exception();
}


Failure* Top::Throw(Object* exception, MessageLocation* location) {
  DoThrow(exception, location, NULL, false);
  return Failure::Exception();
}


Failure* Top::ReThrow(Object* exception, MessageLocation* location) {
  DoThrow(exception, location, NULL, true);
  return Failure::Exception();
}


void Top::ScheduleThrow(Object* exception) {
  // When scheduling a throw we first throw the exception to get the
  // error reporting if it is uncaught before rescheduling it.
  Throw(exception);
  thread_local_.scheduled_exception_ = pending_exception();
  thread_local_.external_caught_exception_ = false;
  clear_pending_exception();
}


Object* Top::PromoteScheduledException() {
  Object* thrown = scheduled_exception();
  clear_scheduled_exception();
  // Re-throw the exception to avoid getting repeated error reporting.
  return ReThrow(thrown);
}


// TODO(1233523): Get rid of this hackish abstraction once all
// JavaScript frames have a function associated with them.

// NOTE: The stack trace frame iterator is an iterator that only
// traverse proper JavaScript frames; that is JavaScript frames that
// have proper JavaScript functions. This excludes the problematic
// functions in runtime.js.
class StackTraceFrameIterator: public JavaScriptFrameIterator {
 public:
  StackTraceFrameIterator() {
    if (!done() && !frame()->function()->IsJSFunction()) Advance();
  }

  void Advance() {
    while (true) {
      JavaScriptFrameIterator::Advance();
      if (done()) return;
      if (frame()->function()->IsJSFunction()) return;
    }
  }
};


void Top::PrintCurrentStackTrace(FILE* out) {
  StackTraceFrameIterator it;
  while (!it.done()) {
    HandleScope scope;
    // Find code position if recorded in relocation info.
    JavaScriptFrame* frame = it.frame();
    int pos = frame->FindCode()->SourcePosition(frame->pc());
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
    // Generate and print strack trace line.
    Handle<String> line =
        Execution::GetStackTraceLine(recv, fun, pos_obj, is_top_level);
    if (line->length() > 0) {
      line->PrintOn(out);
      fprintf(out, "\n");
    }
  }
}


void Top::ReportUncaughtException(Handle<Object> exception,
                                  MessageLocation* location,
                                  Handle<String> stack_trace) {
  MessageLocation computed_location(empty_script(), -1, -1);
  if (location == NULL) {
    location = &computed_location;

    StackTraceFrameIterator it;
    if (!it.done()) {
      JavaScriptFrame* frame = it.frame();
      JSFunction* fun = JSFunction::cast(frame->function());
      Object* script = fun->shared()->script();
      if (script->IsScript() &&
          !(Script::cast(script)->source()->IsUndefined())) {
        int pos = frame->FindCode()->SourcePosition(frame->pc());
        // Compute the location from the function and the reloc info.
        Handle<Script> casted_script(Script::cast(script));
        computed_location = MessageLocation(casted_script, pos, pos + 1);
      }
    }
  }

  // Report the uncaught exception.
  MessageHandler::ReportMessage("uncaught_exception",
                                location,
                                HandleVector<Object>(&exception, 1));

  // Optionally, report the stack trace separately.
  if (!stack_trace.is_null()) {
    MessageHandler::ReportMessage("stack_trace",
                                  location,
                                  HandleVector<String>(&stack_trace, 1));
  }
}


bool Top::ShouldReportException(bool* is_caught_externally) {
  StackHandler* handler =
      StackHandler::FromAddress(Top::handler(Top::GetCurrentThread()));

  // Determine if we have an external exception handler and get the
  // address of the external handler so we can compare the address to
  // determine which one is closer to the top of the stack.
  bool has_external_handler = (thread_local_.try_catch_handler_ != NULL);
  Address external_handler_address =
      reinterpret_cast<Address>(thread_local_.try_catch_handler_);

  // NOTE: The stack is assumed to grown towards lower addresses. If
  // the handler is at a higher address than the external address it
  // means that it is below it on the stack.

  // Find the top-most try-catch or try-finally handler.
  while (handler != NULL && handler->is_entry()) {
    handler = handler->next();
  }

  // The exception has been externally caught if and only if there is
  // an external handler which is above any JavaScript try-catch or
  // try-finally handlers.
  *is_caught_externally = has_external_handler &&
      (handler == NULL || handler->address() > external_handler_address);

  // Find the top-most try-catch handler.
  while (handler != NULL && !handler->is_try_catch()) {
    handler = handler->next();
  }

  // If we have a try-catch handler then the exception is caught in
  // JavaScript code.
  bool is_uncaught_by_js = (handler == NULL);

  // If there is no external try-catch handler, we report the
  // exception if it isn't caught by JavaScript code.
  if (!has_external_handler) return is_uncaught_by_js;

  if (is_uncaught_by_js || handler->address() > external_handler_address) {
    // Only report the exception if the external handler is verbose.
    return thread_local_.try_catch_handler_->is_verbose_;
  } else {
    // Report the exception if it isn't caught by JavaScript code.
    return is_uncaught_by_js;
  }
}


void Top::DoThrow(Object* exception,
                  MessageLocation* location,
                  const char* message,
                  bool is_rethrow) {
  ASSERT(!has_pending_exception());
  ASSERT(!external_caught_exception());

  HandleScope scope;
  Handle<Object> exception_handle(exception);

  bool is_caught_externally = false;
  bool report_exception = (exception != Failure::OutOfMemoryException()) &&
    ShouldReportException(&is_caught_externally);
  if (is_rethrow) report_exception = false;

  // If the exception is caught externally, we store it in the
  // try/catch handler. The C code can find it later and process it if
  // necessary.
  if (is_caught_externally) {
    thread_local_.try_catch_handler_->exception_ =
      reinterpret_cast<void*>(*exception_handle);
  }

  // Notify debugger of exception.
  Debugger::OnException(exception_handle, report_exception);

  if (report_exception) {
    if (message != NULL) {
      MessageHandler::ReportMessage(message);
    } else {
      Handle<String> stack_trace;
      if (FLAG_trace_exception) stack_trace = StackTrace();
      ReportUncaughtException(exception_handle, location, stack_trace);
    }
  }
  thread_local_.external_caught_exception_ = is_caught_externally;
  // NOTE: Notifying the debugger or reporting the exception may have caused
  // new exceptions. For now, we just ignore that and set the pending exception
  // to the original one.
  set_pending_exception(*exception_handle);
}


void Top::TraceException(bool flag) {
  FLAG_trace_exception = flag;
}


bool Top::optional_reschedule_exception(bool is_bottom_call) {
  if (!is_out_of_memory() &&
      (thread_local_.external_caught_exception_ || is_bottom_call)) {
    thread_local_.external_caught_exception_ = false;
    clear_pending_exception();
    return false;
  } else {
    thread_local_.scheduled_exception_ = pending_exception();
    clear_pending_exception();
    return true;
  }
}


bool Top::is_out_of_memory() {
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


Handle<Context> Top::global_context() {
  GlobalObject* global = thread_local_.context_->global();
  return Handle<Context>(global->global_context());
}


Object* Top::LookupSpecialFunction(JSObject* receiver,
                                   JSObject* prototype,
                                   JSFunction* function) {
  if (receiver->IsJSArray()) {
    FixedArray* table = context()->global_context()->special_function_table();
    for (int index = 0; index < table->length(); index +=3) {
      if ((prototype == table->get(index)) &&
          (function == table->get(index+1))) {
        return table->get(index+2);
      }
    }
  }
  return Heap::undefined_value();
}


char* Top::ArchiveThread(char* to) {
  memcpy(to, reinterpret_cast<char*>(&thread_local_), sizeof(thread_local_));
  InitializeThreadLocal();
  return to + sizeof(thread_local_);
}


char* Top::RestoreThread(char* from) {
  memcpy(reinterpret_cast<char*>(&thread_local_), from, sizeof(thread_local_));
  return from + sizeof(thread_local_);
}


ExecutionAccess::ExecutionAccess() {
  Top::break_access_->Lock();
}


ExecutionAccess::~ExecutionAccess() {
  Top::break_access_->Unlock();
}


} }  // namespace v8::internal
