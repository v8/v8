// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LOOKUP_INL_H_
#define V8_LOOKUP_INL_H_

#include "src/lookup.h"

#include "src/handles-inl.h"

namespace v8 {
namespace internal {

template <class T>
Handle<T> LookupIterator::GetStoreTarget() const {
  DCHECK(receiver_->IsJSReceiver());
  if (receiver_->IsJSGlobalProxy()) {
    Map* map = JSGlobalProxy::cast(*receiver_)->map();
    if (map->has_hidden_prototype()) {
      return handle(JSGlobalObject::cast(map->prototype()), isolate_);
    }
  }
  return Handle<T>::cast(receiver_);
}
inline Handle<InterceptorInfo> LookupIterator::GetInterceptor() const {
  DCHECK_EQ(INTERCEPTOR, state_);
  InterceptorInfo* result =
      IsElement() ? GetInterceptor<true>(JSObject::cast(*holder_))
                  : GetInterceptor<false>(JSObject::cast(*holder_));
  return handle(result, isolate_);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_LOOKUP_INL_H_
