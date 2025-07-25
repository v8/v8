// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_DISPLAY_NAMES_H_
#define V8_OBJECTS_JS_DISPLAY_NAMES_H_

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include <set>
#include <string>

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/managed.h"
#include "src/objects/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class DisplayNamesInternal;

#include "torque-generated/src/objects/js-display-names-tq.inc"

class JSDisplayNames
    : public TorqueGeneratedJSDisplayNames<JSDisplayNames, JSObject> {
 public:
  // Creates display names object with properties derived from input
  // locales and options.
  static MaybeDirectHandle<JSDisplayNames> New(Isolate* isolate,
                                               DirectHandle<Map> map,
                                               DirectHandle<Object> locales,
                                               DirectHandle<Object> options,
                                               const char* method_name);

  static DirectHandle<JSObject> ResolvedOptions(
      Isolate* isolate, DirectHandle<JSDisplayNames> format_holder);

  static MaybeDirectHandle<Object> Of(Isolate* isolate,
                                      DirectHandle<JSDisplayNames> holder,
                                      Handle<Object> code_obj);

  V8_EXPORT_PRIVATE static const std::set<std::string>& GetAvailableLocales();

  Handle<String> StyleAsString(Isolate* isolate) const;
  Handle<String> FallbackAsString(Isolate* isolate) const;
  DirectHandle<String> LanguageDisplayAsString(Isolate* isolate) const;

  // Style: identifying the display names style used.
  //
  // ecma402/#sec-properties-of-intl-displaynames-instances
  enum class Style {
    kLong,   // Everything spelled out.
    kShort,  // Abbreviations used when possible.
    kNarrow  // Use the shortest possible form.
  };
  inline void set_style(Style style);
  inline Style style() const;

  // Type: identifying the fallback of the display names.
  //
  // ecma402/#sec-properties-of-intl-displaynames-instances
  enum class Fallback {
    kCode,
    kNone,
  };
  inline void set_fallback(Fallback fallback);
  inline Fallback fallback() const;

  enum class LanguageDisplay {
    kDialect,
    kStandard,
  };
  inline void set_language_display(LanguageDisplay language_display);
  inline LanguageDisplay language_display() const;

  // Bit positions in |flags|.
  DEFINE_TORQUE_GENERATED_JS_DISPLAY_NAMES_FLAGS()

  static_assert(StyleBits::is_valid(Style::kLong));
  static_assert(StyleBits::is_valid(Style::kShort));
  static_assert(StyleBits::is_valid(Style::kNarrow));
  static_assert(FallbackBit::is_valid(Fallback::kCode));
  static_assert(FallbackBit::is_valid(Fallback::kNone));
  static_assert(LanguageDisplayBit::is_valid(LanguageDisplay::kDialect));
  static_assert(LanguageDisplayBit::is_valid(LanguageDisplay::kStandard));

  DECL_ACCESSORS(internal, Tagged<Managed<DisplayNamesInternal>>)

  DECL_PRINTER(JSDisplayNames)

  TQ_OBJECT_CONSTRUCTORS(JSDisplayNames)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DISPLAY_NAMES_H_
