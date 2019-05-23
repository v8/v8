// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_TRACING_PERFETTO_JSON_CONSUMER_H_
#define V8_LIBPLATFORM_TRACING_PERFETTO_JSON_CONSUMER_H_

#include <ostream>

#include "src/libplatform/tracing/perfetto-consumer-base.h"

namespace perfetto {
namespace protos {
class ChromeTraceEvent_Arg;
class ChromeTracePacket;
}  // namespace protos
}  // namespace perfetto

namespace v8 {
namespace platform {
namespace tracing {

// A consumer that converts the proto trace data to JSON and writes it to a
// file.
class PerfettoJSONConsumer final : public PerfettoConsumerBase {
 public:
  PerfettoJSONConsumer(base::Semaphore* finished, std::ostream* stream);
  ~PerfettoJSONConsumer() override;

 private:
  void ProcessPacket(
      const ::perfetto::protos::ChromeTracePacket& packet) override;

  // Internal implementation
  void AppendJSONString(const char* str);
  void AppendArgValue(const ::perfetto::protos::ChromeTraceEvent_Arg& arg);

  std::ostream* stream_;
  bool append_comma_ = false;
};

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_TRACING_PERFETTO_JSON_CONSUMER_H_
