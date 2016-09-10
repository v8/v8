// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_V8CONSOLEAGENTIMPL_H_
#define V8_INSPECTOR_V8CONSOLEAGENTIMPL_H_

#include "src/inspector/Allocator.h"
#include "src/inspector/protocol/Console.h"
#include "src/inspector/protocol/Forward.h"

namespace v8_inspector {

class V8ConsoleMessage;
class V8InspectorSessionImpl;

using protocol::ErrorString;

class V8ConsoleAgentImpl : public protocol::Console::Backend {
  V8_INSPECTOR_DISALLOW_COPY(V8ConsoleAgentImpl);

 public:
  V8ConsoleAgentImpl(V8InspectorSessionImpl*, protocol::FrontendChannel*,
                     protocol::DictionaryValue* state);
  ~V8ConsoleAgentImpl() override;

  void enable(ErrorString*) override;
  void disable(ErrorString*) override;
  void clearMessages(ErrorString*) override;

  void restore();
  void messageAdded(V8ConsoleMessage*);
  void reset();
  bool enabled();

 private:
  void reportAllMessages();
  bool reportMessage(V8ConsoleMessage*, bool generatePreview);

  V8InspectorSessionImpl* m_session;
  protocol::DictionaryValue* m_state;
  protocol::Console::Frontend m_frontend;
  bool m_enabled;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_V8CONSOLEAGENTIMPL_H_
