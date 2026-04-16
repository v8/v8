# Git CL Workflow Skill

This skill defines the standard process for managing Gerrit ChangeLists (CLs) in V8.

## 1. Setup & Environment (Pre-requisite: `v8-workflow`)

Before performing any CL operations, ensure you have consulted the user regarding the isolation strategy as defined in the `v8-workflow` skill.
- **Isolation**: Confirm whether the user wants to operate in the current branch or a new worktree.
- **Issue Association**: Verify you are targeting the correct CL using `git cl status`. If reusing a CL, do NOT run `git cl issue 0`.

## 2. Avoiding Interactive Hangs (No-Vim Policy)

Jetski cannot interact with Vim or other terminal-based text editors. To avoid hanging the session, you MUST use non-interactive commit and upload methods:

- **Commit Flags**: Always use non-interactive commit methods.
- **Manual Message Injection**: If the script is unavailable, use `-m` or `-F`.
  - `git commit -m "Your message"`
  - `git commit -F commit_msg.txt`
- **Empty Patchsets**: NEVER upload an empty patchset.
  - Before uploading, verify `git diff HEAD^ HEAD` is not empty.
  - If the changes are already live, inform the user and skip the upload.

To ensure future auditability and clarity, every commit message MUST follow these guidelines:

1.  **Summarize the Entire Change**: Provide a clear, holistic overview of the purpose and impact of the entire change.
2.  **Professional Tone**: Use concise, declarative, and natural engineering language.
3.  **Prefix Formatting**:
    - Use `[agents] title` (no colon) ONLY for changes to the agent automation suite itself.
    - For standard V8 engineering tasks, use the appropriate component prefix (e.g., `[compiler]`, `[parser]`, `[wasm]`, `[maglev]`).

## 4. Uploading to Gerrit (Protecting Patch History)

To prevent breaking the diff history in Gerrit (e.g., "Base not found" or messy inter-patchset diffs), you should follow these guidelines:

- **Committing Changes**: Prefer creating **new local commits** for incremental changes instead of amending, as it preserves local history and is often less annoying. `git cl upload` will still update the same CL as long as the branch is associated correctly.
- **Descriptive Patch Message**: Always provide a meaningful message for each patchset during upload.
  - `git cl upload -f -m "Brief description of what changed since the last patchset"`
- **Preserve Change-Id**: Ensure the `Change-Id` footer is preserved in the commit message.

- **Mis-targeting Safeguards**:
  - Before uploading, run `git cl status` to verify the Issue Description matches your task.
  - Review the modified files (`git show --stat`) to ensure no unrelated files are being swept in.
  - If the CL context (e.g., subsystem) seems mismatched, STOP and ask the user for confirmation.

## 5. Post-Upload Requirements

Immediately after a successful upload:
- **Show CL Link**: Always provide the `chromium-review.googlesource.com` link to the user.
- **Context Preservation**: Tag your commit message with `TAG=agy` and the conversation ID.
