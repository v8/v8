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

#include <stdarg.h>

#include "v8.h"

#include "log.h"
#include "platform.h"

namespace v8 { namespace internal {

#ifdef ENABLE_LOGGING_AND_PROFILING

//
// Command line flags used by Logger.
//
DEFINE_bool(log, false,
            "Minimal logging (no API, code, GC, suspect, or handles samples).");
DEFINE_bool(log_all, false, "Log all events to the log file.");
DEFINE_bool(log_api, false, "Log API events to the log file.");
DEFINE_bool(log_code, false,
            "Log code events to the log file without profiling.");
DEFINE_bool(log_gc, false,
            "Log heap samples on garbage collection for the hp2ps tool.");
DEFINE_bool(log_suspect, false, "Log suspect operations.");
DEFINE_bool(log_handles, false, "Log global handle events.");
DEFINE_bool(log_state_changes, false, "Log state changes.");
DEFINE_bool(prof, false,
            "Log statistical profiling information (implies --log-code).");
DEFINE_bool(sliding_state_window, false,
            "Update sliding state window counters.");

DEFINE_string(logfile, "v8.log", "Specify the name of the log file.");


//
// Sliding state window.  Updates counters to keep track of the last
// window of kBufferSize states.  This is useful to track where we
// spent our time.
//
class SlidingStateWindow {
 public:
  SlidingStateWindow();
  ~SlidingStateWindow();
  void AddState(StateTag state);

 private:
  static const int kBufferSize = 256;
  int current_index_;
  bool is_full_;
  byte buffer_[kBufferSize];


  void IncrementStateCounter(StateTag state) {
    Counters::state_counters[state].Increment();
  }


  void DecrementStateCounter(StateTag state) {
    Counters::state_counters[state].Decrement();
  }
};


//
// The Profiler samples pc and sp values for the main thread.
// Each sample is appended to a circular buffer.
// An independent thread removes data and writes it to the log.
// This design minimizes the time spent in the sampler.
//
class Profiler: public Thread {
 public:
  Profiler();
  void Engage();
  void Disengage();

  // Inserts collected profiling data into buffer.
  void Insert(TickSample* sample) {
    if (Succ(head_) == tail_) {
      overflow_ = true;
    } else {
      buffer_[head_] = *sample;
      head_ = Succ(head_);
      buffer_semaphore_->Signal();  // Tell we have an element.
    }
  }

  // Waits for a signal and removes profiling data.
  bool Remove(TickSample* sample) {
    buffer_semaphore_->Wait();  // Wait for an element.
    *sample = buffer_[tail_];
    bool result = overflow_;
    tail_ = Succ(tail_);
    overflow_ = false;
    return result;
  }

  void Run();

 private:
  // Returns the next index in the cyclic buffer.
  int Succ(int index) { return (index + 1) % kBufferSize; }

  // Cyclic buffer for communicating profiling samples
  // between the signal handler and the worker thread.
  static const int kBufferSize = 128;
  TickSample buffer_[kBufferSize];  // Buffer storage.
  int head_;  // Index to the buffer head.
  int tail_;  // Index to the buffer tail.
  bool overflow_;  // Tell whether a buffer overflow has occurred.
  Semaphore* buffer_semaphore_;  // Sempahore used for buffer synchronization.

  // Tells whether worker thread should continue running.
  bool running_;
};


//
// Ticker used to provide ticks to the profiler and the sliding state
// window.
//
class Ticker: public ProfileSampler {
 public:
  explicit Ticker(int interval):
      ProfileSampler(interval), window_(NULL), profiler_(NULL) {}

  ~Ticker() { if (IsActive()) Stop(); }

  void Tick(TickSample* sample) {
    if (profiler_) profiler_->Insert(sample);
    if (window_) window_->AddState(Logger::state());
  }

  void SetWindow(SlidingStateWindow* window) {
    window_ = window;
    if (!IsActive()) Start();
  }

  void ClearWindow() {
    window_ = NULL;
    if (!profiler_ && IsActive()) Stop();
  }

  void SetProfiler(Profiler* profiler) {
    profiler_ = profiler;
    if (!IsActive()) Start();
  }

  void ClearProfiler() {
    profiler_ = NULL;
    if (!window_ && IsActive()) Stop();
  }

 private:
  SlidingStateWindow* window_;
  Profiler* profiler_;
};


//
// SlidingStateWindow implementation.
//
SlidingStateWindow::SlidingStateWindow(): current_index_(0), is_full_(false) {
  for (int i = 0; i < kBufferSize; i++) {
    buffer_[i] = static_cast<byte>(OTHER);
  }
  Logger::ticker_->SetWindow(this);
}


SlidingStateWindow::~SlidingStateWindow() {
  Logger::ticker_->ClearWindow();
}


void SlidingStateWindow::AddState(StateTag state) {
  if (is_full_) {
    DecrementStateCounter(static_cast<StateTag>(buffer_[current_index_]));
  } else if (current_index_ == kBufferSize - 1) {
    is_full_ = true;
  }
  buffer_[current_index_] = static_cast<byte>(state);
  IncrementStateCounter(state);
  ASSERT(IsPowerOf2(kBufferSize));
  current_index_ = (current_index_ + 1) & (kBufferSize - 1);
}


//
// Profiler implementation.
//
Profiler::Profiler() {
  buffer_semaphore_ = OS::CreateSemaphore(0);
  head_ = 0;
  tail_ = 0;
  overflow_ = false;
  running_ = false;
}


void Profiler::Engage() {
  OS::LogSharedLibraryAddresses();

  // Start thread processing the profiler buffer.
  running_ = true;
  Start();

  // Register to get ticks.
  Logger::ticker_->SetProfiler(this);

  LOG(StringEvent("profiler", "begin"));
}


void Profiler::Disengage() {
  // Stop receiving ticks.
  Logger::ticker_->ClearProfiler();

  // Terminate the worker thread by setting running_ to false,
  // inserting a fake element in the queue and then wait for
  // the thread to terminate.
  running_ = false;
  TickSample sample;
  sample.pc = 0;
  sample.sp = 0;
  sample.state = OTHER;
  Insert(&sample);
  Join();

  LOG(StringEvent("profiler", "end"));
}


void Profiler::Run() {
  TickSample sample;
  bool overflow = Logger::profiler_->Remove(&sample);
  while (running_) {
    LOG(TickEvent(&sample, overflow));
    overflow = Logger::profiler_->Remove(&sample);
  }
}


//
// Synchronize class used for ensuring block structured
// locking for the Logger::*Event functions.
//

class Synchronize {
 public:
  explicit Synchronize(Mutex* mutex) {
    mutex_ = mutex;
    mutex_->Lock();
  }
  ~Synchronize() {
    mutex_->Unlock();
  }
 private:
  // Mutex used for enforcing block structured access.
  Mutex* mutex_;
};


//
// Logger class implementation.
//
Ticker* Logger::ticker_ = NULL;
FILE* Logger::logfile_ = NULL;
Profiler* Logger::profiler_ = NULL;
Mutex* Logger::mutex_ = NULL;
VMState* Logger::current_state_ = NULL;
SlidingStateWindow* Logger::sliding_state_window_ = NULL;

#endif  // ENABLE_LOGGING_AND_PROFILING

void Logger::Preamble(const char* content) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "%s", content);
#endif
}


void Logger::StringEvent(const char* name, const char* value) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "%s,\"%s\"\n", name, value);
#endif
}


void Logger::IntEvent(const char* name, int value) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "%s,%d\n", name, value);
#endif
}


void Logger::HandleEvent(const char* name, Object** location) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_handles) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "%s,0x%x\n", name,
          reinterpret_cast<unsigned int>(location));
#endif
}


#ifdef ENABLE_LOGGING_AND_PROFILING
// ApiEvent is private so all the calls come from the Logger class.  It is the
// caller's responsibility to ensure that logfile_ is not NULL and that
// FLAG_log_api is true.
void Logger::ApiEvent(const char* format, ...) {
  ASSERT(logfile_ != NULL && FLAG_log_api);
  Synchronize s(mutex_);
  va_list ap;
  va_start(ap, format);
  vfprintf(logfile_, format, ap);
}
#endif


void Logger::ApiNamedSecurityCheck(Object* key) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_api) return;
  if (key->IsString()) {
    SmartPointer<char> str =
        String::cast(key)->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL);
    ApiEvent("api,check-security,\"%s\"\n", *str);
  } else if (key->IsUndefined()) {
    ApiEvent("api,check-security,undefined\n");
  } else {
    ApiEvent("api,check-security,['no-name']\n");
  }
#endif
}


void Logger::SharedLibraryEvent(const char* library_path,
                                unsigned start,
                                unsigned end) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_prof) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "shared-library,\"%s\",0x%08x,0x%08x\n", library_path,
          start, end);
#endif
}


void Logger::SharedLibraryEvent(const wchar_t* library_path,
                                unsigned start,
                                unsigned end) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_prof) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "shared-library,\"%ls\",0x%08x,0x%08x\n", library_path,
          start, end);
#endif
}

void Logger::ApiIndexedSecurityCheck(uint32_t index) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_api) return;
  ApiEvent("api,check-security,%u\n", index);
#endif
}


void Logger::ApiNamedPropertyAccess(const char* tag,
                                    JSObject* holder,
                                    Object* name) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  ASSERT(name->IsString());
  if (logfile_ == NULL || !FLAG_log_api) return;
  String* class_name_obj = holder->class_name();
  SmartPointer<char> class_name =
      class_name_obj->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL);
  SmartPointer<char> property_name =
      String::cast(name)->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL);
  Logger::ApiEvent("api,%s,\"%s\",\"%s\"\n", tag, *class_name, *property_name);
#endif
}

void Logger::ApiIndexedPropertyAccess(const char* tag,
                                      JSObject* holder,
                                      uint32_t index) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_api) return;
  String* class_name_obj = holder->class_name();
  SmartPointer<char> class_name =
      class_name_obj->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL);
  Logger::ApiEvent("api,%s,\"%s\",%u\n", tag, *class_name, index);
#endif
}

void Logger::ApiObjectAccess(const char* tag, JSObject* object) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_api) return;
  String* class_name_obj = object->class_name();
  SmartPointer<char> class_name =
      class_name_obj->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL);
  Logger::ApiEvent("api,%s,\"%s\"\n", tag, *class_name);
#endif
}


void Logger::ApiEntryCall(const char* name) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_api) return;
  Logger::ApiEvent("api,%s\n", name);
#endif
}


void Logger::NewEvent(const char* name, void* object, size_t size) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "new,%s,0x%x,%u\n", name,
          reinterpret_cast<unsigned int>(object),
          static_cast<unsigned int>(size));
#endif
}


void Logger::DeleteEvent(const char* name, void* object) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "delete,%s,0x%x\n", name,
          reinterpret_cast<unsigned int>(object));
#endif
}


void Logger::CodeCreateEvent(const char* tag, Code* code, const char* comment) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_code) return;
  Synchronize s(mutex_);

  fprintf(logfile_, "code-creation,%s,0x%x,%d,\"", tag,
          reinterpret_cast<unsigned int>(code->address()),
          code->instruction_size());
  for (const char* p = comment; *p != '\0'; p++) {
    if (*p == '\"') fprintf(logfile_, "\\");
    fprintf(logfile_, "%c", *p);
  }
  fprintf(logfile_, "\"\n");
#endif
}


void Logger::CodeCreateEvent(const char* tag, Code* code, String* name) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_code) return;
  Synchronize s(mutex_);
  SmartPointer<char> str =
      name->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL);
  fprintf(logfile_, "code-creation,%s,0x%x,%d,\"%s\"\n", tag,
          reinterpret_cast<unsigned int>(code->address()),
          code->instruction_size(), *str);
#endif
}


void Logger::CodeCreateEvent(const char* tag, Code* code, int args_count) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_code) return;
  Synchronize s(mutex_);

  fprintf(logfile_, "code-creation,%s,0x%x,%d,\"args_count: %d\"\n", tag,
          reinterpret_cast<unsigned int>(code->address()),
          code->instruction_size(),
          args_count);
#endif
}


void Logger::CodeMoveEvent(Address from, Address to) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_code) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "code-move,0x%x,0x%x\n",
          reinterpret_cast<unsigned int>(from),
          reinterpret_cast<unsigned int>(to));
#endif
}


void Logger::CodeDeleteEvent(Address from) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_code) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "code-delete,0x%x\n", reinterpret_cast<unsigned int>(from));
#endif
}


void Logger::ResourceEvent(const char* name, const char* tag) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "%s,%s,", name, tag);

  uint32_t sec, usec;
  if (OS::GetUserTime(&sec, &usec) != -1) {
    fprintf(logfile_, "%d,%d,", sec, usec);
  }
  fprintf(logfile_, "%.0f", OS::TimeCurrentMillis());

  fprintf(logfile_, "\n");
#endif
}


void Logger::SuspectReadEvent(String* name, String* obj) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_suspect) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "suspect-read,");
  obj->PrintOn(logfile_);
  fprintf(logfile_, ",\"");
  name->PrintOn(logfile_);
  fprintf(logfile_, "\"\n");
#endif
}


void Logger::HeapSampleBeginEvent(const char* space, const char* kind) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_gc) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "heap-sample-begin,\"%s\",\"%s\"\n", space, kind);
#endif
}


void Logger::HeapSampleEndEvent(const char* space, const char* kind) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_gc) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "heap-sample-end,\"%s\",\"%s\"\n", space, kind);
#endif
}


void Logger::HeapSampleItemEvent(const char* type, int number, int bytes) {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (logfile_ == NULL || !FLAG_log_gc) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "heap-sample-item,%s,%d,%d\n", type, number, bytes);
#endif
}


#ifdef ENABLE_LOGGING_AND_PROFILING
void Logger::TickEvent(TickSample* sample, bool overflow) {
  if (logfile_ == NULL) return;
  Synchronize s(mutex_);
  fprintf(logfile_, "tick,0x%x,0x%x,%d", sample->pc, sample->sp,
          static_cast<int>(sample->state));
  if (overflow) fprintf(logfile_, ",overflow");
  fprintf(logfile_, "\n");
}
#endif


bool Logger::Setup() {
#ifdef ENABLE_LOGGING_AND_PROFILING
  // --log-all enables all the log flags.
  if (FLAG_log_all) {
    FLAG_log_api = true;
    FLAG_log_code = true;
    FLAG_log_gc = true;
    FLAG_log_suspect = true;
    FLAG_log_handles = true;
  }

  // --prof implies --log-code.
  if (FLAG_prof) FLAG_log_code = true;

  // Each of the individual log flags implies --log.  Check after
  // checking --log-all and --prof in case they set --log-code.
  if (FLAG_log_api || FLAG_log_code || FLAG_log_gc ||
      FLAG_log_handles || FLAG_log_suspect) {
    FLAG_log = true;
  }

  // If we're logging anything, we need to open the log file.
  if (FLAG_log) {
    if (strcmp(FLAG_logfile, "-") == 0) {
      logfile_ = stdout;
    } else {
      logfile_ = fopen(FLAG_logfile, "w");
    }
    mutex_ = OS::CreateMutex();
  }

  current_state_ = new VMState(OTHER);

  ticker_ = new Ticker(10);

  if (FLAG_sliding_state_window && sliding_state_window_ == NULL) {
    sliding_state_window_ = new SlidingStateWindow();
  }

  if (FLAG_prof) {
    profiler_ = new Profiler();
    profiler_->Engage();
  }

  return true;

#else
  return false;
#endif
}


void Logger::TearDown() {
#ifdef ENABLE_LOGGING_AND_PROFILING
  // Stop the profiler before closing the file.
  if (profiler_ != NULL) {
    profiler_->Disengage();
    delete profiler_;
    profiler_ = NULL;
  }

  // Deleting the current_state_ has the side effect of assigning to it(!).
  while (current_state_) delete current_state_;
  delete sliding_state_window_;

  delete ticker_;

  if (logfile_ != NULL) {
    fclose(logfile_);
    logfile_ = NULL;
    delete mutex_;
    mutex_ = NULL;
  }
#endif
}


void Logger::EnableSlidingStateWindow() {
#ifdef ENABLE_LOGGING_AND_PROFILING
  // If the ticker is NULL, Logger::Setup has not been called yet.  In
  // that case, we set the sliding_state_window flag so that the
  // sliding window computation will be started when Logger::Setup is
  // called.
  if (ticker_ == NULL) {
    FLAG_sliding_state_window = true;
    return;
  }
  // Otherwise, if the sliding state window computation has not been
  // started we do it now.
  if (sliding_state_window_ == NULL) {
    sliding_state_window_ = new SlidingStateWindow();
  }
#endif
}


//
// VMState class implementation.  A simple stack of VM states held by the
// logger and partially threaded through the call stack.  States are pushed by
// VMState construction and popped by destruction.
//
#ifdef ENABLE_LOGGING_AND_PROFILING
static const char* StateToString(StateTag state) {
  switch (state) {
    case GC:
      return "GC";
    case COMPILER:
      return "COMPILER";
    case OTHER:
      return "OTHER";
    default:
      UNREACHABLE();
      return NULL;
  }
}

VMState::VMState(StateTag state) {
  state_ = state;
  previous_ = Logger::current_state_;
  Logger::current_state_ = this;

  if (FLAG_log_state_changes) {
    LOG(StringEvent("Entering", StateToString(state_)));
    if (previous_) {
      LOG(StringEvent("From", StateToString(previous_->state_)));
    }
  }
}


VMState::~VMState() {
  Logger::current_state_ = previous_;

  if (FLAG_log_state_changes) {
    LOG(StringEvent("Leaving", StateToString(state_)));
    if (previous_) {
      LOG(StringEvent("To", StateToString(previous_->state_)));
    }
  }
}
#endif

} }  // namespace v8::internal
