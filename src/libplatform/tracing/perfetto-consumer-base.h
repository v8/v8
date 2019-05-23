// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_TRACING_PERFETTO_CONSUMER_BASE_H_
#define V8_LIBPLATFORM_TRACING_PERFETTO_CONSUMER_BASE_H_

#include <memory>

#include "perfetto/tracing/core/consumer.h"
#include "perfetto/tracing/core/tracing_service.h"
#include "src/base/logging.h"

namespace perfetto {
namespace protos {
class ChromeTracePacket;
}  // namespace protos
}  // namespace perfetto

namespace v8 {

namespace base {
class Semaphore;
}

namespace platform {
namespace tracing {

// A base class for custom Consumers within V8. Implements V8-specific logic
// for interacting with the tracing controller and leaves the consumption of
// the trace events to the subclass.
// A Perfetto Consumer gets streamed trace events from the Service via
// OnTraceData(). A Consumer can be configured (via
// service_endpoint()->EnableTracing()) to listen to various different types of
// trace events. The Consumer is responsible for producing whatever tracing
// output the system should have.
class PerfettoConsumerBase : public ::perfetto::Consumer {
 public:
  using ServiceEndpoint = ::perfetto::TracingService::ConsumerEndpoint;

  ServiceEndpoint* service_endpoint() const { return service_endpoint_.get(); }
  void set_service_endpoint(std::unique_ptr<ServiceEndpoint> endpoint) {
    service_endpoint_ = std::move(endpoint);
  }

 protected:
  explicit PerfettoConsumerBase(base::Semaphore* finished);

 private:
  // ::perfetto::Consumer implementation
  void OnConnect() override {}
  void OnDisconnect() override {}
  void OnTracingDisabled() override {}
  void OnTraceData(std::vector<::perfetto::TracePacket> packets,
                   bool has_more) final;
  void OnDetach(bool success) override {}
  void OnAttach(bool success, const ::perfetto::TraceConfig&) override {}
  void OnTraceStats(bool success, const ::perfetto::TraceStats&) override {
    UNREACHABLE();
  }
  void OnObservableEvents(const ::perfetto::ObservableEvents&) override {
    UNREACHABLE();
  }

  // Subclasses override this method to respond to trace packets.
  virtual void ProcessPacket(
      const ::perfetto::protos::ChromeTracePacket& packet) = 0;

  std::unique_ptr<ServiceEndpoint> service_endpoint_;
  base::Semaphore* finished_semaphore_;
};

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_TRACING_PERFETTO_CONSUMER_BASE_H_
