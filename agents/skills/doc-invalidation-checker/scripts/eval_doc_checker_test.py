#!/usr/bin/env vpython3
# Copyright 2026 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Unit tests for eval_doc_checker.py and its promptfoo.yaml test cases."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import eval_doc_checker

REPO_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO_ROOT / "agents" / "testing"))
import lint_promptfoo_testcases  # pylint: disable=wrong-import-position


class EvalDocCheckerTest(unittest.TestCase):

  def setUp(self):
    self.temp_dir = tempfile.TemporaryDirectory()
    self.root = Path(self.temp_dir.name)
    self.repo_dir = self.root / "repo"
    self.worktree_root = self.root / "worktrees"
    self.repo_dir.mkdir(parents=True)

    self._git(self.repo_dir, "init", "-b", "main")
    self._git(self.repo_dir, "config", "user.name", "Test User")
    self._git(self.repo_dir, "config", "user.email", "test@example.com")

    # Copy real src/maglev/maglev-compiler.cc so git apply works on both
    # maglev-max-stack-slots and internal-noop-refactor patches.
    real_compiler_cc = (REPO_ROOT / "src" / "maglev" /
                        "maglev-compiler.cc").read_text(encoding="utf-8")
    self._write("src/maglev/maglev-compiler.cc", real_compiler_cc)
    self._write(
        "docs/compiler/maglev/compiler-maglev.md",
        ("Here `constexpr uint32_t kMaxStackSlots = 4 * KB / "
         "kSystemPointerSize` (512 slots on 64-bit).\n"),
    )
    self._write("docs/security/triaging.md", "# Manual Triaging Guide\n")
    self._git(self.repo_dir, "add", "-A")
    self._git(self.repo_dir, "commit", "-m", "Initial commit")

  def tearDown(self):
    self.temp_dir.cleanup()

  def _git(self, cwd: Path, *args: str) -> str:
    res = subprocess.run(
        ["git", "--no-pager", *args],
        cwd=cwd,
        capture_output=True,
        text=True,
        check=True,
    )
    return res.stdout

  def _write(self, rel_path: str, content: str, base: Path | None = None):
    target_root = base or self.repo_dir
    path = target_root / rel_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")

  def test_all_promptfoo_yaml_and_patches_valid_in_real_v8_checkout(self):
    self.assertEqual(5, len(eval_doc_checker.EVAL_CASES))
    yaml_files = [
        str(REPO_ROOT / case.yaml_path)
        for case in eval_doc_checker.EVAL_CASES.values()
    ]
    self.assertEqual(0, lint_promptfoo_testcases.main(["prog", *yaml_files]))

    for case_id, case in eval_doc_checker.EVAL_CASES.items():
      with self.subTest(case_id=case_id):
        for patch_rel in case.patches:
          patch_path = REPO_ROOT / patch_rel
          self.assertTrue(patch_path.is_file(), f"Missing patch {patch_rel}")
          subprocess.run(
              ["git", "apply", "--check",
               str(patch_path)],
              cwd=REPO_ROOT,
              check=True,
          )
        for doc_path in [*case.must_modify, *case.must_not_modify]:
          target = REPO_ROOT / doc_path
          self.assertTrue(target.exists(),
                          f"Missing doc target {doc_path} in {case_id}")

  def test_positive_case_fails_before_fix_and_passes_after_fix(self):
    case = eval_doc_checker.EVAL_CASES["maglev-max-stack-slots"]
    meta = eval_doc_checker.setup_case(
        case,
        self.worktree_root,
        repo_root=self.repo_dir,
        mode="explicit",
        diff_kind="committed",
    )
    wt_dir = Path(meta["worktree_dir"])
    try:
      initial_grade = eval_doc_checker.grade_case(case, wt_dir)
      self.assertFalse(initial_grade["passed"])
      self.assertEqual([], initial_grade["modified_docs"])

      self._write(
          "docs/compiler/maglev/compiler-maglev.md",
          ("Here `constexpr uint32_t kMaxStackSlots = 8 * KB / "
           "kSystemPointerSize` (1024 slots on 64-bit).\n"),
          base=wt_dir,
      )
      passing_grade = eval_doc_checker.grade_case(case, wt_dir)
      self.assertTrue(passing_grade["passed"], passing_grade["failures"])
      self.assertEqual(
          ["docs/compiler/maglev/compiler-maglev.md"],
          passing_grade["modified_docs"],
      )

      # Also passes if the agent staged and amended the commit.
      self._git(wt_dir, "add", "docs/compiler/maglev/compiler-maglev.md")
      self._git(wt_dir, "commit", "--amend", "--no-edit")
      amended_grade = eval_doc_checker.grade_case(case, wt_dir)
      self.assertTrue(amended_grade["passed"], amended_grade["failures"])
    finally:
      eval_doc_checker.cleanup_case(
          case.case_id, self.worktree_root, repo_root=self.repo_dir)

  def test_guardrail_rejects_modifying_triaging_md_or_em_dashes(self):
    case = eval_doc_checker.EVAL_CASES["maglev-max-stack-slots"]
    meta = eval_doc_checker.setup_case(
        case,
        self.worktree_root,
        repo_root=self.repo_dir,
        mode="implicit",
        diff_kind="uncommitted",
    )
    wt_dir = Path(meta["worktree_dir"])
    try:
      self._write(
          "docs/compiler/maglev/compiler-maglev.md",
          ("Here `constexpr uint32_t kMaxStackSlots = 8 * KB / "
           "kSystemPointerSize` \u2014 1024 slots on 64-bit "
           "[bad](missing.md).\n"),
          base=wt_dir,
      )
      self._write("docs/security/triaging.md", "# Modified!\n", base=wt_dir)

      grade = eval_doc_checker.grade_case(case, wt_dir)
      self.assertFalse(grade["passed"])
      joined = "\n".join(grade["failures"])
      self.assertIn("docs/security/triaging.md", joined)
      self.assertIn("Unicode em-dash (U+2014)", joined)
      self.assertIn("Broken relative markdown links", joined)
    finally:
      eval_doc_checker.cleanup_case(
          case.case_id, self.worktree_root, repo_root=self.repo_dir)

  def test_negative_noop_case_passes_only_when_no_docs_touched(self):
    case = eval_doc_checker.EVAL_CASES["internal-noop-refactor"]
    meta = eval_doc_checker.setup_case(
        case,
        self.worktree_root,
        repo_root=self.repo_dir,
        mode="explicit",
        diff_kind="committed",
    )
    wt_dir = Path(meta["worktree_dir"])
    try:
      grade_clean = eval_doc_checker.grade_case(case, wt_dir)
      self.assertTrue(grade_clean["passed"], grade_clean["failures"])

      self._write(
          "docs/compiler/maglev/compiler-maglev.md",
          "Unnecessary churn.\n",
          base=wt_dir,
      )
      grade_churn = eval_doc_checker.grade_case(case, wt_dir)
      self.assertFalse(grade_churn["passed"])
      self.assertIn(
          "docs/compiler/maglev/compiler-maglev.md",
          "\n".join(grade_churn["failures"]),
      )
    finally:
      eval_doc_checker.cleanup_case(
          case.case_id, self.worktree_root, repo_root=self.repo_dir)


if __name__ == "__main__":
  unittest.main()
