// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_OPTIONAL_H_
#define V8_BASE_OPTIONAL_H_

#include "absl/types/optional.h"

namespace v8 {
namespace base {

using absl::bad_optional_access;
template <typename T>
using Optional = absl::optional<T>;
using absl::in_place;
using absl::in_place_t;
using absl::make_optional;
using absl::nullopt;
using absl::nullopt_t;

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_OPTIONAL_H_
