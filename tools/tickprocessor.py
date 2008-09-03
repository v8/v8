# Copyright 2008 Google Inc.  All rights reserved.
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

import csv, splaytree, sys


class CodeEntry(object):

  def __init__(self, start_addr, name):
    self.start_addr = start_addr
    self.tick_count = 0
    self.name = name

  def IncrementTickCount(self):
    self.tick_count += 1

  def SetStartAddress(self, start_addr):
    self.start_addr = start_addr

  def ToString(self):
    return self.name

  def IsSharedLibraryEntry(self):
    return False


class SharedLibraryEntry(CodeEntry):

  def __init__(self, start_addr, name):
    CodeEntry.__init__(self, start_addr, name)

  def IsSharedLibraryEntry(self):
    return True


class JSCodeEntry(CodeEntry):

  def __init__(self, start_addr, name, type, size):
    CodeEntry.__init__(self, start_addr, name)
    self.type = type
    self.size = size

  def ToString(self):
    return self.name + ' ' + self.type


class TickProcessor(object):

  def __init__(self):
    self.log_file = ''
    self.deleted_code = []
    self.vm_extent = {}
    self.js_entries = splaytree.SplayTree()
    self.cpp_entries = splaytree.SplayTree()
    self.total_number_of_ticks = 0
    self.number_of_library_ticks = 0
    self.unaccounted_number_of_ticks = 0
    self.excluded_number_of_ticks = 0

  def ProcessLogfile(self, filename, included_state = None):
    self.log_file = filename
    self.included_state = included_state
    try:
      logfile = open(filename, 'rb')
    except IOError:
      sys.exit("Could not open logfile: " + filename)
    try:
      logreader = csv.reader(logfile)
      for row in logreader:
        if row[0] == 'tick':
          self.ProcessTick(int(row[1], 16), int(row[2], 16), int(row[3]))
        elif row[0] == 'code-creation':
          self.ProcessCodeCreation(row[1], int(row[2], 16), int(row[3]), row[4])
        elif row[0] == 'code-move':
          self.ProcessCodeMove(int(row[1], 16), int(row[2], 16))
        elif row[0] == 'code-delete':
          self.ProcessCodeDelete(int(row[1], 16))
        elif row[0] == 'shared-library':
          self.AddSharedLibraryEntry(row[1], int(row[2], 16), int(row[3], 16))
          self.ParseVMSymbols(row[1], int(row[2], 16), int(row[3], 16))
    finally:
      logfile.close()

  def AddSharedLibraryEntry(self, filename, start, end):
    # Mark the pages used by this library.
    i = start
    while i < end:
      page = i >> 12
      self.vm_extent[page] = 1
      i += 4096
    # Add the library to the entries so that ticks for which we do not
    # have symbol information is reported as belonging to the library.
    self.cpp_entries.Insert(start, SharedLibraryEntry(start, filename))

  def ParseVMSymbols(self, filename, start, end):
    return

  def ProcessCodeCreation(self, type, addr, size, name):
    self.js_entries.Insert(addr, JSCodeEntry(addr, name, type, size))

  def ProcessCodeMove(self, from_addr, to_addr):
    try:
      removed_node = self.js_entries.Remove(from_addr)
      removed_node.value.SetStartAddress(to_addr);
      self.js_entries.Insert(to_addr, removed_node.value)
    except 'KeyNotFound':
      print('Code move event for unknown code: 0x%x' % from_addr)

  def ProcessCodeDelete(self, from_addr):
    try:
      removed_node = self.js_entries.Remove(from_addr)
      self.deleted_code.append(removed_node.value)
    except 'KeyNotFound':
      print('Code delete event for unknown code: 0x%x' % from_addr)

  def IncludeTick(self, pc, sp, state):
    return (self.included_state is None) or (self.included_state == state)

  def ProcessTick(self, pc, sp, state):
    if not self.IncludeTick(pc, sp, state):
      self.excluded_number_of_ticks += 1;
      return
    self.total_number_of_ticks += 1
    page = pc >> 12
    if page in self.vm_extent:
      entry = self.cpp_entries.FindGreatestsLessThan(pc).value
      if entry.IsSharedLibraryEntry():
        self.number_of_library_ticks += 1
      entry.IncrementTickCount()
      return
    max = self.js_entries.FindMax()
    min = self.js_entries.FindMin()
    if max != None and pc < max.key and pc > min.key:
      self.js_entries.FindGreatestsLessThan(pc).value.IncrementTickCount()
      return
    self.unaccounted_number_of_ticks += 1

  def PrintResults(self):
    print('Statistical profiling result from %s, (%d ticks, %d unaccounted, %d excluded).' %
          (self.log_file,
           self.total_number_of_ticks,
           self.unaccounted_number_of_ticks,
           self.excluded_number_of_ticks))
    if self.total_number_of_ticks > 0:
      js_entries = self.js_entries.ExportValueList()
      js_entries.extend(self.deleted_code)
      cpp_entries = self.cpp_entries.ExportValueList()
      # Print the library ticks.
      self.PrintHeader('Shared libraries')
      self.PrintEntries(cpp_entries, lambda e:e.IsSharedLibraryEntry())
      # Print the JavaScript ticks.
      self.PrintHeader('JavaScript')
      self.PrintEntries(js_entries, lambda e:not e.IsSharedLibraryEntry())
      # Print the C++ ticks.
      self.PrintHeader('C++')
      self.PrintEntries(cpp_entries, lambda e:not e.IsSharedLibraryEntry())

  def PrintHeader(self, header_title):
    print('\n [%s]:' % header_title)
    print('   total  nonlib   name')

  def PrintEntries(self, entries, condition):
    number_of_accounted_ticks = self.total_number_of_ticks - self.unaccounted_number_of_ticks
    number_of_non_library_ticks = number_of_accounted_ticks - self.number_of_library_ticks
    entries.sort(key=lambda e:e.tick_count, reverse=True)
    for entry in entries:
      if entry.tick_count > 0 and condition(entry):
        total_percentage = entry.tick_count * 100.0 / number_of_accounted_ticks
        if entry.IsSharedLibraryEntry():
          non_library_percentage = 0
        else:
          non_library_percentage = entry.tick_count * 100.0 / number_of_non_library_ticks
        print('  %(total)5.1f%% %(nonlib)6.1f%%   %(name)s' % {
          'total' : total_percentage,
          'nonlib' : non_library_percentage,
          'name' : entry.ToString()
        })

if __name__ == '__main__':
  sys.exit('You probably want to run windows-tick-processor.py or linux-tick-processor.py.')
