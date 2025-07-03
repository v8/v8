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

Once the initial dependencies are installed V8 can generally be built with e.g.
```
gclient sync
tools/dev/gm.py x64.release
```

The tool `gm.py` lists all its available targets and configuration modes.
A common target is x64.optdebug.d8 to build V8's shell called `d8` for the x64 architecture in optdebug mode.

For the general build you can suppress stdout but should keep logging stderr.

## Coding

- Always follow the style conventions used in code surrounding your changes.
- Otherwise, follow Chromium's C++ style guide with a fallback to Google's C++ style guide.
- Use `git cl format` on your changes.
