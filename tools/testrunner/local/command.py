# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


from contextlib import contextmanager
import os
import subprocess
import sys
import threading

from ..local import utils
from ..objects import output


@contextmanager
def win_error_mode():
  """ Try to change the error mode to avoid dialogs on fatal errors. Don't
  touch any existing error mode flags by merging the existing error mode.
  See http://blogs.msdn.com/oldnewthing/archive/2004/07/27/198410.aspx.
  """

  def set_error_mode(mode):
    prev_error_mode = SEM_INVALID_VALUE
    try:
      import ctypes
      prev_error_mode = (
          ctypes.windll.kernel32.SetErrorMode(mode))  #@UndefinedVariable
    except ImportError:
      pass
    return prev_error_mode

  SEM_INVALID_VALUE = -1
  SEM_NOGPFAULTERRORBOX = 0x0002  # Microsoft Platform SDK WinBase.h

  if utils.IsWindows():
    error_mode = SEM_NOGPFAULTERRORBOX
    prev_error_mode = set_error_mode(error_mode)
    set_error_mode(error_mode | prev_error_mode)

  yield

  if utils.IsWindows() and prev_error_mode != SEM_INVALID_VALUE:
    set_error_mode(prev_error_mode)


class Command(object):
  def __init__(self, shell, args=None, cmd_prefix=None, timeout=60, env=None,
               verbose=False):
    assert(timeout > 0)

    self.shell = shell
    self.args = args or []
    self.cmd_prefix = cmd_prefix or []
    self.timeout = timeout
    self.env = env or {}
    self.verbose = verbose

  def execute(self, **additional_popen_kwargs):
    if self.verbose:
      print '# %s' % self

    with win_error_mode():
      try:
        process = subprocess.Popen(
          args=self._get_popen_args(),
          stdout=subprocess.PIPE,
          stderr=subprocess.PIPE,
          env=self._get_env(),
          **additional_popen_kwargs
        )
      except Exception as e:
        sys.stderr.write('Error executing: %s\n' % self)
        raise e

    # Variable to communicate with the timer.
    timeout_occured = [False]
    timer = threading.Timer(
        self.timeout, self._kill_process, [process, timeout_occured])
    timer.start()

    stdout, stderr = process.communicate()

    timer.cancel()

    return output.Output(
      process.returncode,
      timeout_occured[0],
      stdout.decode('utf-8', 'replace').encode('utf-8'),
      stderr.decode('utf-8', 'replace').encode('utf-8'),
      process.pid,
    )

  def _get_popen_args(self):
    args = self._to_args_list()
    if utils.IsWindows():
      return subprocess.list2cmdline(args)
    return args

  def _get_env(self):
    env = os.environ.copy()
    env.update(self.env)
    # GTest shard information is read by the V8 tests runner. Make sure it
    # doesn't leak into the execution of gtests we're wrapping. Those might
    # otherwise apply a second level of sharding and as a result skip tests.
    env.pop('GTEST_TOTAL_SHARDS', None)
    env.pop('GTEST_SHARD_INDEX', None)
    return env

  def _kill_process(self, process, timeout_occured):
    timeout_occured[0] = True
    try:
      if utils.IsWindows():
        self._kill_process_windows(process)
      else:
        self._kill_process_posix(process)

    except OSError:
      sys.stderr.write('Error: Process %s already ended.\n' % process.pid)

  def _kill_process_windows(self, process):
    if self.verbose:
      print 'Attempting to kill process %d' % process.pid
      sys.stdout.flush()
    tk = subprocess.Popen(
        'taskkill /T /F /PID %d' % process.pid,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    stdout, stderr = tk.communicate()
    if self.verbose:
      print 'Taskkill results for %d' % process.pid
      print stdout
      print stderr
      print 'Return code: %d' % tk.returncode
      sys.stdout.flush()

  def _kill_process_posix(self, process):
    if utils.GuessOS() == 'macos':
      # TODO(machenbach): Temporary output for investigating hanging test
      # driver on mac.
      print 'Attempting to kill process %d - cmd %s' % (process.pid, self)
      try:
        print subprocess.check_output(
          'ps -e | egrep "d8|cctest|unittests"', shell=True)
      except Exception:
        pass
      sys.stdout.flush()

    process.kill()

    if utils.GuessOS() == 'macos':
      # TODO(machenbach): Temporary output for investigating hanging test
      # driver on mac. This will probably not print much, since kill only
      # sends the signal.
      print 'Return code after signalling the kill: %s' % process.returncode
      sys.stdout.flush()

  def __str__(self):
    return self.to_string()

  def to_string(self, relative=False):
    def escape(part):
      # Escape spaces. We may need to escape more characters for this to work
      # properly.
      if ' ' in part:
        return '"%s"' % part
      return part

    parts = map(escape, self._to_args_list())
    cmd = ' '.join(parts)
    if relative:
      cmd = cmd.replace(os.getcwd() + os.sep, '')
    return cmd

  def _to_args_list(self):
    return self.cmd_prefix + [self.shell] + self.args
