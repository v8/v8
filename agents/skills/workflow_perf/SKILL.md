---
name: workflow_perf
description: "Workflow for performance and memory evaluation in V8."
---

# Workflow: Performance Evaluation

Use this skill when tasked with improving the performance or memory usage of a workload in V8. This workflow focuses on identifying bottlenecks and applying V8-side optimizations.

## Activation Criteria
-   User requests to optimize a specific benchmark or script.
-   Goal is to reduce execution time, CPU cycles, or memory footprint.

## Core Principles

1.  **Data-Driven**: Always base optimization decisions on profiling data, not intuition.
2.  **V8-Centric**: For V8 engineers, performance work usually means changing V8 to better handle the JS pattern, rather than changing the JS itself (though both are valid for general users).
3.  **Holistic View**: Look for general efficiency improvements, not just the single hottest function.
4.  **Contextual Interpretation**: Assume unknown terms in performance tasks are likely benchmark names or domain-specific concepts, not environmental terms (e.g., "WSL" is likely a JetStream story, not the OS). Verify before assuming. If in doubt, ask the user.
5.  **Mandatory Orchestration**: The agent executing this workflow MUST act as an Orchestrator. Do not run benchmarks, profiles, or searches sequentially yourself. Break down the work and delegate to subagents to maximize parallelism.

## Workflow

### Planning
For performance analysis, you do NOT need to create a full `implementation_plan.md` until you are actually *fixing* the performance issue you've detected. Instead, maintain an **Analysis Plan** (e.g., in `task.md` or as a list of questions to answer) to guide the investigation.

### 1. Parallel Track Initialization
Initialize the following tracks concurrently:
-   **Track A: Profiling & Tracing**:
    -   Run the workload under `perf` (e.g., `perf stat`, `perf record`).
    -   Use V8 tracing flags to gather specific runtime telemetry.
-   **Track B: JS Source Analysis**:
    -   Study the JavaScript benchmark to understand the core operations and potential hotspots.
-   **Track C: Static V8 Research**:
    -   Search for known optimization patterns or issues related to the observed JS patterns in the V8 codebase.

### 2. Specific V8 Tracing Flags
Use these flags to diagnose performance issues:
-   `--prof`: Generates profiling data (`v8.log`) for tick processor analysis.
-   `--trace-opt`: Logs when functions are optimized by TurboFan/Maglev. Useful to check if hot functions are getting optimized.
-   `--trace-deopt`: Logs when functions are deoptimized and the reason. Critical for fixing performance cliffs.
-   `--trace-ic`: Logs inline cache state changes. High miss rates indicate polymorphic or megamorphic behavior.
-   `--trace-gc`: Logs garbage collection events. High frequency indicates excessive allocations.
-   `--trace-maps`: Logs map creation and transitions. Useful for debugging map stability issues.

### 3. Running Benchmarks with Crossbench
Crossbench is the central benchmark runner that should always be used to run things like JetStream.
-   **Location**: It is often installed in `~/crossbench`, but could be anywhere. If not found in standard locations, ask the user for the path.
-   **Update**: Always ensure you are running the latest version.
-   **Basic Usage**:
    -   Navigate to the crossbench directory.
    -   **MANDATORY**: Use `poetry run cb` instead of just running `./cb.py`.
    ```bash
    # Run JetStream3 with a specific d8 binary
    poetry run cb jetstream --browser=/path/to/d8 --env-validation=warn
    ```
-   **Profiling with Probes**:
    -   Use `--probe='v8.log'` to generate a `v8.log` file.
    -   Use `--probe='perf'` to generate a linux-perf trace.
    -   Use `poetry run cb describe probes` to see all available probes.
-   **Best Practices (from Crossbench GEMINI.md)**:
    -   Use `--env-validation=warn` to bypass environment input prompts.
    -   Use `poetry run cb describe` to understand subcommands and probes.
    -   Prefer creating JSON files instead of HJSON for configurations to minimize quoting errors.
    -   Validate generated configs with `poetry run cb_validate_hjson -- file.hjson`.
-   **Known Stories**: Be aware that 'WSL' in JetStream3 refers to the **WebGPU Shading Language** workload, not the Windows Subsystem for Linux environment.
-   **Environment Fallbacks**: If a recommended tool (like `poetry` for Crossbench) is missing or failing, fall back to alternatives immediately (like `jsb_run_bench`) or ask the user, instead of spending many turns trying to fix the environment.

### 4. Alternative: Running Benchmarks with jsb_run_bench
In the `jetski` environment, you can also use the `jsb_run_bench` tool from `v8-utils` as an alternative for quick runs.
-   **Run for Scores**: Call `jsb_run_bench` with paths to `d8` binaries to compare performance.
-   **Profile**: Supports `record: "perf"` and `record: "v8log"`.

### 5. Profile Analysis & Tick Processor
-   **Generate Profile**: Run `d8 --prof script.js` or use Crossbench with `--probe='v8.log'`.
-   **Analyze with Tick Processor**: Run `tools/node-tick-processor v8.log` or use `v8log_analyze` if available.
-   **Interpretation**:
    -   Look at the C++ entry points and JS functions taking the most ticks.
    -   Check if time is spent in runtime functions vs. generated code.
    -   Identify if specific builtins are taking significant time.

### 6. Tracing Compiler Graphs (Turbolizer)
For peak performance, it is often necessary to inspect the intermediate representations (IR) of the optimizing compiler (TurboFan or Turboshaft).
-   **Generate Graph Data**: Run `d8 --trace-turbo script.js` or pass flags in Crossbench/jsb_run_bench. This generates JSON files containing the graph state at various optimization phases (e.g., `turbo-*.json`).
-   **Visualize with Turbolizer**: Use the Turbolizer tool (available internally at `go/turbolizer` or in the V8 repository under `tools/turbolizer`).
-   **Analysis**:
    -   Inspect the graph at different phases to see how nodes are simplified, combined, or eliminated.
    -   Look for missed optimizations, such as redundant checks that were not hoisted or allocations that failed to be eliminated by escape analysis.
    -   Identify unexpected deoptimization points.

### 7. Identifying General Efficiency Improvements
Beyond hotspots, look for areas where V8 can be improved to handle patterns better:
-   **Reducing Allocations**: High GC overhead implies frequent allocations. Investigate if V8 can optimize allocation folding, escape analysis, or if the allocations are unavoidable.
-   **Optimizing Hot Loops**: Ensure loops are not deoptimizing in V8. Check if checks can be hoisted by the compiler or if loop peeling is effective in the VM.
-   **Hidden Class (Map) Stability**: Understand how object shapes evolve and cause polymorphic or megamorphic IC states. Investigate if V8 can be optimized to handle these transitions better.

### 8. Analysis & Reprioritization
-   Analyze profile results (e.g., flamegraphs, top functions).
-   **Dynamic Reprioritization**:
    -   **High GC Time**: If profile shows significant time in GC, pivot to allocation analysis and reducing memory churn.
    -   **High IC Misses**: If `--trace-ic` shows frequent misses, pivot to investigating object layout and stabilizing hidden classes.
    -   **Dominant Hotspot**: If a single function dominates execution time, focus all efforts on that component.
    -   **Pattern Identification**: If a V8 change is identified that could improve the pattern generally, prioritize implementing and testing it over further analysis.

### 9. Optimization & Verification
-   Propose a V8 change to improve performance (e.g., specialized builtin, improved optimization pass).
-   Verify by:
    -   Re-running the benchmark.
    -   Comparing `perf` stats (cycles, instructions).
    -   Ensuring no regressions in correctness or other benchmarks.
