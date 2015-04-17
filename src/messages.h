// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The infrastructure used for (localized) message reporting in V8.
//
// Note: there's a big unresolved issue about ownership of the data
// structures used by this framework.

#ifndef V8_MESSAGES_H_
#define V8_MESSAGES_H_

// Forward declaration of MessageLocation.
namespace v8 {
namespace internal {
class MessageLocation;
} }  // namespace v8::internal


class V8Message {
 public:
  V8Message(char* type,
            v8::internal::Handle<v8::internal::JSArray> args,
            const v8::internal::MessageLocation* loc) :
      type_(type), args_(args), loc_(loc) { }
  char* type() const { return type_; }
  v8::internal::Handle<v8::internal::JSArray> args() const { return args_; }
  const v8::internal::MessageLocation* loc() const { return loc_; }
 private:
  char* type_;
  v8::internal::Handle<v8::internal::JSArray> const args_;
  const v8::internal::MessageLocation* loc_;
};


namespace v8 {
namespace internal {

struct Language;
class SourceInfo;

class MessageLocation {
 public:
  MessageLocation(Handle<Script> script, int start_pos, int end_pos,
                  Handle<JSFunction> function = Handle<JSFunction>())
      : script_(script),
        start_pos_(start_pos),
        end_pos_(end_pos),
        function_(function) {}
  MessageLocation() : start_pos_(-1), end_pos_(-1) { }

  Handle<Script> script() const { return script_; }
  int start_pos() const { return start_pos_; }
  int end_pos() const { return end_pos_; }
  Handle<JSFunction> function() const { return function_; }

 private:
  Handle<Script> script_;
  int start_pos_;
  int end_pos_;
  Handle<JSFunction> function_;
};


// A message handler is a convenience interface for accessing the list
// of message listeners registered in an environment
class MessageHandler {
 public:
  // Returns a message object for the API to use.
  static Handle<JSMessageObject> MakeMessageObject(
      Isolate* isolate,
      const char* type,
      MessageLocation* loc,
      Vector< Handle<Object> > args,
      Handle<JSArray> stack_frames);

  // Report a formatted message (needs JS allocation).
  static void ReportMessage(Isolate* isolate,
                            MessageLocation* loc,
                            Handle<Object> message);

  static void DefaultMessageReport(Isolate* isolate,
                                   const MessageLocation* loc,
                                   Handle<Object> message_obj);
  static Handle<String> GetMessage(Isolate* isolate, Handle<Object> data);
  static SmartArrayPointer<char> GetLocalizedMessage(Isolate* isolate,
                                                     Handle<Object> data);
};


#define MESSAGE_TEMPLATES(T)                                                  \
  /* Error */                                                                 \
  T(CyclicProto, "Cyclic __proto__ value")                                    \
  /* TypeError */                                                             \
  T(ApplyNonFunction,                                                         \
    "Function.prototype.apply was called on %, which is a % and not a "       \
    "function")                                                               \
  T(CalledNonCallable, "% is not a function")                                 \
  T(CannotConvertToPrimitive, "Cannot convert object to primitive value")     \
  T(GeneratorRunning, "Generator is already running")                         \
  T(IncompatibleMethodReceiver, "Method % called on incompatible receiver %") \
  T(InstanceofFunctionExpected,                                               \
    "Expecting a function in instanceof check, but got %")                    \
  T(InstanceofNonobjectProto,                                                 \
    "Function has non-object prototype '%' in instanceof check")              \
  T(InvalidInOperatorUse, "Cannot use 'in' operator to search for '%' in %")  \
  T(NotConstructor, "% is not a constructor")                                 \
  T(PropertyNotFunction, "Property '%' of object % is not a function")        \
  T(SymbolToPrimitive,                                                        \
    "Cannot convert a Symbol wrapper object to a primitive value")            \
  T(SymbolToNumber, "Cannot convert a Symbol value to a number")              \
  T(SymbolToString, "Cannot convert a Symbol value to a string")              \
  T(UndefinedOrNullToObject, "Cannot convert undefined or null to object")    \
  T(WithExpression, "% has no properties")                                    \
  T(WrongArgs, "%: Arguments list has wrong type")                            \
  /* RangeError */                                                            \
  T(StackOverflow, "Maximum call stack size exceeded")                        \
  /* EvalError */                                                             \
  T(CodeGenFromStrings, "%")

class MessageTemplate {
 public:
  enum Template {
#define TEMPLATE(NAME, STRING) k##NAME,
    MESSAGE_TEMPLATES(TEMPLATE)
#undef TEMPLATE
        kLastMessage
  };

  static MaybeHandle<String> FormatMessage(int template_index,
                                           Handle<String> arg0,
                                           Handle<String> arg1,
                                           Handle<String> arg2);
};
} }  // namespace v8::internal

#endif  // V8_MESSAGES_H_
