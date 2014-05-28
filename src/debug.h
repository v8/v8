// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_H_
#define V8_DEBUG_H_

#include "allocation.h"
#include "arguments.h"
#include "assembler.h"
#include "execution.h"
#include "factory.h"
#include "flags.h"
#include "frames-inl.h"
#include "hashmap.h"
#include "liveedit.h"
#include "platform.h"
#include "string-stream.h"
#include "v8threads.h"

#include "../include/v8-debug.h"

namespace v8 {
namespace internal {


// Forward declarations.
class EnterDebugger;


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


// The different types of breakpoint position alignments.
// Must match Debug.BreakPositionAlignment in debug-debugger.js
enum BreakPositionAlignment {
  STATEMENT_ALIGNED = 0,
  BREAK_POSITION_ALIGNED = 1
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
  void FindBreakLocationFromPosition(int position,
      BreakPositionAlignment alignment);
  void Reset();
  bool Done() const;
  void SetBreakPoint(Handle<Object> break_point_object);
  void ClearBreakPoint(Handle<Object> break_point_object);
  void SetOneShot();
  void ClearOneShot();
  bool IsStepInLocation(Isolate* isolate);
  void PrepareStepIn(Isolate* isolate);
  bool IsExit() const;
  bool HasBreakPoint();
  bool IsDebugBreak();
  Object* BreakPointObjects();
  void ClearAllDebugBreak();


  inline int code_position() {
    return static_cast<int>(pc() - debug_info_->code()->entry());
  }
  inline int break_point() { return break_point_; }
  inline int position() { return position_; }
  inline int statement_position() { return statement_position_; }
  inline Address pc() { return reloc_iterator_->rinfo()->pc(); }
  inline Code* code() { return debug_info_->code(); }
  inline RelocInfo* rinfo() { return reloc_iterator_->rinfo(); }
  inline RelocInfo::Mode rmode() const {
    return reloc_iterator_->rinfo()->rmode();
  }
  inline RelocInfo* original_rinfo() {
    return reloc_iterator_original_->rinfo();
  }
  inline RelocInfo::Mode original_rmode() const {
    return reloc_iterator_original_->rinfo()->rmode();
  }

  bool IsDebuggerStatement();

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

  void SetDebugBreakAtIC();
  void ClearDebugBreakAtIC();

  bool IsDebugBreakAtReturn();
  void SetDebugBreakAtReturn();
  void ClearDebugBreakAtReturn();

  bool IsDebugBreakSlot();
  bool IsDebugBreakAtSlot();
  void SetDebugBreakAtSlot();
  void ClearDebugBreakAtSlot();

  DISALLOW_COPY_AND_ASSIGN(BreakLocationIterator);
};


// Cache of all script objects in the heap. When a script is added a weak handle
// to it is created and that weak handle is stored in the cache. The weak handle
// callback takes care of removing the script from the cache. The key used in
// the cache is the script id.
class ScriptCache : private HashMap {
 public:
  explicit ScriptCache(Isolate* isolate)
      : HashMap(HashMap::PointersMatch),
        isolate_(isolate),
        collected_scripts_(10) {}
  virtual ~ScriptCache() { Clear(); }

  // Add script to the cache.
  void Add(Handle<Script> script);

  // Return the scripts in the cache.
  Handle<FixedArray> GetScripts();

  // Generate debugger events for collected scripts.
  void ProcessCollectedScripts();

 private:
  // Calculate the hash value from the key (script id).
  static uint32_t Hash(int key) {
    return ComputeIntegerHash(key, v8::internal::kZeroHashSeed);
  }

  // Clear the cache releasing all the weak handles.
  void Clear();

  // Weak handle callback for scripts in the cache.
  static void HandleWeakScript(
      const v8::WeakCallbackData<v8::Value, void>& data);

  Isolate* isolate_;
  // List used during GC to temporarily store id's of collected scripts.
  List<int> collected_scripts_;
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



// Message delivered to the message handler callback. This is either a debugger
// event or the response to a command.
class MessageImpl: public v8::Debug::Message {
 public:
  // Create a message object for a debug event.
  static MessageImpl NewEvent(DebugEvent event,
                              bool running,
                              Handle<JSObject> exec_state,
                              Handle<JSObject> event_data);

  // Create a message object for the response to a debug command.
  static MessageImpl NewResponse(DebugEvent event,
                                 bool running,
                                 Handle<JSObject> exec_state,
                                 Handle<JSObject> event_data,
                                 Handle<String> response_json,
                                 v8::Debug::ClientData* client_data);

  // Implementation of interface v8::Debug::Message.
  virtual bool IsEvent() const;
  virtual bool IsResponse() const;
  virtual DebugEvent GetEvent() const;
  virtual bool WillStartRunning() const;
  virtual v8::Handle<v8::Object> GetExecutionState() const;
  virtual v8::Handle<v8::Object> GetEventData() const;
  virtual v8::Handle<v8::String> GetJSON() const;
  virtual v8::Handle<v8::Context> GetEventContext() const;
  virtual v8::Debug::ClientData* GetClientData() const;
  virtual v8::Isolate* GetIsolate() const;

 private:
  MessageImpl(bool is_event,
              DebugEvent event,
              bool running,
              Handle<JSObject> exec_state,
              Handle<JSObject> event_data,
              Handle<String> response_json,
              v8::Debug::ClientData* client_data);

  bool is_event_;  // Does this message represent a debug event?
  DebugEvent event_;  // Debug event causing the break.
  bool running_;  // Will the VM start running after this event?
  Handle<JSObject> exec_state_;  // Current execution state.
  Handle<JSObject> event_data_;  // Data associated with the event.
  Handle<String> response_json_;  // Response JSON if message holds a response.
  v8::Debug::ClientData* client_data_;  // Client data passed with the request.
};


// Details of the debug event delivered to the debug event listener.
class EventDetailsImpl : public v8::Debug::EventDetails {
 public:
  EventDetailsImpl(DebugEvent event,
                   Handle<JSObject> exec_state,
                   Handle<JSObject> event_data,
                   Handle<Object> callback_data,
                   v8::Debug::ClientData* client_data);
  virtual DebugEvent GetEvent() const;
  virtual v8::Handle<v8::Object> GetExecutionState() const;
  virtual v8::Handle<v8::Object> GetEventData() const;
  virtual v8::Handle<v8::Context> GetEventContext() const;
  virtual v8::Handle<v8::Value> GetCallbackData() const;
  virtual v8::Debug::ClientData* GetClientData() const;
 private:
  DebugEvent event_;  // Debug event causing the break.
  Handle<JSObject> exec_state_;         // Current execution state.
  Handle<JSObject> event_data_;         // Data associated with the event.
  Handle<Object> callback_data_;        // User data passed with the callback
                                        // when it was registered.
  v8::Debug::ClientData* client_data_;  // Data passed to DebugBreakForCommand.
};


// Message send by user to v8 debugger or debugger output message.
// In addition to command text it may contain a pointer to some user data
// which are expected to be passed along with the command reponse to message
// handler.
class CommandMessage {
 public:
  static CommandMessage New(const Vector<uint16_t>& command,
                            v8::Debug::ClientData* data);
  CommandMessage();
  ~CommandMessage();

  // Deletes user data and disposes of the text.
  void Dispose();
  Vector<uint16_t> text() const { return text_; }
  v8::Debug::ClientData* client_data() const { return client_data_; }
 private:
  CommandMessage(const Vector<uint16_t>& text,
                 v8::Debug::ClientData* data);

  Vector<uint16_t> text_;
  v8::Debug::ClientData* client_data_;
};


// A Queue of CommandMessage objects.  A thread-safe version is
// LockingCommandMessageQueue, based on this class.
class CommandMessageQueue BASE_EMBEDDED {
 public:
  explicit CommandMessageQueue(int size);
  ~CommandMessageQueue();
  bool IsEmpty() const { return start_ == end_; }
  CommandMessage Get();
  void Put(const CommandMessage& message);
  void Clear() { start_ = end_ = 0; }  // Queue is empty after Clear().
 private:
  // Doubles the size of the message queue, and copies the messages.
  void Expand();

  CommandMessage* messages_;
  int start_;
  int end_;
  int size_;  // The size of the queue buffer.  Queue can hold size-1 messages.
};


// LockingCommandMessageQueue is a thread-safe circular buffer of CommandMessage
// messages.  The message data is not managed by LockingCommandMessageQueue.
// Pointers to the data are passed in and out. Implemented by adding a
// Mutex to CommandMessageQueue.  Includes logging of all puts and gets.
class LockingCommandMessageQueue BASE_EMBEDDED {
 public:
  LockingCommandMessageQueue(Logger* logger, int size);
  bool IsEmpty() const;
  CommandMessage Get();
  void Put(const CommandMessage& message);
  void Clear();
 private:
  Logger* logger_;
  CommandMessageQueue queue_;
  mutable Mutex mutex_;
  DISALLOW_COPY_AND_ASSIGN(LockingCommandMessageQueue);
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
  void OnDebugBreak(Handle<Object> break_points_hit, bool auto_continue);
  void OnException(Handle<Object> exception, bool uncaught);
  void OnBeforeCompile(Handle<Script> script);

  enum AfterCompileFlags {
    NO_AFTER_COMPILE_FLAGS,
    SEND_WHEN_DEBUGGING
  };
  void OnAfterCompile(Handle<Script> script,
                      AfterCompileFlags after_compile_flags);
  void OnScriptCollected(int id);

  void SetEventListener(Handle<Object> callback, Handle<Object> data);
  void SetMessageHandler(v8::Debug::MessageHandler handler);

  // Add a debugger command to the command queue.
  void EnqueueCommandMessage(Vector<const uint16_t> command,
                             v8::Debug::ClientData* client_data = NULL);

  // Check whether there are commands in the command queue.
  bool HasCommands();

  // Enqueue a debugger command to the command queue for event listeners.
  void EnqueueDebugCommand(v8::Debug::ClientData* client_data = NULL);

  MUST_USE_RESULT MaybeHandle<Object> Call(Handle<JSFunction> fun,
                                           Handle<Object> data);

  Handle<Context> GetDebugContext();

  bool ignore_debugger() const { return ignore_debugger_; }
  void set_live_edit_enabled(bool v) { live_edit_enabled_ = v; }
  bool live_edit_enabled() const {
    return FLAG_enable_liveedit && live_edit_enabled_ ;
  }

  bool is_active() { return is_active_; }

  class IgnoreScope {
   public:
    explicit IgnoreScope(Debug* debug)
        : debug_(debug),
          old_state_(debug->ignore_debugger_) {
      debug_->ignore_debugger_ = true;
    }

    ~IgnoreScope() {
      debug_->ignore_debugger_ = old_state_;
    }

   private:
    Debug* debug_;
    bool old_state_;
    DISALLOW_COPY_AND_ASSIGN(IgnoreScope);
  };


  bool Load();
  void Unload();
  bool IsLoaded() { return !debug_context_.is_null(); }
  bool InDebugger() { return thread_local_.debugger_entry_ != NULL; }

  void Break(Arguments args, JavaScriptFrame*);
  void SetAfterBreakTarget(JavaScriptFrame* frame);
  bool SetBreakPoint(Handle<JSFunction> function,
                     Handle<Object> break_point_object,
                     int* source_position);
  bool SetBreakPointForScript(Handle<Script> script,
                              Handle<Object> break_point_object,
                              int* source_position,
                              BreakPositionAlignment alignment);
  void ClearBreakPoint(Handle<Object> break_point_object);
  void ClearAllBreakPoints();
  void FloodWithOneShot(Handle<JSFunction> function);
  void FloodBoundFunctionWithOneShot(Handle<JSFunction> function);
  void FloodHandlerWithOneShot();
  void ChangeBreakOnException(ExceptionBreakType type, bool enable);
  bool IsBreakOnException(ExceptionBreakType type);

  void PromiseHandlePrologue(Handle<JSFunction> promise_getter);
  void PromiseHandleEpilogue();
  // Returns a promise if it does not have a reject handler.
  Handle<Object> GetPromiseForUncaughtException();

  void PrepareStep(StepAction step_action,
                   int step_count,
                   StackFrame::Id frame_id);
  void ClearStepping();
  void ClearStepOut();
  bool IsStepping() { return thread_local_.step_count_ > 0; }
  bool StepNextContinue(BreakLocationIterator* break_location_iterator,
                        JavaScriptFrame* frame);
  static Handle<DebugInfo> GetDebugInfo(Handle<SharedFunctionInfo> shared);
  static bool HasDebugInfo(Handle<SharedFunctionInfo> shared);

  void PrepareForBreakPoints();

  // This function is used in FunctionNameUsing* tests.
  Object* FindSharedFunctionInfoInScript(Handle<Script> script, int position);

  // Returns whether the operation succeeded. Compilation can only be triggered
  // if a valid closure is passed as the second argument, otherwise the shared
  // function needs to be compiled already.
  bool EnsureDebugInfo(Handle<SharedFunctionInfo> shared,
                       Handle<JSFunction> function);

  // Returns true if the current stub call is patched to call the debugger.
  static bool IsDebugBreak(Address addr);
  // Returns true if the current return statement has been patched to be
  // a debugger breakpoint.
  static bool IsDebugBreakAtReturn(RelocInfo* rinfo);

  // Check whether a code stub with the specified major key is a possible break
  // point location.
  static bool IsSourceBreakStub(Code* code);
  static bool IsBreakStub(Code* code);

  // Find the builtin to use for invoking the debug break
  static Handle<Code> FindDebugBreak(Handle<Code> code, RelocInfo::Mode mode);

  static Handle<Object> GetSourceBreakLocations(
      Handle<SharedFunctionInfo> shared,
      BreakPositionAlignment position_aligment);

  // Getter for the debug_context.
  inline Handle<Context> debug_context() { return debug_context_; }

  // Check whether a global object is the debug global object.
  bool IsDebugGlobal(GlobalObject* global);

  // Check whether this frame is just about to return.
  bool IsBreakAtReturn(JavaScriptFrame* frame);

  // Fast check to see if any break points are active.
  inline bool has_break_points() { return has_break_points_; }

  void NewBreak(StackFrame::Id break_frame_id);
  void SetBreak(StackFrame::Id break_frame_id, int break_id);
  StackFrame::Id break_frame_id() {
    return thread_local_.break_frame_id_;
  }
  int break_id() { return thread_local_.break_id_; }

  bool StepInActive() { return thread_local_.step_into_fp_ != 0; }
  void HandleStepIn(Handle<JSFunction> function,
                    Handle<Object> holder,
                    Address fp,
                    bool is_constructor);
  Address step_in_fp() { return thread_local_.step_into_fp_; }
  Address* step_in_fp_addr() { return &thread_local_.step_into_fp_; }

  bool StepOutActive() { return thread_local_.step_out_fp_ != 0; }
  Address step_out_fp() { return thread_local_.step_out_fp_; }

  EnterDebugger* debugger_entry() {
    return thread_local_.debugger_entry_;
  }
  void set_debugger_entry(EnterDebugger* entry) {
    thread_local_.debugger_entry_ = entry;
  }

  // Check whether any of the specified interrupts are pending.
  bool has_pending_interrupt() {
    return thread_local_.has_pending_interrupt_;
  }

  // Set specified interrupts as pending.
  void set_has_pending_interrupt(bool value) {
    thread_local_.has_pending_interrupt_ = value;
  }

  // Getter and setter for the disable break state.
  bool disable_break() { return disable_break_; }
  void set_disable_break(bool disable_break) {
    disable_break_ = disable_break;
  }

  // Getters for the current exception break state.
  bool break_on_exception() { return break_on_exception_; }
  bool break_on_uncaught_exception() {
    return break_on_uncaught_exception_;
  }

  void FramesHaveBeenDropped(StackFrame::Id new_break_frame_id,
                             LiveEdit::FrameDropMode mode,
                             Object** restarter_frame_function_pointer);

  // Support for setting the address to jump to when returning from break point.
  Address after_break_target_address() {
    return reinterpret_cast<Address>(&after_break_target_);
  }

  Address restarter_frame_function_pointer_address() {
    Object*** address = &thread_local_.restarter_frame_function_pointer_;
    return reinterpret_cast<Address>(address);
  }

  static const int kEstimatedNofBreakPointsInFunction = 16;

  // Passed to MakeWeak.
  static void HandleWeakDebugInfo(
      const v8::WeakCallbackData<v8::Value, void>& data);

  friend Handle<FixedArray> GetDebuggedFunctions();  // In test-debug.cc
  friend void CheckDebuggerUnloaded(bool check_functions);  // In test-debug.cc

  // Threading support.
  char* ArchiveDebug(char* to);
  char* RestoreDebug(char* from);
  static int ArchiveSpacePerThread();
  void FreeThreadResources() { }

  // Mirror cache handling.
  void ClearMirrorCache();

  // Script cache handling.
  void CreateScriptCache();
  void DestroyScriptCache();
  void AddScriptToScriptCache(Handle<Script> script);
  Handle<FixedArray> GetLoadedScripts();

  // Record function from which eval was called.
  static void RecordEvalCaller(Handle<Script> script);

  // Garbage collection notifications.
  void AfterGarbageCollection();

 private:
  explicit Debug(Isolate* isolate);

  MUST_USE_RESULT MaybeHandle<Object> MakeJSObject(
      Vector<const char> constructor_name,
      int argc,
      Handle<Object> argv[]);
  MUST_USE_RESULT MaybeHandle<Object> MakeExecutionState();
  MUST_USE_RESULT MaybeHandle<Object> MakeBreakEvent(
      Handle<Object> break_points_hit);
  MUST_USE_RESULT MaybeHandle<Object> MakeExceptionEvent(
      Handle<Object> exception,
      bool uncaught,
      Handle<Object> promise);
  MUST_USE_RESULT MaybeHandle<Object> MakeCompileEvent(
      Handle<Script> script, bool before);
  MUST_USE_RESULT MaybeHandle<Object> MakeScriptCollectedEvent(int id);

  void CallEventCallback(v8::DebugEvent event,
                         Handle<Object> exec_state,
                         Handle<Object> event_data,
                         v8::Debug::ClientData* client_data);
  void CallCEventCallback(v8::DebugEvent event,
                          Handle<Object> exec_state,
                          Handle<Object> event_data,
                          v8::Debug::ClientData* client_data);
  void CallJSEventCallback(v8::DebugEvent event,
                           Handle<Object> exec_state,
                           Handle<Object> event_data);
  void UpdateState();

  void ProcessDebugEvent(v8::DebugEvent event,
                         Handle<JSObject> event_data,
                         bool auto_continue);
  void NotifyMessageHandler(v8::DebugEvent event,
                            Handle<JSObject> exec_state,
                            Handle<JSObject> event_data,
                            bool auto_continue);

  // Invoke the message handler function.
  void InvokeMessageHandler(MessageImpl message);

  inline bool EventActive() {
    // Check whether the message handler was been cleared.
    // TODO(yangguo): handle loading and unloading of the debugger differently.
    // Currently argument event is not used.
    return !ignore_debugger_ && is_active_;
  }

  static bool CompileDebuggerScript(Isolate* isolate, int index);
  void ClearOneShot();
  void ActivateStepIn(StackFrame* frame);
  void ClearStepIn();
  void ActivateStepOut(StackFrame* frame);
  void ClearStepNext();
  // Returns whether the compile succeeded.
  void RemoveDebugInfo(Handle<DebugInfo> debug_info);
  Handle<Object> CheckBreakPoints(Handle<Object> break_point);
  bool CheckBreakPoint(Handle<Object> break_point_object);

  void EnsureFunctionHasDebugBreakSlots(Handle<JSFunction> function);
  void RecompileAndRelocateSuspendedGenerators(
      const List<Handle<JSGeneratorObject> > &suspended_generators);

  void ThreadInit();

  class PromiseOnStack {
   public:
    PromiseOnStack(Isolate* isolate,
                   PromiseOnStack* prev,
                   Handle<JSFunction> getter);
    ~PromiseOnStack();
    StackHandler* handler() { return handler_; }
    Handle<JSFunction> getter() { return getter_; }
    PromiseOnStack* prev() { return prev_; }
   private:
    Isolate* isolate_;
    StackHandler* handler_;
    Handle<JSFunction> getter_;
    PromiseOnStack* prev_;
  };

  // Global handles.
  Handle<Context> debug_context_;
  Handle<Object> event_listener_;
  Handle<Object> event_listener_data_;

  v8::Debug::MessageHandler message_handler_;

  static const int kQueueInitialSize = 4;
  Semaphore command_received_;  // Signaled for each command received.
  LockingCommandMessageQueue command_queue_;
  LockingCommandMessageQueue event_command_queue_;

  bool is_active_;
  bool ignore_debugger_;
  bool live_edit_enabled_;
  bool has_break_points_;
  bool disable_break_;
  bool break_on_exception_;
  bool break_on_uncaught_exception_;

  ScriptCache* script_cache_;  // Cache of all scripts in the heap.
  DebugInfoListNode* debug_info_list_;  // List of active debug info objects.

  // Storage location for jump when exiting debug break calls.
  // Note that this address is not GC safe.  It should be computed immediately
  // before returning to the DebugBreakCallHelper.
  Address after_break_target_;

  // Per-thread data.
  class ThreadLocal {
   public:
    // Top debugger entry.
    EnterDebugger* debugger_entry_;

    // Counter for generating next break id.
    int break_count_;

    // Current break id.
    int break_id_;

    // Frame id for the frame of the current break.
    StackFrame::Id break_frame_id_;

    // Step action for last step performed.
    StepAction last_step_action_;

    // Source statement position from last step next action.
    int last_statement_position_;

    // Number of steps left to perform before debug event.
    int step_count_;

    // Frame pointer from last step next action.
    Address last_fp_;

    // Number of queued steps left to perform before debug event.
    int queued_step_count_;

    // Frame pointer for frame from which step in was performed.
    Address step_into_fp_;

    // Frame pointer for the frame where debugger should be called when current
    // step out action is completed.
    Address step_out_fp_;

    // Pending interrupts scheduled while debugging.
    bool has_pending_interrupt_;

    // Stores the way how LiveEdit has patched the stack. It is used when
    // debugger returns control back to user script.
    LiveEdit::FrameDropMode frame_drop_mode_;

    // When restarter frame is on stack, stores the address
    // of the pointer to function being restarted. Otherwise (most of the time)
    // stores NULL. This pointer is used with 'step in' implementation.
    Object** restarter_frame_function_pointer_;

    // When a promise is being resolved, we may want to trigger a debug event
    // if we catch a throw.  For this purpose we remember the try-catch
    // handler address that would catch the exception.  We also hold onto a
    // closure that returns a promise if the exception is considered uncaught.
    // Due to the possibility of reentry we use a linked list.
    PromiseOnStack* promise_on_stack_;
  };

  // Storage location for registers when handling debug break calls
  ThreadLocal thread_local_;

  Isolate* isolate_;

  friend class Isolate;
  friend class EnterDebugger;
  friend class FrameDropper;

  DISALLOW_COPY_AND_ASSIGN(Debug);
};


DECLARE_RUNTIME_FUNCTION(Debug_Break);


// This class is used for entering the debugger. Create an instance in the stack
// to enter the debugger. This will set the current break state, make sure the
// debugger is loaded and switch to the debugger context. If the debugger for
// some reason could not be entered FailedToEnter will return true.
class EnterDebugger BASE_EMBEDDED {
 public:
  explicit EnterDebugger(Isolate* isolate);
  ~EnterDebugger();

  // Check whether the debugger could be entered.
  inline bool FailedToEnter() { return load_failed_; }

  // Check whether there are any JavaScript frames on the stack.
  inline bool HasJavaScriptFrames() { return has_js_frames_; }

  // Get the active context from before entering the debugger.
  inline Handle<Context> GetContext() { return save_.context(); }

 private:
  Isolate* isolate_;
  EnterDebugger* prev_;  // Previous debugger entry if entered recursively.
  bool has_js_frames_;  // Were there any JavaScript frames?
  StackFrame::Id break_frame_id_;  // Previous break frame id.
  int break_id_;  // Previous break id.
  bool load_failed_;  // Did the debugger fail to load?
  SaveContext save_;  // Saves previous context.
};


// Stack allocated class for disabling break.
class DisableBreak BASE_EMBEDDED {
 public:
  explicit DisableBreak(Isolate* isolate, bool disable_break)
    : isolate_(isolate) {
    prev_disable_break_ = isolate_->debug()->disable_break();
    isolate_->debug()->set_disable_break(disable_break);
  }
  ~DisableBreak() {
    isolate_->debug()->set_disable_break(prev_disable_break_);
  }

 private:
  Isolate* isolate_;
  // The previous state of the disable break used to restore the value when this
  // object is destructed.
  bool prev_disable_break_;
};


// Code generator routines.
class DebugCodegen : public AllStatic {
 public:
  static void GenerateSlot(MacroAssembler* masm);
  static void GenerateCallICStubDebugBreak(MacroAssembler* masm);
  static void GenerateLoadICDebugBreak(MacroAssembler* masm);
  static void GenerateStoreICDebugBreak(MacroAssembler* masm);
  static void GenerateKeyedLoadICDebugBreak(MacroAssembler* masm);
  static void GenerateKeyedStoreICDebugBreak(MacroAssembler* masm);
  static void GenerateCompareNilICDebugBreak(MacroAssembler* masm);
  static void GenerateReturnDebugBreak(MacroAssembler* masm);
  static void GenerateCallFunctionStubDebugBreak(MacroAssembler* masm);
  static void GenerateCallConstructStubDebugBreak(MacroAssembler* masm);
  static void GenerateCallConstructStubRecordDebugBreak(MacroAssembler* masm);
  static void GenerateSlotDebugBreak(MacroAssembler* masm);
  static void GeneratePlainReturnLiveEdit(MacroAssembler* masm);

  // FrameDropper is a code replacement for a JavaScript frame with possibly
  // several frames above.
  // There is no calling conventions here, because it never actually gets
  // called, it only gets returned to.
  static void GenerateFrameDropperLiveEdit(MacroAssembler* masm);
};


} }  // namespace v8::internal

#endif  // V8_DEBUG_H_
