# Gemini Workspace for V8

This is the workspace configuration for V8 when using Gemini.

Documentation can be found at https://v8.dev/docs.

## Folder structure

- src/: The main source folder providing the implementation of the virtual machine.
- test/: Folder containing most of the testing code.
- include/: Folder containing all of V8's public API that is used when V8 is embedded in other projects such as e.g. the Blink rendering engine.
- out/: Folder containing the results of a build. Usually organized in sub folders for the respective configurations.

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
