// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-list-format.h"

#include <memory>
#include <vector>

#include "src/heap/factory.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-list-format-inl.h"
#include "src/objects/managed.h"
#include "unicode/listformatter.h"

namespace v8 {
namespace internal {

namespace {
const char* kStandard = "standard";
const char* kOr = "or";
const char* kUnit = "unit";
const char* kStandardShort = "standard-short";
const char* kUnitShort = "unit-short";
const char* kUnitNarrow = "unit-narrow";

const char* GetIcuStyleString(JSListFormat::Style style,
                              JSListFormat::Type type) {
  switch (type) {
    case JSListFormat::Type::CONJUNCTION:
      switch (style) {
        case JSListFormat::Style::LONG:
          return kStandard;
        case JSListFormat::Style::SHORT:
          return kStandardShort;
        case JSListFormat::Style::NARROW:
          // Currently, ListFormat::createInstance on "standard-narrow" will
          // fail so we use "standard-short" here.
          // See https://unicode.org/cldr/trac/ticket/11254
          // TODO(ftang): change to return kStandardNarrow; after the above
          // issue fixed in CLDR/ICU.
          // CLDR bug: https://unicode.org/cldr/trac/ticket/11254
          // ICU bug: https://unicode-org.atlassian.net/browse/ICU-20014
          return kStandardShort;
        case JSListFormat::Style::COUNT:
          UNREACHABLE();
      }
    case JSListFormat::Type::DISJUNCTION:
      switch (style) {
        // Currently, ListFormat::createInstance on "or-short" and "or-narrow"
        // will fail so we use "or" here.
        // See https://unicode.org/cldr/trac/ticket/11254
        // TODO(ftang): change to return kOr, kOrShort or kOrNarrow depend on
        // style after the above issue fixed in CLDR/ICU.
        // CLDR bug: https://unicode.org/cldr/trac/ticket/11254
        // ICU bug: https://unicode-org.atlassian.net/browse/ICU-20014
        case JSListFormat::Style::LONG:
        case JSListFormat::Style::SHORT:
        case JSListFormat::Style::NARROW:
          return kOr;
        case JSListFormat::Style::COUNT:
          UNREACHABLE();
      }
    case JSListFormat::Type::UNIT:
      switch (style) {
        case JSListFormat::Style::LONG:
          return kUnit;
        case JSListFormat::Style::SHORT:
          return kUnitShort;
        case JSListFormat::Style::NARROW:
          return kUnitNarrow;
        case JSListFormat::Style::COUNT:
          UNREACHABLE();
      }
    case JSListFormat::Type::COUNT:
      UNREACHABLE();
  }
}

}  // namespace

JSListFormat::Style get_style(const char* str) {
  switch (str[0]) {
    case 'n':
      if (strcmp(&str[1], "arrow") == 0) return JSListFormat::Style::NARROW;
      break;
    case 'l':
      if (strcmp(&str[1], "ong") == 0) return JSListFormat::Style::LONG;
      break;
    case 's':
      if (strcmp(&str[1], "hort") == 0) return JSListFormat::Style::SHORT;
      break;
  }
  UNREACHABLE();
}

JSListFormat::Type get_type(const char* str) {
  switch (str[0]) {
    case 'c':
      if (strcmp(&str[1], "onjunction") == 0)
        return JSListFormat::Type::CONJUNCTION;
      break;
    case 'd':
      if (strcmp(&str[1], "isjunction") == 0)
        return JSListFormat::Type::DISJUNCTION;
      break;
    case 'u':
      if (strcmp(&str[1], "nit") == 0) return JSListFormat::Type::UNIT;
      break;
  }
  UNREACHABLE();
}

MaybeHandle<JSListFormat> JSListFormat::InitializeListFormat(
    Isolate* isolate, Handle<JSListFormat> list_format_holder,
    Handle<Object> input_locales, Handle<Object> input_options) {
  Factory* factory = isolate->factory();
  list_format_holder->set_flags(0);

  Handle<JSReceiver> options;
  // 2. If options is undefined, then
  if (input_options->IsUndefined(isolate)) {
    // a. Let options be ObjectCreate(null).
    options = isolate->factory()->NewJSObjectWithNullProto();
    // 3. Else
  } else {
    // a. Let options be ? ToObject(options).
    ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                               Object::ToObject(isolate, input_options),
                               JSListFormat);
  }

  // 5. Let t be GetOption(options, "type", "string", «"conjunction",
  //    "disjunction", "unit"», "conjunction").
  std::unique_ptr<char[]> type_str = nullptr;
  std::vector<const char*> type_values = {"conjunction", "disjunction", "unit"};
  Maybe<bool> maybe_found_type = Intl::GetStringOption(
      isolate, options, "type", type_values, "Intl.ListFormat", &type_str);
  Type type_enum = Type::CONJUNCTION;
  MAYBE_RETURN(maybe_found_type, MaybeHandle<JSListFormat>());
  if (maybe_found_type.FromJust()) {
    DCHECK_NOT_NULL(type_str.get());
    type_enum = get_type(type_str.get());
  }
  // 6. Set listFormat.[[Type]] to t.
  list_format_holder->set_type(type_enum);

  // 7. Let s be ? GetOption(options, "style", "string",
  //                          «"long", "short", "narrow"», "long").
  std::unique_ptr<char[]> style_str = nullptr;
  std::vector<const char*> style_values = {"long", "short", "narrow"};
  Maybe<bool> maybe_found_style = Intl::GetStringOption(
      isolate, options, "style", style_values, "Intl.ListFormat", &style_str);
  Style style_enum = Style::LONG;
  MAYBE_RETURN(maybe_found_style, MaybeHandle<JSListFormat>());
  if (maybe_found_style.FromJust()) {
    DCHECK_NOT_NULL(style_str.get());
    style_enum = get_style(style_str.get());
  }
  // 15. Set listFormat.[[Style]] to s.
  list_format_holder->set_style(style_enum);

  // 10. Let r be ResolveLocale(%ListFormat%.[[AvailableLocales]],
  // requestedLocales, opt, undefined, localeData).
  Handle<JSObject> r;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, r,
      Intl::ResolveLocale(isolate, "listformat", input_locales, options),
      JSListFormat);

  Handle<Object> locale_obj =
      JSObject::GetDataProperty(r, factory->locale_string());
  Handle<String> locale;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, locale, Object::ToString(isolate, locale_obj), JSListFormat);

  // 18. Set listFormat.[[Locale]] to the value of r.[[Locale]].
  list_format_holder->set_locale(*locale);

  std::unique_ptr<char[]> locale_name = locale->ToCString();
  icu::Locale icu_locale(locale_name.get());
  UErrorCode status = U_ZERO_ERROR;
  icu::ListFormatter* formatter = icu::ListFormatter::createInstance(
      icu_locale, GetIcuStyleString(style_enum, type_enum), status);
  if (U_FAILURE(status)) {
    delete formatter;
    FATAL("Failed to create ICU list formatter, are ICU data files missing?");
  }
  CHECK_NOT_NULL(formatter);

  Handle<Managed<icu::ListFormatter>> managed_formatter =
      Managed<icu::ListFormatter>::FromRawPtr(isolate, 0, formatter);

  list_format_holder->set_formatter(*managed_formatter);
  return list_format_holder;
}

Handle<JSObject> JSListFormat::ResolvedOptions(
    Isolate* isolate, Handle<JSListFormat> format_holder) {
  Factory* factory = isolate->factory();
  Handle<JSObject> result = factory->NewJSObject(isolate->object_function());
  Handle<String> locale(format_holder->locale(), isolate);
  JSObject::AddProperty(isolate, result, factory->locale_string(), locale,
                        NONE);
  JSObject::AddProperty(isolate, result, factory->style_string(),
                        format_holder->StyleAsString(), NONE);
  JSObject::AddProperty(isolate, result, factory->type_string(),
                        format_holder->TypeAsString(), NONE);
  return result;
}

icu::ListFormatter* JSListFormat::UnpackFormatter(Isolate* isolate,
                                                  Handle<JSListFormat> holder) {
  return Managed<icu::ListFormatter>::cast(holder->formatter())->raw();
}

Handle<String> JSListFormat::StyleAsString() const {
  switch (style()) {
    case Style::LONG:
      return GetReadOnlyRoots().long_string_handle();
    case Style::SHORT:
      return GetReadOnlyRoots().short_string_handle();
    case Style::NARROW:
      return GetReadOnlyRoots().narrow_string_handle();
    case Style::COUNT:
      UNREACHABLE();
  }
}

Handle<String> JSListFormat::TypeAsString() const {
  switch (type()) {
    case Type::CONJUNCTION:
      return GetReadOnlyRoots().conjunction_string_handle();
    case Type::DISJUNCTION:
      return GetReadOnlyRoots().disjunction_string_handle();
    case Type::UNIT:
      return GetReadOnlyRoots().unit_string_handle();
    case Type::COUNT:
      UNREACHABLE();
  }
}

}  // namespace internal
}  // namespace v8
