// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/platform.h"

#include "src/base/lazy-instance.h"
#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/heap/cppgc/gc-info-table.h"
#include "src/heap/cppgc/platform.h"

namespace cppgc {
namespace internal {

void Abort() { v8::base::OS::Abort(); }

void FatalOutOfMemoryHandler::operator()(const std::string& reason,
                                         const SourceLocation& loc) const {
  if (custom_handler_) {
    (*custom_handler_)(reason, loc, heap_);
    FATAL("Custom out of memory handler should not have returned");
  }
#ifdef DEBUG
  V8_Fatal(loc.FileName(), static_cast<int>(loc.Line()),
           "Oilpan: Out of memory (%s)", reason.c_str());
#else   // !DEBUG
  V8_Fatal("Oilpan: Out of memory");
#endif  // !DEBUG
}

void FatalOutOfMemoryHandler::SetCustomHandler(Callback* callback) {
  custom_handler_ = callback;
}

}  // namespace internal

namespace {
PageAllocator* g_page_allocator = nullptr;
}  // namespace

TracingController* Platform::GetTracingController() {
  static v8::base::LeakyObject<TracingController> tracing_controller;
  return tracing_controller.get();
}

void InitializeProcess(PageAllocator* page_allocator) {
  CHECK(!g_page_allocator);
  internal::GlobalGCInfoTable::Initialize(page_allocator);
  g_page_allocator = page_allocator;
}

void ShutdownProcess() { g_page_allocator = nullptr; }

}  // namespace cppgc
