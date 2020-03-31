// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_TYPE_TRAITS_H_
#define INCLUDE_CPPGC_TYPE_TRAITS_H_

#include <type_traits>

#include "include/cppgc/member.h"

namespace cppgc {

namespace internal {

// Not supposed to be specialized by the user.
template <typename T>
struct IsWeak : std::false_type {};

template <typename T, typename WriteBarrierPolicy>
struct IsWeak<internal::BasicWeakMember<T, WriteBarrierPolicy>>
    : std::true_type {};

}  // namespace internal

template <typename T>
constexpr bool IsWeakV = internal::IsWeak<T>::value;

}  // namespace cppgc

#endif  // INCLUDE_CPPGC_TYPE_TRAITS_H_
