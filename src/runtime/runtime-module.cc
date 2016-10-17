// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_GetModuleNamespace) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);
  CONVERT_SMI_ARG_CHECKED(module_request, 0);
  Handle<Module> module(isolate->context()->module());
  return *Module::GetModuleNamespace(module, module_request);
}

RUNTIME_FUNCTION(Runtime_LoadModuleExport) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);
  CONVERT_ARG_HANDLE_CHECKED(String, name, 0);
  Handle<Module> module(isolate->context()->module());
  return *Module::LoadExport(module, name);
}

RUNTIME_FUNCTION(Runtime_LoadModuleImport) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 2);
  CONVERT_ARG_HANDLE_CHECKED(String, name, 0);
  CONVERT_SMI_ARG_CHECKED(module_request, 1);
  Handle<Module> module(isolate->context()->module());
  return *Module::LoadImport(module, name, module_request);
}

RUNTIME_FUNCTION(Runtime_StoreModuleExport) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 2);
  CONVERT_ARG_HANDLE_CHECKED(String, name, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, value, 1);
  Handle<Module> module(isolate->context()->module());
  Module::StoreExport(module, name, value);
  return isolate->heap()->undefined_value();
}

}  // namespace internal
}  // namespace v8
