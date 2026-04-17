---
name: git_cl
description: "Conventions for git cl commit messages in V8."
---

# Skill: Git CL Conventions

Use this skill to ensure correct formatting of commits and CLs in V8.

## Commit Message Format

-   **Title**: The title MUST follow the format `[component] Title`.
    -   **CORRECT**: `[compiler] Fix crash in loop unroller`
    -   **INCORRECT**: `[compiler]: Fix crash in loop unroller` (Do NOT use a colon after the component).
-   **Description**: Provide a clear explanation of the "why" and "what". Wrap lines at 72 characters.
-   **Tags**: All tag-like lines (e.g., `TAG=`, `BUG=`, `CONV=`) must be at the very bottom of the CL description.
    -   Always include `TAG=agy` for agent-generated changes.

## CL Description Guidelines

-   **Focus on Content & Effects**: The description should reflect the **contents** and **effects** of the changes, not the process or historical steps taken to get there (e.g., avoid describing 'removed hardcoded paths' unless it is a key feature of the CL).
-   **Highlight Important Details**: Focus on the rationale ('why') and key non-obvious design decisions.
-   **Conciseness**: Keep descriptions focused and avoid unnecessary wordiness.
-   **Keep Description Up-to-Date**: Always update the CL description to accurately reflect the *entire* set of changes in the CL. Do NOT simply append new descriptions to the old ones; rewrite or integrate them into a cohesive summary.

## Git Command Usage

-   **Avoid Interactive Pagers**: Always use `git --no-pager` or ensure `PAGER=cat` is set when running git commands that might produce long output (e.g., `git branch`, `git log`), to avoid getting stuck in a pager waiting for user input.
-   **Avoid Pagers in `git cl`**: Some `git cl` commands (like `desc`) might also use a pager. Use `git --no-pager cl ...` to avoid getting stuck.
-   **Always Format**: Always run `git cl format` before creating a commit or uploading a CL to ensure your code adheres to the style guide.
-   **Avoid Interactive Editors**: When committing or amending, if there is a risk of an editor opening (e.g., when amending or not using `-m`), prefix the command with `EDITOR=cat` (e.g., `EDITOR=cat git commit --amend`) to force non-interactive behavior.
-   **Timeout for Stuck Commands**: If a git command (like `checkout`, `commit`, `upload`) does not show progress within a reasonable time (e.g., 1 minute), kill the task immediately and retry with a better setup or report to the user. Do not let it run indefinitely.
-   **Check Status & Alerts**: Always run `git cl status` after uploading or when checking the state of a CL to identify failing checks or try jobs. Suggest addressing these alerts to the user.
-   **Branching for Unrelated Changes**: If you have changes that are unrelated to the current branch's purpose or active CL, **NEVER** upload them to the same CL or commit them to the same branch. You MUST create a new branch for these changes. Preferably, inform the user about the unrelated changes and ask how they want to proceed (e.g., create new branch, discard, or hold) before uploading.
-   **Base New Branches on Clean Upstream**: When creating a new branch for a clean set of changes or to fix a CL, always base it on a clean upstream (e.g., `origin/main`). Do not simply run `git checkout -b new-branch` from your current branch unless you are sure it is clean.
-   **Verify Process CLs**: For CLs intended to contain ONLY process/documentation changes (like skill updates), always verify that no code files (e.g., `.cc`, `.h`, `.js`, `.py`) are modified before uploading. Use `git diff --name-only origin/main` to check.
-   **Non-Interactive `git cl upload`**: When uploading a CL, do NOT use the `-m` flag as it is deprecated and can trigger interactive editors. Instead, use `-t` to set the patchset title (e.g., `git cl upload -t "Title"`) and use `--commit-description=+` or `--commit-description=-` to manage the description, in addition to using `EDITOR=cat`.
-   **Fetching Latest Patchset of a CL**: Use `git cl patch <CL_NUMBER>` to fetch and checkout the latest patchset of a CL.
