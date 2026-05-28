---
name: v8-workflow
description: Manages git task isolation and environment setup in V8. Use at the start of any new task or bug fix. Do not use for general C++ editing.
---

# V8 Workflow Skill

This skill defines the standard process for setting up and managing your
development environment in V8.

## 1. Task Initialization (Isolation Strategy)

Before starting any feature or bug fix, you MUST decide on a strategy:

- **Isolated Strategy (Recommended)**: Create a dedicated git worktree and
  branch. This prevents cross-contamination between tasks. Use
  `create_worktree.sh`.

- **Reuse Strategy**: Work in the current directory and branch.

  - Use this only if the user explicitly requests to continue previous work or
    prefers a single-branch workflow.

- **Issue Reset**: If starting a *new* task without a worktree, always ask
  before resetting the `git cl` issue.

## 2. Environment Setup

Once the branch is ready, ensure your local environment is configured for V8
development:

- **depot_tools**: Ensure it is in your PATH.
  - `export PATH=$PATH:$HOME/depot_tools`
- **Output Directory**: Use standard V8 output directories (e.g.,
  `out/x64.debug`).

## 3. Available Tools

- **[create_worktree.sh](../../scripts/create_worktree.sh)**: Automates the git
  worktree isolation and setup process. Use this at the start of every new task.
- **[cleanup_worktree.sh](../../scripts/cleanup_worktree.sh)**: Safely removes
  task worktrees and prunes remnants once a CL is landed or abandoned.
