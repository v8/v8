// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_TRACING_PERFETTO_JSON_CONSUMER_H_
#define V8_LIBPLATFORM_TRACING_PERFETTO_JSON_CONSUMER_H_

#include <ostream>

#include "perfetto/tracing/core/consumer.h"
#include "perfetto/tracing/core/tracing_service.h"
#include "src/base/logging.h"

namespace perfetto {
class TraceConfig;
class TracePacket;

namespace protos {
class ChromeTraceEvent_Arg;
class TracePacket;
}  // namespace protos
}  // namespace perfetto

namespace v8 {

namespace base {
class Semaphore;
}

namespace platform {
namespace tracing {

// A Perfetto Consumer gets streamed trace events from the Service via
// OnTraceData(). A Consumer can be configured (via
// service_endpoint()->EnableTracing()) to listen to various different types of
// trace events. The Consumer is responsible for producing whatever tracing
// output the system should have - in this case, converting the proto trace data
// delivered via OnTraceData() to JSON and writing it to a file.
class PerfettoJSONConsumer final : public ::perfetto::Consumer {
 public:
  explicit PerfettoJSONConsumer(std::ostream* stream,
                                base::Semaphore* finished);
  ~PerfettoJSONConsumer() override;

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

  void OnTraceData(std::vector<::perfetto::TracePacket> packets,
                   bool has_more) override;

  void OnDetach(bool success) override {}
  void OnAttach(bool success, const ::perfetto::TraceConfig&) override {}
  void OnTraceStats(bool success, const ::perfetto::TraceStats&) override {
    UNREACHABLE();
  }
  void OnObservableEvents(const ::perfetto::ObservableEvents&) override {
    UNREACHABLE();
  }

  // Internal implementation
  void AppendJSONString(const char* str);
  void AppendArgValue(const ::perfetto::protos::ChromeTraceEvent_Arg& arg);
  void ProcessPacket(const ::perfetto::protos::TracePacket& packet);

  std::ostream* stream_;
  bool append_comma_ = false;
  std::unique_ptr<ServiceEndpoint> service_endpoint_;

  base::Semaphore* finished_semaphore_;
};

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_TRACING_PERFETTO_JSON_CONSUMER_H_
