// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRACING_TRACING_CATEGORY_OBSERVER_H_
#define V8_TRACING_TRACING_CATEGORY_OBSERVER_H_

#include "include/v8-platform.h"
#include "include/v8-tracing.h"

namespace v8 {
namespace tracing {

class TracingCategoryObserverImpl : public TracingCategoryObserver,
                                    public Platform::TraceStateObserver {
 public:
  TracingCategoryObserverImpl();
  ~TracingCategoryObserverImpl();

  // v8::Platform::TraceStateObserver
  void OnTraceEnabled() final;
  void OnTraceDisabled() final;
};

}  // namespace tracing
}  // namespace v8
#endif  // V8_TRACING_TRACING_CATEGORY_OBSERVER_H_
