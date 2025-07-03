# Gemini Workspace for V8

This is the workspace configuration for V8 when using Gemini.

Documentation can be found at https://v8.dev/docs.

Some hints:
- You are an expert C++ developer.
- V8 is shipped to users and running untrusted code; make sure that the code is absolutely correct and bug-free as correctness bugs usually lead to security issues for end users.
- V8 is providing support for running JavaScript and WebAssembly on the web. As such, it is critical to aim for best possible performance when optimizing V8.

## Folder structure

- `src/`: The main source folder providing the implementation of the virtual machine.
- `test/`: Folder containing most of the testing code.
- `include/`: Folder containing all of V8's public API that is used when V8 is embedded in other projects such as e.g. the Blink rendering engine.
- `out/`: Folder containing the results of a build. Usually organized in sub folders for the respective configurations.

## Building

The full documentation for building using GN can be found at https://v8.dev/docs/build-gn.

Once the initial dependencies are installed, V8 can be built using `gm.py`, which is a wrapper around GN and Ninja.

```bash
# List all available build configurations and targets
tools/dev/gm.py

# Build the d8 shell for x64 in release mode
tools/dev/gm.py x64.release

# Build d8 for x64 in debug mode
tools/dev/gm.py x64.debug
```

- **release:** Optimized for performance, with debug information stripped. Use for benchmarking.
- **debug:** Contains full debug information and enables assertions. Slower, but essential for debugging.
- **optdebug:** A compromise with optimizations enabled and debug information included. Good for general development.

## Testing

The primary script for running tests is `tools/run-tests.py`. You specify the build output directory and the tests you want to run.

```bash
# Run all standard tests for the x64.release build
tools/run-tests.py --outdir=out/x64.release

# Run a specific test suite (e.g., cctest)
tools/run-tests.py --outdir=out/x64.release cctest

# Run a specific test file
tools/run-tests.py --outdir=out/x64.release cctest/test-heap
```

The full testing documentation is at https://v8.dev/docs/test.

## Coding and Committing

- Always follow the style conventions used in code surrounding your changes.
- Otherwise, follow [Chromium's C++ style guide](https://chromium.googlesource.com/chromium/src/+/main/styleguide/styleguide.md).
- Use `git cl format` to automatically format your changes.

Commit messages should follow the convention described at https:/v8.dev/docs/contribute#commit-messages. A typical format is:

```
[component]: Short description of the change

Longer description explaining the "why" of the change, not just
the "what". Wrap lines at 72 characters.

Bug: 123456
```

- The `component` is the area of the codebase (e.g., `compiler`, `runtime`, `api`).
- The `Bug:` line is important for linking to issues in the tracker at https://crbug.com/
