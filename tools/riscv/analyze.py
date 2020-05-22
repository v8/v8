#!/usr/bin/python3

# This is a simple tool to parse debug output from the RISC-V assembler and
# simulator to generate information useful for debugging.
#
# Current features:
#  * Call stack
#
# To use it, first execute your test with the flags `--print-all-code` and
# `--trace-sim`, dumping the output to a file. Then execute this tool, passing
# it that dump file and it will generate the call stack to stdout.
#
#   $ cctest --print-all-code -trace-sim test-interpreter-intrinsics/Call &> out
#   $ analyze.py out

import sys
import argparse
import re


class Trampoline:
    def __init__(self):
        self.start = 0
        self.end = 0

        def __repr__(self):
            return f"Trampoline: {self.start} - {self.end}"

        def hasPC(self, pc):
            if pc >= self.start and pc <= self.end:
                return True
            return False


class Function:
    def __init__(self, kind):
        self.kind = kind
        self.name = "unnamed"
        self.compiler = ""
        self.address = 0
        self.trampoline = None
        self.start = 0
        self.end = 0

    def __repr__(self):
        return f"Function: {self.name}"

    def __str__(self):
        return self.name

    # Returns a tuple with the first value indicating if the PC is within this
    # function and the second indicating if it is in the trampoline
    def hasPC(self, pc):
        if pc >= self.start and pc <= self.end:
            return True, False
        elif self.trampoline.hasPC(pc):
            return True, True
        return False, False


unknownFunc = Function('unknown')
unknownFunc.name = 'unknown'
hostFunc = Function('host')
hostFunc.name = 'host'


class FunctionCall:
    indentLevel = 0

    def __init__(self, func, pc, ra, sp=None, fp=None):
        self.func = func
        self.pc = pc
        self.ra = ra
        self.sp = sp
        self.fp = fp
        self.indentLevel = FunctionCall.indentLevel
        FunctionCall.indentLevel = FunctionCall.indentLevel + 1

    def returnFrom(self, ra=None, sp=None, fp=None):
        if ra is not None and ra != self.ra:
            print(
                f"### WARNING: Expected return address = {self.ra}, actual = {ra}")
        if sp is not None and self.sp is not None and sp != self.sp:
            print(
                f"### WARNING: Expected stack pointer = {self.sp}, actual = {sp}")
        if fp is not None and self.fp is not None and fp != self.fp:
            print(
                f"### WARNING: Expected frame pointer = {self.fp}, actual = {fp}")
        FunctionCall.indentLevel = FunctionCall.indentLevel - 1


def isStore(s):
    if s in ["sd", "sw", "sh", "sb", "fsd", "fsw"]:
        return True
    return False


def isBranch(s):
    if s[0] == 'b':
        return True
    return False


def isJump(s):
    if s == "j" or s == "jr":
        return True
    return False


def isJumpAndLink(s):
    if s[0:3] == "jal":
        return True
    return False


parser = argparse.ArgumentParser()
parser.add_argument('--inline', action='store_true', default=False,
                    dest='inline', help='Print comments inline with trace')
parser.add_argument('logfile', nargs=1)
args = parser.parse_args()

tracefile = open(args.logfile[0])
functions = {}
current = None
inTrampoline = False
inBody = False
inSafePoints = False
skip = 0

inTraceSim = False
callStack = []
registers = {
    "zero_reg": 0,
    "ra": 0,
    "sp": 0,
    "gp": 0,
    "tp": 0,
    "t0": 0,
    "t1": 0,
    "t2": 0,
    "fp": 0,
    "s1": 0,
    "a0": 0,
    "a1": 0,
    "a2": 0,
    "a3": 0,
    "a4": 0,
    "a5": 0,
    "a6": 0,
    "a7": 0,
    "s2": 0,
    "s3": 0,
    "s4": 0,
    "s5": 0,
    "s6": 0,
    "s7": 0,
    "s8": 0,
    "s9": 0,
    "s10": 0,
    "s11": 0,
    "t3": 0,
    "t4": 0,
    "t5": 0,
    "t6": 0
}

nextLine = tracefile.readline()
while nextLine:
    line = nextLine
    nextLine = tracefile.readline()
    if skip > 0:
        skip = skip - 1
        continue

    words = line.split()
    if len(words) == 0:
        continue

    if words[0] == "kind":
        # Start a new function
        current = Function(words[2])
    elif words[0] == "name":
        current.name = words[2]
    elif words[0] == "compiler":
        current.compiler = words[2]
    elif words[0] == "address":
        current.address = words[2]
    elif words[0] == "Trampoline":
        current.trampoline = Trampoline()
        inTrampoline = True
    elif words[0] == "Instructions":
        inTrampoline = False
        inBody = True
    elif words[0] == "Safepoints" or words[0] == "Deoptimization":
        inBody = False
        inSafePoints = True
    elif words[0] == "RelocInfo":
        inSafePoints = False
        # End this function
        inBody = False
        functions[current.start] = current
        if current.trampoline is not None:
            functions[current.trampoline.start] = current
        current = None
        # skip the next line
        skip = 1
    elif words[0] == "---":
        inTraceSim = False
    elif words[0] == "CallImpl:":
        addr = int(words[7], 16)
        func = functions[addr]
        call = FunctionCall(func, addr, 0xFFFFFFFFFFFFFFFE)
        callStack.append(call)
        print(f"### Start in {func.name}")
        inTraceSim = True
    else:
        if inSafePoints:
            continue

        pc = None
        try:
            pc = int(words[0], 16)
        except ValueError:
            pass

        if pc is not None and len(words) >= 3:
            if not inTraceSim:
                if words[1] == "0":
                    if inTrampoline:
                        current.trampoline.start = int(words[0], 16)
                    elif inBody:
                        current.start = int(words[0], 16)
                else:
                    if inTrampoline:
                        current.trampoline.end = int(words[0], 16)
                    elif inBody:
                        current.end = int(words[0], 16)
            else:
                # Track all register writes
                if isJumpAndLink(words[2]):
                    val = int(line[66:82], 16)
                    parts = line[44:66].split(',')
                    if len(parts) == 1:  # implicit rd = ra
                        registers['ra'] = val
                    else:                # explicit rd
                        registers[parts[0]] = val
                elif not isStore(words[2]) and \
                        not isBranch(words[2]) and \
                        not isJump(words[2]):
                    rdRes = re.search('^[a-zA-Z0-9_]+', line[44:-1])
                    val = None
                    try:
                        val = int(line[66:82], 16)
                    except ValueError:
                        pass
                    if rdRes is not None and val is not None:
                        rd = rdRes.group(0)
                        if rd in registers:
                            registers[rd] = val

                # Check for a call instruction: "jalr ra, N(R)"
                if len(words) >= 4 and words[2] == "jalr" and words[3] == "ra,":
                    addrParts = re.split('[\(\)]', words[4])
                    addr = int(addrParts[0]) + registers[addrParts[1]]
                    if addr in functions.keys():
                        func = functions[addr]
                        call = FunctionCall(func, addr, int(
                            words[5], 16), registers['sp'], registers['fp'])
                        callStack.append(call)
                        print(
                            f"### {'  ' * call.indentLevel}Call {func.name} {words[6]}")
                    else:
                        func = unknownFunc
                        if nextLine and nextLine.startswith("Call to host function"):
                            func = hostFunc
                        call = FunctionCall(func, addr, int(
                            words[5], 16), registers['sp'], registers['fp'])
                        callStack.append(call)
                        print(
                            f"### {'  ' * call.indentLevel}Call {func.name} {words[6]}")
                if len(words) >= 4 and words[2] == "jalr" and not words[3].endswith(','):
                    addr = registers[words[3]]
                    if addr in functions.keys():
                        func = functions[addr]
                        call = FunctionCall(func, addr, int(
                            words[4], 16), registers['sp'], registers['fp'])
                        callStack.append(call)
                        print(
                            f"### {'  ' * call.indentLevel}Call {func.name} {words[5]}")
                    else:
                        func = unknownFunc
                        if nextLine and nextLine.startswith("Call to host function"):
                            func = hostFunc
                        call = FunctionCall(func, addr, int(
                            words[4], 16), registers['sp'], registers['fp'])
                        callStack.append(call)
                        print(
                            f"### {'  ' * call.indentLevel}Call {func.name} {words[5]}")

                # Check for a return
                if len(words) >= 5 and words[2] == "jalr" and \
                        words[3] == "zero_reg," and words[4] == "0(ra)":
                    if len(callStack) > 0:
                        call = callStack.pop()
                        print(
                            f"### {'  ' * call.indentLevel}Return from {call.func.name} {words[6]}")
                        call.returnFrom(
                            registers['ra'], registers['sp'], registers['fp'])
                    else:
                        print(
                            f"### {'  ' * call.indentLevel}Return from top-level {words[6]}")
                        call.returnFrom(
                            registers['ra'], registers['sp'], registers['fp'])
                if words[2] == "ret":
                    if len(callStack) > 0:
                        call = callStack.pop()
                        print(
                            f"### {'  ' * call.indentLevel}Return from {call.func.name} {words[4]}")
                        call.returnFrom(
                            registers['ra'], registers['sp'], registers['fp'])
                    else:
                        print(
                            f"### {'  ' * call.indentLevel}Return from top-level {words[4]}")
                        call.returnFrom(
                            registers['ra'], registers['sp'], registers['fp'])

            if args.inline and inTraceSim:
                print(line, end='')
        if words[0] == "Returned":
            call = callStack.pop()
            print(f"### {'  ' * call.indentLevel}Return from {call.func.name}")
            call.returnFrom()

    line = nextLine

tracefile.close()
