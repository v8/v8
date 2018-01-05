# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections
import traceback

from . import base
from ..local import pool


# Global function for multiprocessing, because pickling a static method doesn't
# work on Windows.
def run_job(job):
  return job.run()


JobResult = collections.namedtuple('JobResult', ['id', 'result'])


class Job(object):
  def __init__(self, test_id, cmd, outproc):
    self.test_id = test_id
    self.cmd = cmd
    self.outproc = outproc

  def run(self):
    output = self.cmd.execute()
    return JobResult(self.test_id, self.outproc.process(output))


class ExecutionProc(base.TestProc):
  def __init__(self, jobs, context):
    super(ExecutionProc, self).__init__()
    self._pool = pool.Pool(jobs)
    self._context = context
    self._tests = {}

  def connect_to(self, next_proc):
    assert False, 'ExecutionProc cannot be connected to anything'

  def start(self):
    try:
      it = self._pool.imap_unordered(
        fn=run_job,
        gen=[],
        process_context_fn=None,
        process_context_args=None,
      )
      for pool_result in it:
        if pool_result.heartbeat:
          continue

        job_result = pool_result.value
        test_id, result = job_result

        test = self._tests[test_id]
        del self._tests[test_id]
        self._send_result(test, result)
    except KeyboardInterrupt:
      raise
    except:
      traceback.print_exc()
      raise
    finally:
      self._pool.terminate()

  def next_test(self, test):
    test_id = test.procid
    self._tests[test_id] = test

    # TODO(majeski): Don't modify test. It's currently used in the progress
    # indicator.
    test.cmd = test.get_command(self._context)

    # TODO(majeski): Needs factory for outproc as in local/execution.py
    outproc = test.output_proc
    self._pool.add([Job(test_id, test.cmd, outproc)])

  def result_for(self, test, result, is_last):
    assert False, 'ExecutionProc cannot receive results'
