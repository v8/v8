// Copyright 2006-2011 the V8 project authors. All rights reserved.
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

// TODO(cira): Remove v8 prefix from v8Locale once we have stable API.
v8Locale = function(optLocale) {
  native function NativeJSLocale();
  var properties = NativeJSLocale(optLocale);
  this.locale = properties.locale;
  this.language = properties.language;
  this.script = properties.script;
  this.region = properties.region;
};

v8Locale.availableLocales = function() {
  native function NativeJSAvailableLocales();
  return NativeJSAvailableLocales();
};

v8Locale.prototype.maximizedLocale = function() {
  native function NativeJSMaximizedLocale();
  return new v8Locale(NativeJSMaximizedLocale(this.locale));
};

v8Locale.prototype.minimizedLocale = function() {
  native function NativeJSMinimizedLocale();
  return new v8Locale(NativeJSMinimizedLocale(this.locale));
};

v8Locale.prototype.displayLocale_ = function(displayLocale) {
  var result = this.locale;
  if (displayLocale !== undefined) {
    result = displayLocale.locale;
  }
  return result;
};

v8Locale.prototype.displayLanguage = function(optDisplayLocale) {
  var displayLocale = this.displayLocale_(optDisplayLocale);
  native function NativeJSDisplayLanguage();
  return NativeJSDisplayLanguage(this.locale, displayLocale);
};

v8Locale.prototype.displayScript = function(optDisplayLocale) {
  var displayLocale = this.displayLocale_(optDisplayLocale);
  native function NativeJSDisplayScript();
  return NativeJSDisplayScript(this.locale, displayLocale);
};

v8Locale.prototype.displayRegion = function(optDisplayLocale) {
  var displayLocale = this.displayLocale_(optDisplayLocale);
  native function NativeJSDisplayRegion();
  return NativeJSDisplayRegion(this.locale, displayLocale);
};

v8Locale.prototype.displayName = function(optDisplayLocale) {
  var displayLocale = this.displayLocale_(optDisplayLocale);
  native function NativeJSDisplayName();
  return NativeJSDisplayName(this.locale, displayLocale);
};

v8Locale.v8BreakIterator = function(locale, type) {
  native function NativeJSBreakIterator();
  var iterator = NativeJSBreakIterator(locale, type);
  iterator.type = type;
  return iterator;
};

v8Locale.v8BreakIterator.BreakType = {
  'unknown': -1,
  'none': 0,
  'number': 100,
  'word': 200,
  'kana': 300,
  'ideo': 400
};

v8Locale.prototype.v8CreateBreakIterator = function(type) {
  return new v8Locale.v8BreakIterator(this.locale, type);
};
