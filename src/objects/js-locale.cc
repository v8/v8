// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-locale.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/api.h"
#include "src/global-handles.h"
#include "src/heap/factory.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-locale-inl.h"
#include "unicode/char16ptr.h"
#include "unicode/locid.h"
#include "unicode/uloc.h"
#include "unicode/unistr.h"

namespace v8 {
namespace internal {

namespace {

// Helper function to check a language tag is valid. It will return false if
// the parsing is not the same as the tag. For example, it will return false if
// the tag is too long.
bool IsValidLanguageTag(const char* tag, int length) {
  // icu::Locale::forLanguageTag won't return U_STRING_NOT_TERMINATED_WARNING
  // for incorrect locale yet. So we still need the following
  // uloc_forLanguageTag
  // TODO(ftang): Remove once icu::Locale::forLanguageTag indicate error.
  char result[ULOC_FULLNAME_CAPACITY];
  UErrorCode status = U_ZERO_ERROR;
  int parsed_length = 0;
  int icu_length = uloc_forLanguageTag(tag, result, ULOC_FULLNAME_CAPACITY,
                                       &parsed_length, &status);
  return U_SUCCESS(status) && parsed_length == length &&
         status != U_STRING_NOT_TERMINATED_WARNING && icu_length != 0;
}

// Helper function to check a locale is valid. It will return false if
// the length of the extension fields are incorrect. For example, en-u-a or
// en-u-co-b will return false.
bool IsValidLocale(const icu::Locale& locale) {
  // icu::Locale::toLanguageTag won't return U_STRING_NOT_TERMINATED_WARNING for
  // incorrect locale yet. So we still need the following uloc_toLanguageTag
  // TODO(ftang): Change to use icu::Locale::toLanguageTag once it indicate
  // error.
  char result[ULOC_FULLNAME_CAPACITY];
  UErrorCode status = U_ZERO_ERROR;
  uloc_toLanguageTag(locale.getName(), result, ULOC_FULLNAME_CAPACITY, true,
                     &status);
  return U_SUCCESS(status) && status != U_STRING_NOT_TERMINATED_WARNING;
}

struct OptionData {
  const char* name;
  const char* key;
  const std::vector<const char*>* possible_values;
  bool is_bool_value;
};

// Inserts tags from options into locale string.
Maybe<bool> InsertOptionsIntoLocale(Isolate* isolate,
                                    Handle<JSReceiver> options,
                                    icu::Locale* icu_locale) {
  CHECK(isolate);
  CHECK(!icu_locale->isBogus());

  const std::vector<const char*> hour_cycle_values = {"h11", "h12", "h23",
                                                      "h24"};
  const std::vector<const char*> case_first_values = {"upper", "lower",
                                                      "false"};
  const std::vector<const char*> empty_values = {};
  const std::array<OptionData, 6> kOptionToUnicodeTagMap = {
      {{"calendar", "ca", &empty_values, false},
       {"collation", "co", &empty_values, false},
       {"hourCycle", "hc", &hour_cycle_values, false},
       {"caseFirst", "kf", &case_first_values, false},
       {"numeric", "kn", &empty_values, true},
       {"numberingSystem", "nu", &empty_values, false}}};

  // TODO(cira): Pass in values as per the spec to make this to be
  // spec compliant.

  UErrorCode status = U_ZERO_ERROR;
  for (const auto& option_to_bcp47 : kOptionToUnicodeTagMap) {
    std::unique_ptr<char[]> value_str = nullptr;
    bool value_bool = false;
    Maybe<bool> maybe_found =
        option_to_bcp47.is_bool_value
            ? Intl::GetBoolOption(isolate, options, option_to_bcp47.name,
                                  "locale", &value_bool)
            : Intl::GetStringOption(isolate, options, option_to_bcp47.name,
                                    *(option_to_bcp47.possible_values),
                                    "locale", &value_str);
    MAYBE_RETURN(maybe_found, Nothing<bool>());

    // TODO(cira): Use fallback value if value is not found to make
    // this spec compliant.
    if (!maybe_found.FromJust()) continue;

    if (option_to_bcp47.is_bool_value) {
      value_str = value_bool ? isolate->factory()->true_string()->ToCString()
                             : isolate->factory()->false_string()->ToCString();
    }
    DCHECK_NOT_NULL(value_str.get());

    // Convert bcp47 key and value into legacy ICU format so we can use
    // uloc_setKeywordValue.
    const char* key = uloc_toLegacyKey(option_to_bcp47.key);
    DCHECK_NOT_NULL(key);

    // Overwrite existing, or insert new key-value to the locale string.
    const char* value = uloc_toLegacyType(key, value_str.get());
    if (value) {
      icu_locale->setKeywordValue(key, value, status);
      if (U_FAILURE(status)) {
        return Just(false);
      }
    } else {
      return Just(false);
    }
  }

  // Check all the unicode extension fields are in the right length.
  if (!IsValidLocale(*icu_locale)) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NewRangeError(MessageTemplate::kLocaleBadParameters),
        Nothing<bool>());
  }

  return Just(true);
}

Handle<Object> UnicodeKeywordValue(Isolate* isolate, Handle<JSLocale> locale,
                                   const char* key) {
  icu::Locale* icu_locale = locale->icu_locale()->raw();
  UErrorCode status = U_ZERO_ERROR;
  std::string value =
      icu_locale->getUnicodeKeywordValue<std::string>(key, status);
  CHECK(U_SUCCESS(status));
  if (value == "") {
    return isolate->factory()->undefined_value();
  }
  return isolate->factory()->NewStringFromAsciiChecked(value.c_str());
}

}  // namespace

MaybeHandle<JSLocale> JSLocale::Initialize(Isolate* isolate,
                                           Handle<JSLocale> locale,
                                           Handle<String> locale_str,
                                           Handle<JSReceiver> options) {
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  UErrorCode status = U_ZERO_ERROR;

  if (locale_str->length() == 0) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kLocaleNotEmpty),
                    JSLocale);
  }

  v8::String::Utf8Value bcp47_locale(v8_isolate,
                                     v8::Utils::ToLocal(locale_str));
  CHECK_LT(0, bcp47_locale.length());
  CHECK_NOT_NULL(*bcp47_locale);

  if (!IsValidLanguageTag(*bcp47_locale, bcp47_locale.length())) {
    THROW_NEW_ERROR(isolate,
                    NewRangeError(MessageTemplate::kLocaleBadParameters),
                    JSLocale);
  }

  status = U_ZERO_ERROR;
  icu::Locale icu_locale = icu::Locale::forLanguageTag(*bcp47_locale, status);
  if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
    THROW_NEW_ERROR(isolate,
                    NewRangeError(MessageTemplate::kLocaleBadParameters),
                    JSLocale);
  }

  Maybe<bool> error = InsertOptionsIntoLocale(isolate, options, &icu_locale);
  MAYBE_RETURN(error, MaybeHandle<JSLocale>());
  if (!error.FromJust()) {
    THROW_NEW_ERROR(isolate,
                    NewRangeError(MessageTemplate::kLocaleBadParameters),
                    JSLocale);
  }

  // 31. Set locale.[[Locale]] to r.[[locale]].
  Handle<Managed<icu::Locale>> managed_locale =
      Managed<icu::Locale>::FromRawPtr(isolate, 0, icu_locale.clone());
  locale->set_icu_locale(*managed_locale);

  return locale;
}

namespace {
Handle<String> MorphLocale(Isolate* isolate, String locale,
                           void (*morph_func)(icu::Locale*, UErrorCode*)) {
  UErrorCode status = U_ZERO_ERROR;
  icu::Locale icu_locale =
      icu::Locale::forLanguageTag(locale.ToCString().get(), status);
  CHECK(U_SUCCESS(status));
  CHECK(!icu_locale.isBogus());
  (*morph_func)(&icu_locale, &status);
  CHECK(U_SUCCESS(status));
  CHECK(!icu_locale.isBogus());
  std::string locale_str = Intl::ToLanguageTag(icu_locale);
  return isolate->factory()->NewStringFromAsciiChecked(locale_str.c_str());
}

}  // namespace

Handle<String> JSLocale::Maximize(Isolate* isolate, String locale) {
  return MorphLocale(isolate, locale,
                     [](icu::Locale* icu_locale, UErrorCode* status) {
                       icu_locale->addLikelySubtags(*status);
                     });
}

Handle<String> JSLocale::Minimize(Isolate* isolate, String locale) {
  return MorphLocale(isolate, locale,
                     [](icu::Locale* icu_locale, UErrorCode* status) {
                       icu_locale->minimizeSubtags(*status);
                     });
}

Handle<Object> JSLocale::Language(Isolate* isolate, Handle<JSLocale> locale) {
  Factory* factory = isolate->factory();
  const char* language = locale->icu_locale()->raw()->getLanguage();
  if (strlen(language) == 0) return factory->undefined_value();
  return factory->NewStringFromAsciiChecked(language);
}

Handle<Object> JSLocale::Script(Isolate* isolate, Handle<JSLocale> locale) {
  Factory* factory = isolate->factory();
  const char* script = locale->icu_locale()->raw()->getScript();
  if (strlen(script) == 0) return factory->undefined_value();
  return factory->NewStringFromAsciiChecked(script);
}

Handle<Object> JSLocale::Region(Isolate* isolate, Handle<JSLocale> locale) {
  Factory* factory = isolate->factory();
  const char* region = locale->icu_locale()->raw()->getCountry();
  if (strlen(region) == 0) return factory->undefined_value();
  return factory->NewStringFromAsciiChecked(region);
}

Handle<String> JSLocale::BaseName(Isolate* isolate, Handle<JSLocale> locale) {
  icu::Locale icu_locale =
      icu::Locale::createFromName(locale->icu_locale()->raw()->getBaseName());
  std::string base_name = Intl::ToLanguageTag(icu_locale);
  return isolate->factory()->NewStringFromAsciiChecked(base_name.c_str());
}

Handle<Object> JSLocale::Calendar(Isolate* isolate, Handle<JSLocale> locale) {
  return UnicodeKeywordValue(isolate, locale, "ca");
}

Handle<Object> JSLocale::CaseFirst(Isolate* isolate, Handle<JSLocale> locale) {
  return UnicodeKeywordValue(isolate, locale, "kf");
}

Handle<Object> JSLocale::Collation(Isolate* isolate, Handle<JSLocale> locale) {
  return UnicodeKeywordValue(isolate, locale, "co");
}

Handle<Object> JSLocale::HourCycle(Isolate* isolate, Handle<JSLocale> locale) {
  return UnicodeKeywordValue(isolate, locale, "hc");
}

Handle<Object> JSLocale::Numeric(Isolate* isolate, Handle<JSLocale> locale) {
  Factory* factory = isolate->factory();
  icu::Locale* icu_locale = locale->icu_locale()->raw();
  UErrorCode status = U_ZERO_ERROR;
  std::string numeric =
      icu_locale->getUnicodeKeywordValue<std::string>("kn", status);
  return (numeric == "true") ? factory->true_value() : factory->false_value();
}

Handle<Object> JSLocale::NumberingSystem(Isolate* isolate,
                                         Handle<JSLocale> locale) {
  return UnicodeKeywordValue(isolate, locale, "nu");
}

Handle<String> JSLocale::ToString(Isolate* isolate, Handle<JSLocale> locale) {
  icu::Locale* icu_locale = locale->icu_locale()->raw();
  std::string locale_str = Intl::ToLanguageTag(*icu_locale);
  return isolate->factory()->NewStringFromAsciiChecked(locale_str.c_str());
}

}  // namespace internal
}  // namespace v8
