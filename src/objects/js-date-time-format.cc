// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-date-time-format.h"

#include <memory>
#include <string>
#include <vector>

#include "src/heap/factory.h"
#include "src/isolate.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-date-time-format-inl.h"

#include "unicode/calendar.h"
#include "unicode/smpdtfmt.h"
#include "unicode/unistr.h"

namespace v8 {
namespace internal {

namespace {

class PatternMap {
 public:
  PatternMap(std::string pattern, std::string value)
      : pattern(pattern), value(value) {}
  virtual ~PatternMap() {}
  std::string pattern;
  std::string value;
};

class PatternItem {
 public:
  PatternItem(const std::string property, std::vector<PatternMap> pairs,
              std::vector<const char*>* allowed_values)
      : property(property), pairs(pairs), allowed_values(allowed_values) {}
  virtual ~PatternItem() {}

  const std::string property;
  // It is important for the pattern in the pairs from longer one to shorter one
  // if the longer one contains substring of an shorter one.
  std::vector<PatternMap> pairs;
  std::vector<const char*>* allowed_values;
};

static const std::vector<PatternItem>& GetPatternItems() {
  static std::vector<const char*> kLongShort = {"long", "short"};
  static std::vector<const char*> kNarrowLongShort = {"narrow", "long",
                                                      "short"};
  static std::vector<const char*> k2DigitNumeric = {"2-digit", "numeric"};
  static std::vector<const char*> kNarrowLongShort2DigitNumeric = {
      "narrow", "long", "short", "2-digit", "numeric"};
  static std::vector<PatternItem> kPatternItems = {
      PatternItem("weekday",
                  {{"EEEEE", "narrow"}, {"EEEE", "long"}, {"EEE", "short"}},
                  &kNarrowLongShort),
      PatternItem("era",
                  {{"GGGGG", "narrow"}, {"GGGG", "long"}, {"GGG", "short"}},
                  &kNarrowLongShort),
      PatternItem("year", {{"yy", "2-digit"}, {"y", "numeric"}},
                  &k2DigitNumeric),
      // Sometimes we get L instead of M for month - standalone name.
      PatternItem("month",
                  {{"MMMMM", "narrow"},
                   {"MMMM", "long"},
                   {"MMM", "short"},
                   {"MM", "2-digit"},
                   {"M", "numeric"},
                   {"LLLLL", "narrow"},
                   {"LLLL", "long"},
                   {"LLL", "short"},
                   {"LL", "2-digit"},
                   {"L", "numeric"}},
                  &kNarrowLongShort2DigitNumeric),
      PatternItem("day", {{"dd", "2-digit"}, {"d", "numeric"}},
                  &k2DigitNumeric),
      PatternItem("hour",
                  {{"HH", "2-digit"},
                   {"H", "numeric"},
                   {"hh", "2-digit"},
                   {"h", "numeric"}},
                  &k2DigitNumeric),
      PatternItem("minute", {{"mm", "2-digit"}, {"m", "numeric"}},
                  &k2DigitNumeric),
      PatternItem("second", {{"ss", "2-digit"}, {"s", "numeric"}},
                  &k2DigitNumeric),
      PatternItem("timeZoneName", {{"zzzz", "long"}, {"z", "short"}},
                  &kLongShort)};
  return kPatternItems;
}

class PatternData {
 public:
  PatternData(const std::string property, std::vector<PatternMap> pairs,
              std::vector<const char*>* allowed_values)
      : property(property), allowed_values(allowed_values) {
    for (const auto& pair : pairs) {
      map.insert(std::make_pair(pair.value, pair.pattern));
    }
  }
  virtual ~PatternData() {}

  const std::string property;
  std::map<const std::string, const std::string> map;
  std::vector<const char*>* allowed_values;
};

enum HourOption {
  H_UNKNOWN,
  H_12,
  H_24,
};

static const std::vector<PatternData> CreateCommonData() {
  std::vector<PatternData> build;
  for (const auto& item : GetPatternItems()) {
    if (item.property != "hour") {
      build.push_back(
          PatternData(item.property, item.pairs, item.allowed_values));
    }
  }
  return build;
}

static const std::vector<PatternData> CreateData(const char* digit2,
                                                 const char* numeric) {
  static std::vector<const char*> k2DigitNumeric = {"2-digit", "numeric"};
  static const std::vector<PatternData> common = CreateCommonData();
  std::vector<PatternData> build(common);
  build.push_back(PatternData(
      "hour", {{digit2, "2-digit"}, {numeric, "numeric"}}, &k2DigitNumeric));
  return build;
}

static const std::vector<PatternData>& GetPatternData(HourOption option) {
  static const std::vector<PatternData> data = CreateData("jj", "j");
  static const std::vector<PatternData> data_h12 = CreateData("hh", "h");
  static const std::vector<PatternData> data_h24 = CreateData("HH", "H");
  switch (option) {
    case HourOption::H_12:
      return data_h12;
    case HourOption::H_24:
      return data_h24;
    case HourOption::H_UNKNOWN:
      return data;
  }
}

void SetPropertyFromPattern(Isolate* isolate, const std::string& pattern,
                            Handle<JSObject> options) {
  Factory* factory = isolate->factory();
  const std::vector<PatternItem>& items = GetPatternItems();
  for (const auto& item : items) {
    for (const auto& pair : item.pairs) {
      if (pattern.find(pair.pattern) != std::string::npos) {
        // After we find the first pair in the item which matching the pattern,
        // we set the property and look for the next item in kPatternItems.
        CHECK(JSReceiver::CreateDataProperty(
                  isolate, options,
                  factory->NewStringFromAsciiChecked(item.property.c_str()),
                  factory->NewStringFromAsciiChecked(pair.value.c_str()),
                  kDontThrow)
                  .FromJust());
        break;
      }
    }
  }
  // hour12
  // b. If p is "hour12", then
  //  i. Let hc be dtf.[[HourCycle]].
  //  ii. If hc is "h11" or "h12", let v be true.
  //  iii. Else if, hc is "h23" or "h24", let v be false.
  //  iv. Else, let v be undefined.
  if (pattern.find("h") != std::string::npos) {
    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->NewStringFromStaticChars("hour12"),
              factory->true_value(), kDontThrow)
              .FromJust());
  } else if (pattern.find("H") != std::string::npos) {
    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->NewStringFromStaticChars("hour12"),
              factory->false_value(), kDontThrow)
              .FromJust());
  }
}

std::string GetGMTTzID(Isolate* isolate, const std::string& input) {
  std::string ret = "Etc/GMT";
  switch (input.length()) {
    case 8:
      if (input[7] == '0') return ret + '0';
      break;
    case 9:
      if ((input[7] == '+' || input[7] == '-') &&
          IsInRange(input[8], '0', '9')) {
        return ret + input[7] + input[8];
      }
      break;
    case 10:
      if ((input[7] == '+' || input[7] == '-') && (input[8] == '1') &&
          IsInRange(input[9], '0', '4')) {
        return ret + input[7] + input[8] + input[9];
      }
      break;
  }
  return "";
}

// Locale independenty version of isalpha for ascii range. This will return
// false if the ch is alpha but not in ascii range.
bool IsAsciiAlpha(char ch) {
  return IsInRange(ch, 'A', 'Z') || IsInRange(ch, 'a', 'z');
}

// Locale independent toupper for ascii range. This will not return İ (dotted I)
// for i under Turkish locale while std::toupper may.
char LocaleIndependentAsciiToUpper(char ch) {
  return (IsInRange(ch, 'a', 'z')) ? (ch - 'a' + 'A') : ch;
}

// Locale independent tolower for ascii range.
char LocaleIndependentAsciiToLower(char ch) {
  return (IsInRange(ch, 'A', 'Z')) ? (ch - 'A' + 'a') : ch;
}

// Returns titlecased location, bueNos_airES -> Buenos_Aires
// or ho_cHi_minH -> Ho_Chi_Minh. It is locale-agnostic and only
// deals with ASCII only characters.
// 'of', 'au' and 'es' are special-cased and lowercased.
// ICU's timezone parsing is case sensitive, but ECMAScript is case insensitive
std::string ToTitleCaseTimezoneLocation(Isolate* isolate,
                                        const std::string& input) {
  std::string title_cased;
  int word_length = 0;
  for (char ch : input) {
    // Convert first char to upper case, the rest to lower case
    if (IsAsciiAlpha(ch)) {
      title_cased += word_length == 0 ? LocaleIndependentAsciiToUpper(ch)
                                      : LocaleIndependentAsciiToLower(ch);
      word_length++;
    } else if (ch == '_' || ch == '-' || ch == '/') {
      // Special case Au/Es/Of to be lower case.
      if (word_length == 2) {
        size_t pos = title_cased.length() - 2;
        std::string substr = title_cased.substr(pos, 2);
        if (substr == "Of" || substr == "Es" || substr == "Au") {
          title_cased[pos] = LocaleIndependentAsciiToLower(title_cased[pos]);
        }
      }
      title_cased += ch;
      word_length = 0;
    } else {
      // Invalid input
      return std::string();
    }
  }
  return title_cased;
}

}  // namespace

std::string JSDateTimeFormat::CanonicalizeTimeZoneID(Isolate* isolate,
                                                     const std::string& input) {
  std::string upper = input;
  transform(upper.begin(), upper.end(), upper.begin(),
            LocaleIndependentAsciiToUpper);
  if (upper == "UTC" || upper == "GMT" || upper == "ETC/UTC" ||
      upper == "ETC/GMT") {
    return "UTC";
  }
  // We expect only _, '-' and / beside ASCII letters.
  // All inputs should conform to Area/Location(/Location)*, or Etc/GMT* .
  // TODO(jshin): 1. Support 'GB-Eire", 'EST5EDT", "ROK', 'US/*', 'NZ' and many
  // other aliases/linked names when moving timezone validation code to C++.
  // See crbug.com/364374 and crbug.com/v8/8007 .
  // 2. Resolve the difference betwee CLDR/ICU and IANA time zone db.
  // See http://unicode.org/cldr/trac/ticket/9892 and crbug.com/645807 .
  if (strncmp(upper.c_str(), "ETC/GMT", 7) == 0) {
    return GetGMTTzID(isolate, input);
  }
  return ToTitleCaseTimezoneLocation(isolate, input);
}

MaybeHandle<JSObject> JSDateTimeFormat::ResolvedOptions(
    Isolate* isolate, Handle<JSReceiver> format_holder) {
  Factory* factory = isolate->factory();

  // 3. Let dtf be ? UnwrapDateTimeFormat(dtf).
  if (!Intl::IsObjectOfType(isolate, format_holder,
                            Intl::Type::kDateTimeFormat)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                                 factory->NewStringFromStaticChars(
                                     "Intl.DateTimeFormat.resolvedOptions"),
                                 format_holder),
                    JSObject);
  }
  CHECK(format_holder->IsJSObject());
  icu::SimpleDateFormat* icu_simple_date_format =
      DateFormat::UnpackDateFormat(Handle<JSObject>::cast(format_holder));

  // 4. Let options be ! ObjectCreate(%ObjectPrototype%).
  Handle<JSObject> options = factory->NewJSObject(isolate->object_function());

  // 5. For each row of Table 6, except the header row, in any order, do
  // a. Let p be the Property value of the current row.
  Handle<Object> resolved_obj;

  // After we move all the data to JSDateTimeFormat, we should just get locale
  // and numberingSystem from the member data. This is here until we move
  // everything.
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, resolved_obj,
      JSReceiver::GetProperty(isolate, format_holder,
                              factory->intl_resolved_symbol()),
      JSObject);
  CHECK(resolved_obj->IsJSObject());
  Handle<JSObject> resolved = Handle<JSObject>::cast(resolved_obj);

  // locale
  Handle<Object> locale_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, locale_obj,
      JSReceiver::GetProperty(isolate, resolved, factory->locale_string()),
      JSObject);
  CHECK(locale_obj->IsString());
  Handle<String> locale = Handle<String>::cast(locale_obj);
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->locale_string(), locale, kDontThrow)
            .FromJust());

  // numberingSystem
  // replace to factory->numberingSystem_string(), after +/1168518 landed.
  Handle<String> numberingSystem_string =
      factory->NewStringFromStaticChars("numberingSystem");
  Handle<Object> numbering_system_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, numbering_system_obj,
      JSReceiver::GetProperty(isolate, resolved, numberingSystem_string),
      JSObject);
  if (numbering_system_obj->IsString()) {
    Handle<String> numbering_system =
        Handle<String>::cast(numbering_system_obj);
    CHECK(JSReceiver::CreateDataProperty(isolate, options,
                                         numberingSystem_string,
                                         numbering_system, kDontThrow)
              .FromJust());
  }

  icu::UnicodeString pattern_unicode;
  icu_simple_date_format->toPattern(pattern_unicode);
  std::string pattern;
  pattern_unicode.toUTF8String(pattern);
  SetPropertyFromPattern(isolate, pattern, options);

  // calendar
  const icu::Calendar* calendar = icu_simple_date_format->getCalendar();
  // getType() returns legacy calendar type name instead of LDML/BCP47 calendar
  // key values. intl.js maps them to BCP47 values for key "ca".
  // TODO(jshin): Consider doing it here, instead.
  std::string calendar_str = calendar->getType();

  // Maps ICU calendar names to LDML/BCP47 types for key 'ca'.
  // See typeMap section in third_party/icu/source/data/misc/keyTypeData.txt
  // and
  // http://www.unicode.org/repos/cldr/tags/latest/common/bcp47/calendar.xml
  if (calendar_str == "gregorian") {
    calendar_str = "gregory";
  } else if (calendar_str == "ethiopic-amete-alem") {
    calendar_str = "ethioaa";
  }
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->NewStringFromStaticChars("calendar"),
            factory->NewStringFromAsciiChecked(calendar_str.c_str()),
            kDontThrow)
            .FromJust());

  // timezone
  const icu::TimeZone& tz = calendar->getTimeZone();
  icu::UnicodeString time_zone;
  tz.getID(time_zone);
  UErrorCode error = U_ZERO_ERROR;
  icu::UnicodeString canonical_time_zone;
  icu::TimeZone::getCanonicalID(time_zone, canonical_time_zone, error);
  if (U_SUCCESS(error)) {
    Handle<String> timezone_value;
    // In CLDR (http://unicode.org/cldr/trac/ticket/9943), Etc/UTC is made
    // a separate timezone ID from Etc/GMT even though they're still the same
    // timezone. We have Etc/UTC because 'UTC', 'Etc/Universal',
    // 'Etc/Zulu' and others are turned to 'Etc/UTC' by ICU. Etc/GMT comes
    // from Etc/GMT0, Etc/GMT+0, Etc/GMT-0, Etc/Greenwich.
    // ecma402##sec-canonicalizetimezonename step 3
    if (canonical_time_zone == UNICODE_STRING_SIMPLE("Etc/UTC") ||
        canonical_time_zone == UNICODE_STRING_SIMPLE("Etc/GMT")) {
      timezone_value = factory->NewStringFromAsciiChecked("UTC");
    } else {
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, timezone_value,
          factory->NewStringFromTwoByte(
              Vector<const uint16_t>(reinterpret_cast<const uint16_t*>(
                                         canonical_time_zone.getBuffer()),
                                     canonical_time_zone.length())),
          JSObject);
    }
    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->NewStringFromStaticChars("timeZone"),
              timezone_value, kDontThrow)
              .FromJust());
  } else {
    // Somehow on Windows we will reach here.
    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->NewStringFromStaticChars("timeZone"),
              factory->undefined_value(), kDontThrow)
              .FromJust());
  }
  return options;
}

Maybe<std::string> JSDateTimeFormat::OptionsToSkeleton(
    Isolate* isolate, Handle<JSReceiver> options) {
  std::string result;
  bool hour12;
  Maybe<bool> maybe_get_hour12 = Intl::GetBoolOption(
      isolate, options, "hour12", "Intl.DateTimeFormat", &hour12);
  MAYBE_RETURN(maybe_get_hour12, Nothing<std::string>());
  HourOption hour_option = HourOption::H_UNKNOWN;
  if (maybe_get_hour12.FromJust()) {
    hour_option = hour12 ? HourOption::H_12 : HourOption::H_24;
  }

  for (const auto& item : GetPatternData(hour_option)) {
    std::unique_ptr<char[]> input;
    Maybe<bool> maybe_get_option = Intl::GetStringOption(
        isolate, options, item.property.c_str(), *(item.allowed_values),
        "Intl.DateTimeFormat", &input);
    MAYBE_RETURN(maybe_get_option, Nothing<std::string>());
    if (maybe_get_option.FromJust()) {
      DCHECK_NOT_NULL(input.get());
      result += item.map.find(input.get())->second;
    }
  }
  return Just(result);
}

}  // namespace internal
}  // namespace v8
