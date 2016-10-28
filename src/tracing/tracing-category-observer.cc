// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tracing/tracing-category-observer.h"

#include "include/v8.h"
#include "src/flags.h"
#include "src/tracing/trace-event.h"
#include "src/v8.h"

namespace v8 {
namespace tracing {

std::unique_ptr<TracingCategoryObserver> TracingCategoryObserver::Create() {
  return std::unique_ptr<TracingCategoryObserver>(
      new TracingCategoryObserverImpl());
}

TracingCategoryObserverImpl::TracingCategoryObserverImpl() {}

TracingCategoryObserverImpl::~TracingCategoryObserverImpl() {
  OnTraceDisabled();
}

void TracingCategoryObserverImpl::OnTraceEnabled() {
  bool enabled = false;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(
      TRACE_DISABLED_BY_DEFAULT("v8.runtime_stats"), &enabled);
  if (enabled) {
    v8::internal::FLAG_runtime_stats |= ENABLED_BY_TRACING;
  }
}

void TracingCategoryObserverImpl::OnTraceDisabled() {
  v8::internal::FLAG_runtime_stats &= ~ENABLED_BY_TRACING;
}

}  // namespace tracing
}  // namespace v8
