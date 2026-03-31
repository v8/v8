---
title: 'WebAssembly compilation pipeline'
description: 'This article explains V8’s WebAssembly compilers and when they compile WebAssembly code.'
---

WebAssembly is a binary format that allows you to run code from programming languages other than JavaScript on the web efficiently and securely. In this document we dive into the WebAssembly compilation pipeline in V8 and explain how we use the different compilers to provide good performance.

## Liftoff

Initially, V8 does not compile any functions in a WebAssembly module. Instead, functions get compiled lazily with the baseline compiler [Liftoff](/blog/liftoff) when the function gets called for the first time. Liftoff is a [one-pass compiler](https://en.wikipedia.org/wiki/One-pass_compiler), which means it iterates over the WebAssembly code once and emits machine code immediately for each WebAssembly instruction. One-pass compilers excel at fast code generation, but can only apply a small set of optimizations. Indeed, Liftoff can compile WebAssembly code very fast, tens of megabytes per second.

Once Liftoff compilation is finished, the resulting machine code gets registered with the WebAssembly module, so that for future calls to the function the compiled code can be used immediately.

## TurboFan

Liftoff emits decently fast machine code in a very short period of time. However, because it emits code for each WebAssembly instruction independently, there is very little room for optimizations, like improving register allocations or common compiler optimizations like redundant load elimination, strength reduction, or function inlining.

This is why _hot_ functions, which are functions that get executed often, get re-compiled with [TurboFan](/docs/turbofan), the optimizing compiler in V8 for both WebAssembly and JavaScript. TurboFan is a [multi-pass compiler](https://en.wikipedia.org/wiki/Multi-pass_compiler), which means that it builds multiple internal representations of the compiled code before emitting machine code. These additional internal representations allow optimizations and better register allocations, resulting in significantly faster code.

V8 monitors how often WebAssembly functions get called. Once a function reaches a certain threshold, the function is considered _hot_, and re-compilation gets triggered on a background thread. Once compilation is finished, the new code gets registered with the WebAssembly module, replacing the existing Liftoff code. Any new calls to that function will then use the new, optimized code produced by TurboFan, not the Liftoff code. Note though that we don’t do on-stack-replacement. This means that if TurboFan code becomes available after the function was called, the function call will complete its execution with Liftoff code.

## Code caching

If the WebAssembly module was compiled with `WebAssembly.compileStreaming` then the TurboFan-generated machine code will also get cached. When the same WebAssembly module is fetched again from the same URL then the cached code can be used immediately without additional compilation. More information about code caching is available [in a separate blog post](/blog/wasm-code-caching).

Code caching gets triggered whenever the amount of generated TurboFan code reaches a certain threshold. This means that for large WebAssembly modules the TurboFan code gets cached incrementally, whereas for small WebAssembly modules the TurboFan code may never get cached. Liftoff code does not get cached, as Liftoff compilation is nearly as fast as loading code from the cache.

## Debugging

As mentioned earlier, TurboFan applies optimizations, many of which involve re-ordering code, eliminating variables or even skipping whole sections of code. This means that if you want to set a breakpoint at a specific instruction, it might not be clear where program execution should actually stop. In other words, TurboFan code is not well suited for debugging. Therefore, when debugging is started by opening DevTools, all TurboFan code is replaced by Liftoff code again ("tiered down"), as each WebAssembly instruction maps to exactly one section of machine code and all local and global variables are intact.

## Profiling

To make things a bit more confusing, within DevTools all code will get tiered up (recompiled with TurboFan) again when the Performance tab is opened and the "Record" button in clicked. The "Record" button starts performance profiling. Profiling the Liftoff code would not be representative as it is only used while TurboFan isn’t finished and can be significantly slower than TurboFan’s output, which will be running for the vast majority of time.

## Flags for experimentation

For experimentation, V8 and Chrome can be configured to compile WebAssembly code only with Liftoff or only with TurboFan. It is even possible to experiment with lazy compilation, where functions only get compiled when they get called for the first time. The following flags enable these experimental modes:

- Liftoff only:
    - In V8, set the `--liftoff --no-wasm-tier-up` flags.
    - In Chrome, disable WebAssembly tiering (`chrome://flags/#enable-webassembly-tiering`) and enable WebAssembly baseline compiler (`chrome://flags/#enable-webassembly-baseline`).

- TurboFan only:
    - In V8, set the `--no-liftoff --no-wasm-tier-up` flags.
    - In Chrome, disable WebAssembly tiering (`chrome://flags/#enable-webassembly-tiering`) and disable WebAssembly baseline compiler (`chrome://flags/#enable-webassembly-baseline`).

- Lazy compilation:
    - Lazy compilation is a compilation mode where a function is only compiled when it is called for the first time. Similar to the production configuration the function is first compiled with Liftoff (blocking execution). After Liftoff compilation finishes, the function gets recompiled with TurboFan in the background.
    - In V8, set the `--wasm-lazy-compilation` flag.
    - In Chrome, enable WebAssembly lazy compilation (`chrome://flags/#enable-webassembly-lazy-compilation`).

## Compile time

There are different ways to measure the compilation time of Liftoff and TurboFan. In the production configuration of V8, the compilation time of Liftoff can be measured from JavaScript by measuring the time it takes for `new WebAssembly.Module()` to finish, or the time it takes `WebAssembly.compile()` to resolve the promise. To measure the compilation time of TurboFan, one can do the same in a TurboFan-only configuration.

![The trace for WebAssembly compilation in [Google Earth](https://earth.google.com/web).](/_img/wasm-compilation-pipeline/trace.svg)

The compilation can also be measured in more detail in `chrome://tracing/` by enabling the `v8.wasm` category. Liftoff compilation is then the time spent from starting the compilation until the `wasm.BaselineFinished` event, TurboFan compilation ends at the `wasm.TopTierFinished` event. Compilation itself starts at the `wasm.StartStreamingCompilation` event for `WebAssembly.compileStreaming()`, at the `wasm.SyncCompile` event for `new WebAssembly.Module()`, and at the `wasm.AsyncCompile` event for `WebAssembly.compile()`, respectively. Liftoff compilation is indicated with `wasm.BaselineCompilation` events, TurboFan compilation with `wasm.TopTierCompilation` events. The figure above shows the trace recorded for Google Earth, with the key events being highlighted.

More detailed tracing data is available with the `v8.wasm.detailed` category, which, among other information, provides the compilation time of single functions.
