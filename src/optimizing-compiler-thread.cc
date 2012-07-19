// Copyright 2012 the V8 project authors. All rights reserved.
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

#include "optimizing-compiler-thread.h"

#include "v8.h"

#include "hydrogen.h"
#include "isolate.h"
#include "v8threads.h"

namespace v8 {
namespace internal {


void OptimizingCompilerThread::Run() {
#ifdef DEBUG
  thread_id_ = ThreadId::Current().ToInteger();
#endif
  Isolate::SetIsolateThreadLocals(isolate_, NULL);

  while (true) {
    input_queue_semaphore_->Wait();
    if (Acquire_Load(&stop_thread_)) {
      stop_semaphore_->Signal();
      return;
    }

    Heap::RelocationLock relocation_lock(isolate_->heap());
    OptimizingCompiler* optimizing_compiler = NULL;
    input_queue_.Dequeue(&optimizing_compiler);
    Barrier_AtomicIncrement(&queue_length_, static_cast<Atomic32>(-1));

    ASSERT(!optimizing_compiler->info()->closure()->IsOptimized());

    OptimizingCompiler::Status status = optimizing_compiler->OptimizeGraph();
    ASSERT(status != OptimizingCompiler::FAILED);
    // Prevent an unused-variable error in release mode.
    (void) status;

    output_queue_.Enqueue(optimizing_compiler);
    isolate_->stack_guard()->RequestCodeReadyEvent();
  }
}


void OptimizingCompilerThread::Stop() {
  Release_Store(&stop_thread_, static_cast<AtomicWord>(true));
  input_queue_semaphore_->Signal();
  stop_semaphore_->Wait();
}


void OptimizingCompilerThread::InstallOptimizedFunctions() {
  HandleScope handle_scope(isolate_);
  int functions_installed = 0;
  while (!output_queue_.IsEmpty()) {
    OptimizingCompiler* compiler = NULL;
    output_queue_.Dequeue(&compiler);
    Compiler::InstallOptimizedCode(compiler);
    functions_installed++;
  }
  if (FLAG_trace_parallel_recompilation && functions_installed != 0) {
    PrintF("  ** Installed %d function(s).\n", functions_installed);
  }
}


void OptimizingCompilerThread::QueueForOptimization(
    OptimizingCompiler* optimizing_compiler) {
  input_queue_.Enqueue(optimizing_compiler);
  input_queue_semaphore_->Signal();
}

#ifdef DEBUG
bool OptimizingCompilerThread::IsOptimizerThread() {
  if (!FLAG_parallel_recompilation) return false;
  return ThreadId::Current().ToInteger() == thread_id_;
}
#endif


} }  // namespace v8::internal
