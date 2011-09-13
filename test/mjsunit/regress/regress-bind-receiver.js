function strict() { 'use strict'; return this; }
function lenient() { return this; }
var obj = {};
	
assertEquals(true, strict.bind(true)());
assertEquals(42, strict.bind(42)());
assertEquals("", strict.bind("")());
assertEquals(null, strict.bind(null)()l);
assertEquals(undefined, strict.bind(undefined)());
assertEquals(obj, strict.bind(obj)());

assertEquals(true, lenient.bind(true)() instanceof Boolean);
assertEquals(true, lenient.bind(42)() instanceof Number);
assertEquals(true, lenient.bind("")() instanceof String);
assertEquals(this, lenient.bind(null)());
assertEquals(this, lenient.bind(undefined)());
assertEquals(obj, lenient.bind(obj)());
