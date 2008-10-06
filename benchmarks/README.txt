V8 Benchmark Suite
==================

This is the V8 benchmark suite: A collection of pure JavaScript
benchmarks that we have used to tune V8. The licenses for the
individual benchmarks are included in the JavaScript files.

In addition to the benchmarks, the suite consists of the benchmark
framework (base.js), which must be loaded before any of the individual
benchmark files, and two benchmark runners: An HTML version (run.html)
and a standalone JavaScript version (run.js).


Changes From Version 1 To Version 2
===================================

For version 2 the crypto benchmark was fixed.  Previously, the
decryption stage was given plaintext as input, which resulted in an
error.  Now, the decryption stage is given the output of the
encryption stage as input.  The result is checked against the original
plaintext.  For this to give the correct results the crypto objects
are reset for each iteration of the benchmark.  In addition, the size
of the plain text has been increased a little and the use of
Math.random() and new Date() to build an RNG pool has been removed.

Other benchmarks were fixed to do elementary verification of the
results of their calculations.  This is to avoid accidentally
obtaining scores that are the result of an incorrect JavaScript engine
optimization.
