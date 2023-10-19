# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Tools for tracking process statistics like memory consumption.
"""

from contextlib import contextmanager
from threading import Thread, Event


PROBING_INTERVAL_SEC = 0.2


class ProcessStats:
  """Storage class for process statistics indicating if data is available."""
  def __init__(self):
    self._max_rss = 0
    self._max_vms = 0
    self._available = False

  @property
  def max_rss(self):
    return self._max_rss

  @property
  def max_vms(self):
    return self._max_vms

  @property
  def available(self):
    return self._available

  def update(self, memory_info):
    self._max_rss = max(self._max_rss, memory_info.rss)
    self._max_vms = max(self._max_vms, memory_info.vms)
    self._available = True


class EmptyProcessLogger:
  @contextmanager
  def log_stats(self, process):
    """When wrapped, logs memory statistics of the Popen process argument.

    This base-class version can be used as a null object to turn off the
    feature, yielding null-object stats.
    """
    yield ProcessStats()


class PSUtilProcessLogger(EmptyProcessLogger):
  def __init__(self, probing_interval_sec=PROBING_INTERVAL_SEC):
    self.probing_interval_sec = probing_interval_sec

  @contextmanager
  def log_stats(self, process):
    try:
      process_handle = psutil.Process(process.pid)
    except psutil.NoSuchProcess:
      # Fetching process stats has an expected race condition with the
      # running process, which might just have ended already.
      yield ProcessStats()
      return

    stats = ProcessStats()
    finished = Event()
    def run_logger():
      try:
        while True:
          stats.update(process_handle.memory_info())
          if finished.wait(self.probing_interval_sec):
            break
      except psutil.NoSuchProcess:
        pass

    logger = Thread(target=run_logger)
    logger.start()
    try:
      yield stats
    finally:
      finished.set()

    # Until we have joined the logger thread, we can't access the stats
    # without a race condition.
    logger.join()


EMPTY_PROCESS_LOGGER = EmptyProcessLogger()
try:
  # Process utils are only supported when we use vpython or when psutil is
  # installed.
  import psutil
  PROCESS_LOGGER = PSUtilProcessLogger()
except:
  PROCESS_LOGGER = EMPTY_PROCESS_LOGGER
