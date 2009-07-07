var x = "";
var v = new Object();
var w = new Object();
var vv = function() { x += "hest"; return 1; }
var ww = function() { x += "fisk"; return 2; }
v.valueOf = vv;
w.valueOf = ww;
assertEquals(1, Math.min(v,w));
assertEquals("hestfisk", x, "min");

x = "";
assertEquals(2, Math.max(v,w));
assertEquals("hestfisk", x, "max");

x = "";
assertEquals(Math.atan2(1, 2), Math.atan2(v, w));
// JSC says fiskhest.
assertEquals("hestfisk", x, "atan2");

x = "";
assertEquals(1, Math.pow(v, w));
assertEquals("hestfisk", x, "pow");

var year = { valueOf: function() { x += 1; return 2007; } };
var month = { valueOf: function() { x += 2; return 2; } };
var date = { valueOf: function() { x += 3; return 4; } };
var hours = { valueOf: function() { x += 4; return 13; } };
var minutes = { valueOf: function() { x += 5; return 50; } };
var seconds = { valueOf: function() { x += 6; return 0; } };
var ms = { valueOf: function() { x += 7; return 999; } };

x = "";
new Date(year, month, date, hours, minutes, seconds, ms);
// JSC fails this one: Returns 12345671234567.
assertEquals("1234567", x, "Date");

x = "";
Date(year, month, date, hours, minutes, seconds, ms);
assertEquals("", x, "Date not constructor");

x = "";
Date.UTC(year, month, date, hours, minutes, seconds, ms);
// JSC fails this one: Returns 12345671234567.
assertEquals("1234567", x, "Date.UTC");

x = "";
new Date().setSeconds(seconds, ms);
assertEquals("67", x, "Date.UTC");

x = "";
new Date().setSeconds(seconds, ms);
assertEquals("67", x, "Date.setSeconds");

x = "";
new Date().setUTCSeconds(seconds, ms);
assertEquals("67", x, "Date.setUTCSeconds");

x = "";
new Date().setMinutes(minutes, seconds, ms);
assertEquals("567", x, "Date.setMinutes");

x = "";
new Date().setUTCMinutes(minutes, seconds, ms);
assertEquals("567", x, "Date.setUTCMinutes");

x = "";
new Date().setHours(hours, minutes, seconds, ms);
assertEquals("4567", x, "Date.setHours");

x = "";
new Date().setUTCHours(hours, minutes, seconds, ms);
assertEquals("4567", x, "Date.setUTCHours");

x = "";
new Date().setDate(date, hours, minutes, seconds, ms);
assertEquals("3", x, "Date.setDate");

x = "";
new Date().setUTCDate(date, hours, minutes, seconds, ms);
assertEquals("3", x, "Date.setUTCDate");

x = "";
new Date().setMonth(month, date, hours, minutes, seconds, ms);
assertEquals("23", x, "Date.setMonth");

x = "";
new Date().setUTCMonth(month, date, hours, minutes, seconds, ms);
assertEquals("23", x, "Date.setUTCMonth");

x = "";
new Date().setFullYear(year, month, date, hours, minutes, seconds, ms);
assertEquals("123", x, "Date.setFullYear");

x = "";
new Date().setUTCFullYear(year, month, date, hours, minutes, seconds, ms);
assertEquals("123", x, "Date.setUTCFullYear");

var a = { valueOf: function() { x += "hest"; return 97; } };
var b = { valueOf: function() { x += "fisk"; return 98; } };
assertEquals("ab", String.fromCharCode(a, b), "String.fromCharCode");

print("ok");
