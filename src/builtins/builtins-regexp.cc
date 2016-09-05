// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/builtins/builtins-utils.h"

#include "src/string-builder.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 21.2 RegExp Objects

namespace {

// ES#sec-isregexp IsRegExp ( argument )
Maybe<bool> IsRegExp(Isolate* isolate, Handle<Object> object) {
  if (!object->IsJSReceiver()) return Just(false);

  Handle<JSReceiver> receiver = Handle<JSReceiver>::cast(object);

  Handle<Object> match;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, match,
      JSObject::GetProperty(receiver, isolate->factory()->match_symbol()),
      Nothing<bool>());

  if (!match->IsUndefined(isolate)) return Just(match->BooleanValue());
  return Just(object->IsJSRegExp());
}

Handle<String> PatternFlags(Isolate* isolate, Handle<JSRegExp> regexp) {
  IncrementalStringBuilder builder(isolate);
  const JSRegExp::Flags flags = regexp->GetFlags();

  if ((flags & JSRegExp::kGlobal) != 0) builder.AppendCharacter('g');
  if ((flags & JSRegExp::kIgnoreCase) != 0) builder.AppendCharacter('i');
  if ((flags & JSRegExp::kMultiline) != 0) builder.AppendCharacter('m');
  if ((flags & JSRegExp::kUnicode) != 0) builder.AppendCharacter('u');
  if ((flags & JSRegExp::kSticky) != 0) builder.AppendCharacter('y');

  return builder.Finish().ToHandleChecked();
}

// ES#sec-regexpinitialize
// Runtime Semantics: RegExpInitialize ( obj, pattern, flags )
MaybeHandle<JSRegExp> RegExpInitialize(Isolate* isolate,
                                       Handle<JSRegExp> regexp,
                                       Handle<Object> pattern,
                                       Handle<Object> flags) {
  Handle<String> pattern_string;
  if (pattern->IsUndefined(isolate)) {
    pattern_string = isolate->factory()->empty_string();
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, pattern_string,
                               Object::ToString(isolate, pattern), JSRegExp);
  }

  Handle<String> flags_string;
  if (flags->IsUndefined(isolate)) {
    flags_string = isolate->factory()->empty_string();
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, flags_string,
                               Object::ToString(isolate, flags), JSRegExp);
  }

  // TODO(jgruber): We could avoid the flags back and forth conversions.
  RETURN_RESULT(isolate,
                JSRegExp::Initialize(regexp, pattern_string, flags_string),
                JSRegExp);
}

}  // namespace

// ES#sec-regexp-pattern-flags
// RegExp ( pattern, flags )
BUILTIN(RegExpConstructor) {
  HandleScope scope(isolate);

  Handle<HeapObject> new_target = args.new_target();
  Handle<Object> pattern = args.atOrUndefined(isolate, 1);
  Handle<Object> flags = args.atOrUndefined(isolate, 2);

  Handle<JSFunction> target =
      handle(isolate->native_context()->regexp_function(), isolate);

  bool pattern_is_regexp;
  {
    Maybe<bool> maybe_pattern_is_regexp = IsRegExp(isolate, pattern);
    if (maybe_pattern_is_regexp.IsNothing()) {
      DCHECK(isolate->has_pending_exception());
      return isolate->heap()->exception();
    }
    pattern_is_regexp = maybe_pattern_is_regexp.FromJust();
  }

  if (new_target->IsUndefined(isolate)) {
    new_target = target;

    // ES6 section 21.2.3.1 step 3.b
    if (pattern_is_regexp && flags->IsUndefined(isolate)) {
      Handle<Object> pattern_constructor;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, pattern_constructor,
          Object::GetProperty(pattern,
                              isolate->factory()->constructor_string()));

      if (*pattern_constructor == *new_target) {
        return *pattern;
      }
    }
  } else if (!new_target->IsJSReceiver()) {
    // TODO(jgruber): Better error message.
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledNonCallable, new_target));
  }
  Handle<JSReceiver> new_target_receiver = Handle<JSReceiver>::cast(new_target);

  if (pattern->IsJSRegExp()) {
    Handle<JSRegExp> regexp_pattern = Handle<JSRegExp>::cast(pattern);

    if (flags->IsUndefined(isolate)) {
      flags = PatternFlags(isolate, regexp_pattern);
    }
    pattern = handle(regexp_pattern->source(), isolate);
  } else if (pattern_is_regexp) {
    Handle<Object> pattern_source;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, pattern_source,
        Object::GetProperty(pattern, isolate->factory()->source_string()));

    if (flags->IsUndefined(isolate)) {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, flags,
          Object::GetProperty(pattern, isolate->factory()->flags_string()));
    }
    pattern = pattern_source;
  }

  Handle<JSObject> object;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, object, JSObject::New(target, new_target_receiver));
  Handle<JSRegExp> regexp = Handle<JSRegExp>::cast(object);

  RETURN_RESULT_OR_FAILURE(isolate,
                           RegExpInitialize(isolate, regexp, pattern, flags));
}

BUILTIN(RegExpPrototypeCompile) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSRegExp, regexp, "RegExp.prototype.compile");

  Handle<Object> pattern = args.atOrUndefined(isolate, 1);
  Handle<Object> flags = args.atOrUndefined(isolate, 2);

  if (pattern->IsJSRegExp()) {
    Handle<JSRegExp> pattern_regexp = Handle<JSRegExp>::cast(pattern);

    if (!flags->IsUndefined(isolate)) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewTypeError(MessageTemplate::kRegExpFlags));
    }

    flags = PatternFlags(isolate, pattern_regexp);
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, pattern,
        Object::GetProperty(pattern, isolate->factory()->source_string()));
  }

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, regexp, RegExpInitialize(isolate, regexp, pattern, flags));

  // Return undefined for compatibility with JSC.
  // See http://crbug.com/585775 for web compat details.

  return isolate->heap()->undefined_value();
}

#define APPEND_CHAR_FOR_FLAG(flag, c)                                        \
  do {                                                                       \
    Handle<Object> property;                                                 \
    Handle<Name> name = isolate->factory()->flag##_string();                 \
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, property,                    \
                                       JSReceiver::GetProperty(recv, name)); \
    if (property->BooleanValue()) {                                          \
      builder.AppendCharacter(c);                                            \
    }                                                                        \
  } while (false);

// ES6 21.2.5.3.
BUILTIN(RegExpPrototypeFlagsGetter) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSReceiver, recv, "get RegExp.prototype.flags");

  IncrementalStringBuilder builder(isolate);

  APPEND_CHAR_FOR_FLAG(global, 'g');
  APPEND_CHAR_FOR_FLAG(ignoreCase, 'i');
  APPEND_CHAR_FOR_FLAG(multiline, 'm');
  APPEND_CHAR_FOR_FLAG(unicode, 'u');
  APPEND_CHAR_FOR_FLAG(sticky, 'y');

  RETURN_RESULT_OR_FAILURE(isolate, builder.Finish());
}

#undef APPEND_CHAR_FOR_FLAG

// ES6 21.2.5.10.
BUILTIN(RegExpPrototypeSourceGetter) {
  HandleScope scope(isolate);

  Handle<Object> recv = args.receiver();
  if (!recv->IsJSRegExp()) {
    // TODO(littledan): Remove this RegExp compat workaround
    Handle<JSFunction> regexp_fun = isolate->regexp_function();
    if (*recv == regexp_fun->prototype()) {
      isolate->CountUsage(v8::Isolate::kRegExpPrototypeSourceGetter);
      return *isolate->factory()->NewStringFromAsciiChecked("(?:)");
    }
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kRegExpNonRegExp,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "RegExp.prototype.source")));
  }

  Handle<JSRegExp> regexp = Handle<JSRegExp>::cast(recv);
  return regexp->source();
}

BUILTIN(RegExpPrototypeToString) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSReceiver, recv, "RegExp.prototype.toString");

  if (*recv == isolate->regexp_function()->prototype()) {
    isolate->CountUsage(v8::Isolate::kRegExpPrototypeToString);
  }

  IncrementalStringBuilder builder(isolate);

  builder.AppendCharacter('/');
  {
    Handle<Object> source;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, source,
        JSReceiver::GetProperty(recv, isolate->factory()->source_string()));
    Handle<String> source_str;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, source_str,
                                       Object::ToString(isolate, source));
    builder.AppendString(source_str);
  }

  builder.AppendCharacter('/');
  {
    Handle<Object> flags;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, flags,
        JSReceiver::GetProperty(recv, isolate->factory()->flags_string()));
    Handle<String> flags_str;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, flags_str,
                                       Object::ToString(isolate, flags));
    builder.AppendString(flags_str);
  }

  RETURN_RESULT_OR_FAILURE(isolate, builder.Finish());
}

// ES6 21.2.4.2.
BUILTIN(RegExpPrototypeSpeciesGetter) {
  HandleScope scope(isolate);
  return *args.receiver();
}

#define REGEXP_FLAG_GETTER(name, counter, getter)                              \
  BUILTIN(RegExpPrototype##name##Getter) {                                     \
    HandleScope scope(isolate);                                                \
    Handle<Object> recv = args.receiver();                                     \
    if (!recv->IsJSRegExp()) {                                                 \
      /* TODO(littledan): Remove this RegExp compat workaround */              \
      Handle<JSFunction> regexp_fun = isolate->regexp_function();              \
      if (*recv == regexp_fun->prototype()) {                                  \
        isolate->CountUsage(v8::Isolate::kRegExpPrototype##counter##Getter);   \
        return isolate->heap()->undefined_value();                             \
      }                                                                        \
      THROW_NEW_ERROR_RETURN_FAILURE(                                          \
          isolate, NewTypeError(MessageTemplate::kRegExpNonRegExp,             \
                                isolate->factory()->NewStringFromAsciiChecked( \
                                    getter)));                                 \
    }                                                                          \
    Handle<JSRegExp> regexp = Handle<JSRegExp>::cast(recv);                    \
    const bool ret = (regexp->GetFlags() & JSRegExp::k##name) != 0;            \
    return *isolate->factory()->ToBoolean(ret);                                \
  }

// ES6 21.2.5.4.
REGEXP_FLAG_GETTER(Global, OldFlag, "RegExp.prototype.global")

// ES6 21.2.5.5.
REGEXP_FLAG_GETTER(IgnoreCase, OldFlag, "RegExp.prototype.ignoreCase")

// ES6 21.2.5.7.
REGEXP_FLAG_GETTER(Multiline, OldFlag, "RegExp.prototype.multiline")

// ES6 21.2.5.12.
REGEXP_FLAG_GETTER(Sticky, Sticky, "RegExp.prototype.sticky")

// ES6 21.2.5.15.
REGEXP_FLAG_GETTER(Unicode, Unicode, "RegExp.prototype.unicode")

#undef REGEXP_FLAG_GETTER

namespace {

// Constants for accessing RegExpLastMatchInfo.
// TODO(jgruber): Currently, RegExpLastMatchInfo is still a JSObject maintained
// and accessed from JS. This is a crutch until all RegExp logic is ported, then
// we can take care of RegExpLastMatchInfo.
const int kNumberOfCapturesIndex = 0;
const int kLastSubjectIndex = 1;
const int kLastInputIndex = 2;
const int kFirstCaptureIndex = 3;

Handle<Object> GetLastMatchField(Isolate* isolate, int index) {
  Handle<JSFunction> global_regexp = isolate->regexp_function();
  Handle<Object> last_match_info_obj = JSReceiver::GetDataProperty(
      global_regexp, isolate->factory()->regexp_last_match_info_symbol());

  Handle<JSReceiver> last_match_info =
      Handle<JSReceiver>::cast(last_match_info_obj);
  return JSReceiver::GetElement(isolate, last_match_info, index)
      .ToHandleChecked();
}

void SetLastMatchField(Isolate* isolate, int index, Handle<Object> value) {
  Handle<JSFunction> global_regexp = isolate->regexp_function();
  Handle<Object> last_match_info_obj = JSReceiver::GetDataProperty(
      global_regexp, isolate->factory()->regexp_last_match_info_symbol());

  Handle<JSReceiver> last_match_info =
      Handle<JSReceiver>::cast(last_match_info_obj);
  JSReceiver::SetElement(isolate, last_match_info, index, value, SLOPPY)
      .ToHandleChecked();
}

int GetLastMatchNumberOfCaptures(Isolate* isolate) {
  Handle<Object> obj = GetLastMatchField(isolate, kNumberOfCapturesIndex);
  return Handle<Smi>::cast(obj)->value();
}

Handle<String> GetLastMatchSubject(Isolate* isolate) {
  return Handle<String>::cast(GetLastMatchField(isolate, kLastSubjectIndex));
}

Handle<Object> GetLastMatchInput(Isolate* isolate) {
  return GetLastMatchField(isolate, kLastInputIndex);
}

int GetLastMatchCapture(Isolate* isolate, int i) {
  Handle<Object> obj = GetLastMatchField(isolate, kFirstCaptureIndex + i);
  return Handle<Smi>::cast(obj)->value();
}

Object* GenericCaptureGetter(Isolate* isolate, int capture) {
  HandleScope scope(isolate);
  const int index = capture * 2;
  if (index >= GetLastMatchNumberOfCaptures(isolate)) {
    return isolate->heap()->empty_string();
  }

  const int match_start = GetLastMatchCapture(isolate, index);
  const int match_end = GetLastMatchCapture(isolate, index + 1);
  if (match_start == -1 || match_end == -1) {
    return isolate->heap()->empty_string();
  }

  Handle<String> last_subject = GetLastMatchSubject(isolate);
  return *isolate->factory()->NewSubString(last_subject, match_start,
                                           match_end);
}

}  // namespace

// The properties $1..$9 are the first nine capturing substrings of the last
// successful match, or ''.  The function RegExpMakeCaptureGetter will be
// called with indices from 1 to 9.
#define DEFINE_CAPTURE_GETTER(i)               \
  BUILTIN(RegExpPrototypeCapture##i##Getter) { \
    HandleScope scope(isolate);                \
    return GenericCaptureGetter(isolate, i);   \
  }
DEFINE_CAPTURE_GETTER(1)
DEFINE_CAPTURE_GETTER(2)
DEFINE_CAPTURE_GETTER(3)
DEFINE_CAPTURE_GETTER(4)
DEFINE_CAPTURE_GETTER(5)
DEFINE_CAPTURE_GETTER(6)
DEFINE_CAPTURE_GETTER(7)
DEFINE_CAPTURE_GETTER(8)
DEFINE_CAPTURE_GETTER(9)
#undef DEFINE_CAPTURE_GETTER

// The properties `input` and `$_` are aliases for each other.  When this
// value is set the value it is set to is coerced to a string.
// Getter and setter for the input.

BUILTIN(RegExpPrototypeInputGetter) {
  HandleScope scope(isolate);
  Handle<Object> obj = GetLastMatchInput(isolate);
  return obj->IsUndefined(isolate) ? isolate->heap()->empty_string()
                                   : String::cast(*obj);
}

BUILTIN(RegExpPrototypeInputSetter) {
  HandleScope scope(isolate);
  Handle<Object> value = args.atOrUndefined(isolate, 1);
  Handle<String> str;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, str,
                                     Object::ToString(isolate, value));
  SetLastMatchField(isolate, kLastInputIndex, str);
  return isolate->heap()->undefined_value();
}

// Getters for the static properties lastMatch, lastParen, leftContext, and
// rightContext of the RegExp constructor.  The properties are computed based
// on the captures array of the last successful match and the subject string
// of the last successful match.
BUILTIN(RegExpPrototypeLastMatchGetter) {
  HandleScope scope(isolate);
  return GenericCaptureGetter(isolate, 0);
}

BUILTIN(RegExpPrototypeLastParenGetter) {
  HandleScope scope(isolate);
  const int length = GetLastMatchNumberOfCaptures(isolate);
  if (length <= 2) return isolate->heap()->empty_string();  // No captures.

  DCHECK_EQ(0, length % 2);
  const int last_capture = (length / 2) - 1;

  // We match the SpiderMonkey behavior: return the substring defined by the
  // last pair (after the first pair) of elements of the capture array even if
  // it is empty.
  return GenericCaptureGetter(isolate, last_capture);
}

BUILTIN(RegExpPrototypeLeftContextGetter) {
  HandleScope scope(isolate);
  const int start_index = GetLastMatchCapture(isolate, 0);
  Handle<String> last_subject = GetLastMatchSubject(isolate);
  return *isolate->factory()->NewSubString(last_subject, 0, start_index);
}

BUILTIN(RegExpPrototypeRightContextGetter) {
  HandleScope scope(isolate);
  const int start_index = GetLastMatchCapture(isolate, 1);
  Handle<String> last_subject = GetLastMatchSubject(isolate);
  const int len = last_subject->length();
  return *isolate->factory()->NewSubString(last_subject, start_index, len);
}

}  // namespace internal
}  // namespace v8
