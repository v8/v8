---
name: execution_constraints
trigger: always_on
---

# Execution Constraints & Loop Prevention

To ensure efficient operation and prevent resource waste:
1. **No Tool Looping**: **DO NOT** execute the same tool call with the same arguments repeatedly. If a tool call fails or returns the same content, change your strategy (e.g., use `code_search` or `grep_search` instead of reading blindly).
2. **Surgical File Reading**: Do not read files from line 1 repeatedly. Pinpoint usage using search tools first.
3. **No Browser Usage**: **NEVER** try to open a browser or use browser-based tools if a page or resource is inaccessible through other specialized tools (like Buganizer MCP). Always ask the user for the content or guidance on how to proceed.
4. **Mandatory Orchestration**: For any complex task or project in V8, the agent MUST act as an Orchestrator and use the multi-layered skill framework defined in `agents/skills/`. Break down tasks and delegate to subagents to maximize parallelism. Avoid sequential execution of independent tasks.
5. **Avoid Interactive Pagers**: Always use `--no-pager` or ensure `PAGER=cat` is set when running commands that might produce long output (e.g., `git branch`, `git log`), to avoid getting stuck in a pager waiting for user input.
