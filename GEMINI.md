# Gemini Workspace for V8

This is the workspace configuration for V8 when using Gemini.

For understanding V8 concepts and structure, refer to the [v8_understanding](agents/skills/v8_understanding/SKILL.md) skill.

Some hints:
- You are an expert C++ developer.
- V8 is shipped to users and running untrusted code; make sure that the code is absolutely correct and bug-free as correctness bugs usually lead to security issues for end users.
- V8 is providing support for running JavaScript and WebAssembly on the web. As such, it is critical to aim for best possible performance when optimizing V8.

## Workspace Skills

To keep this configuration light, detailed information has been moved to specialized skills. Use them on demand:
- **If setup seems missing, read the V8 Setup skill.**

-   **Folder Structure**: Understand the layout of the V8 repository. See [v8_structure](agents/skills/v8_structure/SKILL.md).
-   **Key Commands**: Find commands for building and debugging. See [v8_commands](agents/skills/v8_commands/SKILL.md).
-   **Testing**: Detailed guide for running and interpreting tests. See [v8_testing](agents/skills/v8_testing/SKILL.md).
-   **Best Practices**: Common pitfalls and fix proposal guidelines. See [v8_best_practices](agents/skills/v8_best_practices/SKILL.md).
-   **Setup**: Handles missing dependencies and configuration for V8 tools. See [v8_setup](agents/skills/v8_setup/SKILL.md).
-   **Git CL Conventions**: Commit message format and usage. See [git_cl](agents/skills/git_cl/SKILL.md).

## Coding and Committing

- Always follow the style conventions used in code surrounding your changes.
- Otherwise, follow [Chromium's C++ style guide](https://chromium.googlesource.com/chromium/src/+/main/styleguide/styleguide.md).
- Use `git cl format` to automatically format your changes.
- Follow [git_cl](agents/skills/git_cl/SKILL.md) for commit conventions.
- For best practices and common pitfalls, see [v8_best_practices](agents/skills/v8_best_practices/SKILL.md).



## Agent Framework

- **Mandatory Orchestration**: For any task in V8, the agent MUST act as an Orchestrator and use the multi-layered skill framework defined in `agents/skills/`.
- Follow the rules in `agents/rules/framework.md` and `agents/rules/execution_constraints.md`.
- This ensures efficiency, parallelism, and consistency across all tasks.
