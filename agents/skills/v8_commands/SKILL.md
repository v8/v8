---
name: v8_commands
description: "Key commands for building, debugging, and testing in V8."
---

# V8 Key Commands

Use this skill to find the correct commands for common development tasks in V8. This unifies the commands listed in workspace configuration and ensures consistent usage. Refer to [env_abstraction](../env_abstraction/SKILL.md) for platform-specific overrides.

## Building

V8 uses `gm.py` as a wrapper around GN and Ninja for building.

-   **Build (Debug):** `tools/dev/gm.py quiet x64.debug tests`
-   **Build (Optimized Debug):** `tools/dev/gm.py quiet x64.optdebug tests`
-   **Build (Release):** `tools/dev/gm.py quiet x64.release tests`

*Note: Always pass the `quiet` keyword unless told otherwise to avoid excessive output.*

## Debugging

Run `d8` with GDB or LLDB for native code debugging.

```bash
# Example of running d8 with gdb
gdb --args out/x64.debug/d8 --my-flag my-script.js
```

### Common Diagnostic Flags
- `--trace-opt`: Log optimized functions.
- `--trace-deopt`: Log when and why functions are deoptimized.
- `--trace-gc`: Log garbage collection events.
- `--allow-natives-syntax`: Enables calling of internal V8 functions (e.g., `%OptimizeFunctionOnNextCall(f)`) from JavaScript for testing purposes.

A comprehensive list of all flags can be found by running `out/x64.debug/d8 --help`.

## Testing

For detailed instructions on running tests and interpreting failures, see the dedicated [v8_testing](../v8_testing/SKILL.md) skill.

Key commands summary:
-   **Run tests:** `tools/run-tests.py --progress dots --outdir=out/x64.optdebug`
