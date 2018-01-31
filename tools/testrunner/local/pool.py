#!/usr/bin/env python
# Copyright 2014 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from Queue import Empty
from contextlib import contextmanager
from multiprocessing import Event, Process, Queue
import signal
import traceback


def setup_testing():
  """For testing only: Use threading under the hood instead of multiprocessing
  to make coverage work.
  """
  global Queue
  global Event
  global Process
  del Queue
  del Event
  del Process
  from Queue import Queue
  from threading import Event
  from threading import Thread as Process


class NormalResult():
  def __init__(self, result):
    self.result = result
    self.exception = None

class ExceptionResult():
  def __init__(self, exception):
    self.exception = exception


class MaybeResult():
  def __init__(self, heartbeat, value):
    self.heartbeat = heartbeat
    self.value = value

  @staticmethod
  def create_heartbeat():
    return MaybeResult(True, None)

  @staticmethod
  def create_result(value):
    return MaybeResult(False, value)


def Worker(fn, work_queue, done_queue, pause_event, read_again_event,
           process_context_fn=None, process_context_args=None):
  """Worker to be run in a child process.
  The worker stops when the poison pill "STOP" is reached.
  It pauses when pause event is set and waits until read again event is true.
  """
  try:
    kwargs = {}
    if process_context_fn and process_context_args is not None:
      kwargs.update(process_context=process_context_fn(*process_context_args))
    for args in iter(work_queue.get, "STOP"):
      if pause_event.is_set():
        done_queue.put(NormalResult(None))
        read_again_event.wait()
        continue

      try:
        done_queue.put(NormalResult(fn(*args, **kwargs)))
      except Exception, e:
        traceback.print_exc()
        print(">>> EXCEPTION: %s" % e)
        done_queue.put(ExceptionResult(e))
  except KeyboardInterrupt:
    assert False, 'Unreachable'


@contextmanager
def without_sigint():
    handler = signal.signal(signal.SIGINT, signal.SIG_IGN)
    yield
    signal.signal(signal.SIGINT, handler)


class Pool():
  """Distributes tasks to a number of worker processes.
  New tasks can be added dynamically even after the workers have been started.
  Requirement: Tasks can only be added from the parent process, e.g. while
  consuming the results generator."""

  # Factor to calculate the maximum number of items in the work/done queue.
  # Necessary to not overflow the queue's pipe if a keyboard interrupt happens.
  BUFFER_FACTOR = 4

  def __init__(self, num_workers, heartbeat_timeout=30):
    self.num_workers = num_workers
    self.processes = []
    self.terminated = False

    # Invariant: processing_count >= #work_queue + #done_queue. It is greater
    # when a worker takes an item from the work_queue and before the result is
    # submitted to the done_queue. It is equal when no worker is working,
    # e.g. when all workers have finished, and when no results are processed.
    # Count is only accessed by the parent process. Only the parent process is
    # allowed to remove items from the done_queue and to add items to the
    # work_queue.
    self.processing_count = 0
    self.heartbeat_timeout = heartbeat_timeout

    # Disable sigint to make multiprocessing data structure inherit it and
    # ignore ctrl-c
    with without_sigint():
      self.work_queue = Queue()
      self.done_queue = Queue()
      self.pause_event = Event()
      self.read_again_event = Event()

  def imap_unordered(self, fn, gen,
                     process_context_fn=None, process_context_args=None):
    """Maps function "fn" to items in generator "gen" on the worker processes
    in an arbitrary order. The items are expected to be lists of arguments to
    the function. Returns a results iterator. A result value of type
    MaybeResult either indicates a heartbeat of the runner, i.e. indicating
    that the runner is still waiting for the result to be computed, or it wraps
    the real result.

    Args:
      process_context_fn: Function executed once by each worker. Expected to
          return a process-context object. If present, this object is passed
          as additional argument to each call to fn.
      process_context_args: List of arguments for the invocation of
          process_context_fn. All arguments will be pickled and sent beyond the
          process boundary.
    """
    if self.terminated:
      return
    try:
      internal_error = False
      gen = iter(gen)
      self.advance = self._advance_more

      # Disable sigint to make workers inherit it and ignore ctrl-c
      with without_sigint():
        for w in xrange(self.num_workers):
          p = Process(target=Worker, args=(fn,
                                          self.work_queue,
                                          self.done_queue,
                                          self.pause_event,
                                          self.read_again_event,
                                          process_context_fn,
                                          process_context_args))
          p.start()
          self.processes.append(p)

      self.advance(gen)
      while self.processing_count > 0:
        while True:
          try:
            result = self._get_result_from_queue()
          except:
            # TODO(machenbach): Handle a few known types of internal errors
            # gracefully, e.g. missing test files.
            internal_error = True
            continue
          yield result
          break

        self.advance(gen)
    except KeyboardInterrupt:
      raise
    except Exception as e:
      traceback.print_exc()
      print(">>> EXCEPTION: %s" % e)
    finally:
      self.terminate()

    if internal_error:
      raise Exception("Internal error in a worker process.")

  def _advance_more(self, gen):
    while self.processing_count < self.num_workers * self.BUFFER_FACTOR:
      try:
        self.work_queue.put(gen.next())
        self.processing_count += 1
      except StopIteration:
        self.advance = self._advance_empty
        break

  def _advance_empty(self, gen):
    pass

  def add(self, args):
    """Adds an item to the work queue. Can be called dynamically while
    processing the results from imap_unordered."""
    assert not self.terminated

    self.work_queue.put(args)
    self.processing_count += 1

  def terminate(self):
    """Terminates execution and waits for ongoing jobs."""
    # Iteration but ignore the results
    list(self.terminate_with_results())

  def terminate_with_results(self):
    """Terminates execution and waits for ongoing jobs. It's a generator
    returning heartbeats and results for all jobs that started before calling
    terminate.
    """
    if self.terminated:
      return
    self.terminated = True

    self.pause_event.set()

    # Drain out work queue from tests
    try:
      while self.processing_count > 0:
        self.work_queue.get(True, 1)
        self.processing_count -= 1
    except Empty:
      pass

    # Make sure all processes stop
    for _ in self.processes:
      # During normal tear down the workers block on get(). Feed a poison pill
      # per worker to make them stop.
      self.work_queue.put("STOP")

    # Workers stopped reading work queue if stop event is true to not overtake
    # main process that drains the queue. They should read again to consume
    # poison pill and possibly more tests that we couldn't get during draining.
    self.read_again_event.set()

    # Wait for results
    while self.processing_count:
      result = self._get_result_from_queue()
      if result.heartbeat or result.value:
        yield result

    for p in self.processes:
      p.join()

  def _get_result_from_queue(self):
    try:
      result = self.done_queue.get(timeout=self.heartbeat_timeout)
      self.processing_count -= 1
    except Empty:
      return MaybeResult.create_heartbeat()

    if result.exception:
      raise result.exception

    return MaybeResult.create_result(result.result)
