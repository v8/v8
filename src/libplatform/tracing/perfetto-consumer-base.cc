// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/tracing/perfetto-consumer-base.h"

#include "perfetto/trace/chrome/chrome_trace_packet.pb.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "src/base/macros.h"
#include "src/base/platform/semaphore.h"

namespace v8 {
namespace platform {
namespace tracing {

PerfettoConsumerBase::PerfettoConsumerBase(base::Semaphore* finished)
    : finished_semaphore_(finished) {}

void PerfettoConsumerBase::OnTraceData(
    std::vector<::perfetto::TracePacket> packets, bool has_more) {
  for (const ::perfetto::TracePacket& packet : packets) {
    perfetto::protos::ChromeTracePacket proto_packet;
    bool success = packet.Decode(&proto_packet);
    USE(success);
    DCHECK(success);

    ProcessPacket(proto_packet);
  }
  // PerfettoTracingController::StopTracing() waits on this sempahore. This is
  // so that we can ensure that this consumer has finished consuming all of the
  // trace events from the buffer before the buffer is destroyed.
  if (!has_more) finished_semaphore_->Signal();
}

}  // namespace tracing
}  // namespace platform
}  // namespace v8
