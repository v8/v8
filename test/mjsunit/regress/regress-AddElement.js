// Flags: --always-compact
//
// Regression test for the r1512 fix.

var foo = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

foo = foo + foo;
foo = foo + foo;
foo = foo + foo;
foo = foo + foo;
foo = foo + foo;
foo = foo + foo;
foo = foo + foo;
foo = foo + foo;
foo = foo + foo;
foo = foo + foo;
foo = foo + foo;
foo = foo + foo;
foo = foo + foo;
foo = foo + foo;
foo = foo + foo;

foo.replace(/[b]/, "c");  // Flatten foo;

var moving_string = "b" + "c";

var bar = foo.replace(/[a]/g, moving_string);

print(bar.length);
