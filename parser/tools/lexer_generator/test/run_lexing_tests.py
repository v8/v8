# Copyright 2013 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import argparse
import subprocess
import select
import sys
import time
import logging

class ProcessRunner:

  def __init__(self, files, args):
    self.files = files
    self.process_map = {}
    self.complete_processes = {}
    self.left_path = args.left_path
    self.right_path = args.right_path
    self.max_process_count = args.parallel_process_count
    self.buffer_size = 16*1024
    self.args = ['--break-after-illegal']
    if args.use_harmony:
      self.args.append('--use-harmony')
    self.args.append('--%s' % args.encoding)
    if self.right_path:
      self.args.append('--print-tokens-for-compare')

  def build_process_map(self):
    process_map = self.process_map
    for i, f in enumerate(self.files):
      def data(path, cmp_id):
        return {'file': f, 'path' : path, 'cmp_id' : cmp_id, 'buffer' : [] }
      process_map[2 * i] = data(self.left_path, 2 * i + 1)
      if self.right_path:
        process_map[2 * i + 1] = data(self.right_path, 2 * i)

  def read_running_processes(self, running_processes):
    if not self.right_path:
      return
    stdouts = {
      self.process_map[i]['process'].stdout : self.process_map[i]['buffer']
        for i in running_processes }
    while True:
      ready = select.select(stdouts.iterkeys(), [], [], 0)[0]
      if not ready:
        return
      did_something = False
      for fd in ready:
        c = fd.read(self.buffer_size)
        if c == "":
          continue
        did_something = True
        stdouts[fd].append(c)
      if not did_something:
        break

  def wait_processes(self, running_processes):
    complete_ids = []
    while True:
      self.read_running_processes(running_processes)
      for i in running_processes:
        data = self.process_map[i]
        response = data['process'].poll()
        if response == None:
          continue
        self.complete_processes[i] = data
        complete_ids.append(i)
      if complete_ids:
        break
      time.sleep(0.001)
    for i in complete_ids:
      running_processes.remove(i)
      del self.process_map[i]

  @staticmethod
  def crashed(data):
    return data['process'].returncode != 0

  @staticmethod
  def buffer_contents(data):
    data['buffer'].append(data['process'].stdout.read())
    return ''.join(data['buffer'])

  @staticmethod
  def analyse_diff(left_data, right_data):
    left = left_data.split("\n");
    right = right_data.split("\n");
    for i in range(min(len(left), len(right))):
      if left[i] != right[i]:
        message = "differ at token %d" % i
        for j in range(i-4, i-1):
          if j >= 0:
            message += "\n\n%s\n%s" % (left[j], right[j])
        message += "\n\n%s\n%s\n" % (left[i], right[i])
        logging.info(message)
        return
    if len(right) > len(left):
      logging.info("right longer")
      return
    if len(left) > len(right):
      logging.info("left longer")
      return

  def compare_results(self, left, right):
    f = left['file']
    assert f == right['file']
    logging.info('checking results for %s' % f)
    if self.crashed(left) or self.crashed(right):
      print "%s failed" % f
      return
    if left['path'] == self.right_path:
      left, right = right, left
    left_data = self.buffer_contents(left)
    right_data = self.buffer_contents(right)
    if left_data != right_data:
      self.analyse_diff(left_data, right_data)
      print "%s failed" % f
      return
    print "%s succeeded" % f

  def process_complete_processes(self):
    complete_processes = self.complete_processes
    complete_ids = []
    for i, data in complete_processes.iteritems():
      if not self.right_path:
        assert not i in complete_ids
        if self.crashed(data):
          print "%s failed" % data['file']
        else:
          print "%s succeeded" % data['file']
        complete_ids.append(i)
      else:
        if i in complete_ids:
          continue
        cmp_id = data['cmp_id']
        if not cmp_id in complete_processes:
          continue
        complete_ids.append(i)
        complete_ids.append(cmp_id)
        self.compare_results(data, complete_processes[cmp_id])
    # clear processed data
    for i in complete_ids:
      del complete_processes[i]

  def run(self):
    assert not self.process_map
    self.build_process_map()
    process_map = self.process_map
    complete_processes = self.complete_processes
    running_processes = set()
    with open('/dev/null', 'w') as dev_null:
      while True:
        for id, data in process_map.iteritems():
          if id in running_processes:
            continue
          if len(running_processes) == self.max_process_count:
            break
          out = subprocess.PIPE if self.right_path else dev_null
          args = [data['path'], data['file']] + self.args
          logging.info("running [%s]" % ' '.join(args))
          data['process'] = subprocess.Popen(args,
                                             stdout=out,
                                             stderr=dev_null,
                                             bufsize=self.buffer_size)
          running_processes.add(id)
        if not running_processes:
          break
        self.wait_processes(running_processes)
        self.process_complete_processes()
    assert not running_processes
    assert not self.process_map
    assert not self.complete_processes

if __name__ == '__main__':

  parser = argparse.ArgumentParser()
  parser.add_argument('-l', '--left-path')
  parser.add_argument('-r', '--right-path', default='')
  parser.add_argument('-i', '--input-files-path', default='')
  parser.add_argument('-f', '--single-file', default='')
  parser.add_argument('-p', '--parallel-process-count', default=1, type=int)
  parser.add_argument('-e', '--encoding',
    choices=['latin1', 'utf8', 'utf16', 'utf8to16', 'utf8tolatin1'],
    default='utf8')
  parser.add_argument('--use-harmony', action='store_true')
  parser.add_argument('-v', '--verbose', action='store_true')
  args = parser.parse_args()

  if args.verbose:
    logging.basicConfig(level=logging.INFO)

  files = []
  if args.input_files_path:
    with open(args.input_files_path, 'r') as f:
      files = [filename for filename in f.read().split('\n') if filename]
  if args.single_file:
    files.append(args.single_file)
  assert files

  process_runner = ProcessRunner(files, args)
  process_runner.run()
