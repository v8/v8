# Copyright 2026 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

USE_PYTHON3 = True


def _CheckPromptfooTestCases(input_api, output_api):
  """Checks that *.promptfoo.yaml files are valid."""
  promptfoo_files = [
      f.AbsoluteLocalPath()
      for f in input_api.AffectedFiles(include_deletes=False)
      if f.LocalPath().endswith('.promptfoo.yaml')
  ]
  if not promptfoo_files:
    return []

  linter_path = input_api.os_path.join(input_api.PresubmitLocalPath(),
                                       'testing', 'lint_promptfoo_testcases.py')
  cmd = ['vpython3', linter_path] + promptfoo_files
  try:
    p = input_api.subprocess.Popen(
        cmd,
        stdout=input_api.subprocess.PIPE,
        stderr=input_api.subprocess.PIPE,
    )
    stdout, stderr = p.communicate()
  except OSError as e:
    return [
        output_api.PresubmitError(
            f'Failed to run promptfoo linter: {e}.\nIs vpython3 in your PATH?')
    ]

  if p.returncode != 0:
    message = (f'Promptfoo linter ({linter_path}) failed with exit code '
               f'{p.returncode}.')
    long_text = (f'STDOUT:\n{stdout.decode("utf-8", "replace")}\n'
                 f'STDERR:\n{stderr.decode("utf-8", "replace")}\n')
    return [output_api.PresubmitError(message, long_text=long_text)]

  return []


def _CheckPromptfooEvalAssociation(input_api, output_api):
  """Checks prompt.md and SKILL.md files have associated promptfoo evals."""
  prompt_or_skill_added = False
  eval_file_affected = False

  for f in input_api.AffectedFiles(include_deletes=False):
    basename = input_api.os_path.basename(f.AbsoluteLocalPath())
    if f.Action() == 'A' and basename.lower() in ('prompt.md', 'skill.md'):
      prompt_or_skill_added = True
    elif f.Action() in ('A', 'M') and basename.endswith('.promptfoo.yaml'):
      eval_file_affected = True

  if prompt_or_skill_added and not eval_file_affected:
    return [
        output_api.PresubmitPromptWarning(
            'Added prompt.md or SKILL.md but no promptfoo evaluation '
            'file (*.promptfoo.yaml) was modified or added.\n'
            'It is highly encouraged to add an evaluation under '
            'agents/prompts/eval/ to validate your prompt or skill changes.')
    ]
  return []


def _CommonChecks(input_api, output_api):
  results = []
  results.extend(_CheckPromptfooTestCases(input_api, output_api))
  results.extend(_CheckPromptfooEvalAssociation(input_api, output_api))

  tests = []
  for directory in (
      'scripts',
      'skills/cl-priority-prefix/scripts',
      'skills/clusterfuzz/scripts',
      'skills/doc-invalidation-checker/scripts',
  ):
    tests.extend(
        input_api.canned_checks.GetUnitTestsInDirectory(
            input_api,
            output_api,
            directory,
            files_to_check=[r'.+_test\.py$'],
            run_on_python2=False,
        ))
  results.extend(input_api.RunTests(tests))
  return results


def CheckChangeOnCommit(input_api, output_api):
  return _CommonChecks(input_api, output_api)


def CheckChangeOnUpload(input_api, output_api):
  return _CommonChecks(input_api, output_api)
