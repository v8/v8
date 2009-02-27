// Regexp shouldn't use String.prototype.slice()
var s = new String("foo");
assertEquals("f", s.slice(0,1));
String.prototype.slice = function() { return "x"; }
assertEquals("x", s.slice(0,1));
assertEquals("g", /g/.exec("gg"));

// Regexp shouldn't use String.prototype.charAt()
var f1 = new RegExp("f", "i");
assertEquals("F", f1.exec("F"));
assertEquals("f", "foo".charAt(0));
String.prototype.charAt = function(idx) { return 'g'; };
assertEquals("g", "foo".charAt(0));
var f2 = new RegExp("[g]", "i");
assertEquals("G", f2.exec("G"));
assertTrue(f2.ignoreCase);

// On the other hand test is defined in a semi-coherent way as a call to exec.
// 15.10.6.3
// SpiderMonkey fails this one.
RegExp.prototype.exec = function(string) { return 'x'; }
assertTrue(/f/.test('x'));
