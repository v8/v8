# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Wrapper around the Android device abstraction from src/build/android.
"""

import logging
import os
import sys


BASE_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__), '..', '..', '..'))
ANDROID_DIR = os.path.join(BASE_DIR, 'build', 'android')
DEVICE_DIR = '/data/local/tmp/v8/'


class TimeoutException(Exception):
  def __init__(self, timeout, output=None):
    self.timeout = timeout
    self.output = output


class CommandFailedException(Exception):
  def __init__(self, status, output):
    self.status = status
    self.output = output


class _Driver(object):
  """Helper class to execute shell commands on an Android device."""
  def __init__(self, device=None):
    assert os.path.exists(ANDROID_DIR)
    sys.path.insert(0, ANDROID_DIR)

    # We import the dependencies only on demand, so that this file can be
    # imported unconditionally.
    import devil_chromium
    from devil.android import device_errors  # pylint: disable=import-error
    from devil.android import device_utils  # pylint: disable=import-error
    from devil.android.perf import cache_control  # pylint: disable=import-error
    from devil.android.perf import perf_control  # pylint: disable=import-error
    global cache_control
    global device_errors
    global perf_control

    devil_chromium.Initialize()

    # Find specified device or a single attached device if none was specified.
    # In case none or multiple devices are attached, this raises an exception.
    self.device = device_utils.DeviceUtils.HealthyDevices(
        retries=5, enable_usb_resets=True, device_arg=device)[0]

  def tear_down(self):
    """Clean up files after running all tests."""
    self.device.RemovePath(DEVICE_DIR, force=True, recursive=True)

  def push_files(self, file_tuples, skip_if_missing=False):
    """Push multiple files/dirs to the device.

    Args:
      file_tuples: List of 3-tuples (host_dir, file_name, target_rel), where
          host_dir is absolute parent directory of the files/dirs to be pushed,
          file_name is file or dir path to be pushed (relative to host_dir), and
          target_rel is parent directory of the target location on the device
          relative to the device's base dir for testing.
      skip_if_missing: Keeps silent about missing files when set. Otherwise logs
          error.
    """
    host_device_tuples = []
    for host_dir, file_name, target_rel in file_tuples:
      file_on_host = os.path.join(host_dir, file_name)
      if not os.path.exists(file_on_host):
        if not skip_if_missing:
          logging.critical('Missing file on host: %s' % file_on_host)
        continue

      file_on_device = os.path.join(DEVICE_DIR, target_rel, file_name)
      host_device_tuples.append((file_on_host, file_on_device))

    try:
      self.device.PushChangedFiles(host_device_tuples)
    except device_errors.CommandFailedError as e:
      logging.critical('PUSH FAILED: %s', e.message)

  def push_executable(self, shell_dir, target_dir, binary):
    """Push files required to run a V8 executable.

    Args:
      shell_dir: Absolute parent directory of the executable on the host.
      target_dir: Parent directory of the executable on the device (relative to
          devices' base dir for testing).
      binary: Name of the binary to push.
    """
    self.push_files([(shell_dir, binary, target_dir)])

    # Push external startup data. Backwards compatible for revisions where
    # these files didn't exist. Or for bots that don't produce these files.
    self.push_files([
      (shell_dir, 'natives_blob.bin', target_dir),
      (shell_dir, 'snapshot_blob.bin', target_dir),
      (shell_dir, 'snapshot_blob_trusted.bin', target_dir),
      (shell_dir, 'icudtl.dat', target_dir),
    ], skip_if_missing=True)

  def run(self, target_dir, binary, args, rel_path, timeout, env=None,
          logcat_file=False):
    """Execute a command on the device's shell.

    Args:
      target_dir: Parent directory of the executable on the device (relative to
          devices' base dir for testing).
      binary: Name of the binary.
      args: List of arguments to pass to the binary.
      rel_path: Relative path on device to use as CWD.
      timeout: Timeout in seconds.
      env: The environment variables with which the command should be run.
      logcat_file: File into which to stream adb logcat log.
    """
    binary_on_device = os.path.join(DEVICE_DIR, target_dir, binary)
    cmd = [binary_on_device] + args
    def run_inner():
      try:
        output = self.device.RunShellCommand(
            cmd,
            cwd=os.path.join(DEVICE_DIR, rel_path),
            check_return=True,
            env=env,
            timeout=timeout,
            retries=0,
            large_output=True,
        )
        return '\n'.join(output)
      except device_errors.AdbCommandFailedError as e:
        raise CommandFailedException(e.status, e.output)
      except device_errors.CommandTimeoutError as e:
        raise TimeoutException(timeout, e.output)


    if logcat_file:
      with self.device.GetLogcatMonitor(output_file=logcat_file) as logmon:
        result = run_inner()
      logmon.Close()
      return result
    else:
      return run_inner()

  def drop_ram_caches(self):
    """Drop ran caches on device."""
    cache = cache_control.CacheControl(self.device)
    cache.DropRamCaches()

  def set_high_perf_mode(self):
    """Set device into high performance mode."""
    perf = perf_control.PerfControl(self.device)
    perf.SetHighPerfMode()

  def set_default_perf_mode(self):
    """Set device into default performance mode."""
    perf = perf_control.PerfControl(self.device)
    perf.SetDefaultPerfMode()


_ANDROID_DRIVER = None
def android_driver(device=None):
  """Singleton access method to the driver class."""
  global _ANDROID_DRIVER
  if not _ANDROID_DRIVER:
    _ANDROID_DRIVER = _Driver(device)
  return _ANDROID_DRIVER
