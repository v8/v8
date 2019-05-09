// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/tracing/perfetto-tracing-controller.h"

#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_writer.h"
#include "perfetto/tracing/core/tracing_service.h"
#include "src/libplatform/tracing/perfetto-json-consumer.h"
#include "src/libplatform/tracing/perfetto-producer.h"
#include "src/libplatform/tracing/perfetto-shared-memory.h"
#include "src/libplatform/tracing/perfetto-tasks.h"

namespace v8 {
namespace platform {
namespace tracing {

PerfettoTracingController::PerfettoTracingController()
    : writer_key_(base::Thread::CreateThreadLocalKey()),
      producer_ready_semaphore_(0),
      consumer_finished_semaphore_(0) {}

void PerfettoTracingController::StartTracing(
    const ::perfetto::TraceConfig& trace_config) {
  DCHECK(!trace_file_.is_open());
  trace_file_.open("v8_perfetto_trace.json");
  CHECK(trace_file_.good());

  DCHECK(!task_runner_);
  task_runner_ = base::make_unique<PerfettoTaskRunner>();
  // The Perfetto service expects calls on the task runner thread which is why
  // the setup below occurs in posted tasks.
  task_runner_->PostTask([&trace_config, this] {
    std::unique_ptr<::perfetto::SharedMemory::Factory> shmem_factory =
        base::make_unique<PerfettoSharedMemoryFactory>();

    service_ = ::perfetto::TracingService::CreateInstance(
        std::move(shmem_factory), task_runner_.get());
    producer_ = base::make_unique<PerfettoProducer>(this);
    consumer_ = base::make_unique<PerfettoJSONConsumer>(
        &trace_file_, &consumer_finished_semaphore_);

    producer_->set_service_endpoint(service_->ConnectProducer(
        producer_.get(), 0, "v8.perfetto-producer", 0, true));

    consumer_->set_service_endpoint(
        service_->ConnectConsumer(consumer_.get(), 0));

    // We need to wait for the OnConnected() callbacks of the producer and
    // consumer to be called.
    consumer_->service_endpoint()->EnableTracing(trace_config);
  });

  producer_ready_semaphore_.Wait();
}

void PerfettoTracingController::StopTracing() {
  // Finish all of the tasks such as existing AddTraceEvent calls. These
  // require the data structures below to work properly, so keep them alive
  // until the tasks are done.
  task_runner_->FinishImmediateTasks();

  task_runner_->PostTask([this] {
    // Causes each thread-local writer to be deleted which will trigger a
    // final Flush() on each writer as well.
    // TODO(petermarshall): There as a race here where the writer is still being
    // used by a thread writing trace events (the thread read the value of
    // perfetto_recording_ before it was changed). We either need to synchronize
    // all tracing threads here or use TLS destructors like Chrome.
    writers_to_finalize_.clear();

    // Trigger the consumer to finish. This can trigger multiple calls to
    // PerfettoJSONConsumer::OnTraceData(), with the final call passing has_more
    // as false.
    consumer_->service_endpoint()->ReadBuffers();
  });

  // Wait until the final OnTraceData() call with has_more=false has completed.
  consumer_finished_semaphore_.Wait();

  task_runner_->PostTask([this] {
    consumer_.reset();
    producer_.reset();
    service_.reset();
  });

  // Finish the above task, and any callbacks that were triggered.
  task_runner_->FinishImmediateTasks();
  task_runner_.reset();

  DCHECK(trace_file_.is_open());
  trace_file_.close();
}

PerfettoTracingController::~PerfettoTracingController() {
  base::Thread::DeleteThreadLocalKey(writer_key_);
}

::perfetto::TraceWriter*
PerfettoTracingController::GetOrCreateThreadLocalWriter() {
  if (base::Thread::HasThreadLocal(writer_key_)) {
    return static_cast<::perfetto::TraceWriter*>(
        base::Thread::GetExistingThreadLocal(writer_key_));
  }

  std::unique_ptr<::perfetto::TraceWriter> tw = producer_->CreateTraceWriter();

  ::perfetto::TraceWriter* writer = tw.get();
  // We don't have thread-local storage destructors but we need to delete each
  // thread local TraceWriter, so that they can release the trace buffer chunks
  // they are holding on to. To do this, we keep a vector of all writers that we
  // create so they can be deleted when tracing is stopped.
  {
    base::MutexGuard guard(&writers_mutex_);
    writers_to_finalize_.push_back(std::move(tw));
  }

  base::Thread::SetThreadLocal(writer_key_, writer);
  return writer;
}

void PerfettoTracingController::OnProducerReady() {
  producer_ready_semaphore_.Signal();
}

}  // namespace tracing
}  // namespace platform
}  // namespace v8
