# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from enum import Enum
from pathlib import Path

import logging
import re
import shutil
import sys

from blinkpy.common.system.log_utils import configure_logging
from blinkpy.w3c.common import read_credentials
from blinkpy.w3c.test_importer import TestImporter

TEST_FILE_REFERENCE_IN_STATUS_FILE = re.compile("^\s*'(.*)':.*,$")
TEST262_FAILURE_LINE = re.compile("^test262/(.*) default: FAIL")
TEST262_REPO_URL = 'https://chromium.googlesource.com/external/github.com/tc39/test262'
V8_TEST262_ROLLS_META_BUG = 'v8:7834'

_log = logging.getLogger(__file__)


class GitFileStatus(Enum):
  ADDED = 'A'
  DELETED = 'D'
  MODIFIED = 'M'
  UNKOWN = 'X'


class V8TestImporter(TestImporter):

  def __init__(self,
               phase,
               host,
               test262_github=None,
               test262_failure_file=None):
    super().__init__(host, test262_github, wpt_manifests=None)
    self.project_config = host.project_config
    self.project_root = Path(self.project_config.project_root)
    self.test262_status_file = self.project_root / 'test' / 'test262' / 'test262.status'
    self.phase = phase
    self.test262_failure_file = test262_failure_file

  def main(self, argv=None):
    options = self.parse_args(argv)
    self.verbose = options.verbose

    log_level = logging.DEBUG if self.verbose else logging.INFO
    configure_logging(logging_level=log_level, include_time=True)

    if options.auto_update and options.auto_upload:
      _log.error('--auto-upload and --auto-update cannot be used together.')
      return False

    credentials = read_credentials(self.host, options.credentials_json)
    gh_user = credentials.get('GH_USER')
    gh_token = credentials.get('GH_TOKEN')
    if not gh_user or not gh_token:
      _log.warning('You have not set your GitHub credentials. This '
                   'script may fail with a network error when making '
                   'an API request to GitHub.')
      _log.warning('See https://chromium.googlesource.com/chromium/src'
                   '/+/main/docs/testing/web_platform_tests.md'
                   '#GitHub-credentials for instructions on how to set '
                   'your credentials up.')
    self.github = self.project_config.github_factory(self.host, gh_user,
                                                     gh_token)

    test262_revision = self.fetch_test262(gh_token)
    v8_test262_revision = self.find_current_test262_revision()

    if self.run_prebuild_phase():
      if test262_revision == v8_test262_revision:
        _log.info(
            f'No changes to import. {test262_revision} == {v8_test262_revision}'
        )
        return False

      _log.info(f'Importing test262@{test262_revision} to V8')
      self.roll_as_dependency(test262_revision)
      self.overwrite_files()
      self.sync_folders(v8_test262_revision, test262_revision)

    failure_lines = []
    if self.run_build_phase():
      failure_lines = self.build_and_test()

    # We either have the lines from the build phase or we read them from a file
    # provided by the executor of POSTBUILD phase.
    failure_lines = failure_lines or self.failure_lines_from_file()
    if self.run_postbuild_phase():
      self.update_status_file(v8_test262_revision, test262_revision,
                              failure_lines)

    if self.run_upload_phase():
      self.commit_and_upload_changes(v8_test262_revision, test262_revision)
      # TODO: Create bug if status update yields a skip block

    return True

  @property
  def test262_path(self):
    return Path(self.local_test262.path)

  def run_prebuild_phase(self):
    return self.phase in ['ALL', 'PREBUILD']

  def run_build_phase(self):
    return self.phase in ['ALL']

  def run_postbuild_phase(self):
    return self.phase in ['ALL', 'POSTBUILD']

  def run_upload_phase(self):
    return self.phase in ['ALL', 'UPLOAD']

  def fetch_test262(self, gh_token):
    _log.info(f'Fetching test262')
    self.local_test262 = self.project_config.local_repo_factory(
        self.host, gh_token=gh_token)
    self.local_test262.fetch()
    self.test262_git = self.host.git(self.test262_path)
    return self.test262_git.latest_git_commit()


  def find_current_test262_revision(self):
    _log.info(f'Finding current test262 revision in V8')
    return self.host.executive.run_command(
        ['gclient', 'getdep', '-r', 'test/test262/data'],
        cwd=self.project_root).splitlines()[-1].strip()

  def roll_as_dependency(self, test262_revision):
    self.host.executive.run_command(
        ['gclient', 'setdep', '-r', f'test/test262/data@{test262_revision}'],
        cwd=self.project_root)

  def overwrite_files(self):
    for file in self.project_config.files_to_copy:
      _log.info(f'Overwriting {file.destination} with {file.source}')
      shutil.copyfile(
          Path(self.local_test262.path) / file.source,
          self.project_root / file.destination)

  def sync_folders(self, v8_test262_revision, test262_revision):
    for folder in self.project_config.paths_to_sync:
      _log.info(f'Sync {folder.destination} with {folder.source}')
      destination = self.project_root / folder.destination
      for f in destination.glob('**/*'):
        if f.is_file():
          relative_path = f.relative_to(self.project_root)
          source_file = self.test262_path / relative_path
          status = self.get_git_file_status(v8_test262_revision,
                                            test262_revision, source_file)
          self.update_file(f, status, source_file)

  def get_git_file_status(self, v8_test262_revision, test262_revision,
                          source_file):
    status_line = self.test262_git.run([
        'diff', '--name-status', v8_test262_revision, test262_revision, '--',
        source_file
    ]).splitlines()
    assert len(status_line) < 2, f'Expected zero or one line, got {status_line}'
    if len(status_line) == 0:
      return GitFileStatus.UNKOWN
    return GitFileStatus(status_line[0][0])

  def update_file(self, local_file, status, source_file):
    if status == GitFileStatus.ADDED:
      _log.info(
          f'{local_file} just arived from an export. Will copy for good measure.'
      )
      shutil.copy(source_file, local_file)
    elif status == GitFileStatus.DELETED:
      _log.info(f'{local_file} got removed from test262.')
      local_file.unlink()
    elif status == GitFileStatus.MODIFIED:
      _log.info(f'{local_file} got updated in test262.')
      shutil.copy(source_file, local_file)
    else:
      _log.warning(
          f'{local_file} has no counterpart in Test262. Maybe it was never exported?'
      )

  def build_and_test(self):
    """Builds and run test262 tests V8."""
    gn_gen = self.host.executive.run_command(['gn', 'gen', '-C', 'out/Default'],
                                             cwd=self.project_root)
    print(gn_gen)
    self.host.executive.run_command(
        ['autoninja', '-C', 'out/Default'], cwd=self.project_root)
    test_results = self.host.executive.run_command(
        [
            sys.executable, 'tools/run-tests.py', '--outdir=out/Default',
            '--progress=verbose', '--exit-after-n-failures=0', 'test262'
        ],
        error_handler=testing_error_handler,
        cwd=self.project_root).splitlines()
    failure_matches = [TEST262_FAILURE_LINE.match(l) for l in test_results]
    # Ensure we have no duplicates
    failure_lines = sorted(set(m.group(1) for m in failure_matches if m))
    return failure_lines

  def failure_lines_from_file(self):
    if not self.test262_failure_file:
      _log.warning('No failure file provided. Skipping.')
      return []
    with open(self.test262_failure_file, 'r') as r_file:
      return r_file.readlines()


  def update_status_file(self, v8_test262_revision, test262_revision,
                         failure_lines):
    updated_status, removed_lines = self.remove_deleted_tests(
        v8_test262_revision, test262_revision)

    added_lines = self.detected_skip_lines(failure_lines)
    if removed_lines or added_lines:
      updated_status = self.rewrite_status_file_content(updated_status,
                                                        removed_lines,
                                                        added_lines,
                                                        v8_test262_revision,
                                                        test262_revision)
      with open(self.test262_status_file, 'w') as w_file:
        w_file.writelines(updated_status)

  def remove_deleted_tests(self, v8_test262_revision, test262_revision):
    _log.info(f'Remove deleted tests references from status file')
    updated_status = []
    removed_lines = []
    deleted_tests = self.get_updated_tests(
        v8_test262_revision, test262_revision, update_kind='D')
    with open(self.test262_status_file, 'r') as r_file:
      for line in r_file.readlines():
        result = TEST_FILE_REFERENCE_IN_STATUS_FILE.match(line)
        if result and (result.group(1) in deleted_tests):
          _log.info(f'... removing {result.group(1)}')
          removed_lines.append(line)
        else:
          updated_status.append(line)
    return updated_status, removed_lines

  def detected_skip_lines(self, failure_lines):
    return [f"  '{test}': [SKIP],\n" for test in failure_lines]

  def rewrite_status_file_content(self, updated_status, removed_lines,
                                  added_lines, v8_test262_revision,
                                  test262_revision):
    # TODO(liviurau): This is easy to unit test. Add unit tests.
    status_lines_before_eof = updated_status[:-2]
    eof_status_lines = updated_status[-2:]
    assert eof_status_lines == [
        '\n', ']\n'
    ], f'Unexpected status file eof. {eof_status_lines}'
    import_header_lines = [
        '\n####', f'# Import test262@{test262_revision[:8]}',
        f'# {TEST262_REPO_URL}/+log/{v8_test262_revision[:8]}..{test262_revision[:8]}\n'
    ]
    deleted_mentions_comment_lines = [
        f'# Removed {removed}' for removed in removed_lines
    ]
    new_failing_tests_lines = ['[ALWAYS, {\n'] + added_lines + [
        '}],', f'# End import test262@{test262_revision[:8]}', '####\n'
    ]
    return (status_lines_before_eof + import_header_lines +
            deleted_mentions_comment_lines + new_failing_tests_lines +
            eof_status_lines)

  def get_updated_tests(self,
                        v8_test262_revision,
                        test262_revision,
                        update_kind='D'):
    lines = self.test262_git.run([
        'diff', '--name-only', f'--diff-filter={update_kind}',
        v8_test262_revision, test262_revision, '--', 'test'
    ]).splitlines()
    return [
        re.sub(r'^test/(.*)\.js$', r'\1', line)
        for line in lines
        if line.strip()
    ]


  def commit_and_upload_changes(self, v8_test262_revision, test262_revision):
    _log.info('Committing changes.')
    self.project_git.run([
        'commit', '-a', '-m', '[test262] Roll test262', '-m',
        f'{TEST262_REPO_URL}/+log/{v8_test262_revision[:8]}..{test262_revision[:8]}'
    ])

    _log.info('Uploading changes.')
    self.project_git.run([
        'cl',
        'upload',
        '--bypass-hooks',
        '-f',
        '-b',
        V8_TEST262_ROLLS_META_BUG,
    ])

    _log.info(f'Issue: {self.project_git.run(["cl", "issue"]).strip()}')


def testing_error_handler(error):
  pass
