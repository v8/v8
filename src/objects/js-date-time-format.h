// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_DATE_TIME_FORMAT_H_
#define V8_OBJECTS_JS_DATE_TIME_FORMAT_H_

#include "src/isolate.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace U_ICU_NAMESPACE {
class SimpleDateFormat;
}

namespace v8 {
namespace internal {

class JSDateTimeFormat : public JSObject {
 public:
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSObject> ResolvedOptions(
      Isolate* isolate, Handle<JSReceiver> date_time_holder);

  // The UnwrapDateTimeFormat abstract operation gets the underlying
  // DateTimeFormat operation for various methods which implement ECMA-402 v1
  // semantics for supporting initializing existing Intl objects.
  //
  // ecma402/#sec-unwrapdatetimeformat
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSObject> Unwrap(
      Isolate* isolate, Handle<JSReceiver> receiver, const char* method_name);

  // Convert the options to ICU DateTimePatternGenerator skeleton.
  static Maybe<std::string> OptionsToSkeleton(Isolate* isolate,
                                              Handle<JSReceiver> options);

  // Return the time zone id which match ICU's expectation of title casing
  // return empty string when error.
  static std::string CanonicalizeTimeZoneID(Isolate* isolate,
                                            const std::string& input);

  // ecma402/#sec-datetime-format-functions
  // DateTime Format Functions
  V8_WARN_UNUSED_RESULT static MaybeHandle<String> DateTimeFormat(
      Isolate* isolate, Handle<JSObject> date_time_format_holder,
      Handle<Object> date);

  // ecma-402/#sec-todatetimeoptions
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSObject> ToDateTimeOptions(
      Isolate* isolate, Handle<Object> input_options, const char* required,
      const char* defaults);

  V8_WARN_UNUSED_RESULT static MaybeHandle<String> ToLocaleDateTime(
      Isolate* isolate, Handle<Object> date, Handle<Object> locales,
      Handle<Object> options, const char* required, const char* defaults,
      const char* service);

  DECL_CAST(JSDateTimeFormat)

// Layout description.
#define JS_DATE_TIME_FORMAT_FIELDS(V) \
  /* Total size. */                   \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(JSObject::kHeaderSize,
                                JS_DATE_TIME_FORMAT_FIELDS)
#undef JS_DATE_TIME_FORMAT_FIELDS

  DECL_PRINTER(JSDateTimeFormat)
  DECL_VERIFIER(JSDateTimeFormat)

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSDateTimeFormat);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DATE_TIME_FORMAT_H_
