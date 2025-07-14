# V8 Development Guide

This is the comprehensive workspace configuration and development guide for V8 when using Gemini.

**Official Documentation:** https://v8.dev/docs

## Quick Reference Commands

### Build Commands
```bash
# Debug build (full debugging info, assertions enabled)
tools/dev/gm.py x64.debug >/dev/null

# Optimized debug build (good balance for development)
tools/dev/gm.py x64.optdebug >/dev/null

# Release build (optimized for performance, use for benchmarking)
tools/dev/gm.py x64.release >/dev/null

# List all available configurations
tools/dev/gm.py >/dev/null
```

### Testing Commands
```bash
# Run all tests
tools/run-tests.py --progress dots --exit-after-n-failures=5 --outdir=out/x64.optdebug

# Run C++ unit tests only
tools/run-tests.py --progress dots --exit-after-n-failures=5 --outdir=out/x64.optdebug cctest unittests

# Run JavaScript tests only
tools/run-tests.py --progress dots --exit-after-n-failures=5 --outdir=out/x64.optdebug mjsunit

# Run a specific test
tools/run-tests.py --progress dots --exit-after-n-failures=5 --outdir=out/x64.optdebug mjsunit/array-join

# Run tests with additional filtering
tools/run-tests.py --progress dots --exit-after-n-failures=5 --outdir=out/x64.optdebug --filter="*regex*"
```

### Code Quality Commands
```bash
# Format code (always run before committing)
git cl format

# Check for style violations
git cl presubmit

# Run basic linting
tools/dev/gm.py x64.debug lint
```

## Development Context

### Key Principles
- **Security First:** V8 runs untrusted code, so correctness is paramount. Bugs often become security vulnerabilities.
- **Performance Critical:** V8 powers the web, so optimization is essential for user experience.
- **Web Standards Compliance:** Changes must align with ECMAScript and WebAssembly specifications.
- **Backward Compatibility:** Maintain compatibility with existing JavaScript code.

### Expert C++ Development
As an expert C++ developer working on V8, focus on:
- **Memory Safety:** Use V8's handle system correctly, be mindful of GC interactions
- **Performance:** Optimize hot paths, minimize allocations, use efficient data structures
- **Code Quality:** Follow established patterns, write clear comments, maintain testability
- **Security:** Validate inputs, handle edge cases, avoid undefined behavior

## Architecture Overview

### Core Components
V8 consists of several key subsystems that work together:

**Parsing & Compilation Pipeline:**
- **Parser** (`src/parsing/`) → **Ignition** (`src/interpreter/`) → **Maglev** (`src/maglev/`) → **TurboFan** (`src/compiler/`)

**Runtime Systems:**
- **Heap Management** (`src/heap/`) - Garbage collection and memory allocation
- **Object System** (`src/objects/`) - JavaScript value representation
- **Builtins** (`src/builtins/`) - Core JavaScript functionality
- **Runtime** (`src/runtime/`) - C++ functions callable from JavaScript

**Optimization & Execution:**
- **Inline Caching** (`src/ic/`) - Property access optimization
- **Deoptimization** (`src/deoptimizer/`) - Fallback from optimized code
- **Code Generation** (`src/codegen/`) - Machine code generation

## Detailed Folder Structure

### Source Code Organization (`src/`)

**Core Engine:**
- `src/api/` - V8 public C++ API implementation (see `include/` for declarations)
- `src/execution/` - Execution environment: Isolate, contexts, stack management
- `src/objects/` - JavaScript and internal object representations
- `src/runtime/` - C++ runtime functions callable from JavaScript
- `src/builtins/` - JavaScript built-in functions implementation

**Compilation Pipeline:**
- `src/parsing/` - JavaScript parser and scanner
- `src/ast/` - Abstract Syntax Tree representation
- `src/interpreter/` - Ignition bytecode compiler and interpreter
- `src/baseline/` - Sparkplug baseline compiler (bytecode → machine code)
- `src/maglev/` - Maglev mid-tier optimizing compiler
- `src/compiler/` - TurboFan optimizing compiler + Turboshaft

**Memory Management:**
- `src/heap/` - Garbage collector and memory management
- `src/handles/` - GC-safe object reference system
- `src/zone/` - Bump-pointer region-based zone allocator

**Code Generation:**
- `src/codegen/` - Machine code generation and metadata
  - Architecture-specific subdirectories (keep in sync!)
  - `compiler.cc` - Compiler entry points
  - Macro assemblers for direct code generation
  - CodeStubAssembler for higher-level code generation

**Specialized Components:**
- `src/ic/` - Inline Caching for property access optimization
- `src/deoptimizer/` - Optimization bailout handling
- `src/wasm/` - WebAssembly implementation
- `src/regexp/` - Regular expression engine
- `src/bigint/` - BigInt arithmetic operations
- `src/json/` - JSON parsing and serialization
- `src/strings/` - String operations and Unicode handling
- `src/numbers/` - Numeric operations

**Development & Debugging:**
- `src/d8/` - D8 shell implementation
- `src/debug/` - Debugger and debug protocol
- `src/inspector/` - DevTools inspector protocol
- `src/profiler/` - CPU and heap profilers
- `src/tracing/` - Event tracing system

**Platform & Infrastructure:**
- `src/base/` - Low-level utilities and platform abstraction
- `src/libplatform/` - Platform abstraction for task runners
- `src/init/` - V8 initialization
- `src/snapshot/` - Startup snapshots and code caching
- `src/logging/` - Logging infrastructure
- `src/sandbox/` - Memory sandbox security feature

**Language Features:**
- `src/torque/` - Torque domain-specific language
- `src/asmjs/` - Asm.js → WebAssembly pipeline

### Other Important Directories
- `test/` - Test suites and testing infrastructure
- `include/` - Public API headers for embedding V8
- `out/` - Build output directory (organized by configuration)
- `tools/` - Development tools and scripts
- `third_party/` - External dependencies

## Building V8

### Prerequisites
Ensure you have the necessary dependencies installed. See https://v8.dev/docs/build-gn for detailed setup instructions.

### Build Configurations
- **`debug`** - Full debug info, assertions enabled, slower execution
- **`optdebug`** - Optimized but with debug info, good for development
- **`release`** - Fully optimized, no debug info, use for benchmarking

### Advanced Build Options
```bash
# Build with specific GN arguments
tools/dev/gm.py x64.debug --gn-args='is_component_build=true'

# Build only specific targets
tools/dev/gm.py x64.debug d8 unittests

# Clean build
tools/dev/gm.py x64.debug --clean
```

**Important:** Always redirect stdout to `/dev/null` to avoid wasting tokens on compilation output. Errors will still appear on stderr.

## Debugging Guide

### Debug Builds
Use `debug` or `optdebug` builds for development work:
```bash
# Build debug version
tools/dev/gm.py x64.debug >/dev/null

# Run with debugger
gdb --args out/x64.debug/d8 --allow-natives-syntax my-script.js
lldb -- out/x64.debug/d8 --allow-natives-syntax my-script.js
```

### Essential V8 Flags
```bash
# Optimization tracing
--trace-opt                    # Log optimized functions
--trace-deopt                  # Log deoptimization events
--trace-opt-verbose            # Verbose optimization info

# Garbage collection
--trace-gc                     # Log GC events
--trace-gc-verbose             # Detailed GC information

# Compilation
--trace-ignition               # Trace bytecode generation
--trace-turbo                  # Trace TurboFan compilation

# Testing and development
--allow-natives-syntax         # Enable % functions for testing
--verify-heap                  # Verify heap integrity (debug builds)
--stress-runs=N               # Run with various stress modes

# Performance analysis
--prof                        # Generate profiling data
--perf-prof                   # Generate perf-compatible output
```

### Common Debug Scenarios
```bash
# Debug optimization issues
out/x64.debug/d8 --trace-opt --trace-deopt --allow-natives-syntax script.js

# Debug GC issues
out/x64.debug/d8 --trace-gc --verify-heap script.js

# Debug with internal function access
out/x64.debug/d8 --allow-natives-syntax --trace-opt script.js
```

### Debugging Torque Code
When debugging Torque-generated code:
1. Check generated C++ files in `out/<config>/gen/torque-generated/`
2. Use `--trace-turbo` to see TurboFan compilation of Torque builtins
3. Look at the CodeStubAssembler output for low-level implementation details

## Testing Framework

### Test Suite Overview
- **`unittests`** - Modern C++ unit tests (preferred)
- **`cctest`** - Legacy C++ unit tests (being migrated)
- **`mjsunit`** - JavaScript-based feature tests
- **`webkit`** - WebKit JavaScript test suite
- **`test262`** - ECMAScript conformance tests
- **`wasm-spec-tests`** - WebAssembly specification tests
- **`fuzzer`** - Fuzzing tests for robustness

### Running Tests
```bash
# Run all standard tests
tools/run-tests.py --progress dots --exit-after-n-failures=5 --outdir=out/x64.optdebug

# Run specific test suites
tools/run-tests.py --progress dots --outdir=out/x64.optdebug mjsunit cctest

# Run with parallel execution
tools/run-tests.py --progress dots --outdir=out/x64.optdebug -j8

# Run tests matching a pattern
tools/run-tests.py --progress dots --outdir=out/x64.optdebug --filter="*array*"

# Run a single test file
tools/run-tests.py --progress dots --outdir=out/x64.optdebug mjsunit/array-join
```

### Test Failure Analysis
When tests fail, you'll see output like:
```
=== mjsunit/array-join ===
--- stderr ---
TypeError: Array.prototype.join called on null or undefined
...
Command: out/x64.optdebug/d8 --test test/mjsunit/mjsunit.js test/mjsunit/array-join.js --random-seed=12345 --allow-natives-syntax
```

**Debugging failed tests:**
```bash
# Re-run single test
tools/run-tests.py --progress dots --outdir=out/x64.optdebug mjsunit/array-join

# Run the command directly for debugging
out/x64.optdebug/d8 --test test/mjsunit/mjsunit.js test/mjsunit/array-join.js --allow-natives-syntax

# Run with additional debug flags
out/x64.debug/d8 --test test/mjsunit/mjsunit.js test/mjsunit/array-join.js --allow-natives-syntax --trace-opt
```

## Code Style & Contribution Guidelines

### Style Guide
- Follow [Chromium's C++ Style Guide](https://chromium.googlesource.com/chromium/src/+/main/styleguide/styleguide.md)
- Match conventions in surrounding code
- Use `git cl format` before committing
- Keep diffs focused and minimal

### Commit Message Format
```
[component]: Brief description (72 chars max)

Detailed explanation of the change, including the reasoning
behind it. Wrap at 72 characters.

Bug: v8:12345
Change-Id: I1234567890abcdef...
```

**Components:** `api`, `compiler`, `runtime`, `heap`, `parser`, `interpreter`, `baseline`, `maglev`, `wasm`, `debugger`, `test`, etc.

### Code Review Process
1. Make your changes following the style guide
2. Run `git cl format` to format code
3. Run relevant tests to ensure no regressions
4. Create a commit with proper message format
5. Upload for review with `git cl upload`

## Working with Torque

### Overview
Torque is V8's domain-specific language for writing builtins and object definitions. It compiles to CodeStubAssembler (CSA) code, providing a higher-level, more maintainable way to implement performance-critical JavaScript functionality.

### Key Concepts
- **Purpose:** Simplify builtin implementation compared to raw CSA
- **Type Safety:** Strong typing prevents many runtime errors
- **Performance:** Compiles to efficient machine code
- **Maintainability:** More readable than equivalent CSA code

### File Organization
- **Source files:** `*.tq` (primarily in `src/builtins/` and `src/objects/`)
- **Generated files:** `out/<config>/gen/torque-generated/`
  - `*-tq.cc`, `*-tq.h`, `*-tq-inl.inc` - C++ implementations
  - `*-csa.cc`, `*-csa.h` - CodeStubAssembler implementations
  - `builtin-definitions.h` - Builtin registry
  - `class-definitions.h` - Object class definitions
  - `factory.cc` - Object factory functions

### Syntax Essentials
```torque
// Macro (inlined function)
macro AddSmis(a: Smi, b: Smi): Smi {
  return a + b;
}

// Builtin (standalone function)
builtin ArrayJoin(implicit context: Context)(
    receiver: JSReceiver, separator: JSAny): JSAny {
  // Implementation here
}

// JavaScript-callable builtin
transitioning javascript builtin ArrayPrototypeJoin(
    js-implicit context: NativeContext, receiver: JSAny)(
    separator: JSAny): JSAny {
  // Implementation here
}

// External C++ function declaration
extern macro IsJSArray(HeapObject): bool;

// Control flow with labels
macro TryFastPath(obj: JSAny): Smi labels SlowPath {
  const smi = Cast<Smi>(obj) otherwise goto SlowPath;
  return smi;
}
```

### Key Features
- **Labels and goto:** Efficient control flow for performance-critical paths
- **Type system:** Mirrors V8's object hierarchy for type safety
- **Implicit parameters:** `context`, `receiver` automatically passed
- **Transitioning:** Indicates functions that may change object maps
- **External integration:** Call C++ CSA functions with `extern`

### Development Workflow
1. **Identify the target:** Find the relevant `.tq` file or create a new one
2. **Implement in Torque:** Write the high-level Torque code
3. **Build and test:** Run `tools/dev/gm.py` to compile and test
4. **Debug if needed:** Check generated files in `out/<config>/gen/torque-generated/`
5. **Iterate:** Refine implementation based on performance and correctness

### Performance Considerations
- **Avoid allocations:** Use stack-allocated objects when possible
- **Minimize branches:** Use labels for exceptional cases
- **Cache lookups:** Store frequently accessed values in variables
- **Type checks:** Use `Cast<T>()` for safe, efficient type conversions

## Best Practices & Common Pitfalls

### Development Best Practices
- **Study existing code:** Understand patterns before implementing
- **Keep functions focused:** Single responsibility principle
- **Use appropriate data structures:** Choose based on performance requirements
- **Handle edge cases:** Consider null, undefined, and error conditions
- **Write tests:** Add comprehensive test coverage for new features

### Common Pitfalls to Avoid
- **Editing generated files:** Never modify files in `out/` directory
- **Ignoring GC safety:** Use Handle<T> for objects that might be collected
- **Inconsistent style:** Follow surrounding code conventions
- **Missing includes:** Be careful with forward declarations and inline functions
- **Unrelated changes:** Keep commits focused on specific changes
- **Wrong build configuration:** Match test build to development build

### Memory Management
- **Use handles:** Wrap HeapObject pointers in Handle<T> for GC safety
- **Zone allocation:** Use zone allocators for temporary objects
- **Avoid leaks:** Be careful with manual memory management
- **Understand GC:** Know when garbage collection can occur

### Performance Optimization
- **Profile first:** Use V8's profiling tools to identify bottlenecks
- **Hot path optimization:** Focus on frequently executed code
- **Minimize allocations:** Reuse objects when possible
- **Cache effectively:** Store computed values when beneficial
- **Inline judiciously:** Balance code size and performance

### Security Considerations
- **Validate inputs:** Check parameters from JavaScript code
- **Bounds checking:** Ensure array and string accesses are safe
- **Integer overflow:** Handle arithmetic carefully
- **Memory safety:** Avoid buffer overflows and use-after-free

## Useful Resources

### Documentation
- **V8 Official Docs:** https://v8.dev/docs
- **Torque Language:** https://v8.dev/docs/torque
- **Testing Guide:** https://v8.dev/docs/test
- **Build Instructions:** https://v8.dev/docs/build-gn
- **Contribution Guide:** https://v8.dev/docs/contribute

### Tools and Scripts
- **Build tool:** `tools/dev/gm.py`
- **Test runner:** `tools/run-tests.py`
- **Code formatter:** `git cl format`
- **Tick processor:** `tools/linux-tick-processor` (for profiling)

### Debugging Resources
- **Flag reference:** Run `out/x64.debug/d8 --help` for all flags
- **Internal functions:** Use `--allow-natives-syntax` for % functions
- **Profiling:** Use `--prof` and `tools/linux-tick-processor`
- **GC debugging:** Use `--trace-gc` and `--verify-heap`

### Community
- **Bug tracker:** https://crbug.com/v8
- **Code review:** https://chromium-review.googlesource.com
- **Mailing list:** v8-users@googlegroups.com
- **IRC:** #v8 on Freenode

---

This guide provides a comprehensive foundation for V8 development. For the most current information, always refer to the official V8 documentation at https://v8.dev/docs.
