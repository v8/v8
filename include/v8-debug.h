// Copyright 2008 the V8 project authors. All rights reserved.
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

#ifndef V8_DEBUG_H_
#define V8_DEBUG_H_

#include "v8.h"

#ifdef _WIN32
typedef int int32_t;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;  // NOLINT
typedef long long int64_t;  // NOLINT

// Setup for Windows DLL export/import. See v8.h in this directory for
// information on how to build/use V8 as a DLL.
#if defined(BUILDING_V8_SHARED) && defined(USING_V8_SHARED)
#error both BUILDING_V8_SHARED and USING_V8_SHARED are set - please check the\
  build configuration to ensure that at most one of these is set
#endif

#ifdef BUILDING_V8_SHARED
#define EXPORT __declspec(dllexport)
#elif USING_V8_SHARED
#define EXPORT __declspec(dllimport)
#else
#define EXPORT
#endif

#else  // _WIN32

// Setup for Linux shared library export. See v8.h in this directory for
// information on how to build/use V8 as shared library.
#if defined(__GNUC__) && (__GNUC__ >= 4)
#define EXPORT __attribute__ ((visibility("default")))
#else  // defined(__GNUC__) && (__GNUC__ >= 4)
#define EXPORT
#endif  // defined(__GNUC__) && (__GNUC__ >= 4)

#endif  // _WIN32


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


class EXPORT Debug {
 public:
  /**
   * A client object passed to the v8 debugger whose ownership will be taken by
   * it. v8 is always responsible for deleting the object.
   */
  class ClientData {
   public:
    virtual ~ClientData() {}
  };


  /**
   * Debug event callback function.
   *
   * \param event the type of the debug event that triggered the callback
   *   (enum DebugEvent)
   * \param exec_state execution state (JavaScript object)
   * \param event_data event specific data (JavaScript object)
   * \param data value passed by the user to SetDebugEventListener
   */
  typedef void (*EventCallback)(DebugEvent event,
                                     Handle<Object> exec_state,
                                     Handle<Object> event_data,
                                     Handle<Value> data);


  /**
   * Debug message callback function.
   *
   * \param message the debug message
   * \param length length of the message
   * \param data the data value passed when registering the message handler
   * \param client_data the data value passed into Debug::SendCommand along
   *     with the request that led to the message or NULL if the message is an
   *     asynchronous event. The debugger takes ownership of the data and will
   *     delete it before dying even if there is no message handler.
   * A MessageHandler does not take posession of the message string,
   * and must not rely on the data persisting after the handler returns.
   */
  typedef void (*MessageHandler)(const uint16_t* message, int length,
                                      ClientData* client_data);

  /**
   * Debug host dispatch callback function.
   *
   * \param dispatch the dispatch value
   * \param data the data value passed when registering the dispatch handler
   */
  typedef void (*HostDispatchHandler)(ClientData* dispatch);


  // Set a C debug event listener.
  static bool SetDebugEventListener(EventCallback that,
                                    Handle<Value> data = Handle<Value>());

  // Set a JavaScript debug event listener.
  static bool SetDebugEventListener(v8::Handle<v8::Object> that,
                                    Handle<Value> data = Handle<Value>());

  // Break execution of JavaScript.
  static void DebugBreak();

  // Message based interface. The message protocol is JSON.
  static void SetMessageHandler(MessageHandler handler,
                                bool message_handler_thread = true);
  static void SendCommand(const uint16_t* command, int length,
                          ClientData* client_data = NULL);

  // Dispatch interface.
  static void SetHostDispatchHandler(HostDispatchHandler handler);
  static void SendHostDispatch(ClientData* dispatch);

 /**
  * Run a JavaScript function in the debugger.
  * \param fun the function to call
  * \param data passed as second argument to the function
  * With this call the debugger is entered and the function specified is called
  * with the execution state as the first argument. This makes it possible to
  * get access to information otherwise not available during normal JavaScript
  * execution e.g. details on stack frames. The following example show a
  * JavaScript function which when passed to v8::Debug::Call will return the
  * current line of JavaScript execution.
  *
  * \code
  *   function frame_source_line(exec_state) {
  *     return exec_state.frame(0).sourceLine();
  *   }
  * \endcode
  */
  static Handle<Value> Call(v8::Handle<v8::Function> fun,
                            Handle<Value> data = Handle<Value>());

 /**
  * Enable the V8 builtin debug agent. The debugger agent will listen on the
  * supplied TCP/IP port for remote debugger connection.
  * \param name the name of the embedding application
  * \param port the TCP/IP port to listen on
  */
  static bool EnableAgent(const char* name, int port);
};


}  // namespace v8


#undef EXPORT


#endif  // V8_DEBUG_H_
