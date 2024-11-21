// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_CRDTP_MAYBE_H_
#define V8_CRDTP_MAYBE_H_

#include <cassert>
#include <memory>
#include <optional>

namespace v8_crdtp {

// =============================================================================
// Templates for optional
// pointers / values which are used in ../lib/Forward_h.template.
// =============================================================================

namespace detail {
template <typename T>
struct MaybeTypedef {
  typedef std::unique_ptr<T> type;
};

template <>
struct MaybeTypedef<bool> {
  typedef std::optional<bool> type;
};

template <>
struct MaybeTypedef<int> {
  typedef std::optional<int> type;
};

template <>
struct MaybeTypedef<double> {
  typedef std::optional<double> type;
};

template <>
struct MaybeTypedef<std::string> {
  typedef std::optional<std::string> type;
};

}  // namespace detail

template <typename T>
using Maybe = typename detail::MaybeTypedef<T>::type;

}  // namespace v8_crdtp

#endif  // V8_CRDTP_MAYBE_H_
