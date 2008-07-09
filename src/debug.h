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

#ifndef V8_DEBUG_H_
#define V8_DEBUG_H_

#include "../public/debug.h"
#include "assembler.h"
#include "code-stubs.h"
#include "factory.h"
#include "platform.h"
#include "string-stream.h"


namespace v8 { namespace internal {

// Step actions. NOTE: These values are in macros.py as well.
enum StepAction {
  StepNone = -1,  // Stepping not prepared.
  StepOut = 0,   // Step out of the current function.
  StepNext = 1,  // Step to the next statement in the current function.
  StepIn = 2,    // Step into new functions invoked or the next statement
                 // in the current function.
  StepMin = 3,   // Perform a minimum step in the current function.
  StepInMin = 4  // Step into new functions invoked or perform a minimum step
                 // in the current function.
};


// Type of exception break. NOTE: These values are in macros.py as well.
enum ExceptionBreakType {
  BreakException = 0,
  BreakUncaughtException = 1
};


// Type of exception break. NOTE: These values are in macros.py as well.
enum BreakLocatorType {
  ALL_BREAK_LOCATIONS = 0,
  SOURCE_BREAK_LOCATIONS = 1
};


// Class for iterating through the break points in a function and changing
// them.
class BreakLocationIterator {
 public:
  explicit BreakLocationIterator(Handle<DebugInfo> debug_info,
                                 BreakLocatorType type);
  virtual ~BreakLocationIterator();

  void Next();
  void Next(int count);
  void FindBreakLocationFromAddress(Address pc);
  void FindBreakLocationFromPosition(int position);
  void Reset();
  bool Done() const;
  void SetBreakPoint(Handle<Object> break_point_object);
  void ClearBreakPoint(Handle<Object> break_point_object);
  void SetOneShot();
  void ClearOneShot();
  void PrepareStepIn();
  bool IsExit() const;
  bool HasBreakPoint();
  bool IsDebugBreak();
  Object* BreakPointObjects();


  inline int code_position() { return pc() - debug_info_->code()->entry(); }
  inline int break_point() { return break_point_; }
  inline int position() { return position_; }
  inline int statement_position() { return statement_position_; }
  inline Address pc() { return reloc_iterator_->rinfo()->pc(); }
  inline Code* code() { return debug_info_->code(); }
  inline RelocInfo* rinfo() { return reloc_iterator_->rinfo(); }
  inline RelocMode rmode() const { return reloc_iterator_->rinfo()->rmode(); }
  inline RelocInfo* original_rinfo() {
    return reloc_iterator_original_->rinfo();
  }
  inline RelocMode original_rmode() const {
    return reloc_iterator_original_->rinfo()->rmode();
  }

 protected:
  bool RinfoDone() const;
  void RinfoNext();

  BreakLocatorType type_;
  int break_point_;
  int position_;
  int statement_position_;
  Handle<DebugInfo> debug_info_;
  RelocIterator* reloc_iterator_;
  RelocIterator* reloc_iterator_original_;

 private:
  void SetDebugBreak();
  void ClearDebugBreak();

  DISALLOW_EVIL_CONSTRUCTORS(BreakLocationIterator);
};


// Linked list holding debug info objects. The debug info objects are kept as
// weak handles to avoid a debug info object to keep a function alive.
class DebugInfoListNode {
 public:
  explicit DebugInfoListNode(DebugInfo* debug_info);
  virtual ~DebugInfoListNode();

  DebugInfoListNode* next() { return next_; }
  void set_next(DebugInfoListNode* next) { next_ = next; }
  Handle<DebugInfo> debug_info() { return debug_info_; }

 private:
  // Global (weak) handle to the debug info object.
  Handle<DebugInfo> debug_info_;

  // Next pointer for linked list.
  DebugInfoListNode* next_;
};


// This class contains the debugger support. The main purpose is to handle
// setting break points in the code.
//
// This class controls the debug info for all functions which currently have
// active breakpoints in them. This debug info is held in the heap root object
// debug_info which is a FixedArray. Each entry in this list is of class
// DebugInfo.
class Debug {
 public:
  static void Setup(bool create_heap_objects);
  static bool Load();
  static void Unload();
  static bool IsLoaded() { return !debug_context_.is_null(); }
  static bool InDebugger() { return Top::is_break(); }
  static void Iterate(ObjectVisitor* v);

  static Object* Break(Arguments args);
  static void SetBreakPoint(Handle<SharedFunctionInfo> shared,
                            int source_position,
                            Handle<Object> break_point_object);
  static void ClearBreakPoint(Handle<Object> break_point_object);
  static void FloodWithOneShot(Handle<SharedFunctionInfo> shared);
  static void FloodHandlerWithOneShot();
  static void ChangeBreakOnException(ExceptionBreakType type, bool enable);
  static void PrepareStep(StepAction step_action, int step_count);
  static void ClearStepping();
  static bool StepNextContinue(BreakLocationIterator* break_location_iterator,
                               JavaScriptFrame* frame);
  static Handle<DebugInfo> GetDebugInfo(Handle<SharedFunctionInfo> shared);
  static bool HasDebugInfo(Handle<SharedFunctionInfo> shared);

  // Returns whether the operation succedded.
  static bool EnsureDebugInfo(Handle<SharedFunctionInfo> shared);

  static bool IsDebugBreak(Address addr);

  // Check whether a code stub with the specified major key is a possible break
  // point location.
  static bool IsSourceBreakStub(Code* code);
  static bool IsBreakStub(Code* code);

  // Find the builtin to use for invoking the debug break
  static Handle<Code> FindDebugBreak(RelocInfo* rinfo);

  static Handle<Object> GetSourceBreakLocations(
      Handle<SharedFunctionInfo> shared);
  static Code* GetCodeTarget(Address target);

  // Getter for the debug_context.
  inline static Handle<Context> debug_context() { return debug_context_; }

  // Check whether a global object is the debug global object.
  static bool IsDebugGlobal(GlobalObject* global);

  // Fast check to see if any break points are active.
  inline static bool has_break_points() { return has_break_points_; }

  static bool StepInActive() { return thread_local_.step_into_fp_ != 0; }
  static Address step_in_fp() { return thread_local_.step_into_fp_; }
  static Address* step_in_fp_addr() { return &thread_local_.step_into_fp_; }

  // Getter and setter for the disable break state.
  static bool disable_break() { return disable_break_; }
  static void set_disable_break(bool disable_break) {
    disable_break_ = disable_break;
  }

  // Getters for the current exception break state.
  static bool break_on_exception() { return break_on_exception_; }
  static bool break_on_uncaught_exception() {
    return break_on_uncaught_exception_;
  }

  enum AddressId {
    k_after_break_target_address,
    k_debug_break_return_address,
    k_register_address
  };

  // Support for setting the address to jump to when returning from break point.
  static Address* after_break_target_address() {
    return reinterpret_cast<Address*>(&thread_local_.after_break_target_);
  }

  // Support for saving/restoring registers when handling debug break calls.
  static Address* register_address(int r) {
    return reinterpret_cast<Address *>(&registers_[r]);
  }

  // Addres of the debug break return entry code.
  static Code* debug_break_return_entry() { return debug_break_return_entry_; }

  // Support for getting the address of the debug break on return code.
  static Address* debug_break_return_address() {
    return reinterpret_cast<Address*>(&debug_break_return_);
  }

  static const int kEstimatedNofDebugInfoEntries = 16;
  static const int kEstimatedNofBreakPointsInFunction = 16;

  static void HandleWeakDebugInfo(v8::Persistent<v8::Object> obj, void* data);

  friend class Debugger;
  friend Handle<FixedArray> GetDebuggedFunctions();  // Found in test-debug.cc

  // Threading support.
  static char* ArchiveDebug(char* to);
  static char* RestoreDebug(char* from);
  static int ArchiveSpacePerThread();

  // Code generation assumptions.
  static const int kIa32CallInstructionLength = 5;
  static const int kIa32JSReturnSequenceLength = 6;

 private:
  static bool CompileDebuggerScript(int index);
  static void ClearOneShot();
  static void ActivateStepIn(StackFrame* frame);
  static void ClearStepIn();
  static void ClearStepNext();
  // Returns whether the compile succedded.
  static bool EnsureCompiled(Handle<SharedFunctionInfo> shared);
  static void RemoveDebugInfo(Handle<DebugInfo> debug_info);
  static void SetAfterBreakTarget(JavaScriptFrame* frame);
  static Handle<Object> CheckBreakPoints(Handle<Object> break_point);
  static bool CheckBreakPoint(Handle<Object> break_point_object);

  // Global handle to debug context where all the debugger JavaScript code is
  // loaded.
  static Handle<Context> debug_context_;

  // Boolean state indicating whether any break points are set.
  static bool has_break_points_;
  static DebugInfoListNode* debug_info_list_;

  static bool disable_break_;
  static bool break_on_exception_;
  static bool break_on_uncaught_exception_;

  // Per-thread:
  class ThreadLocal {
   public:
    // Step action for last step performed.
    StepAction last_step_action_;

    // Source statement position from last step next action.
    int last_statement_position_;

    // Number of steps left to perform before debug event.
    int step_count_;

    // Frame pointer from last step next action.
    Address last_fp_;

    // Frame pointer for frame from which step in was performed.
    Address step_into_fp_;

    // Storage location for jump when exiting debug break calls.
    Address after_break_target_;
  };

  // Storage location for registers when handling debug break calls
  static JSCallerSavedBuffer registers_;
  static ThreadLocal thread_local_;
  static void ThreadInit();

  // Code object for debug break return entry code.
  static Code* debug_break_return_entry_;

  // Code to call for handling debug break on return.
  static Code* debug_break_return_;

  DISALLOW_EVIL_CONSTRUCTORS(Debug);
};


class PendingRequest;
class DebugMessageThread;


class Debugger {
 public:
  static void DebugRequest(const uint16_t* json_request, int length);
  static bool ProcessPendingRequests();

  static Handle<Object> MakeJSObject(Vector<const char> constructor_name,
                                     int argc, Object*** argv,
                                     bool* caught_exception);
  static Handle<Object> MakeExecutionState(bool* caught_exception);
  static Handle<Object> MakeBreakEvent(Handle<Object> exec_state,
                                       Handle<Object> break_points_hit,
                                       bool* caught_exception);
  static Handle<Object> MakeExceptionEvent(Handle<Object> exec_state,
                                           Handle<Object> exception,
                                           bool uncaught,
                                           bool* caught_exception);
  static Handle<Object> MakeNewFunctionEvent(Handle<Object> func,
                                             bool* caught_exception);
  static Handle<Object> MakeCompileEvent(Handle<Script> script,
                                         Handle<Object> script_function,
                                         bool* caught_exception);
  static Handle<String> ProcessRequest(Handle<Object> exec_state,
                                       Handle<Object> request,
                                       bool stopped);
  static bool IsPlainBreakRequest(Handle<Object> request);

  static void OnDebugBreak(Handle<Object> break_points_hit);
  static void OnException(Handle<Object> exception, bool uncaught);
  static void OnBeforeCompile(Handle<Script> script);
  static void OnAfterCompile(Handle<Script> script,
                           Handle<JSFunction> fun);
  static void OnNewFunction(Handle<JSFunction> fun);
  static void OnPendingRequestProcessed(Handle<Object> event_data);
  static void ProcessDebugEvent(v8::DebugEvent event,
                                Handle<Object> event_data);
  static void SetMessageHandler(v8::DebugMessageHandler handler, void* data);
  static void SendMessage(Vector<uint16_t> message);
  static void ProcessCommand(Vector<const uint16_t> command);
  static void UpdateActiveDebugger();
  inline static bool EventActive(v8::DebugEvent event) {
    // Currently argument event is not used.
    return !Debugger::compiling_natives_ && Debugger::debugger_active_;
  }

  static void set_debugger_active(bool debugger_active) {
    Debugger::debugger_active_ = debugger_active;
  }
  static bool debugger_active() { return Debugger::debugger_active_; }
  static void set_compiling_natives(bool compiling_natives) {
    Debugger::compiling_natives_ = compiling_natives;
  }
  static bool compiling_natives() { return Debugger::compiling_natives_; }

 private:
  static bool debugger_active_;  // Are there any active debugger?
  static bool compiling_natives_;  // Are we compiling natives?
  static DebugMessageThread* message_thread_;
  static v8::DebugMessageHandler debug_message_handler_;
  static void* debug_message_handler_data_;

  // Head and tail of linked list of pending commands. The list is protected
  // by a mutex as it can be updated/read from different threads.
  static Mutex* pending_requests_access_;
  static PendingRequest* pending_requests_head_;
  static PendingRequest* pending_requests_tail_;
};


// Linked list of pending requests issued by debugger while V8 was running.
class PendingRequest {
 public:
  PendingRequest(const uint16_t* json_request, int length);
  ~PendingRequest();

  PendingRequest* next() { return next_; }
  void set_next(PendingRequest* next) { next_ = next; }
  Handle<String> request();

 private:
  Vector<uint16_t> json_request_;  // Request string.
  PendingRequest* next_;  // Next pointer for linked list.
};


class DebugMessageThread: public Thread {
 public:
  DebugMessageThread();
  virtual ~DebugMessageThread();

  void DebugEvent(v8::DebugEvent,
                  Handle<Object> exec_state,
                  Handle<Object> event_data);
  void SetEventJSON(Vector<uint16_t> event_json);
  void SetEventJSONFromEvent(Handle<Object> event_data);
  void SetCommand(Vector<uint16_t> command);
  void SetResult(const char* result);
  void SetResult(Vector<uint16_t> result);
  void CommandResult(Vector<uint16_t> result);

  void ProcessCommand(Vector<uint16_t> command);

  void OnDebuggerInactive();

 protected:
  void Run();
  void HandleCommand();

  bool host_running_;  // Is the debugging host running or stopped
  v8::DebugEvent event_;  // Active event
  Semaphore* command_received_;  // Signal from the telnet connection
  Semaphore* debug_event_;  // Signal from the V8 thread
  Semaphore* debug_command_;  // Signal to the V8 thread
  Semaphore* debug_result_;  // Signal from the V8 thread

 private:
  void SetVector(Vector<uint16_t>* vector, Vector<uint16_t> value);
  bool TwoByteEqualsAscii(Vector<uint16_t> two_byte, const char* ascii);

  Vector<uint16_t> event_json_;  // Active event JSON.
  Vector<uint16_t> command_;  // Current command.
  Vector<uint16_t> result_;  // Result of processing command.
  DISALLOW_EVIL_CONSTRUCTORS(DebugMessageThread);
};


// Helper class to support saving/restoring the top break frame id.
class SaveBreakFrame {
 public:
  SaveBreakFrame() : set_(!it_.done()) {
    if (set_) {
      // Store the previous break is and frame id.
      break_id_ = Top::break_id();
      break_frame_id_ = Top::break_frame_id();

      // Create the new break info.
      Top::new_break(it_.frame()->id());
    }
  }

  ~SaveBreakFrame() {
    if (set_) {
      // restore to the previous break state.
      Top::set_break(break_frame_id_, break_id_);
    }
  }

 private:
  JavaScriptFrameIterator it_;
  const bool set_;  // Was the break actually set?
  StackFrame::Id break_frame_id_;  // Previous break frame id.
  int break_id_;  // Previous break id.
};


class EnterDebuggerContext BASE_EMBEDDED {
 public:
  // Enter the debugger by storing the previous top context and setting the
  // current top context to the debugger context.
  EnterDebuggerContext()  {
    // NOTE the member variable save which saves the previous context before
    // this change.
    Top::set_context(*Debug::debug_context());
    Top::set_security_context(*Debug::debug_context());
  }

 private:
  SaveContext save;
};


// Stack allocated class for disabling break.
class DisableBreak BASE_EMBEDDED {
 public:
  // Enter the debugger by storing the previous top context and setting the
  // current top context to the debugger context.
  explicit DisableBreak(bool disable_break)  {
    prev_disable_break_ = Debug::disable_break();
    Debug::set_disable_break(disable_break);
  }
  ~DisableBreak() {
    Debug::set_disable_break(prev_disable_break_);
  }

 private:
  // The previous state of the disable break used to restore the value when this
  // object is destructed.
  bool prev_disable_break_;
};


// Debug_Address encapsulates the Address pointers used in generating debug
// code.
class Debug_Address {
 public:
  Debug_Address(Debug::AddressId id, int reg = 0)
    : id_(id), reg_(reg) {
    ASSERT(reg == 0 || id == Debug::k_register_address);
  }

  static Debug_Address AfterBreakTarget() {
    return Debug_Address(Debug::k_after_break_target_address);
  }

  static Debug_Address DebugBreakReturn() {
    return Debug_Address(Debug::k_debug_break_return_address);
  }

  static Debug_Address Register(int reg) {
    return Debug_Address(Debug::k_register_address, reg);
  }

  Address address() const {
    switch (id_) {
      case Debug::k_after_break_target_address:
        return reinterpret_cast<Address>(Debug::after_break_target_address());
      case Debug::k_debug_break_return_address:
        return reinterpret_cast<Address>(Debug::debug_break_return_address());
      case Debug::k_register_address:
        return reinterpret_cast<Address>(Debug::register_address(reg_));
      default:
        UNREACHABLE();
        return NULL;
    }
  }
 private:
  Debug::AddressId id_;
  int reg_;
};


} }  // namespace v8::internal

#endif  // V8_DEBUG_H_
