// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stdio.h>

#include "include/libplatform/v8-tracing.h"
#include "src/tracing/trace-event.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace platform {
namespace tracing {

TEST(TestTraceConfig) {
  LocalContext env;
  TraceConfig* trace_config = new TraceConfig();
  trace_config->EnableSampling();
  trace_config->AddIncludedCategory("v8");
  trace_config->AddIncludedCategory(TRACE_DISABLED_BY_DEFAULT("v8.runtime"));
  trace_config->AddExcludedCategory("v8.cpu_profile");

  CHECK_EQ(trace_config->IsSamplingEnabled(), true);
  CHECK_EQ(trace_config->IsSystraceEnabled(), false);
  CHECK_EQ(trace_config->IsArgumentFilterEnabled(), false);
  CHECK_EQ(trace_config->IsCategoryGroupEnabled("v8"), true);
  CHECK_EQ(trace_config->IsCategoryGroupEnabled("v8.cpu_profile"), false);
  CHECK_EQ(trace_config->IsCategoryGroupEnabled("v8.cpu_profile.hires"), false);
  CHECK_EQ(trace_config->IsCategoryGroupEnabled(
               TRACE_DISABLED_BY_DEFAULT("v8.runtime")),
           true);
  delete trace_config;
}

TEST(TestTraceObject) {
  TraceObject trace_object;
  uint8_t category_enabled_flag = 41;
  trace_object.Initialize('X', &category_enabled_flag, "Test.Trace",
                          "Test.Scope", 42, 123, 0, NULL, NULL, NULL, 0);
  CHECK_EQ('X', trace_object.phase());
  CHECK_EQ(category_enabled_flag, *trace_object.category_enabled_flag());
  CHECK_EQ(std::string("Test.Trace"), std::string(trace_object.name()));
  CHECK_EQ(std::string("Test.Scope"), std::string(trace_object.scope()));
  CHECK_EQ(0, trace_object.duration());
  CHECK_EQ(0, trace_object.cpu_duration());
}

class MockTraceWriter : public TraceWriter {
 public:
  void AppendTraceEvent(TraceObject* trace_event) override {
    events_.push_back(trace_event->name());
  }

  void Flush() override {}

  std::vector<std::string> events() { return events_; }

 private:
  std::vector<std::string> events_;
};

TEST(TestTraceBufferRingBuffer) {
  // We should be able to add kChunkSize * 2 + 1 trace events.
  const int HANDLES_COUNT = TraceBufferChunk::kChunkSize * 2 + 1;
  MockTraceWriter* writer = new MockTraceWriter();
  TraceBuffer* ring_buffer =
      TraceBuffer::CreateTraceBufferRingBuffer(2, writer);
  std::string names[HANDLES_COUNT];
  for (int i = 0; i < HANDLES_COUNT; ++i) {
    names[i] = "Test.EventNo" + std::to_string(i);
  }

  std::vector<uint64_t> handles(HANDLES_COUNT);
  uint8_t category_enabled_flag = 41;
  for (size_t i = 0; i < handles.size(); ++i) {
    TraceObject* trace_object = ring_buffer->AddTraceEvent(&handles[i]);
    CHECK_NOT_NULL(trace_object);
    trace_object->Initialize('X', &category_enabled_flag, names[i].c_str(),
                             "Test.Scope", 42, 123, 0, NULL, NULL, NULL, 0);
    trace_object = ring_buffer->GetEventByHandle(handles[i]);
    CHECK_NOT_NULL(trace_object);
    CHECK_EQ('X', trace_object->phase());
    CHECK_EQ(names[i], std::string(trace_object->name()));
    CHECK_EQ(category_enabled_flag, *trace_object->category_enabled_flag());
  }

  // We should only be able to retrieve the last kChunkSize + 1.
  for (size_t i = 0; i < TraceBufferChunk::kChunkSize; ++i) {
    CHECK_NULL(ring_buffer->GetEventByHandle(handles[i]));
  }

  for (size_t i = TraceBufferChunk::kChunkSize; i < handles.size(); ++i) {
    TraceObject* trace_object = ring_buffer->GetEventByHandle(handles[i]);
    CHECK_NOT_NULL(trace_object);
    // The object properties should be correct.
    CHECK_EQ('X', trace_object->phase());
    CHECK_EQ(names[i], std::string(trace_object->name()));
    CHECK_EQ(category_enabled_flag, *trace_object->category_enabled_flag());
  }

  // Check Flush(), that the writer wrote the last kChunkSize  1 event names.
  ring_buffer->Flush();
  auto events = writer->events();
  CHECK_EQ(TraceBufferChunk::kChunkSize + 1, events.size());
  for (size_t i = TraceBufferChunk::kChunkSize; i < handles.size(); ++i) {
    CHECK_EQ(names[i], events[i - TraceBufferChunk::kChunkSize]);
  }
  delete ring_buffer;
}

TEST(TestJSONTraceWriter) {
  std::ostringstream stream;
  v8::Platform* old_platform = i::V8::GetCurrentPlatform();
  v8::Platform* default_platform = v8::platform::CreateDefaultPlatform();
  i::V8::SetPlatformForTesting(default_platform);
  // Create a scope for the tracing controller to terminate the trace writer.
  {
    TracingController tracing_controller;
    platform::SetTracingController(default_platform, &tracing_controller);
    TraceWriter* writer = TraceWriter::CreateJSONTraceWriter(stream);

    TraceBuffer* ring_buffer =
        TraceBuffer::CreateTraceBufferRingBuffer(1, writer);
    tracing_controller.Initialize(ring_buffer);
    TraceConfig* trace_config = new TraceConfig();
    trace_config->AddIncludedCategory("v8-cat");
    tracing_controller.StartTracing(trace_config);

    TraceObject trace_object;
    trace_object.InitializeForTesting(
        'X', tracing_controller.GetCategoryGroupEnabled("v8-cat"), "Test0",
        v8::internal::tracing::kGlobalScope, 42, 123, 0, NULL, NULL, NULL, 0,
        11, 22, 100, 50, 33, 44);
    writer->AppendTraceEvent(&trace_object);
    trace_object.InitializeForTesting(
        'Y', tracing_controller.GetCategoryGroupEnabled("v8-cat"), "Test1",
        v8::internal::tracing::kGlobalScope, 43, 456, 0, NULL, NULL, NULL, 0,
        55, 66, 110, 55, 77, 88);
    writer->AppendTraceEvent(&trace_object);
    tracing_controller.StopTracing();
  }

  std::string trace_str = stream.str();
  std::string expected_trace_str =
      "{\"traceEvents\":[{\"pid\":11,\"tid\":22,\"ts\":100,\"tts\":50,"
      "\"ph\":\"X\",\"cat\":\"v8-cat\",\"name\":\"Test0\",\"args\":{},"
      "\"dur\":33,\"tdur\":44},{\"pid\":55,\"tid\":66,\"ts\":110,\"tts\":55,"
      "\"ph\":\"Y\",\"cat\":\"v8-cat\",\"name\":\"Test1\",\"args\":{},\"dur\":"
      "77,\"tdur\":88}]}";

  CHECK_EQ(expected_trace_str, trace_str);

  i::V8::SetPlatformForTesting(old_platform);
}

TEST(TestTracingController) {
  v8::Platform* old_platform = i::V8::GetCurrentPlatform();
  v8::Platform* default_platform = v8::platform::CreateDefaultPlatform();
  i::V8::SetPlatformForTesting(default_platform);

  TracingController tracing_controller;
  platform::SetTracingController(default_platform, &tracing_controller);

  MockTraceWriter* writer = new MockTraceWriter();
  TraceBuffer* ring_buffer =
      TraceBuffer::CreateTraceBufferRingBuffer(1, writer);
  tracing_controller.Initialize(ring_buffer);
  TraceConfig* trace_config = new TraceConfig();
  trace_config->AddIncludedCategory("v8");
  tracing_controller.StartTracing(trace_config);

  TRACE_EVENT0("v8", "v8.Test");
  // cat category is not included in default config
  TRACE_EVENT0("cat", "v8.Test2");
  TRACE_EVENT0("v8", "v8.Test3");
  tracing_controller.StopTracing();

  CHECK_EQ(2, writer->events().size());
  CHECK_EQ(std::string("v8.Test"), writer->events()[0]);
  CHECK_EQ(std::string("v8.Test3"), writer->events()[1]);

  i::V8::SetPlatformForTesting(old_platform);
}

}  // namespace tracing
}  // namespace platform
}  // namespace v8
