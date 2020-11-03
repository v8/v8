// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { LogReader, parseString } from "./logreader.mjs";
import { CodeMap, CodeEntry } from "./codemap.mjs";
export {
    ArgumentsProcessor, UnixCppEntriesProvider, 
    WindowsCppEntriesProvider, MacCppEntriesProvider,
  } from  "./tickprocessor.mjs";
  import { inherits } from  "./tickprocessor.mjs";


export class CppProcessor extends LogReader {
  constructor(cppEntriesProvider, timedRange, pairwiseTimedRange) {
    super({}, timedRange, pairwiseTimedRange);
    this.dispatchTable_ = {
        'shared-library': {
          parsers: [parseString, parseInt, parseInt, parseInt],
          processor: this.processSharedLibrary }
    };
    this.cppEntriesProvider_ = cppEntriesProvider;
    this.codeMap_ = new CodeMap();
    this.lastLogFileName_ = null;
  }

  /**
   * @override
   */
  printError(str) {
    print(str);
  };

  processLogFile(fileName) {
    this.lastLogFileName_ = fileName;
    var line;
    while (line = readline()) {
      this.processLogLine(line);
    }
  };

  processLogFileInTest(fileName) {
    // Hack file name to avoid dealing with platform specifics.
    this.lastLogFileName_ = 'v8.log';
    var contents = readFile(fileName);
    this.processLogChunk(contents);
  };

  processSharedLibrary(name, startAddr, endAddr, aslrSlide) {
    var self = this;
    var libFuncs = this.cppEntriesProvider_.parseVmSymbols(
        name, startAddr, endAddr, aslrSlide, function(fName, fStart, fEnd) {
      var entry = new CodeEntry(fEnd - fStart, fName, 'CPP');
      self.codeMap_.addStaticCode(fStart, entry);
    });
  };

  dumpCppSymbols() {
    var staticEntries = this.codeMap_.getAllStaticEntriesWithAddresses();
    var total = staticEntries.length;
    for (var i = 0; i < total; ++i) {
      var entry = staticEntries[i];
      var printValues = ['cpp', '0x' + entry[0].toString(16), entry[1].size,
                        '"' + entry[1].name + '"'];
      print(printValues.join(','));
    }
  }
}
