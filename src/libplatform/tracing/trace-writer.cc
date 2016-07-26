// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/tracing/trace-writer.h"

#include "src/base/platform/platform.h"

namespace v8 {
namespace platform {
namespace tracing {

JSONTraceWriter::JSONTraceWriter(std::ostream& stream) : stream_(stream) {
  stream_ << "{\"traceEvents\":[";
}

JSONTraceWriter::~JSONTraceWriter() { stream_ << "]}"; }

void JSONTraceWriter::AppendTraceEvent(TraceObject* trace_event) {
  if (append_comma_) stream_ << ",";
  append_comma_ = true;
  if (trace_event->scope() == NULL) {
    stream_ << "{\"pid\":" << trace_event->pid()
            << ",\"tid\":" << trace_event->tid()
            << ",\"ts\":" << trace_event->ts()
            << ",\"tts\":" << trace_event->tts() << ",\"ph\":\""
            << trace_event->phase() << "\",\"cat\":\""
            << TracingController::GetCategoryGroupName(
                   trace_event->category_enabled_flag())
            << "\",\"name\":\"" << trace_event->name()
            << "\",\"args\":{},\"dur\":" << trace_event->duration()
            << ",\"tdur\":" << trace_event->cpu_duration() << "}";
  } else {
    stream_ << "{\"pid\":" << trace_event->pid()
            << ",\"tid\":" << trace_event->tid()
            << ",\"ts\":" << trace_event->ts()
            << ",\"tts\":" << trace_event->tts() << ",\"ph\":\""
            << trace_event->phase() << "\",\"cat\":\""
            << TracingController::GetCategoryGroupName(
                   trace_event->category_enabled_flag())
            << "\",\"name\":\"" << trace_event->name() << "\",\"scope\":\""
            << trace_event->scope()
            << "\",\"args\":{},\"dur\":" << trace_event->duration()
            << ",\"tdur\":" << trace_event->cpu_duration() << "}";
  }
  // TODO(fmeawad): Add support for Flow Events.
}

void JSONTraceWriter::Flush() {}

TraceWriter* TraceWriter::CreateJSONTraceWriter(std::ostream& stream) {
  return new JSONTraceWriter(stream);
}

}  // namespace tracing
}  // namespace platform
}  // namespace v8
