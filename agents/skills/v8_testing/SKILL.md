---
name: v8_testing
description: "Detailed guide for running and interpreting tests in V8."
---

# V8 Testing

Use this skill to understand how to run tests in V8 and how to handle failures. This specializes the general commands for testing.

## Primary Script
The primary script for running tests is `tools/run-tests.py`. You specify the build output directory and the tests you want to run.

## Key Test Suites
- **unittests**: C++ unit tests for V8's internal components.
- **cctest**: Another, older format for C++ unit tests (deprecated, in the process of being moved to unittests).
- **mjsunit**: JavaScript-based tests for JavaScript language features and builtins.

## Running Tests

```bash
# Run all standard tests for the x64.optdebug build
tools/run-tests.py --progress dots --exit-after-n-failures=5 --outdir=out/x64.optdebug

# Run a specific test suite (e.g., cctest)
tools/run-tests.py --progress dots --exit-after-n-failures=5 --outdir=out/x64.optdebug cctest

# Run a specific test file
tools/run-tests.py --progress dots --exit-after-n-failures=5 --outdir=out/x64.optdebug cctest/test-heap
```

*Note: Always pass `--progress dots` so that there is minimal progress reporting, to avoid cluttering the output.*

## Interpreting Failures

If there are any failing tests, they will be reported along their stderr and a command to reproduce them e.g.

```
=== mjsunit/maglev/regress-429656023 ===
--- stderr ---
#
# Fatal error in ../../src/heap/local-factory.h, line 41
# unreachable code
#
...stack trace...
Received signal 6
Command: out/x64.optdebug/d8 --test test/mjsunit/mjsunit.js test/mjsunit/maglev/regress-429656023.js --random-seed=-190258694 --nohard-abort --verify-heap --allow-natives-syntax
```

You can retry the test either by running the test name with `tools/run-tests.py`, e.g. `tools/run-tests.py --progress dots --outdir=out/x64.optdebug mjsunit/maglev/regress-429656023`, or by running the command directly. When running the command directly, you can add additional flags to help debug the issue, and you can try running a different build (e.g. running a debug build if a release build fails).
