// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_PUBLIC_V8INSPECTOR_H_
#define V8_INSPECTOR_PUBLIC_V8INSPECTOR_H_

#include "src/inspector/public/StringView.h"
#include "src/inspector/public/V8ContextInfo.h"

#include <v8.h>

namespace v8_inspector {

class V8InspectorClient;
class V8InspectorSession;
class V8StackTrace;

class PLATFORM_EXPORT V8Inspector {
public:
    static std::unique_ptr<V8Inspector> create(v8::Isolate*, V8InspectorClient*);
    virtual ~V8Inspector() { }

    // Contexts instrumentation.
    virtual void contextCreated(const V8ContextInfo&) = 0;
    virtual void contextDestroyed(v8::Local<v8::Context>) = 0;
    virtual void resetContextGroup(int contextGroupId) = 0;

    // Various instrumentation.
    virtual void willExecuteScript(v8::Local<v8::Context>, int scriptId) = 0;
    virtual void didExecuteScript(v8::Local<v8::Context>) = 0;
    virtual void idleStarted() = 0;
    virtual void idleFinished() = 0;

    // Async stack traces instrumentation.
    virtual void asyncTaskScheduled(const StringView& taskName, void* task, bool recurring) = 0;
    virtual void asyncTaskCanceled(void* task) = 0;
    virtual void asyncTaskStarted(void* task) = 0;
    virtual void asyncTaskFinished(void* task) = 0;
    virtual void allAsyncTasksCanceled() = 0;

    // Exceptions instrumentation.
    virtual unsigned exceptionThrown(v8::Local<v8::Context>, const StringView& message, v8::Local<v8::Value> exception, const StringView& detailedMessage, const StringView& url, unsigned lineNumber, unsigned columnNumber, std::unique_ptr<V8StackTrace>, int scriptId) = 0;
    virtual void exceptionRevoked(v8::Local<v8::Context>, unsigned exceptionId, const StringView& message) = 0;

    // Connection.
    class PLATFORM_EXPORT Channel {
    public:
        virtual ~Channel() { }
        virtual void sendProtocolResponse(int callId, const StringView& message) = 0;
        virtual void sendProtocolNotification(const StringView& message) = 0;
        virtual void flushProtocolNotifications() = 0;
    };
    virtual std::unique_ptr<V8InspectorSession> connect(int contextGroupId, Channel*, const StringView& state) = 0;

    // API methods.
    virtual std::unique_ptr<V8StackTrace> createStackTrace(v8::Local<v8::StackTrace>) = 0;
    virtual std::unique_ptr<V8StackTrace> captureStackTrace(bool fullStack) = 0;
};

} // namespace v8_inspector


#endif // V8_INSPECTOR_PUBLIC_V8INSPECTOR_H_
