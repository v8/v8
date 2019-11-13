# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Fork from commands.py and output.py in v8 test driver.

import json
import os
import signal
import subprocess
import sys
from threading import Event, Timer

import v8_fuzz_config

# List of default flags passed to each d8 run.
DEFAULT_FLAGS = [
  '--correctness-fuzzer-suppressions',
  '--expose-gc',
  '--allow-natives-syntax',
  '--invoke-weak-callbacks',
  '--omit-quit',
  '--es-staging',
  '--wasm-staging',
  '--no-wasm-async-compilation',
  '--suppress-asm-messages',
]

BASE_PATH = os.path.dirname(os.path.abspath(__file__))

# List of files passed to each d8 run before the testcase.
DEFAULT_FILES = [
  os.path.join(BASE_PATH, 'v8_mock.js'),
  os.path.join(BASE_PATH, 'v8_suppressions.js'),
]

# Architecture-specific mock file
ARCH_MOCKS = os.path.join(BASE_PATH, 'v8_mock_archs.js')

DEFAULT_ARCH = 'x64'
SUPPORTED_ARCHS = ['ia32', 'x64', 'arm', 'arm64']

# Timeout in seconds for one d8 run.
TIMEOUT = 3


def _infer_v8_architecture(executable):
  """Infer the V8 architecture from the build configuration next to the
  executable.
  """
  build_dir = os.path.dirname(executable)
  with open(os.path.join(build_dir, 'v8_build_config.json')) as f:
    arch = json.load(f)['v8_current_cpu']
  arch = 'ia32' if arch == 'x86' else arch
  assert arch in SUPPORTED_ARCHS
  return arch


def _startup_files(arch):
  """Default files and optional architecture-specific mock file."""
  files = DEFAULT_FILES[:]
  if arch != DEFAULT_ARCH:
    files.append(ARCH_MOCKS)
  return files


class Command(object):
  """Represents a configuration for running V8 multiple times with certain
  flags and files.
  """
  def __init__(self, label, executable, random_seed, config_flags):
    self.label = label
    self.executable = executable
    self.config_flags = config_flags
    self.common_flags =  DEFAULT_FLAGS + ['--random-seed', str(random_seed)]

    # Ensure absolute paths.
    if not os.path.isabs(self.executable):
      self.executable = os.path.join(BASE_PATH, self.executable)

    # Ensure executables exist.
    assert os.path.exists(self.executable)

    self.arch = _infer_v8_architecture(self.executable)
    self.files = _startup_files(self.arch)

  def run(self, testcase, verbose=False):
    """Run the executable with a specific testcase."""
    args = [self.executable] + self.flags + self.files + [testcase]
    if verbose:
      print('# Command line for %s comparison:' % self.label)
      print(' '.join(args))
    if self.executable.endswith('.py'):
      # Wrap with python in tests.
      args = [sys.executable] + args
    return Execute(
        args,
        cwd=os.path.dirname(os.path.abspath(testcase)),
        timeout=TIMEOUT,
    )

  @property
  def flags(self):
    return self.common_flags + self.config_flags


class Output(object):
  def __init__(self, exit_code, timed_out, stdout, pid):
    self.exit_code = exit_code
    self.timed_out = timed_out
    self.stdout = stdout
    self.pid = pid

  def HasCrashed(self):
    # Timed out tests will have exit_code -signal.SIGTERM.
    if self.timed_out:
      return False
    return (self.exit_code < 0 and
            self.exit_code != -signal.SIGABRT)

  def HasTimedOut(self):
    return self.timed_out


def Execute(args, cwd, timeout=None):
  popen_args = [c for c in args if c != ""]
  try:
    process = subprocess.Popen(
      args=popen_args,
      stdout=subprocess.PIPE,
      stderr=subprocess.STDOUT,
      cwd=cwd
    )
  except Exception as e:
    sys.stderr.write("Error executing: %s\n" % popen_args)
    raise e

  timeout_event = Event()

  def kill_process():
    timeout_event.set()
    try:
      process.kill()
    except OSError:
      sys.stderr.write('Error: Process %s already ended.\n' % process.pid)

  timer = Timer(timeout, kill_process)
  timer.start()
  stdout, _ = process.communicate()
  timer.cancel()

  return Output(
      process.returncode,
      timeout_event.is_set(),
      stdout.decode('utf-8', 'replace').encode('utf-8'),
      process.pid,
  )
