// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/elements.h"
#include "src/factory.h"
#include "src/isolate-inl.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {


RUNTIME_FUNCTION(Runtime_IsJSProxy) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_CHECKED(Object, obj, 0);
  return isolate->heap()->ToBoolean(obj->IsJSProxy());
}


RUNTIME_FUNCTION(Runtime_JSProxyGetHandler) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_CHECKED(JSProxy, proxy, 0);
  return proxy->handler();
}


RUNTIME_FUNCTION(Runtime_JSProxyGetTarget) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_CHECKED(JSProxy, proxy, 0);
  return proxy->target();
}


RUNTIME_FUNCTION(Runtime_JSProxyRevoke) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSProxy, proxy, 0);
  JSProxy::Revoke(proxy);
  return isolate->heap()->undefined_value();
}

}  // namespace internal
}  // namespace v8
