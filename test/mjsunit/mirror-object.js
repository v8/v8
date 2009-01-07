// Copyright 2008 the V8 project authors. All rights reserved.
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

// Flags: --expose-debug-as debug
// Test the mirror object for objects

function testObjectMirror(o, cls_name, ctor_name, hasSpecialProperties) {
  // Create mirror and JSON representation.
  var mirror = debug.MakeMirror(o);
  var json = mirror.toJSONProtocol(true);

  // Check the mirror hierachy.
  assertTrue(mirror instanceof debug.Mirror);
  assertTrue(mirror instanceof debug.ValueMirror);
  assertTrue(mirror instanceof debug.ObjectMirror);

  // Check the mirror properties.
  assertTrue(mirror.isObject());
  assertEquals('object', mirror.type());
  assertFalse(mirror.isPrimitive());
  assertEquals(cls_name, mirror.className());
  assertTrue(mirror.constructorFunction() instanceof debug.ObjectMirror);
  assertTrue(mirror.protoObject() instanceof debug.Mirror);
  assertTrue(mirror.prototypeObject() instanceof debug.Mirror);
  assertFalse(mirror.hasNamedInterceptor(), "hasNamedInterceptor()");
  assertFalse(mirror.hasIndexedInterceptor(), "hasIndexedInterceptor()");

  var names = mirror.propertyNames();
  var properties = mirror.properties()
  assertEquals(names.length, properties.length);
  for (var i = 0; i < properties.length; i++) {
    assertTrue(properties[i] instanceof debug.Mirror);
    assertTrue(properties[i] instanceof debug.PropertyMirror);
    assertEquals('property', properties[i].type());
    assertEquals(names[i], properties[i].name());
  }
  
  for (var p in o) {
    var property_mirror = mirror.property(p);
    assertTrue(property_mirror instanceof debug.PropertyMirror);
    assertEquals(p, property_mirror.name());
    // If the object has some special properties don't test for these.
    if (!hasSpecialProperties) {
      assertEquals(0, property_mirror.attributes(), property_mirror.name());
      assertFalse(property_mirror.isReadOnly());
      assertTrue(property_mirror.isEnum());
      assertTrue(property_mirror.canDelete());
    }
  }

  // Parse JSON representation and check.
  var fromJSON = eval('(' + json + ')');
  assertEquals('object', fromJSON.type);
  assertEquals(cls_name, fromJSON.className);
  assertEquals('function', fromJSON.constructorFunction.type);
  if (ctor_name !== undefined)
    assertEquals(ctor_name, fromJSON.constructorFunction.name);
  assertEquals(void 0, fromJSON.namedInterceptor);
  assertEquals(void 0, fromJSON.indexedInterceptor);

  // For array the index properties are seperate from named properties.
  if (!cls_name == 'Array') {
    assertEquals(names.length, fromJSON.properties.length, 'Some properties missing in JSON');
  }

  // Check that the serialization contains all properties.
  for (var i = 0; i < fromJSON.properties.length; i++) {
    var name = fromJSON.properties[i].name;
    if (!name) name = fromJSON.properties[i].index;
    var found = false;
    for (var j = 0; j < names.length; j++) {
      if (names[j] == name) {
        assertEquals(properties[i].value().type(), fromJSON.properties[i].value.type);
        // If property type is normal nothing is serialized.
        if (properties[i].propertyType() != debug.PropertyType.Normal) {
          assertEquals(properties[i].propertyType(), fromJSON.properties[i].propertyType);
        } else {
          assertTrue(typeof(fromJSON.properties[i].propertyType) === 'undefined');
        }
        // If there are no attributes nothing is serialized.
        if (properties[i].attributes() != debug.PropertyAttribute.None) {
          assertEquals(properties[i].attributes(), fromJSON.properties[i].attributes);
        } else {
          assertTrue(typeof(fromJSON.properties[i].attributes) === 'undefined');
        }
        if (!properties[i].value() instanceof debug.AccessorMirror &&
            properties[i].value().isPrimitive()) {
          // NaN is not equal to NaN.
          if (isNaN(properties[i].value().value())) {
            assertTrue(isNaN(fromJSON.properties[i].value.value));
          } else {
            assertEquals(properties[i].value().value(), fromJSON.properties[i].value.value);
          }
        }
        found = true;
      }
    }
    assertTrue(found, '"' + name + '" not found (' + json + ')');
  }
}


function Point(x,y) {
  this.x_ = x;
  this.y_ = y;
}


// Test a number of different objects.
testObjectMirror({}, 'Object', 'Object');
testObjectMirror({'a':1,'b':2}, 'Object', 'Object');
testObjectMirror({'1':void 0,'2':null,'f':function pow(x,y){return Math.pow(x,y);}}, 'Object', 'Object');
testObjectMirror(new Point(-1.2,2.003), 'Object', 'Point');
testObjectMirror(this, 'global', undefined, true);  // Global object has special properties
testObjectMirror([], 'Array', 'Array');
testObjectMirror([1,2], 'Array', 'Array');

// Test circular references.
o = {};
o.o = o;
testObjectMirror(o, 'Object', 'Object');

// Test that non enumerable properties are part of the mirror
global_mirror = debug.MakeMirror(this);
assertEquals('property', global_mirror.property("Math").type());
assertFalse(global_mirror.property("Math").isEnum(), "Math is enumerable" + global_mirror.property("Math").attributes());

math_mirror = global_mirror.property("Math").value();
assertEquals('property', math_mirror.property("E").type());
assertFalse(math_mirror.property("E").isEnum(), "Math.E is enumerable");
assertTrue(math_mirror.property("E").isReadOnly());
assertFalse(math_mirror.property("E").canDelete());

// Test objects with JavaScript accessors.
o = {}
o.__defineGetter__('a', function(){throw 'a';})
o.__defineSetter__('b', function(){throw 'b';})
o.__defineGetter__('c', function(){throw 'c';})
o.__defineSetter__('c', function(){throw 'c';})
testObjectMirror(o, 'Object', 'Object');
mirror = debug.MakeMirror(o);
// a has getter but no setter.
assertTrue(mirror.property('a').value() instanceof debug.AccessorMirror);
assertEquals(debug.PropertyType.Callbacks, mirror.property('a').propertyType());
// b has setter but no getter.
assertTrue(mirror.property('b').value() instanceof debug.AccessorMirror);
assertEquals(debug.PropertyType.Callbacks, mirror.property('b').propertyType());
// c has both getter and setter.
assertTrue(mirror.property('c').value() instanceof debug.AccessorMirror);
assertEquals(debug.PropertyType.Callbacks, mirror.property('c').propertyType());

// Test objects with native accessors.
mirror = debug.MakeMirror(new String('abc'));
assertTrue(mirror instanceof debug.ObjectMirror);
assertTrue(mirror.property('length').value() instanceof debug.AccessorMirror);
assertTrue(mirror.property('length').value().isNative());
assertEquals('a', mirror.property(0).value().value());
assertEquals('b', mirror.property(1).value().value());
assertEquals('c', mirror.property(2).value().value());
