// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_LIST_FORMAT_H_
#define V8_OBJECTS_JS_LIST_FORMAT_H_

#include "src/heap/factory.h"
#include "src/isolate.h"
#include "src/objects.h"
#include "unicode/uversion.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace U_ICU_NAMESPACE {
class ListFormatter;
}

namespace v8 {
namespace internal {

class JSListFormat : public JSObject {
 public:
  // Initializes relative time format object with properties derived from input
  // locales and options.
  static MaybeHandle<JSListFormat> InitializeListFormat(
      Isolate* isolate, Handle<JSListFormat> list_format_holder,
      Handle<Object> locales, Handle<Object> options);

  static Handle<JSObject> ResolvedOptions(Isolate* isolate,
                                          Handle<JSListFormat> format_holder);

  // Unpacks formatter object from corresponding JavaScript object.
  static icu::ListFormatter* UnpackFormatter(
      Isolate* isolate, Handle<JSListFormat> list_format_holder);

  Handle<String> StyleAsString() const;
  Handle<String> TypeAsString() const;

  DECL_CAST(JSListFormat)

  // ListFormat accessors.
  DECL_ACCESSORS(locale, String)

  // TODO(ftang): Style requires only 2 bits and Type requires only 2 bits
  // but here we're using 64 bits for each. We should fold these two fields into
  // a single Flags field and use BIT_FIELD_ACCESSORS to access it.
  //
  // Style: identifying the relative time format style used.
  //
  // ecma402/#sec-properties-of-intl-listformat-instances
  enum class Style {
    LONG,    // Everything spelled out.
    SHORT,   // Abbreviations used when possible.
    NARROW,  // Use the shortest possible form.
    COUNT
  };
  inline void set_style(Style style);
  inline Style style() const;

  // Type: identifying the list of types used.
  //
  // ecma402/#sec-properties-of-intl-listformat-instances
  enum class Type {
    CONJUNCTION,  // for "and"-based lists (e.g., "A, B and C")
    DISJUNCTION,  // for "or"-based lists (e.g., "A, B or C"),
    UNIT,  // for lists of values with units (e.g., "5 pounds, 12 ounces").
    COUNT
  };
  inline void set_type(Type type);
  inline Type type() const;

  DECL_ACCESSORS(formatter, Foreign)
  DECL_PRINTER(JSListFormat)
  DECL_VERIFIER(JSListFormat)

  // Layout description.
  static const int kJSListFormatOffset = JSObject::kHeaderSize;
  static const int kLocaleOffset = kJSListFormatOffset + kPointerSize;
  static const int kStyleOffset = kLocaleOffset + kPointerSize;
  static const int kTypeOffset = kStyleOffset + kPointerSize;
  static const int kFormatterOffset = kTypeOffset + kPointerSize;
  static const int kSize = kFormatterOffset + kPointerSize;

  // Constant to access field
  static const int kFormatterField = 3;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSListFormat);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_LIST_FORMAT_H_
