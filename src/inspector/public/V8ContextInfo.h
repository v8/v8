// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_PUBLIC_V8CONTEXTINFO_H_
#define V8_INSPECTOR_PUBLIC_V8CONTEXTINFO_H_

#include "src/inspector/public/StringView.h"

#include <v8.h>

namespace v8_inspector {

class V8ContextInfo {
 public:
  V8ContextInfo(v8::Local<v8::Context> context, int contextGroupId,
                const StringView& humanReadableName)
      : context(context),
        contextGroupId(contextGroupId),
        humanReadableName(humanReadableName),
        hasMemoryOnConsole(false) {}

  v8::Local<v8::Context> context;
  // Each v8::Context is a part of a group. The group id is used to find
  // appropriate
  // V8DebuggerAgent to notify about events in the context.
  // |contextGroupId| must be non-0.
  int contextGroupId;
  StringView humanReadableName;
  StringView origin;
  StringView auxData;
  bool hasMemoryOnConsole;

 private:
  // Disallow copying and allocating this one.
  enum NotNullTagEnum { NotNullLiteral };
  void* operator new(size_t) = delete;
  void* operator new(size_t, NotNullTagEnum, void*) = delete;
  void* operator new(size_t, void*) = delete;
  V8ContextInfo(const V8ContextInfo&) = delete;
  V8ContextInfo& operator=(const V8ContextInfo&) = delete;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_PUBLIC_V8CONTEXTINFO_H_
