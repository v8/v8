#!/bin/bash
# Copyright 2026 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# Helper to remove worktrees and clean up after a task.

TASK_ID=$1
if [ -z "$TASK_ID" ]; then
  echo "Usage: cleanup_worktree.sh <task_id>"
  exit 1
fi

COMMON_DIR=$(git rev-parse --git-common-dir 2>/dev/null)
if [ -z "$COMMON_DIR" ]; then
  echo "Error: Not in a git repository."
  exit 1
fi
V8_ROOT="$(cd "$COMMON_DIR/.." && pwd)"

BRANCH_NAME="task-$TASK_ID"
WORKTREE_PATH="$V8_ROOT/worktrees/$BRANCH_NAME"

git worktree remove --force "$WORKTREE_PATH" > /dev/null 2>&1
git branch -D "$BRANCH_NAME" > /dev/null 2>&1
