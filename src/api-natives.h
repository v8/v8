// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_API_NATIVES_H_
#define V8_API_NATIVES_H_

#include "src/handles.h"

namespace v8 {
namespace internal {

class ApiNatives {
 public:
  MUST_USE_RESULT static MaybeHandle<JSFunction> InstantiateFunction(
      Handle<FunctionTemplateInfo> data);
  MUST_USE_RESULT static MaybeHandle<JSObject> InstantiateObject(
      Handle<ObjectTemplateInfo> data);
  MUST_USE_RESULT static MaybeHandle<FunctionTemplateInfo> ConfigureInstance(
      Isolate* isolate, Handle<FunctionTemplateInfo> instance,
      Handle<JSObject> data);

  enum ApiInstanceType {
    JavaScriptObjectType,
    GlobalObjectType,
    GlobalProxyType
  };

  static Handle<JSFunction> CreateApiFunction(Isolate* isolate,
                                              Handle<FunctionTemplateInfo> obj,
                                              Handle<Object> prototype,
                                              ApiInstanceType instance_type);
};

}  // namespace internal
}  // namespace v8

#endif
