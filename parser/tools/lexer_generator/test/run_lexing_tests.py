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
    self.args = ['--break-after-illegal']
    if args.use_harmony:
      self.args.append('--use-harmony')
    self.args.append('--%s' % args.encoding)
    if self.right_path:
      self.args.append('--print-tokens')

  def build_process_map(self):
    process_map = self.process_map
    for i, f in enumerate(self.files):
      process_map[2 * i] = {
        'file': f, 'path' : self.left_path, 'type' : 'left' }
      if self.right_path:
        process_map[2 * i + 1] = {
          'file': f, 'path' : self.right, 'type' : 'right' }

  def wait_processes(self, running_processes):
    complete_ids = []
    while True:
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

  def process_complete_processes(self):
    complete_processes = self.complete_processes
    complete_ids = []
    for i, data in complete_processes.iteritems():
      p = data['process']
      if not self.right_path:
        if p.returncode:
          print "%s failed" % data['file']
        else:
          print "%s succeeded" % data['file']
        complete_ids.append(i)
      else:
        # TODO(dcarney): perform compare
        pass
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
          out = sys.PIPE if self.right_path else dev_null
          args = [data['path'], data['file']] + self.args
          logging.info("running [%s]" % ' '.join(args))
          data['process'] = subprocess.Popen(args,
                                             stdout=out,
                                             stderr=dev_null,
                                             bufsize=16*1024)
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
    choices=['latin1', 'utf8', 'utf8to16', 'utf16'], default='utf8')
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
