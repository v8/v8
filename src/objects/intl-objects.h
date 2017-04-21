// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_INTL_OBJECTS_H_
#define V8_OBJECTS_INTL_OBJECTS_H_

#include "src/objects.h"
#include "unicode/uversion.h"

namespace U_ICU_NAMESPACE {
class BreakIterator;
class Collator;
class DecimalFormat;
class SimpleDateFormat;
class UnicodeString;
}

namespace v8 {
namespace internal {

#define DECL_PTR_ACCESSORS(name, type) \
  inline type* name() const;           \
  inline void set_##name(type* value);

// Intl.DateTimeFormat
// ECMA-402#datetimeformat-objects
class JSIntlDateTimeFormat : public JSObject {
 public:
  DECL_PTR_ACCESSORS(simple_date_format, icu::SimpleDateFormat)

  // Constructor for Intl.DateTimeFormat(), based on the resolved locale
  // and user options. Writes resolved options into resolved.
  MUST_USE_RESULT static MaybeHandle<JSIntlDateTimeFormat> New(
      Isolate* isolate, Handle<String> locale, Handle<JSObject> options,
      Handle<JSObject> resolved);

  // Layout description.
  static const int kSimpleDateFormat = JSObject::kHeaderSize;
  static const int kSize = kSimpleDateFormat + kPointerSize;

 private:
  // Finalizer responsible for freeing the icu::SimpleDateFormat
  static void Delete(const v8::WeakCallbackInfo<void>& data);

  DISALLOW_IMPLICIT_CONSTRUCTORS(JSIntlDateTimeFormat);
};

// Intl.NumberFormat
// ECMA-402#numberformat-objects
class JSIntlNumberFormat : public JSObject {
 public:
  DECL_PTR_ACCESSORS(decimal_format, icu::DecimalFormat)

  // Constructor for Intl.NumberFormat(), based on the resolved locale
  // and user options. Writes resolved options into resolved.
  MUST_USE_RESULT static MaybeHandle<JSIntlNumberFormat> New(
      Isolate* isolate, Handle<String> locale, Handle<JSObject> options,
      Handle<JSObject> resolved);

  // Layout description.
  static const int kDecimalFormat = JSObject::kHeaderSize;
  static const int kSize = kDecimalFormat + kPointerSize;

 private:
  // Finalizer responsible for freeing the icu::DecimalFormat
  static void Delete(const v8::WeakCallbackInfo<void>& data);

  DISALLOW_IMPLICIT_CONSTRUCTORS(JSIntlNumberFormat);
};

// Intl.Collator
// ECMA-402#collator-objects
class JSIntlCollator : public JSObject {
 public:
  DECL_PTR_ACCESSORS(collator, icu::Collator)

  // Constructor for Intl.Collator(), based on the resolved locale
  // and user options. Writes resolved options into resolved.
  MUST_USE_RESULT static MaybeHandle<JSIntlCollator> New(
      Isolate* isolate, Handle<String> locale, Handle<JSObject> options,
      Handle<JSObject> resolved);

  // Layout description.
  static const int kCollator = JSObject::kHeaderSize;
  static const int kSize = kCollator + kPointerSize;

 private:
  // Finalizer responsible for freeing the icu::Collator
  static void Delete(const v8::WeakCallbackInfo<void>& data);

  DISALLOW_IMPLICIT_CONSTRUCTORS(JSIntlCollator);
};

// Intl.v8BreakIterator, Custom non-standard V8 word break binding
// TODO(littledan,jwolfe): Specify, implement and ship Intl.Segmenter,
// allowing this interface to be deprecasted and removed.
class JSIntlV8BreakIterator : public JSObject {
 public:
  DECL_PTR_ACCESSORS(break_iterator, icu::BreakIterator)
  DECL_PTR_ACCESSORS(unicode_string, icu::UnicodeString)

  // Constructor for Intl.v8BreakIterator(), based on the resolved locale
  // and user options. Writes resolved options into resolved.
  MUST_USE_RESULT static MaybeHandle<JSIntlV8BreakIterator> New(
      Isolate* isolate, Handle<String> locale, Handle<JSObject> options,
      Handle<JSObject> resolved);

  // Layout description.
  static const int kBreakIterator = JSObject::kHeaderSize;
  static const int kUnicodeString = kBreakIterator + kPointerSize;
  static const int kSize = kUnicodeString + kPointerSize;

 private:
  // Finalizer responsible for freeing the icu::BreakIterator
  // and icu::UnicodeString
  static void Delete(const v8::WeakCallbackInfo<void>& data);

  DISALLOW_IMPLICIT_CONSTRUCTORS(JSIntlV8BreakIterator);
};

#undef DECL_PTR_ACCESSORS

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_INTL_OBJECTS_H_
