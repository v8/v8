-- Copyright 2011 the V8 project authors. All rights reserved.
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions are
-- met:
--
--     * Redistributions of source code must retain the above copyright
--       notice, this list of conditions and the following disclaimer.
--     * Redistributions in binary form must reproduce the above
--       copyright notice, this list of conditions and the following
--       disclaimer in the documentation and/or other materials provided
--       with the distribution.
--     * Neither the name of Google Inc. nor the names of its
--       contributors may be used to endorse or promote products derived
--       from this software without specific prior written permission.
--
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
-- "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
-- LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
-- A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
-- OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
-- SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
-- LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
-- DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
-- THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
-- (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
-- OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-- This is main driver for gcmole tool. See README for more details.
-- Usage: CLANG_BIN=clang-bin-dir lua tools/gcmole/gcmole.lua [arm|ia32|x64]

local DIR = arg[0]:match("^(.+)/[^/]+$")
 
local ARCHS = arg[1] and { arg[1] } or { 'ia32', 'arm', 'x64' }

local io = require "io"
local os = require "os"

function log(...)
   io.stderr:write(string.format(...))
   io.stderr:write "\n"
end

-------------------------------------------------------------------------------
-- Clang invocation

local CLANG_BIN = os.getenv "CLANG_BIN" 

if not CLANG_BIN or CLANG_BIN == "" then
   error "CLANG_BIN not set"
end 

local function MakeClangCommandLine(plugin, triple, arch_define)
   return CLANG_BIN .. "/clang -cc1 -load " .. DIR .. "/libgcmole.so" 
      .. " -plugin "  .. plugin
      .. " -triple " .. triple 
      .. " -D" .. arch_define
      .. " -DENABLE_VMSTATE_TRACKING" 
      .. " -DENABLE_LOGGING_AND_PROFILING" 
      .. " -DENABLE_DEBUGGER_SUPPORT"
      .. " -Isrc"
end

function InvokeClangPluginForEachFile(filenames, cfg, func)
   local cmd_line = MakeClangCommandLine(cfg.plugin,
					 cfg.triple,
					 cfg.arch_define)

   for _, filename in ipairs(filenames) do 
      log("-- %s", filename)

      local action = cmd_line .. " src/" .. filename .. " 2>&1"

      local pipe = io.popen(action)
      func(filename, pipe:lines())
      pipe:close()
   end
end

-------------------------------------------------------------------------------
-- SConscript parsing

local function ParseSConscript()
   local f = assert(io.open("src/SConscript"), "failed to open SConscript")
   local sconscript = f:read('*a')
   f:close()

   local SOURCES = sconscript:match "SOURCES = {(.-)}"; 

   local sources = {}

   for condition, list in
      SOURCES:gmatch "'([^']-)': Split%(\"\"\"(.-)\"\"\"%)" do
      local files = {}
      for file in list:gmatch "[^%s]+" do table.insert(files, file) end
      sources[condition] = files
   end 

   for condition, list in SOURCES:gmatch "'([^']-)': %[(.-)%]" do
      local files = {}
      for file in list:gmatch "'([^']-)'" do table.insert(files, file) end
      sources[condition] = files
   end 

   return sources
end

local function EvaluateCondition(cond, props)
   if cond == 'all' then return true end

   local p, v = cond:match "(%w+):(%w+)"

   assert(p and v, "failed to parse condition: " .. cond)
   assert(props[p] ~= nil, "undefined configuration property: " .. p)

   return props[p] == v
end

local function BuildFileList(sources, props)
   local list = {}
   for condition, files in pairs(sources) do
      if EvaluateCondition(condition, props) then
	 for i = 1, #files do table.insert(list, files[i]) end
      end
   end
   return list
end

local sources = ParseSConscript()

local function FilesForArch(arch)
   return BuildFileList(sources, { os = 'linux',
				   arch = arch,
				   mode = 'debug',
				   simulator = ''})
end

local mtConfig = {}

mtConfig.__index = mtConfig

local function config (t) return setmetatable(t, mtConfig) end

function mtConfig:extend(t)
   local e = {}
   for k, v in pairs(self) do e[k] = v end
   for k, v in pairs(t) do e[k] = v end
   return config(e)
end

local ARCHITECTURES = {
   ia32 = config { triple = "i586-unknown-linux",
		   arch_define = "V8_TARGET_ARCH_IA32" },
   arm = config { triple = "i586-unknown-linux",
		  arch_define = "V8_TARGET_ARCH_ARM" },
   x64 = config { triple = "x86_64-unknown-linux",
		  arch_define = "V8_TARGET_ARCH_X64" }
}

-------------------------------------------------------------------------------
-- GCSuspects Generation 

local gc = {}
local funcs = {}

local function resolve(name)
   local f = funcs[name]
   
   if not f then 
      f = {}
      funcs[name] = f
      
      if name:match "Collect.*Garbage" then gc[name] = true end
   end
   
    return f
end

local function parse (filename, lines)
   local scope

   for funcname in lines do
      if funcname:sub(1, 1) ~= '\t' then
	 resolve(funcname)
	 scope = funcname
      else
	 local name = funcname:sub(2)
	 resolve(name)[scope] = true
      end
   end
end

local function propagate ()
   log "** Propagating GC information"

   local function mark(callers)
      for caller, _ in pairs(callers) do 
	 if not gc[caller] then
	    gc[caller] = true
	    mark(funcs[caller]) 
	 end
      end
   end

   for funcname, callers in pairs(funcs) do
      if gc[funcname] then mark(callers) end
   end
end

local function GenerateGCSuspects(arch, files, cfg)
   log ("** Building GC Suspects for %s", arch)
   InvokeClangPluginForEachFile (files,
                                 cfg:extend { plugin = "dump-callees" },
                                 parse)
   
   propagate()

   local out = assert(io.open("gcsuspects", "w"))
   for name, _ in pairs(gc) do out:write (name, '\n') end
   out:close()
   log ("** GCSuspects generated for %s", arch)
end

-------------------------------------------------------------------------------
-- Analysis

local function CheckCorrectnessForArch(arch) 
   local files = FilesForArch(arch)
   local cfg = ARCHITECTURES[arch]

   GenerateGCSuspects(arch, files, cfg)

   local processed_files = 0
   local errors_found = false
   local function SearchForErrors(filename, lines)
      processed_files = processed_files + 1
      for l in lines do
	 errors_found = errors_found or
	    l:match "^[^:]+:%d+:%d+:" or
	    l:match "error" or
	    l:match "warning"
         print(l)
      end
   end

   log("** Searching for evaluation order problems for %s", arch)
   InvokeClangPluginForEachFile(files,
				cfg:extend { plugin = "find-problems" },
			        SearchForErrors)
   log("** Done processing %d files. %s",
       processed_files,
       errors_found and "Errors found" or "No errors found")

   return errors_found
end

local function SafeCheckCorrectnessForArch(arch)
   local status, errors = pcall(CheckCorrectnessForArch, arch)
   if not status then
      print(string.format("There was an error: %s", errors))
      errors = true
   end
   return errors
end

local errors = false

for _, arch in ipairs(ARCHS) do
   if not ARCHITECTURES[arch] then
      error ("Unknown arch: " .. arch)
   end

   errors = SafeCheckCorrectnessForArch(arch, report) or errors
end

os.exit(errors and 1 or 0)
