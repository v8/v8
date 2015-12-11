// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function IntTest() {
  "use asm";
  function sum(a, b) {
    a = a|0;
    b = b|0;
    var c = (b + 1)|0
    return (a + c + 1)|0;
  }

  function caller() {
    return sum(77,22) | 0;
  }

  return {caller: caller};
}

assertEquals(101, WASM.asmCompileRun(IntTest.toString()));

function Float64Test() {
  "use asm";
  function sum(a, b) {
    a = +a;
    b = +b;
    return +(a + b);
  }

  function caller() {
    var a = +sum(70.1,10.2);
    var ret = 0|0;
    if (a == 80.3) {
      ret = 1|0;
    } else {
      ret = 0|0;
    }
    return ret|0;
  }

  return {caller: caller};
}

assertEquals(1, WASM.asmCompileRun(Float64Test.toString()));

function BadModule() {
  "use asm";
  function caller(a, b) {
    a = a|0;
    b = b+0;
    var c = (b + 1)|0
    return (a + c + 1)|0;
  }

  function caller() {
    return call(1, 2)|0;
  }

  return {caller: caller};
}

assertThrows(function() {
  WASM.asmCompileRun(BadModule.toString())
});

function TestReturnInBlock() {
  "use asm";

  function caller() {
    if(1) {
      {
        {
          return 1;
        }
      }
    }
    return 0;
  }

  return {caller: caller};
}

assertEquals(1, WASM.asmCompileRun(TestReturnInBlock.toString()));

function TestWhileSimple() {
  "use asm";

  function caller() {
    var x = 0;
    while(x < 5) {
      x = (x + 1)|0;
    }
    return x|0;
  }

  return {caller: caller};
}

assertEquals(5, WASM.asmCompileRun(TestWhileSimple.toString()));

function TestWhileWithoutBraces() {
  "use asm";

  function caller() {
    var x = 0;
    while(x <= 3)
      x = (x + 1)|0;
    return x|0;
  }

  return {caller: caller};
}

assertEquals(4, WASM.asmCompileRun(TestWhileWithoutBraces.toString()));

function TestReturnInWhile() {
  "use asm";

  function caller() {
    var x = 0;
    while(x < 10) {
      x = (x + 6)|0;
      return x|0;
    }
    return x|0;
  }

  return {caller: caller};
}

assertEquals(6, WASM.asmCompileRun(TestReturnInWhile.toString()));

function TestReturnInWhileWithoutBraces() {
  "use asm";

  function caller() {
    var x = 0;
    while(x < 5)
      return 7;
    return x|0;
  }

  return {caller: caller};
}

assertEquals(7, WASM.asmCompileRun(TestReturnInWhileWithoutBraces.toString()));

function TestBreakInWhile() {
  "use asm";

  function caller() {
    while(1) {
      break;
    }
    return 8;
  }

  return {caller: caller};
}

assertEquals(8, WASM.asmCompileRun(TestBreakInWhile.toString()));

function TestBreakInNestedWhile() {
  "use asm";

  function caller() {
    var x = 1.0;
    while(x < 1.5) {
      while(1)
        break;
      x = +(x + 0.25);
    }
    var ret = 0;
    if (x == 1.5) {
      ret = 9;
    }
    return ret|0;
  }

  return {caller: caller};
}

assertEquals(9, WASM.asmCompileRun(TestBreakInNestedWhile.toString()));

function TestBreakInBlock() {
  "use asm";

  function caller() {
    var x = 0;
    abc: {
      x = 10;
      if (x == 10) {
        break abc;
      }
      x = 20;
    }
    return x|0;
  }

  return {caller: caller};
}

assertEquals(10, WASM.asmCompileRun(TestBreakInBlock.toString()));

function TestBreakInNamedWhile() {
  "use asm";

  function caller() {
    var x = 0;
    outer: while (1) {
      x = (x + 1)|0;
      while (x == 11) {
        break outer;
      }
    }
    return x|0;
  }

  return {caller: caller};
}

assertEquals(11, WASM.asmCompileRun(TestBreakInNamedWhile.toString()));

function TestContinue() {
  "use asm";

  function caller() {
    var x = 5;
    var ret = 0;
    while (x >= 0) {
      x = (x - 1)|0;
      if (x == 2) {
        continue;
      }
      ret = (ret - 1)|0;
    }
    return ret|0;
  }

  return {caller: caller};
}

assertEquals(-5, WASM.asmCompileRun(TestContinue.toString()));

function TestContinueInNamedWhile() {
  "use asm";

  function caller() {
    var x = 5;
    var y = 0;
    var ret = 0;
    outer: while (x > 0) {
      x = (x - 1)|0;
      y = 0;
      while (y < 5) {
        if (x == 3) {
          continue outer;
        }
        ret = (ret + 1)|0;
        y = (y + 1)|0;
      }
    }
    return ret|0;
  }

  return {caller: caller};
}

assertEquals(20, WASM.asmCompileRun(TestContinueInNamedWhile.toString()));

function TestNot() {
  "use asm";

  function caller() {
    var a = !(2 > 3);
    return a | 0;
  }

  return {caller:caller};
}

assertEquals(1, WASM.asmCompileRun(TestNot.toString()));

function TestNotEquals() {
  "use asm";

  function caller() {
    var a = 3;
    if (a != 2) {
      return 21;
    }
    return 0;
  }

  return {caller:caller};
}

assertEquals(21, WASM.asmCompileRun(TestNotEquals.toString()));

function TestUnsignedComparison() {
  "use asm";

  function caller() {
    var a = 0xffffffff;
    if ((a>>>0) > (0>>>0)) {
      return 22;
    }
    return 0;
  }

  return {caller:caller};
}

assertEquals(22, WASM.asmCompileRun(TestUnsignedComparison.toString()));

function TestMixedAdd() {
  "use asm";

  function caller() {
    var a = 0x80000000;
    var b = 0x7fffffff;
    var c = 0;
    c = ((a>>>0) + b)|0;
    if ((c >>> 0) > (0>>>0)) {
      if (c < 0) {
        return 23;
      }
    }
    return 0;
  }

  return {caller:caller};
}

assertEquals(23, WASM.asmCompileRun(TestMixedAdd.toString()));

function TestInt32HeapAccess(stdlib, foreign, buffer) {
  "use asm";

  var m = new stdlib.Int32Array(buffer);
  function caller() {
    var i = 4;

    m[0] = (i + 1) | 0;
    m[i >> 2] = ((m[0]|0) + 1) | 0;
    m[2] = ((m[i >> 2]|0) + 1) | 0;
    return m[2] | 0;
  }

  return {caller: caller};
}

assertEquals(7, WASM.asmCompileRun(TestInt32HeapAccess.toString()));

function TestHeapAccessIntTypes() {
  var types = [
    ['Int8Array', '>> 0'],
    ['Uint8Array', '>> 0'],
    ['Int16Array', '>> 1'],
    ['Uint16Array', '>> 1'],
    ['Int32Array', '>> 2'],
    ['Uint32Array', '>> 2'],
  ];
  for (var i = 0; i < types.length; i++) {
    var code = TestInt32HeapAccess.toString();
    code = code.replace('Int32Array', types[i][0]);
    code = code.replace(/>> 2/g, types[i][1]);
    assertEquals(7, WASM.asmCompileRun(code));
  }
}

TestHeapAccessIntTypes();

function TestFloatHeapAccess(stdlib, foreign, buffer) {
  "use asm";

  var f32 = new stdlib.Float32Array(buffer);
  var f64 = new stdlib.Float64Array(buffer);
  var fround = stdlib.Math.fround;
  function caller() {
    var i = 8;
    var j = 8;
    var v = 6.0;

    // TODO(bradnelson): Add float32 when asm-wasm supports it.
    f64[2] = v + 1.0;
    f64[i >> 3] = +f64[2] + 1.0;
    f64[j >> 3] = +f64[j >> 3] + 1.0;
    i = +f64[i >> 3] == 9.0;
    return i|0;
  }

  return {caller: caller};
}

assertEquals(1, WASM.asmCompileRun(TestFloatHeapAccess.toString()));

function TestConvertI32() {
  "use asm";

  function caller() {
    var a = 1.5;
    if ((~~(a + a)) == 3) {
      return 24;
    }
    return 0;
  }

  return {caller:caller};
}

assertEquals(24, WASM.asmCompileRun(TestConvertI32.toString()));

function TestConvertF64FromInt() {
  "use asm";

  function caller() {
    var a = 1;
    if ((+(a + a)) > 1.5) {
      return 25;
    }
    return 0;
  }

  return {caller:caller};
}

assertEquals(25, WASM.asmCompileRun(TestConvertF64FromInt.toString()));

function TestConvertF64FromUnsigned() {
  "use asm";

  function caller() {
    var a = 0xffffffff;
    if ((+(a>>>0)) > 0.0) {
      if((+a) < 0.0) {
        return 26;
      }
    }
    return 0;
  }

  return {caller:caller};
}

assertEquals(26, WASM.asmCompileRun(TestConvertF64FromUnsigned.toString()));

function TestModInt() {
  "use asm";

  function caller() {
    var a = -83;
    var b = 28;
    return ((a|0)%(b|0))|0;
  }

  return {caller:caller};
}

assertEquals(-27, WASM.asmCompileRun(TestModInt.toString()));

function TestModUnsignedInt() {
  "use asm";

  function caller() {
    var a = 0x80000000;  //2147483648
    var b = 10;
    return ((a>>>0)%(b>>>0))|0;
  }

  return {caller:caller};
}

assertEquals(8, WASM.asmCompileRun(TestModUnsignedInt.toString()));

function TestModDouble() {
  "use asm";

  function caller() {
    var a = 5.25;
    var b = 2.5;
    if (a%b == 0.25) {
      return 28;
    }
    return 0;
  }

  return {caller:caller};
}

assertEquals(28, WASM.asmCompileRun(TestModDouble.toString()));

/*
TODO: Fix parsing of negative doubles
      Fix code to use trunc instead of casts
function TestModDoubleNegative() {
  "use asm";

  function caller() {
    var a = -34359738368.25;
    var b = 2.5;
    if (a%b == -0.75) {
      return 28;
    }
    return 0;
  }

  return {caller:caller};
}

assertEquals(28, WASM.asmCompileRun(TestModDoubleNegative.toString()));
*/

function TestGlobals() {
  "use asm";

  var a = 0.0;
  var b = 0.0;
  var c = 0.0;

  function add() {
    c = a + b;
  }

  function caller() {
    a = 23.75;
    b = 7.75;
    add();
    return (~~c)|0;
  }

  return {caller:caller};
}

assertEquals(31, WASM.asmCompileRun(TestGlobals.toString()));

function TestGlobalsWithInit() {
  "use asm";

  var a = 0.0;
  var b = 0.0;

  function add() {
    return +(a + b);
  }

  function init() {
    a = 43.25;
    b = 34.25;
  }

  return {init:init,
          add:add};
}

var module = WASM.instantiateModuleFromAsm(TestGlobalsWithInit.toString());
module.init();
assertEquals(77.5, module.add());
