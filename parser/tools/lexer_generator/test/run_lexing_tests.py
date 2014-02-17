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

import subprocess
import sys

def wait_processes(processes):
  for p in processes:
    if p[1].wait():
      print p[0], 'failed'
    else:
      print p[0], 'ok'

if __name__ == '__main__':
  if len(sys.argv) < 4:
    error_message = ('Usage:' + sys.argv[0] +
                     'LEXER_SHELL_PATH FILE_LIST_FILE PARALLEL_PROCESS_COUNT ' +
                     '[OTHER_ARGS]')
    print >> sys.stderr, error_message
    sys.exit(1)
  lexer_shell = sys.argv[1]
  file_file = sys.argv[2]
  process_count = int(sys.argv[3])
  with open(file_file, 'r') as f:
    test_files = [filename for filename in f.read().split('\n') if filename]

  with open('/dev/null', 'w') as dev_null:
    processes = []
    for i, f in enumerate(test_files):
      lexer_shell_args = [lexer_shell, f, '--break-after-illegal'] + sys.argv[4:]
      processes.append((f, subprocess.Popen(lexer_shell_args, stdout=dev_null)))
      if i % process_count == process_count - 1:
        wait_processes(processes)
        processes = []

    wait_processes(processes)
