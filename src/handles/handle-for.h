// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_HANDLE_FOR_H_
#define V8_HANDLES_HANDLE_FOR_H_

namespace v8 {
namespace internal {

// HandleTraits is specialized for Isolate, Factory, OffThreadIsolate, and
// OffThreadFactory.
template <typename IsolateOrFactory>
struct HandleTraits;

// HandleFor<X> will return an appropriate Handle type for X, whether X is an
// Isolate, a Factory, an OffThreadIsolate, or an OffThreadFactory. This allows
// us to use it for both Isolate and Factory based optimisations.
template <typename IsolateOrFactory, typename T>
using HandleFor =
    typename HandleTraits<IsolateOrFactory>::template HandleType<T>;
template <typename IsolateOrFactory, typename T>
using MaybeHandleFor =
    typename HandleTraits<IsolateOrFactory>::template MaybeHandleType<T>;
template <typename IsolateOrFactory>
using HandleScopeFor = typename HandleTraits<IsolateOrFactory>::HandleScopeType;

}  // namespace internal
}  // namespace v8

#endif  // V8_HANDLES_HANDLE_FOR_H_
