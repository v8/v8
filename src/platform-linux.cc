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

// Platform specific code for Linux goes here

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdlib.h>

// Ubuntu Dapper requires memory pages to be marked as
// executable. Otherwise, OS raises an exception when executing code
// in that page.
#include <sys/types.h>  // mmap & munmap
#include <sys/mman.h>  // mmap & munmap
#include <sys/stat.h>  // open
#include <sys/fcntl.h>  // open
#include <unistd.h>  // getpagesize
#include <execinfo.h>  // backtrace, backtrace_symbols
#include <errno.h>
#include <stdarg.h>

#undef MAP_TYPE

#include "v8.h"

#include "platform.h"


namespace v8 { namespace internal {

// 0 is never a valid thread id on Linux since tids and pids share a
// name space and pid 0 is reserved (see man 2 kill).
static const pthread_t kNoThread = (pthread_t) 0;


double ceiling(double x) {
  return ceil(x);
}


void OS::Setup() {
  // Seed the random number generator.
  srandom(static_cast<unsigned int>(TimeCurrentMillis()));
}


int OS::GetUserTime(uint32_t* secs,  uint32_t* usecs) {
  struct rusage usage;

  if (getrusage(RUSAGE_SELF, &usage) < 0) return -1;
  *secs = usage.ru_utime.tv_sec;
  *usecs = usage.ru_utime.tv_usec;
  return 0;
}


double OS::TimeCurrentMillis() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) < 0) return 0.0;
  return (static_cast<double>(tv.tv_sec) * 1000) +
         (static_cast<double>(tv.tv_usec) / 1000);
}


int64_t OS::Ticks() {
  // Linux's gettimeofday has microsecond resolution.
  struct timeval tv;
  if (gettimeofday(&tv, NULL) < 0)
    return 0;
  return (static_cast<int64_t>(tv.tv_sec) * 1000000) + tv.tv_usec;
}


char* OS::LocalTimezone(double time) {
  time_t tv = static_cast<time_t>(floor(time/msPerSecond));
  struct tm* t = localtime(&tv);
  return const_cast<char*>(t->tm_zone);
}


double OS::DaylightSavingsOffset(double time) {
  time_t tv = static_cast<time_t>(floor(time/msPerSecond));
  struct tm* t = localtime(&tv);
  return t->tm_isdst > 0 ? 3600 * msPerSecond : 0;
}


double OS::LocalTimeOffset() {
  time_t tv = time(NULL);
  struct tm* t = localtime(&tv);
  // tm_gmtoff includes any daylight savings offset, so subtract it.
  return static_cast<double>(t->tm_gmtoff * msPerSecond -
                             (t->tm_isdst > 0 ? 3600 * msPerSecond : 0));
}


void OS::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrint(format, args);
  va_end(args);
}


void OS::VPrint(const char* format, va_list args) {
  vprintf(format, args);
}


void OS::PrintError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrintError(format, args);
  va_end(args);
}


void OS::VPrintError(const char* format, va_list args) {
  vfprintf(stderr, format, args);
}


int OS::SNPrintF(char* str, size_t size, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int result = VSNPrintF(str, size, format, args);
  va_end(args);
  return result;
}


int OS::VSNPrintF(char* str, size_t size, const char* format, va_list args) {
  int n = vsnprintf(str, size, format, args);  // forward to linux.
  if (n < 0 || static_cast<size_t>(n) >= size) {
    str[size - 1] = '\0';
    return -1;
  } else {
    return n;
  }
}


double OS::nan_value() { return NAN; }

// We keep the lowest and highest addresses mapped as a quick way of
// determining that pointers are outside the heap (used mostly in assertions
// and verification).  The estimate is conservative, ie, not all addresses in
// 'allocated' space are actually allocated to our heap.  The range is
// [lowest, highest), inclusive on the low and and exclusive on the high end.
static void* lowest_ever_allocated = reinterpret_cast<void*>(-1);
static void* highest_ever_allocated = reinterpret_cast<void*>(0);


static void UpdateAllocatedSpaceLimits(void* address, int size) {
  lowest_ever_allocated = Min(lowest_ever_allocated, address);
  highest_ever_allocated =
      Max(highest_ever_allocated,
          reinterpret_cast<void*>(reinterpret_cast<char*>(address) + size));
}


bool OS::IsOutsideAllocatedSpace(void* address) {
  return address < lowest_ever_allocated || address >= highest_ever_allocated;
}


size_t OS::AllocateAlignment() {
  return getpagesize();
}


void* OS::Allocate(const size_t requested,
                   size_t* allocated,
                   bool executable) {
  const size_t msize = RoundUp(requested, getpagesize());
  int prot = PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0);
  void* mbase = mmap(NULL, msize, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mbase == MAP_FAILED) {
    LOG(StringEvent("OS::Allocate", "mmap failed"));
    return NULL;
  }
  *allocated = msize;
  UpdateAllocatedSpaceLimits(mbase, msize);
  return mbase;
}


void OS::Free(void* buf, const size_t length) {
  // TODO(1240712): munmap has a return value which is ignored here.
  munmap(buf, length);
}


void OS::Sleep(int milliseconds) {
  unsigned int ms = static_cast<unsigned int>(milliseconds);
  usleep(1000 * ms);
}


void OS::Abort() {
  // Redirect to std abort to signal abnormal program termination.
  abort();
}


void OS::DebugBreak() {
#if defined (__arm__) || defined(__thumb__)
  asm("bkpt 0");
#else
  asm("int $3");
#endif
}


class PosixMemoryMappedFile : public OS::MemoryMappedFile {
 public:
  PosixMemoryMappedFile(FILE* file, void* memory, int size)
    : file_(file), memory_(memory), size_(size) { }
  virtual ~PosixMemoryMappedFile();
  virtual void* memory() { return memory_; }
 private:
  FILE* file_;
  void* memory_;
  int size_;
};


OS::MemoryMappedFile* OS::MemoryMappedFile::create(const char* name, int size,
    void* initial) {
  FILE* file = fopen(name, "w+");
  if (file == NULL) return NULL;
  fwrite(initial, size, 1, file);
  void* memory =
      mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(file), 0);
  return new PosixMemoryMappedFile(file, memory, size);
}


PosixMemoryMappedFile::~PosixMemoryMappedFile() {
  if (memory_) munmap(memory_, size_);
  fclose(file_);
}

#ifdef ENABLE_LOGGING_AND_PROFILING
static unsigned  StringToLongLong(char* buffer) {
  return static_cast<unsigned>(strtoll(buffer, NULL, 16));
}

#endif

void OS::LogSharedLibraryAddresses() {
#ifdef ENABLE_LOGGING_AND_PROFILING
  static const int MAP_LENGTH = 1024;
  int fd = open("/proc/self/maps", O_RDONLY);
  if (fd < 0) return;
  while (true) {
    char addr_buffer[11];
    addr_buffer[0] = '0';
    addr_buffer[1] = 'x';
    addr_buffer[10] = 0;
    read(fd, addr_buffer + 2, 8);
    unsigned start = StringToLongLong(addr_buffer);
    read(fd, addr_buffer + 2, 1);
    if (addr_buffer[2] != '-') return;
    read(fd, addr_buffer + 2, 8);
    unsigned end = StringToLongLong(addr_buffer);
    char buffer[MAP_LENGTH];
    int bytes_read = -1;
    do {
      bytes_read++;
      if (bytes_read > MAP_LENGTH - 1)
        break;
      int result = read(fd, buffer + bytes_read, 1);
      // A read error means that -1 is returned.
      if (result < 1) return;
    } while (buffer[bytes_read] != '\n');
    buffer[bytes_read] = 0;
    // There are 56 chars to ignore at this point in the line.
    if (bytes_read < 56) continue;
    // Ignore mappings that are not executable.
    if (buffer[3] != 'x') continue;
    buffer[bytes_read] = 0;
    LOG(SharedLibraryEvent(buffer + 56, start, end));
  }
#endif
}


int OS::StackWalk(OS::StackFrame* frames, int frames_size) {
  void** addresses = NewArray<void*>(frames_size);

  int frames_count = backtrace(addresses, frames_size);

  char** symbols;
  symbols = backtrace_symbols(addresses, frames_count);
  if (symbols == NULL) {
    DeleteArray(addresses);
    return kStackWalkError;
  }

  for (int i = 0; i < frames_count; i++) {
    frames[i].address = addresses[i];
    // Format a text representation of the frame based on the information
    // available.
    SNPrintF(frames[i].text, kStackWalkMaxTextLen, "%s", symbols[i]);
    // Make sure line termination is in place.
    frames[i].text[kStackWalkMaxTextLen - 1] = '\0';
  }

  DeleteArray(addresses);
  free(symbols);

  return frames_count;
}


// Constants used for mmap.
static const int kMmapFd = -1;
static const int kMmapFdOffset = 0;


VirtualMemory::VirtualMemory(size_t size, void* address_hint) {
  address_ = mmap(address_hint, size, PROT_NONE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                  kMmapFd, kMmapFdOffset);
  size_ = size;
}


VirtualMemory::~VirtualMemory() {
  if (IsReserved()) {
    if (0 == munmap(address(), size())) address_ = MAP_FAILED;
  }
}


bool VirtualMemory::IsReserved() {
  return address_ != MAP_FAILED;
}


bool VirtualMemory::Commit(void* address, size_t size, bool executable) {
  int prot = PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0);
  if (MAP_FAILED == mmap(address, size, prot,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                         kMmapFd, kMmapFdOffset)) {
    return false;
  }

  UpdateAllocatedSpaceLimits(address, size);
  return true;
}


bool VirtualMemory::Uncommit(void* address, size_t size) {
  return mmap(address, size, PROT_NONE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
              kMmapFd, kMmapFdOffset) != MAP_FAILED;
}


class ThreadHandle::PlatformData : public Malloced {
 public:
  explicit PlatformData(ThreadHandle::Kind kind) {
    Initialize(kind);
  }

  void Initialize(ThreadHandle::Kind kind) {
    switch (kind) {
      case ThreadHandle::SELF: thread_ = pthread_self(); break;
      case ThreadHandle::INVALID: thread_ = kNoThread; break;
    }
  }
  pthread_t thread_;  // Thread handle for pthread.
};


ThreadHandle::ThreadHandle(Kind kind) {
  data_ = new PlatformData(kind);
}


void ThreadHandle::Initialize(ThreadHandle::Kind kind) {
  data_->Initialize(kind);
}


ThreadHandle::~ThreadHandle() {
  delete data_;
}


bool ThreadHandle::IsSelf() const {
  return pthread_equal(data_->thread_, pthread_self());
}


bool ThreadHandle::IsValid() const {
  return data_->thread_ != kNoThread;
}


Thread::Thread() : ThreadHandle(ThreadHandle::INVALID) {
}


Thread::~Thread() {
}


static void* ThreadEntry(void* arg) {
  Thread* thread = reinterpret_cast<Thread*>(arg);
  // This is also initialized by the first argument to pthread_create() but we
  // don't know which thread will run first (the original thread or the new
  // one) so we initialize it here too.
  thread->thread_handle_data()->thread_ = pthread_self();
  ASSERT(thread->IsValid());
  thread->Run();
  return NULL;
}


void Thread::Start() {
  pthread_create(&thread_handle_data()->thread_, NULL, ThreadEntry, this);
  ASSERT(IsValid());
}


void Thread::Join() {
  pthread_join(thread_handle_data()->thread_, NULL);
}


Thread::LocalStorageKey Thread::CreateThreadLocalKey() {
  pthread_key_t key;
  int result = pthread_key_create(&key, NULL);
  USE(result);
  ASSERT(result == 0);
  return static_cast<LocalStorageKey>(key);
}


void Thread::DeleteThreadLocalKey(LocalStorageKey key) {
  pthread_key_t pthread_key = static_cast<pthread_key_t>(key);
  int result = pthread_key_delete(pthread_key);
  USE(result);
  ASSERT(result == 0);
}


void* Thread::GetThreadLocal(LocalStorageKey key) {
  pthread_key_t pthread_key = static_cast<pthread_key_t>(key);
  return pthread_getspecific(pthread_key);
}


void Thread::SetThreadLocal(LocalStorageKey key, void* value) {
  pthread_key_t pthread_key = static_cast<pthread_key_t>(key);
  pthread_setspecific(pthread_key, value);
}


void Thread::YieldCPU() {
  sched_yield();
}


class LinuxMutex : public Mutex {
 public:

  LinuxMutex() {
    pthread_mutexattr_t attrs;
    int result = pthread_mutexattr_init(&attrs);
    ASSERT(result == 0);
    result = pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_RECURSIVE);
    ASSERT(result == 0);
    result = pthread_mutex_init(&mutex_, &attrs);
    ASSERT(result == 0);
  }

  virtual ~LinuxMutex() { pthread_mutex_destroy(&mutex_); }

  virtual int Lock() {
    int result = pthread_mutex_lock(&mutex_);
    return result;
  }

  virtual int Unlock() {
    int result = pthread_mutex_unlock(&mutex_);
    return result;
  }

 private:
  pthread_mutex_t mutex_;   // Pthread mutex for POSIX platforms.
};


Mutex* OS::CreateMutex() {
  return new LinuxMutex();
}


class LinuxSemaphore : public Semaphore {
 public:
  explicit LinuxSemaphore(int count) {  sem_init(&sem_, 0, count); }
  virtual ~LinuxSemaphore() { sem_destroy(&sem_); }

  virtual void Wait();
  virtual void Signal() { sem_post(&sem_); }
 private:
  sem_t sem_;
};

void LinuxSemaphore::Wait() {
  while (true) {
    int result = sem_wait(&sem_);
    if (result == 0) return;  // Successfully got semaphore.
    CHECK(result == -1 && errno == EINTR);  // Signal caused spurious wakeup.
  }
}

Semaphore* OS::CreateSemaphore(int count) {
  return new LinuxSemaphore(count);
}

#ifdef ENABLE_LOGGING_AND_PROFILING

static Sampler* active_sampler_ = NULL;

static void ProfilerSignalHandler(int signal, siginfo_t* info, void* context) {
  USE(info);
  if (signal != SIGPROF) return;
  if (active_sampler_ == NULL) return;

  TickSample sample;

  // If profiling, we extract the current pc and sp.
  if (active_sampler_->IsProfiling()) {
    // Extracting the sample from the context is extremely machine dependent.
    ucontext_t* ucontext = reinterpret_cast<ucontext_t*>(context);
    mcontext_t& mcontext = ucontext->uc_mcontext;
#if defined (__arm__) || defined(__thumb__)
    sample.pc = mcontext.gregs[R15];
    sample.sp = mcontext.gregs[R13];
#else
    sample.pc = mcontext.gregs[REG_EIP];
    sample.sp = mcontext.gregs[REG_ESP];
#endif
  }

  // We always sample the VM state.
  sample.state = Logger::state();

  active_sampler_->Tick(&sample);
}


class Sampler::PlatformData : public Malloced {
 public:
  PlatformData() {
    signal_handler_installed_ = false;
  }

  bool signal_handler_installed_;
  struct sigaction old_signal_handler_;
  struct itimerval old_timer_value_;
};


Sampler::Sampler(int interval, bool profiling)
    : interval_(interval), profiling_(profiling), active_(false) {
  data_ = new PlatformData();
}


Sampler::~Sampler() {
  delete data_;
}


void Sampler::Start() {
  // There can only be one active sampler at the time on POSIX
  // platforms.
  if (active_sampler_ != NULL) return;

  // Request profiling signals.
  struct sigaction sa;
  sa.sa_sigaction = ProfilerSignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO;
  if (sigaction(SIGPROF, &sa, &data_->old_signal_handler_) != 0) return;
  data_->signal_handler_installed_ = true;

  // Set the itimer to generate a tick for each interval.
  itimerval itimer;
  itimer.it_interval.tv_sec = interval_ / 1000;
  itimer.it_interval.tv_usec = (interval_ % 1000) * 1000;
  itimer.it_value.tv_sec = itimer.it_interval.tv_sec;
  itimer.it_value.tv_usec = itimer.it_interval.tv_usec;
  setitimer(ITIMER_PROF, &itimer, &data_->old_timer_value_);

  // Set this sampler as the active sampler.
  active_sampler_ = this;
  active_ = true;
}


void Sampler::Stop() {
  // Restore old signal handler
  if (data_->signal_handler_installed_) {
    setitimer(ITIMER_PROF, &data_->old_timer_value_, NULL);
    sigaction(SIGPROF, &data_->old_signal_handler_, 0);
    data_->signal_handler_installed_ = false;
  }

  // This sampler is no longer the active sampler.
  active_sampler_ = NULL;
  active_ = false;
}

#endif  // ENABLE_LOGGING_AND_PROFILING

} }  // namespace v8::internal
