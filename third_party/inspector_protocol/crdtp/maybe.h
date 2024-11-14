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
// detail::PtrMaybe, templates for optional
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

  // std::optional<>-compatible accessors (preferred).
  bool has_value() const { return !!value_; }
  operator bool() const { return has_value(); }
  const T& value() const& {
    assert(has_value());
    return *value_;
  }
  T& value() & {
    assert(has_value());
    return *value_;
  }
  T&& value() && {
    assert(has_value());
    return std::move(*value_);
  }
  const T& value_or(const T& default_value) const {
    return has_value() ? *value_ : default_value;
  }
  T* operator->() { return &value(); }
  const T* operator->() const { return &value(); }

  T& operator*() & { return value(); }
  const T& operator*() const& { return value(); }
  T&& operator*() && { return std::move(value()); }
  T* get() const { return value_.get(); }

  // Legacy Maybe<> accessors (deprecated).
  T* fromJust() const {
    assert(value_);
    return value_.get();
  }
  T* fromMaybe(T* default_value) const {
    return value_ ? value_.get() : default_value;
  }
  bool isJust() const { return value_ != nullptr; }

 private:
  std::unique_ptr<T> value_;
};

template <typename T>
struct MaybeTypedef {
  typedef PtrMaybe<T> type;
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
