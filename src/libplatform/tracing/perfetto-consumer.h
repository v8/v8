// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_TRACING_PERFETTO_CONSUMER_H_
#define V8_LIBPLATFORM_TRACING_PERFETTO_CONSUMER_H_

#include <memory>

#include "perfetto/tracing/core/consumer.h"
#include "perfetto/tracing/core/tracing_service.h"
#include "src/base/logging.h"

namespace v8 {
namespace platform {
namespace tracing {

// A dummy Consumer that does nothing because we write directly to a file using
// the Service. This will be replaced later with a JSON consumer that writes
// JSON to a stream, but we need a stand-in for now.

// A Perfetto Consumer gets streamed trace events from the Service via
// OnTraceData(). A Consumer can be configured (via
// service_endpoint()->EnableTracing()) to listen to various different types of
// trace events. The Consumer is responsible for producing whatever tracing
// output the system should have - e.g. converting to JSON and writing it to a
// file.
class PerfettoConsumer final : public ::perfetto::Consumer {
 public:
  using ServiceEndpoint = ::perfetto::TracingService::ConsumerEndpoint;

  ServiceEndpoint* service_endpoint() const { return service_endpoint_.get(); }
  void set_service_endpoint(std::unique_ptr<ServiceEndpoint> endpoint) {
    service_endpoint_ = std::move(endpoint);
  }

 private:
  // ::perfetto::Consumer implementation
  void OnConnect() override {}
  void OnDisconnect() override {}
  void OnTracingDisabled() override {}
  // Note: this callback will never be seen because in EnableTracing we set
  // write_into_file=true. That flag essentially tells the service to directly
  // write into the passed file descriptor, instead of returning the trace
  // contents via IPC (which is what this method does).
  void OnTraceData(std::vector<::perfetto::TracePacket> packets,
                   bool has_more) override {
    UNREACHABLE();
  }
  void OnDetach(bool success) override {}
  void OnAttach(bool success, const ::perfetto::TraceConfig&) override {}
  void OnTraceStats(bool success, const ::perfetto::TraceStats&) override {
    UNREACHABLE();
  }
  void OnObservableEvents(const ::perfetto::ObservableEvents&) override {
    UNREACHABLE();
  }

  std::unique_ptr<ServiceEndpoint> service_endpoint_;
};

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_TRACING_PERFETTO_CONSUMER_H_
