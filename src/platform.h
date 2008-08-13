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

// This module contains the platform-specific code. This make the rest of the
// code less dependent on operating system, compilers and runtime libraries.
// This module does specifically not deal with differences between different
// processor architecture.
// The platform classes have the same definition for all platforms. The
// implementation for a particular platform is put in platform_<os>.cc.
// The build system then uses the implementation for the target platform.
//
// This design has been choosen because it is simple and fast. Alternatively,
// the platform dependent classes could have been implemented using abstract
// superclasses with virtual methods and having specializations for each
// platform. This design was rejected because it was more complicated and
// slower. It would require factory methods for selecting the right
// implementation and the overhead of virtual methods for performance
// sensitive like mutex locking/unlocking.

#ifndef V8_PLATFORM_H_
#define V8_PLATFORM_H_

#ifdef WIN32

enum {
  FP_NAN,
  FP_INFINITE,
  FP_ZERO,
  FP_SUBNORMAL,
  FP_NORMAL
};

#define INFINITY HUGE_VAL

namespace v8 { namespace internal {
int isfinite(double x);
} }
int isnan(double x);
int isinf(double x);
int isless(double x, double y);
int isgreater(double x, double y);
int fpclassify(double x);
int signbit(double x);

int random();

int strcasecmp(const char* s1, const char* s2);
int strncasecmp(const char* s1, const char* s2, int n);

#else

// Unfortunately, the INFINITY macro cannot be used with the '-pedantic'
// warning flag and certain versions of GCC due to a bug:
// http://gcc.gnu.org/bugzilla/show_bug.cgi?id=11931
// For now, we use the more involved template-based version from <limits>, but
// only when compiling with GCC versions affected by the bug (2.96.x - 4.0.x)
// __GNUC_PREREQ is not defined in GCC for Mac OS X, so we define our own macro
#if defined(__GNUC__)
#define __GNUC_VERSION__ (__GNUC__ * 10000 + __GNUC_MINOR__ * 100)
#endif

#if __GNUC_VERSION__ >= 29600 && __GNUC_VERSION__ < 40100
#include <limits>
#undef INFINITY
#define INFINITY std::numeric_limits<double>::infinity()
#endif

#endif  // WIN32

namespace v8 { namespace internal {

double ceiling(double x);

// ----------------------------------------------------------------------------
// OS
//
// This class has static methods for the different platform specific
// functions. Add methods here to cope with differences between the
// supported platforms.

class OS {
 public:
  // Initializes the platform OS support. Called once at VM startup.
  static void Setup();

  // Returns the accumulated user time for thread. This routine
  // can be used for profiling. The implementation should
  // strive for high-precision timer resolution, preferable
  // micro-second resolution.
  static int GetUserTime(uint32_t* secs,  uint32_t* usecs);

  // Get a tick counter normalized to one tick per microsecond.
  // Used for calculating time intervals.
  static int64_t Ticks();

  // Returns current time as the number of milliseconds since
  // 00:00:00 UTC, January 1, 1970.
  static double TimeCurrentMillis();

  // Returns a string identifying the current time zone. The
  // timestamp is used for determining if DST is in effect.
  static char* LocalTimezone(double time);

  // Returns the local time offset in milliseconds east of UTC without
  // taking daylight savings time into account.
  static double LocalTimeOffset();

  // Returns the daylight savings offset for the given time.
  static double DaylightSavingsOffset(double time);

  // Print output to console. This is mostly used for debugging output.
  // On platforms that has standard terminal output, the output
  // should go to stdout.
  static void Print(const char* format, ...);
  static void VPrint(const char* format, va_list args);

  // Print error output to console. This is mostly used for error message
  // output. On platforms that has standard terminal output, the output
  // should go to stderr.
  static void PrintError(const char* format, ...);
  static void VPrintError(const char* format, va_list args);

  // Allocate/Free memory used by JS heap. Pages are readable/writeable, but
  // they are not guaranteed to be executable unless 'executable' is true.
  // Returns the address of allocated memory, or NULL if failed.
  static void* Allocate(const size_t requested,
                        size_t* allocated,
                        bool executable);
  static void Free(void* buf, const size_t length);
  // Get the Alignment guaranteed by Allocate().
  static size_t AllocateAlignment();

  // Returns an indication of whether a pointer is in a space that
  // has been allocated by Allocate().  This method may conservatively
  // always return false, but giving more accurate information may
  // improve the robustness of the stack dump code in the presence of
  // heap corruption.
  static bool IsOutsideAllocatedSpace(void* pointer);

  // Sleep for a number of miliseconds.
  static void Sleep(const int miliseconds);

  // Abort the current process.
  static void Abort();

  // Debug break.
  static void DebugBreak();

  // Walk the stack.
  static const int kStackWalkError = -1;
  static const int kStackWalkMaxNameLen = 256;
  static const int kStackWalkMaxTextLen = 256;
  struct StackFrame {
    void* address;
    char text[kStackWalkMaxTextLen];
  };

  static int StackWalk(StackFrame* frames, int frames_size);

  // Factory method for creating platform dependent Mutex.
  // Please use delete to reclaim the storage for the returned Mutex.
  static Mutex* CreateMutex();

  // Factory method for creating platform dependent Semaphore.
  // Please use delete to reclaim the storage for the returned Semaphore.
  static Semaphore* CreateSemaphore(int count);

  class MemoryMappedFile {
   public:
    static MemoryMappedFile* create(const char* name, int size, void* initial);
    virtual ~MemoryMappedFile() { }
    virtual void* memory() = 0;
  };

  // Safe formatting print. Ensures that str is always null-terminated.
  // Returns the number of chars written, or -1 if output was truncated.
  static int SNPrintF(char* str, size_t size, const char* format, ...);
  static int VSNPrintF(char* str,
                       size_t size,
                       const char* format,
                       va_list args);

  // Support for profiler.  Can do nothing, in which case ticks
  // occuring in shared libraries will not be properly accounted
  // for.
  static void LogSharedLibraryAddresses();

  // Returns the double constant NAN
  static double nan_value();

 private:
  static const int msPerSecond = 1000;

  DISALLOW_IMPLICIT_CONSTRUCTORS(OS);
};


class VirtualMemory {
 public:
  // Reserves virtual memory with size.
  VirtualMemory(size_t size, void* address_hint = 0);
  ~VirtualMemory();

  // Returns whether the memory has been reserved.
  bool IsReserved();

  // Returns the start address of the reserved memory.
  void* address() {
    ASSERT(IsReserved());
    return address_;
  };

  // Returns the size of the reserved memory.
  size_t size() { return size_; }

  // Commits real memory. Returns whether the operation succeeded.
  bool Commit(void* address, size_t size, bool executable);

  // Uncommit real memory.  Returns whether the operation succeeded.
  bool Uncommit(void* address, size_t size);

 private:
  void* address_;  // Start address of the virtual memory.
  size_t size_;  // Size of the virtual memory.
};


// ----------------------------------------------------------------------------
// ThreadHandle
//
// A ThreadHandle represents a thread identifier for a thread. The ThreadHandle
// does not own the underlying os handle. Thread handles can be used for
// refering to threads and testing equality.

class ThreadHandle {
 public:
  enum Kind { SELF, INVALID };
  explicit ThreadHandle(Kind kind);

  // Destructor.
  ~ThreadHandle();

  // Test for thread running.
  bool IsSelf() const;

  // Test for valid thread handle.
  bool IsValid() const;

  // Get platform-specific data.
  class PlatformData;
  PlatformData* thread_handle_data() { return data_; }

  // Initialize the handle to kind
  void Initialize(Kind kind);

 private:
  PlatformData* data_;  // Captures platform dependent data.
};


// ----------------------------------------------------------------------------
// Thread
//
// Thread objects are used for creating and running threads. When the start()
// method is called the new thread starts running the run() method in the new
// thread. The Thread object should not be deallocated before the thread has
// terminated.

class Thread: public ThreadHandle {
 public:
  // Opaque data type for thread-local storage keys.
  enum LocalStorageKey {};

  // Create new thread.
  Thread();
  virtual ~Thread();

  // Start new thread by calling the Run() method in the new thread.
  void Start();

  // Wait until thread terminates.
  void Join();

  // Abstract method for run handler.
  virtual void Run() = 0;

  // Thread-local storage.
  static LocalStorageKey CreateThreadLocalKey();
  static void DeleteThreadLocalKey(LocalStorageKey key);
  static void* GetThreadLocal(LocalStorageKey key);
  static void SetThreadLocal(LocalStorageKey key, void* value);

  // A hint to the scheduler to let another thread run.
  static void YieldCPU();

 private:
  class PlatformData;
  PlatformData* data_;
  DISALLOW_EVIL_CONSTRUCTORS(Thread);
};


// ----------------------------------------------------------------------------
// Mutex
//
// Mutexes are used for serializing access to non-reentrant sections of code.
// The implementations of mutex should allow for nested/recursive locking.

class Mutex {
 public:
  virtual ~Mutex() {}

  // Locks the given mutex. If the mutex is currently unlocked, it becomes
  // locked and owned by the calling thread, and immediately. If the mutex
  // is already locked by another thread, suspends the calling thread until
  // the mutex is unlocked.
  virtual int Lock() = 0;

  // Unlocks the given mutex. The mutex is assumed to be locked and owned by
  // the calling thread on entrance.
  virtual int Unlock() = 0;
};


// ----------------------------------------------------------------------------
// ScopedLock
//
// Stack-allocated ScopedLocks provide block-scoped locking and unlocking
// of a mutex.
class ScopedLock {
 public:
  explicit ScopedLock(Mutex* mutex): mutex_(mutex) {
    mutex_->Lock();
  }
  ~ScopedLock() {
    mutex_->Unlock();
  }

 private:
  Mutex* mutex_;
  DISALLOW_EVIL_CONSTRUCTORS(ScopedLock);
};


// ----------------------------------------------------------------------------
// Semaphore
//
// A semaphore object is a synchronization object that maintains a count. The
// count is decremented each time a thread completes a wait for the semaphore
// object and incremented each time a thread signals the semaphore. When the
// count reaches zero,  threads waiting for the semaphore blocks until the
// count becomes non-zero.

class Semaphore {
 public:
  virtual ~Semaphore() {}

  // Suspends the calling thread until the counter is non zero
  // and then decrements the semaphore counter.
  virtual void Wait() = 0;

  // Increments the semaphore counter.
  virtual void Signal() = 0;
};


#ifdef ENABLE_LOGGING_AND_PROFILING
// ----------------------------------------------------------------------------
// ProfileSampler
//
// A profile sampler periodically samples the program counter and stack pointer
// for the thread that created it.

// TickSample captures the information collected
// for each profiling sample.
struct TickSample {
  unsigned int pc;  // Instruction pointer.
  unsigned int sp;  // Stack pointer.
  StateTag state;    // The state of the VM.
};

class ProfileSampler {
 public:
  // Initialize sampler.
  explicit ProfileSampler(int interval);
  virtual ~ProfileSampler();

  // This method is called for each sampling period with the current program
  // counter.
  virtual void Tick(TickSample* sample) = 0;

  // Start and stop sampler.
  void Start();
  void Stop();

  class PlatformData;
 protected:
  inline bool IsActive() { return active_; }

 private:
  int interval_;
  bool active_;
  PlatformData* data_;  // Platform specific data.
  DISALLOW_IMPLICIT_CONSTRUCTORS(ProfileSampler);
};

#endif  // ENABLE_LOGGING_AND_PROFILING

} }  // namespace v8::internal

#endif  // V8_PLATFORM_H_
