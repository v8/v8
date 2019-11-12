// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CRDTP_JSON_PLATFORM_H_
#define V8_CRDTP_JSON_PLATFORM_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "export.h"

namespace v8_crdtp {
namespace json {
// Client code must provide an instance. Implementation should delegate
// to whatever is appropriate.
class Platform {
 public:
  virtual ~Platform() = default;
  // Parses |str| into |result|. Returns false iff there are
  // leftover characters or parsing errors.
  virtual bool StrToD(const char* str, double* result) const = 0;

  // Prints |value| in a format suitable for JSON.
  virtual std::unique_ptr<char[]> DToStr(double value) const = 0;
};
}  // namespace json
}  // namespace v8_crdtp

#endif  // V8_CRDTP_JSON_PLATFORM_H_
