// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/v8-stack-trace-impl.h"

#include <algorithm>

#include "src/inspector/v8-debugger.h"
#include "src/inspector/wasm-translation.h"

namespace v8_inspector {

namespace {

static const v8::StackTrace::StackTraceOptions stackTraceOptions =
    static_cast<v8::StackTrace::StackTraceOptions>(
        v8::StackTrace::kDetailed |
        v8::StackTrace::kExposeFramesAcrossSecurityOrigins);

std::vector<V8StackTraceImpl::Frame> toFramesVector(
    V8Debugger* debugger, v8::Local<v8::StackTrace> v8StackTrace,
    int maxStackSize) {
  DCHECK(debugger->isolate()->InContext());
  int frameCount = std::min(v8StackTrace->GetFrameCount(), maxStackSize);
  std::vector<V8StackTraceImpl::Frame> frames;
  for (int i = 0; i < frameCount; ++i) {
    v8::Local<v8::StackFrame> v8Frame = v8StackTrace->GetFrame(i);
    frames.emplace_back(v8Frame);
    // TODO(clemensh): Figure out a way to do this translation only right before
    // sending the stack trace over wire.
    if (v8Frame->IsWasm()) frames.back().translate(debugger->wasmTranslation());
  }
  return frames;
}

void calculateAsyncChain(V8Debugger* debugger, int contextGroupId,
                         std::shared_ptr<AsyncStackTrace>* asyncParent,
                         std::shared_ptr<AsyncStackTrace>* asyncCreation,
                         int* maxAsyncDepth) {
  *asyncParent = debugger->currentAsyncParent();
  *asyncCreation = debugger->currentAsyncCreation();
  if (maxAsyncDepth) *maxAsyncDepth = debugger->maxAsyncCallChainDepth();

  DCHECK(!*asyncParent || !*asyncCreation ||
         (*asyncParent)->contextGroupId() ==
             (*asyncCreation)->contextGroupId());
  // Do not accidentally append async call chain from another group. This should
  // not happen if we have proper instrumentation, but let's double-check to be
  // safe.
  if (contextGroupId && *asyncParent && (*asyncParent)->contextGroupId() &&
      (*asyncParent)->contextGroupId() != contextGroupId) {
    asyncParent->reset();
    asyncCreation->reset();
    if (maxAsyncDepth) *maxAsyncDepth = 0;
    return;
  }

  // Only the top stack in the chain may be empty and doesn't contain creation
  // stack, so ensure that second stack is non-empty (it's the top of appended
  // chain).
  if (*asyncParent && !(*asyncCreation) && !(*asyncParent)->creation().lock() &&
      (*asyncParent)->isEmpty()) {
    *asyncParent = (*asyncParent)->parent().lock();
  }
}

std::unique_ptr<protocol::Runtime::StackTrace> buildInspectorObjectCommon(
    const std::vector<V8StackTraceImpl::Frame>& frames,
    const std::shared_ptr<AsyncStackTrace>& asyncParent,
    const std::shared_ptr<AsyncStackTrace>& asyncCreation, int maxAsyncDepth) {
  std::unique_ptr<protocol::Array<protocol::Runtime::CallFrame>>
      inspectorFrames = protocol::Array<protocol::Runtime::CallFrame>::create();
  for (size_t i = 0; i < frames.size(); i++) {
    inspectorFrames->addItem(frames[i].buildInspectorObject());
  }
  std::unique_ptr<protocol::Runtime::StackTrace> stackTrace =
      protocol::Runtime::StackTrace::create()
          .setCallFrames(std::move(inspectorFrames))
          .build();
  if (asyncParent && maxAsyncDepth > 0) {
    stackTrace->setParent(asyncParent->buildInspectorObject(asyncCreation.get(),
                                                            maxAsyncDepth - 1));
  }
  return stackTrace;
}

}  //  namespace

V8StackTraceImpl::Frame::Frame(v8::Local<v8::StackFrame> v8Frame)
    : m_functionName(toProtocolString(v8Frame->GetFunctionName())),
      m_scriptId(String16::fromInteger(v8Frame->GetScriptId())),
      m_sourceURL(toProtocolString(v8Frame->GetScriptNameOrSourceURL())),
      m_lineNumber(v8Frame->GetLineNumber() - 1),
      m_columnNumber(v8Frame->GetColumn() - 1) {
  DCHECK(m_lineNumber + 1 != v8::Message::kNoLineNumberInfo);
  DCHECK(m_columnNumber + 1 != v8::Message::kNoColumnInfo);
}

void V8StackTraceImpl::Frame::translate(WasmTranslation* wasmTranslation) {
  wasmTranslation->TranslateWasmScriptLocationToProtocolLocation(
      &m_scriptId, &m_lineNumber, &m_columnNumber);
}

const String16& V8StackTraceImpl::Frame::functionName() const {
  return m_functionName;
}

const String16& V8StackTraceImpl::Frame::scriptId() const { return m_scriptId; }

const String16& V8StackTraceImpl::Frame::sourceURL() const {
  return m_sourceURL;
}

int V8StackTraceImpl::Frame::lineNumber() const { return m_lineNumber; }

int V8StackTraceImpl::Frame::columnNumber() const { return m_columnNumber; }

std::unique_ptr<protocol::Runtime::CallFrame>
V8StackTraceImpl::Frame::buildInspectorObject() const {
  return protocol::Runtime::CallFrame::create()
      .setFunctionName(m_functionName)
      .setScriptId(m_scriptId)
      .setUrl(m_sourceURL)
      .setLineNumber(m_lineNumber)
      .setColumnNumber(m_columnNumber)
      .build();
}

// static
void V8StackTraceImpl::setCaptureStackTraceForUncaughtExceptions(
    v8::Isolate* isolate, bool capture) {
  isolate->SetCaptureStackTraceForUncaughtExceptions(
      capture, V8StackTraceImpl::maxCallStackSizeToCapture);
}

// static
std::unique_ptr<V8StackTraceImpl> V8StackTraceImpl::create(
    V8Debugger* debugger, int contextGroupId,
    v8::Local<v8::StackTrace> v8StackTrace, int maxStackSize) {
  DCHECK(debugger);

  v8::Isolate* isolate = debugger->isolate();
  v8::HandleScope scope(isolate);

  std::vector<V8StackTraceImpl::Frame> frames;
  if (!v8StackTrace.IsEmpty() && v8StackTrace->GetFrameCount()) {
    frames = toFramesVector(debugger, v8StackTrace, maxStackSize);
  }

  int maxAsyncDepth = 0;
  std::shared_ptr<AsyncStackTrace> asyncParent;
  std::shared_ptr<AsyncStackTrace> asyncCreation;
  calculateAsyncChain(debugger, contextGroupId, &asyncParent, &asyncCreation,
                      &maxAsyncDepth);
  if (frames.empty() && !asyncCreation && !asyncParent) return nullptr;
  return std::unique_ptr<V8StackTraceImpl>(
      new V8StackTraceImpl(frames, maxAsyncDepth, asyncParent, asyncCreation));
}

// static
std::unique_ptr<V8StackTraceImpl> V8StackTraceImpl::capture(
    V8Debugger* debugger, int contextGroupId, int maxStackSize) {
  DCHECK(debugger);
  v8::Isolate* isolate = debugger->isolate();
  v8::HandleScope handleScope(isolate);
  v8::Local<v8::StackTrace> v8StackTrace;
  if (isolate->InContext()) {
    v8StackTrace = v8::StackTrace::CurrentStackTrace(isolate, maxStackSize,
                                                     stackTraceOptions);
  }
  return V8StackTraceImpl::create(debugger, contextGroupId, v8StackTrace,
                                  maxStackSize);
}

V8StackTraceImpl::V8StackTraceImpl(
    const std::vector<Frame> frames, int maxAsyncDepth,
    std::shared_ptr<AsyncStackTrace> asyncParent,
    std::shared_ptr<AsyncStackTrace> asyncCreation)
    : m_frames(frames),
      m_maxAsyncDepth(maxAsyncDepth),
      m_asyncParent(asyncParent),
      m_asyncCreation(asyncCreation) {}

V8StackTraceImpl::~V8StackTraceImpl() {}

std::unique_ptr<V8StackTrace> V8StackTraceImpl::clone() {
  return std::unique_ptr<V8StackTrace>(
      new V8StackTraceImpl(m_frames, 0, std::shared_ptr<AsyncStackTrace>(),
                           std::shared_ptr<AsyncStackTrace>()));
}

bool V8StackTraceImpl::isEmpty() const { return m_frames.empty(); }

StringView V8StackTraceImpl::topSourceURL() const {
  return toStringView(m_frames[0].sourceURL());
}

int V8StackTraceImpl::topLineNumber() const {
  return m_frames[0].lineNumber() + 1;
}

int V8StackTraceImpl::topColumnNumber() const {
  return m_frames[0].columnNumber() + 1;
}

StringView V8StackTraceImpl::topScriptId() const {
  return toStringView(m_frames[0].scriptId());
}

StringView V8StackTraceImpl::topFunctionName() const {
  return toStringView(m_frames[0].functionName());
}

std::unique_ptr<protocol::Runtime::StackTrace>
V8StackTraceImpl::buildInspectorObjectImpl() const {
  return buildInspectorObjectCommon(m_frames, m_asyncParent.lock(),
                                    m_asyncCreation.lock(), m_maxAsyncDepth);
}

std::unique_ptr<protocol::Runtime::API::StackTrace>
V8StackTraceImpl::buildInspectorObject() const {
  return buildInspectorObjectImpl();
}

std::unique_ptr<StringBuffer> V8StackTraceImpl::toString() const {
  String16Builder stackTrace;
  for (size_t i = 0; i < m_frames.size(); ++i) {
    const Frame& frame = m_frames[i];
    stackTrace.append("\n    at " + (frame.functionName().length()
                                         ? frame.functionName()
                                         : "(anonymous function)"));
    stackTrace.append(" (");
    stackTrace.append(frame.sourceURL());
    stackTrace.append(':');
    stackTrace.append(String16::fromInteger(frame.lineNumber()));
    stackTrace.append(':');
    stackTrace.append(String16::fromInteger(frame.columnNumber()));
    stackTrace.append(')');
  }
  String16 string = stackTrace.toString();
  return StringBufferImpl::adopt(string);
}

// static
std::shared_ptr<AsyncStackTrace> AsyncStackTrace::capture(
    V8Debugger* debugger, int contextGroupId, const String16& description,
    int maxStackSize) {
  DCHECK(debugger);

  v8::Isolate* isolate = debugger->isolate();
  v8::HandleScope handleScope(isolate);

  std::vector<V8StackTraceImpl::Frame> frames;
  if (isolate->InContext()) {
    v8::Local<v8::StackTrace> v8StackTrace = v8::StackTrace::CurrentStackTrace(
        isolate, maxStackSize, stackTraceOptions);
    frames = toFramesVector(debugger, v8StackTrace, maxStackSize);
  }

  std::shared_ptr<AsyncStackTrace> asyncParent;
  std::shared_ptr<AsyncStackTrace> asyncCreation;
  calculateAsyncChain(debugger, contextGroupId, &asyncParent, &asyncCreation,
                      nullptr);

  if (frames.empty() && !asyncCreation && !asyncParent) return nullptr;

  // When async call chain is empty but doesn't contain useful schedule stack
  // and parent async call chain contains creationg stack but doesn't
  // synchronous we can merge them together.
  // e.g. Promise ThenableJob.
  if (asyncParent && frames.empty() &&
      asyncParent->m_description == description && !asyncCreation) {
    return asyncParent;
  }

  return std::shared_ptr<AsyncStackTrace>(new AsyncStackTrace(
      contextGroupId, description, frames, asyncParent, asyncCreation));
}

AsyncStackTrace::AsyncStackTrace(
    int contextGroupId, const String16& description,
    const std::vector<V8StackTraceImpl::Frame>& frames,
    std::shared_ptr<AsyncStackTrace> asyncParent,
    std::shared_ptr<AsyncStackTrace> asyncCreation)
    : m_contextGroupId(contextGroupId),
      m_description(description),
      m_frames(frames),
      m_asyncParent(asyncParent),
      m_asyncCreation(asyncCreation) {}

std::unique_ptr<protocol::Runtime::StackTrace>
AsyncStackTrace::buildInspectorObject(AsyncStackTrace* asyncCreation,
                                      int maxAsyncDepth) const {
  std::unique_ptr<protocol::Runtime::StackTrace> stackTrace =
      buildInspectorObjectCommon(m_frames, m_asyncParent.lock(),
                                 m_asyncCreation.lock(), maxAsyncDepth);
  if (!m_description.isEmpty()) stackTrace->setDescription(m_description);
  if (asyncCreation && !asyncCreation->isEmpty()) {
    stackTrace->setPromiseCreationFrame(
        asyncCreation->m_frames[0].buildInspectorObject());
  }
  return stackTrace;
}

int AsyncStackTrace::contextGroupId() const { return m_contextGroupId; }

std::weak_ptr<AsyncStackTrace> AsyncStackTrace::parent() const {
  return m_asyncParent;
}

std::weak_ptr<AsyncStackTrace> AsyncStackTrace::creation() const {
  return m_asyncCreation;
}

bool AsyncStackTrace::isEmpty() const { return m_frames.empty(); }

}  // namespace v8_inspector
