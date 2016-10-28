// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_V8_TRACING_H_
#define V8_V8_TRACING_H_

#include "v8.h"  // NOLINT(build/include)

namespace v8 {
namespace tracing {

class V8_EXPORT TracingCategoryObserver {
 public:
  enum Mode {
    ENABLED_BY_NATIVE = 1 << 0,
    ENABLED_BY_TRACING = 1 << 1,
  };
  static std::unique_ptr<TracingCategoryObserver> Create();
  virtual ~TracingCategoryObserver() = default;

 protected:
  TracingCategoryObserver() = default;
};

}  // namespace tracing
}  // namespace v8

#endif  // V8_V8_TRACING_H_
