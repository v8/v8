// Copyright 2013 the V8 project authors. All rights reserved.
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

#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) \
    || defined(__NetBSD__) || defined(__sun)

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#endif

// GLibc on ARM defines mcontext_t has a typedef for 'struct sigcontext'.
// Old versions of the C library <signal.h> didn't define the type.
#if defined(__ANDROID__) && !defined(__BIONIC_HAVE_UCONTEXT_T) && \
    defined(__arm__) && !defined(__BIONIC_HAVE_STRUCT_SIGCONTEXT)
#include <asm/sigcontext.h>
#endif

#include "v8.h"

#include "log.h"
#include "platform.h"
#include "simulator.h"
#include "v8threads.h"


namespace v8 {
namespace internal {


#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) \
    || defined(__NetBSD__) || defined(__sun)

#if defined(__ANDROID__) && !defined(__BIONIC_HAVE_UCONTEXT_T)

// Not all versions of Android's C library provide ucontext_t.
// Detect this and provide custom but compatible definitions. Note that these
// follow the GLibc naming convention to access register values from
// mcontext_t.
//
// See http://code.google.com/p/android/issues/detail?id=34784

#if defined(__arm__)

typedef struct sigcontext mcontext_t;

typedef struct ucontext {
  uint32_t uc_flags;
  struct ucontext* uc_link;
  stack_t uc_stack;
  mcontext_t uc_mcontext;
  // Other fields are not used by V8, don't define them here.
} ucontext_t;

#elif defined(__mips__)
// MIPS version of sigcontext, for Android bionic.
typedef struct {
  uint32_t regmask;
  uint32_t status;
  uint64_t pc;
  uint64_t gregs[32];
  uint64_t fpregs[32];
  uint32_t acx;
  uint32_t fpc_csr;
  uint32_t fpc_eir;
  uint32_t used_math;
  uint32_t dsp;
  uint64_t mdhi;
  uint64_t mdlo;
  uint32_t hi1;
  uint32_t lo1;
  uint32_t hi2;
  uint32_t lo2;
  uint32_t hi3;
  uint32_t lo3;
} mcontext_t;

typedef struct ucontext {
  uint32_t uc_flags;
  struct ucontext* uc_link;
  stack_t uc_stack;
  mcontext_t uc_mcontext;
  // Other fields are not used by V8, don't define them here.
} ucontext_t;

#elif defined(__i386__)
// x86 version for Android.
typedef struct {
  uint32_t gregs[19];
  void* fpregs;
  uint32_t oldmask;
  uint32_t cr2;
} mcontext_t;

typedef uint32_t kernel_sigset_t[2];  // x86 kernel uses 64-bit signal masks
typedef struct ucontext {
  uint32_t uc_flags;
  struct ucontext* uc_link;
  stack_t uc_stack;
  mcontext_t uc_mcontext;
  // Other fields are not used by V8, don't define them here.
} ucontext_t;
enum { REG_EBP = 6, REG_ESP = 7, REG_EIP = 14 };
#endif

#endif  // __ANDROID__ && !defined(__BIONIC_HAVE_UCONTEXT_T)


static pthread_t GetThreadID() {
#if defined(__ANDROID__)
  // Android's C library provides gettid(2).
  return gettid();
#elif defined(__linux__)
  // Glibc doesn't provide a wrapper for gettid(2).
  return syscall(SYS_gettid);
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) \
    || defined(__sun)
  return pthread_self();
#endif
}


class Sampler::PlatformData : public Malloced {
 public:
  PlatformData()
      : vm_tid_(GetThreadID()),
        profiled_thread_id_(ThreadId::Current()) {}

  pthread_t vm_tid() const { return vm_tid_; }
  ThreadId profiled_thread_id() { return profiled_thread_id_; }

 private:
  pthread_t vm_tid_;
  ThreadId profiled_thread_id_;
};


static void ProfilerSignalHandler(int signal, siginfo_t* info, void* context) {
#if defined(__native_client__)
  // As Native Client does not support signal handling, profiling
  // is disabled.
  return;
#else
  USE(info);
  if (signal != SIGPROF) return;
  Isolate* isolate = Isolate::UncheckedCurrent();
  if (isolate == NULL || !isolate->IsInitialized() || !isolate->IsInUse()) {
    // We require a fully initialized and entered isolate.
    return;
  }
  if (v8::Locker::IsActive() &&
      !isolate->thread_manager()->IsLockedByCurrentThread()) {
    return;
  }

  Sampler* sampler = isolate->logger()->sampler();
  if (sampler == NULL || !sampler->IsActive()) return;

#if defined(USE_SIMULATOR)
#if V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_MIPS
  ThreadId thread_id = sampler->platform_data()->profiled_thread_id();
  Isolate::PerIsolateThreadData* per_thread_data = isolate->
      FindPerThreadDataForThread(thread_id);
  if (!per_thread_data) return;
  Simulator* sim = per_thread_data->simulator();
  // Check if there is active simulator before allocating TickSample.
  if (!sim) return;
#endif
#endif  // USE_SIMULATOR

  TickSample sample_obj;
  TickSample* sample = isolate->cpu_profiler()->TickSampleEvent();
  if (sample == NULL) sample = &sample_obj;

#if defined(USE_SIMULATOR)
#if V8_TARGET_ARCH_ARM
  sample->pc = reinterpret_cast<Address>(sim->get_register(Simulator::pc));
  sample->sp = reinterpret_cast<Address>(sim->get_register(Simulator::sp));
  sample->fp = reinterpret_cast<Address>(sim->get_register(Simulator::r11));
#elif V8_TARGET_ARCH_MIPS
  sample->pc = reinterpret_cast<Address>(sim->get_register(Simulator::pc));
  sample->sp = reinterpret_cast<Address>(sim->get_register(Simulator::sp));
  sample->fp = reinterpret_cast<Address>(sim->get_register(Simulator::fp));
#endif  // V8_TARGET_ARCH_*
#else
  // Extracting the sample from the context is extremely machine dependent.
  ucontext_t* ucontext = reinterpret_cast<ucontext_t*>(context);
  mcontext_t& mcontext = ucontext->uc_mcontext;
  sample->state = isolate->current_vm_state();
#if defined(__linux__)
#if V8_HOST_ARCH_IA32
  sample->pc = reinterpret_cast<Address>(mcontext.gregs[REG_EIP]);
  sample->sp = reinterpret_cast<Address>(mcontext.gregs[REG_ESP]);
  sample->fp = reinterpret_cast<Address>(mcontext.gregs[REG_EBP]);
#elif V8_HOST_ARCH_X64
  sample->pc = reinterpret_cast<Address>(mcontext.gregs[REG_RIP]);
  sample->sp = reinterpret_cast<Address>(mcontext.gregs[REG_RSP]);
  sample->fp = reinterpret_cast<Address>(mcontext.gregs[REG_RBP]);
#elif V8_HOST_ARCH_ARM
#if defined(__GLIBC__) && !defined(__UCLIBC__) && \
    (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ <= 3))
  // Old GLibc ARM versions used a gregs[] array to access the register
  // values from mcontext_t.
  sample->pc = reinterpret_cast<Address>(mcontext.gregs[R15]);
  sample->sp = reinterpret_cast<Address>(mcontext.gregs[R13]);
  sample->fp = reinterpret_cast<Address>(mcontext.gregs[R11]);
#else
  sample->pc = reinterpret_cast<Address>(mcontext.arm_pc);
  sample->sp = reinterpret_cast<Address>(mcontext.arm_sp);
  sample->fp = reinterpret_cast<Address>(mcontext.arm_fp);
#endif  // defined(__GLIBC__) && !defined(__UCLIBC__) &&
        // (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ <= 3))
#elif V8_HOST_ARCH_MIPS
  sample->pc = reinterpret_cast<Address>(mcontext.pc);
  sample->sp = reinterpret_cast<Address>(mcontext.gregs[29]);
  sample->fp = reinterpret_cast<Address>(mcontext.gregs[30]);
#endif  // V8_HOST_ARCH_*
#elif defined(__FreeBSD__)
#if V8_HOST_ARCH_IA32
  sample->pc = reinterpret_cast<Address>(mcontext.mc_eip);
  sample->sp = reinterpret_cast<Address>(mcontext.mc_esp);
  sample->fp = reinterpret_cast<Address>(mcontext.mc_ebp);
#elif V8_HOST_ARCH_X64
  sample->pc = reinterpret_cast<Address>(mcontext.mc_rip);
  sample->sp = reinterpret_cast<Address>(mcontext.mc_rsp);
  sample->fp = reinterpret_cast<Address>(mcontext.mc_rbp);
#elif V8_HOST_ARCH_ARM
  sample->pc = reinterpret_cast<Address>(mcontext.mc_r15);
  sample->sp = reinterpret_cast<Address>(mcontext.mc_r13);
  sample->fp = reinterpret_cast<Address>(mcontext.mc_r11);
#endif  // V8_HOST_ARCH_*
#elif defined(__NetBSD__)
#if V8_HOST_ARCH_IA32
  sample->pc = reinterpret_cast<Address>(mcontext.__gregs[_REG_EIP]);
  sample->sp = reinterpret_cast<Address>(mcontext.__gregs[_REG_ESP]);
  sample->fp = reinterpret_cast<Address>(mcontext.__gregs[_REG_EBP]);
#elif V8_HOST_ARCH_X64
  sample->pc = reinterpret_cast<Address>(mcontext.__gregs[_REG_RIP]);
  sample->sp = reinterpret_cast<Address>(mcontext.__gregs[_REG_RSP]);
  sample->fp = reinterpret_cast<Address>(mcontext.__gregs[_REG_RBP]);
#endif  // V8_HOST_ARCH_*
#elif defined(__OpenBSD__)
  USE(mcontext);
#if V8_HOST_ARCH_IA32
  sample->pc = reinterpret_cast<Address>(ucontext->sc_eip);
  sample->sp = reinterpret_cast<Address>(ucontext->sc_esp);
  sample->fp = reinterpret_cast<Address>(ucontext->sc_ebp);
#elif V8_HOST_ARCH_X64
  sample->pc = reinterpret_cast<Address>(ucontext->sc_rip);
  sample->sp = reinterpret_cast<Address>(ucontext->sc_rsp);
  sample->fp = reinterpret_cast<Address>(ucontext->sc_rbp);
#endif  // V8_HOST_ARCH_*
#elif defined(__sun)
  sample->pc = reinterpret_cast<Address>(mcontext.gregs[REG_PC]);
  sample->sp = reinterpret_cast<Address>(mcontext.gregs[REG_SP]);
  sample->fp = reinterpret_cast<Address>(mcontext.gregs[REG_FP]);
#endif  // __sun
#endif  // USE_SIMULATOR

  sampler->SampleStack(sample);
  sampler->Tick(sample);
#endif  // __native_client__
}


class SignalSender : public Thread {
 public:
  static const int kSignalSenderStackSize = 64 * KB;

  explicit SignalSender(int interval)
      : Thread(Thread::Options("SignalSender", kSignalSenderStackSize)),
        vm_tgid_(getpid()),
        interval_(interval) {}

  static void SetUp() { if (!mutex_) mutex_ = OS::CreateMutex(); }
  static void TearDown() { delete mutex_; }

  static void InstallSignalHandler() {
    struct sigaction sa;
    sa.sa_sigaction = ProfilerSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    signal_handler_installed_ =
        (sigaction(SIGPROF, &sa, &old_signal_handler_) == 0);
  }

  static void RestoreSignalHandler() {
    if (signal_handler_installed_) {
      sigaction(SIGPROF, &old_signal_handler_, 0);
      signal_handler_installed_ = false;
    }
  }

  static void AddActiveSampler(Sampler* sampler) {
    ScopedLock lock(mutex_);
    SamplerRegistry::AddActiveSampler(sampler);
    if (instance_ == NULL) {
      // Start a thread that will send SIGPROF signal to VM threads,
      // when CPU profiling will be enabled.
      instance_ = new SignalSender(sampler->interval());
      instance_->StartSynchronously();
    } else {
      ASSERT(instance_->interval_ == sampler->interval());
    }
  }

  static void RemoveActiveSampler(Sampler* sampler) {
    ScopedLock lock(mutex_);
    SamplerRegistry::RemoveActiveSampler(sampler);
    if (SamplerRegistry::GetState() == SamplerRegistry::HAS_NO_SAMPLERS) {
      instance_->Join();
      delete instance_;
      instance_ = NULL;
      RestoreSignalHandler();
    }
  }

  // Implement Thread::Run().
  virtual void Run() {
    SamplerRegistry::State state;
    while ((state = SamplerRegistry::GetState()) !=
           SamplerRegistry::HAS_NO_SAMPLERS) {
      // When CPU profiling is enabled both JavaScript and C++ code is
      // profiled. We must not suspend.
      if (state == SamplerRegistry::HAS_CPU_PROFILING_SAMPLERS) {
        if (!signal_handler_installed_) InstallSignalHandler();
        SamplerRegistry::IterateActiveSamplers(&DoCpuProfile, this);
      } else {
        if (signal_handler_installed_) RestoreSignalHandler();
      }
      Sleep();  // TODO(svenpanne) Figure out if OS:Sleep(interval_) is enough.
    }
  }

  static void DoCpuProfile(Sampler* sampler, void* raw_sender) {
    if (!sampler->IsProfiling()) return;
    SignalSender* sender = reinterpret_cast<SignalSender*>(raw_sender);
    sender->SendProfilingSignal(sampler->platform_data()->vm_tid());
  }

  void SendProfilingSignal(int tid) {
    if (!signal_handler_installed_) return;
    // Glibc doesn't provide a wrapper for tgkill(2).
#if defined(ANDROID)
    syscall(__NR_tgkill, vm_tgid_, tid, SIGPROF);
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) \
    || defined(__sun)
    pthread_kill(tid, SIGPROF);
#else
    int result = syscall(SYS_tgkill, vm_tgid_, tid, SIGPROF);
    USE(result);
    ASSERT(result == 0);
#endif
  }

  void Sleep() {
    // Convert ms to us and subtract 100 us to compensate delays
    // occuring during signal delivery.
    useconds_t interval = interval_ * 1000 - 100;
#if defined(ANDROID)
    usleep(interval);
#else
    int result = usleep(interval);
#ifdef DEBUG
    if (result != 0 && errno != EINTR) {
      fprintf(stderr,
              "SignalSender usleep error; interval = %u, errno = %d\n",
              interval,
              errno);
      ASSERT(result == 0 || errno == EINTR);
    }
#endif  // DEBUG
    USE(result);
#endif  // ANDROID
  }

  const int vm_tgid_;
  const int interval_;

  // Protects the process wide state below.
  static Mutex* mutex_;
  static SignalSender* instance_;
  static bool signal_handler_installed_;
  static struct sigaction old_signal_handler_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SignalSender);
};


Mutex* SignalSender::mutex_ = NULL;
SignalSender* SignalSender::instance_ = NULL;
struct sigaction SignalSender::old_signal_handler_;
bool SignalSender::signal_handler_installed_ = false;


void Sampler::SetUp() {
  SignalSender::SetUp();
}


void Sampler::TearDown() {
  SignalSender::TearDown();
}


Sampler::Sampler(Isolate* isolate, int interval)
    : isolate_(isolate),
      interval_(interval),
      profiling_(false),
      active_(false),
      samples_taken_(0) {
  data_ = new PlatformData;
}


Sampler::~Sampler() {
  ASSERT(!IsActive());
  delete data_;
}

void Sampler::Start() {
  ASSERT(!IsActive());
  SetActive(true);
  SignalSender::AddActiveSampler(this);
}


void Sampler::Stop() {
  ASSERT(IsActive());
  SignalSender::RemoveActiveSampler(this);
  SetActive(false);
}

#else

void Sampler::SetUp() {
}


void Sampler::TearDown() {
}

#endif  // __linux__ || _*BSD__ || __sun

void Sampler::SampleStack(TickSample* sample) {
  StackTracer::Trace(isolate_, sample);
  if (++samples_taken_ < 0) samples_taken_ = 0;
}

} }  // namespace v8::internal
