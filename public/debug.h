// Copyright 2008 Google Inc. All Rights Reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef _V8_DEBUG
#define _V8_DEBUG

#include "v8.h"

#if defined(__GNUC__) && (__GNUC__ >= 4)
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif

/**
 * Debugger support for the V8 JavaScript engine.
 */
namespace v8 {

// Debug events which can occur in the V8 JavaScript engine.
enum DebugEvent {
  Break = 1,
  Exception = 2,
  NewFunction = 3,
  BeforeCompile = 4,
  AfterCompile  = 5
};


/**
 * Debug event callback function.
 *
 * \param event the debug event from which occoured (from the DebugEvent
 *              enumeration)
 * \param exec_state execution state (JavaScript object)
 * \param event_data event specific data (JavaScript object)
 * \param data value passed by the user to AddDebugEventListener
 */
typedef void (*DebugEventCallback)(DebugEvent event,
                                   Handle<Object> exec_state,
                                   Handle<Object> event_data,
                                   Handle<Value> data);


/**
 * Debug message callback function.
 *
 * \param message the debug message
 * \param length length of the message
 * A DebugMessageHandler does not take posession of the message string,
 * and must not rely on the data persisting after the handler returns.
 */
typedef void (*DebugMessageHandler)(const uint16_t* message, int length,
                                    void* data);


class EXPORT Debug {
 public:
  // Add a C debug event listener.
  static bool AddDebugEventListener(DebugEventCallback that,
                                    Handle<Value> data = Handle<Value>());

  // Add a JavaScript debug event listener.
  static bool AddDebugEventListener(v8::Handle<v8::Function> that,
                                    Handle<Value> data = Handle<Value>());

  // Remove a C debug event listener.
  static void RemoveDebugEventListener(DebugEventCallback that);

  // Remove a JavaScript debug event listener.
  static void RemoveDebugEventListener(v8::Handle<v8::Function> that);

  // Generate a stack dump.
  static void StackDump();

  // Break execution of JavaScript.
  static void DebugBreak();

  // Message based interface. The message protocol is JSON.
  static void SetMessageHandler(DebugMessageHandler handler, void* data = NULL);
  static void SendCommand(const uint16_t* command, int length);
};


}  // namespace v8


#undef EXPORT


#endif  // _V8_DEBUG
