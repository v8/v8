// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_V8STACKTRACEIMPL_H_
#define V8_INSPECTOR_V8STACKTRACEIMPL_H_

#include <memory>
#include <vector>

#include "include/v8-inspector.h"
#include "include/v8.h"
#include "src/base/macros.h"
#include "src/inspector/protocol/Runtime.h"
#include "src/inspector/string-16.h"

namespace v8_inspector {

class AsyncStackTrace;
class V8Debugger;
class WasmTranslation;

class V8StackTraceImpl : public V8StackTrace {
 public:
  static void setCaptureStackTraceForUncaughtExceptions(v8::Isolate*,
                                                        bool capture);
  static const int maxCallStackSizeToCapture = 200;
  static std::unique_ptr<V8StackTraceImpl> create(V8Debugger*,
                                                  int contextGroupId,
                                                  v8::Local<v8::StackTrace>,
                                                  int maxStackSize);
  static std::unique_ptr<V8StackTraceImpl> capture(V8Debugger*,
                                                   int contextGroupId,
                                                   int maxStackSize);

  ~V8StackTraceImpl() override;
  std::unique_ptr<protocol::Runtime::StackTrace> buildInspectorObjectImpl()
      const;

  // V8StackTrace implementation.
  // This method drops the async stack trace.
  std::unique_ptr<V8StackTrace> clone() override;
  bool isEmpty() const override;
  StringView topSourceURL() const override;
  int topLineNumber() const override;    // 1-based.
  int topColumnNumber() const override;  // 1-based.
  StringView topScriptId() const override;
  StringView topFunctionName() const override;
  std::unique_ptr<protocol::Runtime::API::StackTrace> buildInspectorObject()
      const override;
  std::unique_ptr<StringBuffer> toString() const override;

  class Frame {
   public:
    explicit Frame(v8::Local<v8::StackFrame> frame);
    ~Frame() = default;

    void translate(WasmTranslation* wasmTranslation);

    const String16& functionName() const;
    const String16& scriptId() const;
    const String16& sourceURL() const;
    int lineNumber() const;    // 0-based.
    int columnNumber() const;  // 0-based.
    std::unique_ptr<protocol::Runtime::CallFrame> buildInspectorObject() const;

   private:
    String16 m_functionName;
    String16 m_scriptId;
    String16 m_sourceURL;
    int m_lineNumber;    // 0-based.
    int m_columnNumber;  // 0-based.
  };

 private:
  V8StackTraceImpl(const std::vector<Frame> frames, int maxAsyncDepth,
                   std::shared_ptr<AsyncStackTrace> asyncParent,
                   std::shared_ptr<AsyncStackTrace> asyncCreation);

  std::vector<Frame> m_frames;
  int m_maxAsyncDepth;
  std::weak_ptr<AsyncStackTrace> m_asyncParent;
  std::weak_ptr<AsyncStackTrace> m_asyncCreation;

  DISALLOW_COPY_AND_ASSIGN(V8StackTraceImpl);
};

class AsyncStackTrace {
 public:
  static std::shared_ptr<AsyncStackTrace> capture(V8Debugger*,
                                                  int contextGroupId,
                                                  const String16& description,
                                                  int maxStackSize);

  std::unique_ptr<protocol::Runtime::StackTrace> buildInspectorObject(
      AsyncStackTrace* asyncCreation, int maxAsyncDepth) const;

  int contextGroupId() const;
  std::weak_ptr<AsyncStackTrace> parent() const;
  std::weak_ptr<AsyncStackTrace> creation() const;
  bool isEmpty() const;

 private:
  AsyncStackTrace(int contextGroupId, const String16& description,
                  const std::vector<V8StackTraceImpl::Frame>& frames,
                  std::shared_ptr<AsyncStackTrace> asyncParent,
                  std::shared_ptr<AsyncStackTrace> asyncCreation);

  int m_contextGroupId;
  String16 m_description;

  std::vector<V8StackTraceImpl::Frame> m_frames;
  std::weak_ptr<AsyncStackTrace> m_asyncParent;
  std::weak_ptr<AsyncStackTrace> m_asyncCreation;

  DISALLOW_COPY_AND_ASSIGN(AsyncStackTrace);
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_V8STACKTRACEIMPL_H_
