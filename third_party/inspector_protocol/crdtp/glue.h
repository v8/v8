// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CRDTP_GLUE_H_
#define V8_CRDTP_GLUE_H_

#include <cassert>
#include <memory>

namespace v8_crdtp {
namespace glue {
// =============================================================================
// glue::detail::PtrMaybe, templates for optional
// pointers / values which are used in ../lib/Forward_h.template.
// =============================================================================
namespace detail {
template <typename T>
class PtrMaybe {
 public:
  PtrMaybe() = default;
  PtrMaybe(std::unique_ptr<T> value) : value_(std::move(value)) {}
  PtrMaybe(PtrMaybe&& other) noexcept : value_(std::move(other.value_)) {}
  void operator=(std::unique_ptr<T> value) { value_ = std::move(value); }
  T* fromJust() const {
    assert(value_);
    return value_.get();
  }
  T* fromMaybe(T* default_value) const {
    return value_ ? value_.get() : default_value;
  }
  bool isJust() const { return value_ != nullptr; }
  std::unique_ptr<T> takeJust() {
    assert(value_);
    return std::move(value_);
  }

 private:
  std::unique_ptr<T> value_;
};

}  // namespace detail
}  // namespace glue
}  // namespace v8_crdtp

#define PROTOCOL_DISALLOW_COPY(ClassName) \
 private:                                 \
  ClassName(const ClassName&) = delete;   \
  ClassName& operator=(const ClassName&) = delete

#endif  // V8_CRDTP_GLUE_H_
