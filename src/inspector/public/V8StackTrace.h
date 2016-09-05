// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_PUBLIC_V8STACKTRACE_H_
#define V8_INSPECTOR_PUBLIC_V8STACKTRACE_H_

#include "src/inspector/public/StringBuffer.h"
#include "src/inspector/public/StringView.h"
#include "src/inspector/public/protocol/Runtime.h"

namespace v8_inspector {

class V8StackTrace {
 public:
  virtual bool isEmpty() const = 0;
  virtual StringView topSourceURL() const = 0;
  virtual int topLineNumber() const = 0;
  virtual int topColumnNumber() const = 0;
  virtual StringView topScriptId() const = 0;
  virtual StringView topFunctionName() const = 0;

  virtual ~V8StackTrace() {}
  virtual std::unique_ptr<protocol::Runtime::API::StackTrace>
  buildInspectorObject() const = 0;
  virtual std::unique_ptr<StringBuffer> toString() const = 0;

  // Safe to pass between threads, drops async chain.
  virtual std::unique_ptr<V8StackTrace> clone() = 0;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_PUBLIC_V8STACKTRACE_H_
