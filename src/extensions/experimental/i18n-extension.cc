// Copyright 2010 the V8 project authors. All rights reserved.
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

#include "i18n-extension.h"

#include <algorithm>
#include <string>

#include "break-iterator.h"
#include "natives.h"
#include "unicode/locid.h"
#include "unicode/uloc.h"

namespace v8 {
namespace internal {

I18NExtension* I18NExtension::extension_ = NULL;

// Returns a pointer to static string containing the actual
// JavaScript code generated from i18n.js file.
static const char* GetScriptSource() {
  int index = NativesCollection<I18N>::GetIndex("i18n");
  Vector<const char> script_data =
      NativesCollection<I18N>::GetScriptSource(index);

  return script_data.start();
}

I18NExtension::I18NExtension()
    : v8::Extension("v8/i18n", GetScriptSource()) {
}

v8::Handle<v8::FunctionTemplate> I18NExtension::GetNativeFunction(
    v8::Handle<v8::String> name) {
  if (name->Equals(v8::String::New("NativeJSLocale"))) {
    return v8::FunctionTemplate::New(JSLocale);
  } else if (name->Equals(v8::String::New("NativeJSAvailableLocales"))) {
    return v8::FunctionTemplate::New(JSAvailableLocales);
  } else if (name->Equals(v8::String::New("NativeJSMaximizedLocale"))) {
    return v8::FunctionTemplate::New(JSMaximizedLocale);
  } else if (name->Equals(v8::String::New("NativeJSMinimizedLocale"))) {
    return v8::FunctionTemplate::New(JSMinimizedLocale);
  } else if (name->Equals(v8::String::New("NativeJSDisplayLanguage"))) {
    return v8::FunctionTemplate::New(JSDisplayLanguage);
  } else if (name->Equals(v8::String::New("NativeJSDisplayScript"))) {
    return v8::FunctionTemplate::New(JSDisplayScript);
  } else if (name->Equals(v8::String::New("NativeJSDisplayRegion"))) {
    return v8::FunctionTemplate::New(JSDisplayRegion);
  } else if (name->Equals(v8::String::New("NativeJSDisplayName"))) {
    return v8::FunctionTemplate::New(JSDisplayName);
  } else if (name->Equals(v8::String::New("NativeJSBreakIterator"))) {
    return v8::FunctionTemplate::New(BreakIterator::JSBreakIterator);
  }

  return v8::Handle<v8::FunctionTemplate>();
}

v8::Handle<v8::Value> I18NExtension::JSLocale(const v8::Arguments& args) {
  // TODO(cira): Fetch browser locale. Accept en-US as good default for now.
  // We could possibly pass browser locale as a parameter in the constructor.
  std::string locale_name("en-US");
  if (args.Length() == 1 && args[0]->IsString()) {
    locale_name = *v8::String::Utf8Value(args[0]->ToString());
  }

  v8::Local<v8::Object> locale = v8::Object::New();
  locale->Set(v8::String::New("locale"), v8::String::New(locale_name.c_str()));

  icu::Locale icu_locale(locale_name.c_str());

  const char* language = icu_locale.getLanguage();
  locale->Set(v8::String::New("language"), v8::String::New(language));

  const char* script = icu_locale.getScript();
  if (strlen(script)) {
    locale->Set(v8::String::New("script"), v8::String::New(script));
  }

  const char* region = icu_locale.getCountry();
  if (strlen(region)) {
    locale->Set(v8::String::New("region"), v8::String::New(region));
  }

  return locale;
}

// TODO(cira): Filter out locales that Chrome doesn't support.
v8::Handle<v8::Value> I18NExtension::JSAvailableLocales(
    const v8::Arguments& args) {
  v8::Local<v8::Array> all_locales = v8::Array::New();

  int count = 0;
  const icu::Locale* icu_locales = icu::Locale::getAvailableLocales(count);
  for (int i = 0; i < count; ++i) {
    all_locales->Set(i, v8::String::New(icu_locales[i].getName()));
  }

  return all_locales;
}

// Use - as tag separator, not _ that ICU uses.
static std::string NormalizeLocale(const std::string& locale) {
  std::string result(locale);
  // TODO(cira): remove STL dependency.
  std::replace(result.begin(), result.end(), '_', '-');
  return result;
}

v8::Handle<v8::Value> I18NExtension::JSMaximizedLocale(
    const v8::Arguments& args) {
  if (!args.Length() || !args[0]->IsString()) {
    return v8::Undefined();
  }

  UErrorCode status = U_ZERO_ERROR;
  std::string locale_name = *v8::String::Utf8Value(args[0]->ToString());
  char max_locale[ULOC_FULLNAME_CAPACITY];
  uloc_addLikelySubtags(locale_name.c_str(), max_locale,
                        sizeof(max_locale), &status);
  if (U_FAILURE(status)) {
    return v8::Undefined();
  }

  return v8::String::New(NormalizeLocale(max_locale).c_str());
}

v8::Handle<v8::Value> I18NExtension::JSMinimizedLocale(
    const v8::Arguments& args) {
  if (!args.Length() || !args[0]->IsString()) {
    return v8::Undefined();
  }

  UErrorCode status = U_ZERO_ERROR;
  std::string locale_name = *v8::String::Utf8Value(args[0]->ToString());
  char min_locale[ULOC_FULLNAME_CAPACITY];
  uloc_minimizeSubtags(locale_name.c_str(), min_locale,
                       sizeof(min_locale), &status);
  if (U_FAILURE(status)) {
    return v8::Undefined();
  }

  return v8::String::New(NormalizeLocale(min_locale).c_str());
}

// Common code for JSDisplayXXX methods.
static v8::Handle<v8::Value> GetDisplayItem(const v8::Arguments& args,
                                            const std::string& item) {
  if (args.Length() != 2 || !args[0]->IsString() || !args[1]->IsString()) {
    return v8::Undefined();
  }

  std::string base_locale = *v8::String::Utf8Value(args[0]->ToString());
  icu::Locale icu_locale(base_locale.c_str());
  icu::Locale display_locale =
      icu::Locale(*v8::String::Utf8Value(args[1]->ToString()));
  icu::UnicodeString result;
  if (item == "language") {
    icu_locale.getDisplayLanguage(display_locale, result);
  } else if (item == "script") {
    icu_locale.getDisplayScript(display_locale, result);
  } else if (item == "region") {
    icu_locale.getDisplayCountry(display_locale, result);
  } else if (item == "name") {
    icu_locale.getDisplayName(display_locale, result);
  } else {
    return v8::Undefined();
  }

  if (result.length()) {
    return v8::String::New(
        reinterpret_cast<const uint16_t*>(result.getBuffer()), result.length());
  }

  return v8::Undefined();
}

v8::Handle<v8::Value> I18NExtension::JSDisplayLanguage(
    const v8::Arguments& args) {
  return GetDisplayItem(args, "language");
}

v8::Handle<v8::Value> I18NExtension::JSDisplayScript(
    const v8::Arguments& args) {
  return GetDisplayItem(args, "script");
}

v8::Handle<v8::Value> I18NExtension::JSDisplayRegion(
    const v8::Arguments& args) {
  return GetDisplayItem(args, "region");
}

v8::Handle<v8::Value> I18NExtension::JSDisplayName(const v8::Arguments& args) {
  return GetDisplayItem(args, "name");
}

I18NExtension* I18NExtension::get() {
  if (!extension_) {
    extension_ = new I18NExtension();
  }
  return extension_;
}

void I18NExtension::Register() {
  static v8::DeclareExtension i18n_extension_declaration(I18NExtension::get());
}

} }  // namespace v8::internal
