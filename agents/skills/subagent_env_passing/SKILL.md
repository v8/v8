---
name: subagent_env_passing
description: "Workflow for passing environment variables (like PATH) from the Orchestrator to subagents."
---

# Subagent Environment Passing Skill

This skill documents how to ensure subagents have access to the correct environment variables (such as `PATH`) when working in isolated environments like worktrees.

## Workflow

1.  **Retrieve Environment**: The Orchestrator should read the necessary environment variables from its own context if they are not already known. For example, run `echo $PATH` to get the current PATH.
2.  **Pass to Subagent**: When invoking a subagent, include the environment variable values in the `Prompt` provided to the subagent.
3.  **Instruct Subagent**: Explicitly instruct the subagent to set these variables in its environment before running tools that depend on them.

## Example Prompt Snippet

```
Please use the following PATH in your environment:
export PATH=<depot tools path>:$PATH
```
Note: Replace the path above with the actual path retrieved in step 1.
