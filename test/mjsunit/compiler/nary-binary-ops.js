// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test that n-ary chains of binary ops give an equal result to individual
// binary op calls.

// Generate a function of the form
//
// function(init,a0,...,aN) {
//   return init + a0 + ... + aN;
// }
//
// where + can be any binary operation.
function generate_chained_op(op, num_ops) {
    let str = "(function(init";
    for (let i = 0; i < num_ops; i++) {
        str += ",a"+i;
    }
    str += "){return (init";
    for (let i = 0; i < num_ops; i++) {
        str += op+"a"+i;
    }
    str += ");})";
    return eval(str);
}

// Generate a function of the form
//
// function(init,a0,...,aN) {
//   var tmp = init;
//   tmp = tmp + a0;
//   ...
//   tmp = tmp + aN;
//   return tmp;
// }
//
// where + can be any binary operation.
function generate_nonchained_op(op, num_ops) {
    let str = "(function(init";
    for (let i = 0; i < num_ops; i++) {
        str += ",a"+i;
    }
    str += "){ var tmp=init; ";
    for (let i = 0; i < num_ops; i++) {
        str += "tmp=(tmp"+op+"a"+i+");";
    }
    str += "return tmp;})";
    return eval(str);
}

const BINOPS = [
    ",",
    "||",
    "&&",
    "|",
    "^",
    "&",
    "<<",
    ">>",
    ">>>",
    "+",
    "-",
    "*",
    "/",
    "%",
];

// Test each binop to see if the chained version is equivalent to the non-
// chained one.
for (let op of BINOPS) {
    let chained = generate_chained_op(op, 5);
    let nonchained = generate_nonchained_op(op, 5);

    // With numbers.
    assertEquals(
        nonchained(1,2,3,4,5),
        chained(1,2,3,4,5),
        "numeric " + op);

    // With numbers and strings.
    assertEquals(
        nonchained(1,"2",3,"4",5),
        chained(1,"2",3,"4",5),
        "numeric and string " + op);
}
