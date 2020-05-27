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


class Instruction:
    def __init__(self, line, pc, insn, operands, offset):
        self.line = line
        self.pc = pc
        self.insn = insn
        self.operands = operands
        self.offset = offset

    def __repr__(self):
        return f"{hex(self.pc)} {self.insn} {','.join(self.operands)}"

    # Create an Instruction from a line of the code dump, or if it
    # does not look like an instruction, return None
    # A normal instruction looks like:
    #  0x55a1aa324b38   178  00008393       mv        t2, ra
    @classmethod
    def fromLine(cls, line):
        words = line.split()
        if len(words) < 4:
            return None
        pc = None
        offset = None
        insnHex = None
        try:
            pc = int(words[0], 16)
            offset = int(words[1], 16)
            insnHex = int(words[2], 16)
        except ValueError:
            pass
        if pc is None or offset is None or insnHex is None:
            return None

        insn = words[3]
        operands = []
        for idx in range(4, len(words)):
            word = words[idx]
            parts = re.split('[\(\)]', word)
            for part in parts:
                if len(part) > 0:
                    operands.append(part.strip(','))
            if not word.endswith(','):
                # This is the last operand
                break
        return cls(line, pc, insn, operands, offset)


class InstructionTrace:
    def __init__(self, line, pc, insn, operands, result, count):
        self.line = line
        self.pc = pc
        self.insn = insn
        self.operands = operands
        self.result = result
        self.count = count

    def __repr__(self):
        return f"{hex(self.pc)}\t{self.insn} {','.join(self.operands)}\t({insn.count})"

    # Create an InstructionTrace from a line of the simulator trace, or if it
    # does not look like an instruction, return None
    # A normal instruction looks like:
    #   0x00a0caf43be0   00000e37       lui       t3, 0x0               0000000000000000    (71)    int64:0       uint64:0
    @classmethod
    def fromLine(cls, line):
        words = line.split()
        if len(words) < 3:
            return None
        if not words[0].startswith('0x') or len(words[1]) != 8:
            return None

        insn = words[2]
        pc = None
        insnHex = None
        try:
            pc = int(words[0], 16)
            insnHex = int(words[1], 16)
        except ValueError:
            pass
        countRes = re.search('\(([1-9][0-9]* *)\)', line)
        if pc is None or insnHex is None or (countRes is None and
                                             not isControlFlow(insn)):
            return None

        count = -1
        if countRes is not None:
            count = int(countRes.group(1))
        operands = []
        result = None
        if insn == 'ret':  # No operands
            pass
        else:
            resIdx = 3
            for idx in range(3, len(words)):
                resIdx = idx + 1
                word = words[idx]
                parts = re.split('[\(\)]', word)
                for part in parts:
                    if len(part) > 0:
                        operands.append(part.strip(','))
                if not word.endswith(','):
                    # This is the last operand
                    break
            # The result is the next word after the operands
            if resIdx < len(words) and not isStore(insn) and not isBranch(insn):
                result = int(words[resIdx], 16)
            if args.target == 'mips' and (insn == 'bal' or insn == 'jalr'):
                result = pc + 8

        return cls(line, pc, insn, operands, result, count)

    def isStore(self):
        return isStore(self.insn)

    def isBranch(self):
        return isBranch(self.insn)

    def isJump(self):
        return isJump(self.insn)

    def isJumpAndLink(self):
        return isJumpAndLink(self.insn)

    def isCall(self):
        if self.insn == 'jalr' and \
                ((args.target == 'riscv' and
                  (self.operands[0] == 'ra' or len(self.operands) == 1)) or
                 (args.target == 'mips' and self.operands[1] == 'ra')):
            return True
        return False

    def isReturn(self):
        if self.insn == 'ret' or (self.insn == 'jr' and self.operands[0] == 'ra'):
            return True
        return False

    def callTarget(self):
        if not self.isCall():
            return None
        if args.target == 'riscv':
            # jalr xN
            if len(self.operands) == 1:
                return registers[self.operands[0]]

            offset = None
            try:
                int(self.operands[0])
            except ValueError:
                pass
            # jalr M(xN)
            if offset is not None:
                return offset + registers[self.operands[1]]
            else:  # jalr xM, xN
                return registers[self.operands[1]]
        elif args.target == 'mips':
            return registers[self.operands[0]]
        else:
            return None

    def getDestinationReg(self):
        if len(self.operands) == 0:
            return None
        if self.isStore():
            return None
        elif self.isBranch():
            return None
        elif self.isJump():
            return None
        elif self.isJumpAndLink():
            if len(self.operands) == 1:  # Implicit ra
                return 'ra'
            elif args.target == 'riscv':
                return self.operands[0]
            elif args.target == 'mips':
                return self.operands[1]
        return self.operands[0]


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
    if s[0:3] == "jal" or s == "bal":
        return True
    return False


def isControlFlow(s):
    return isBranch(s) or isJump(s) or isJumpAndLink(s) or s == 'ecall'


def printArgs(indentLevel=0):
    print(
        f"### {'  ' * call.indentLevel}  sp={hex(registers['sp'])} fp={hex(registers['fp'])}")
    print(f"### {'  ' * call.indentLevel}  Args:", end='')
    for i in range(0, 8):
        r = f"a{i}"
        val = '?'
        if r in registers:
            val = hex(registers[r])
        print(f" {val}", end='')
    print()


def printReturnValues(indentLevel=0):
    print(f"### {'  ' * call.indentLevel}  Returned: ", end='')
    prefix = 'a' if args.target == 'riscv' else 'v'
    for i in range(0, 2):
        r = f"{prefix}{i}"
        val = '?'
        if r in registers:
            val = hex(registers[r])
        print(f" {val}", end='')
    print()


parser = argparse.ArgumentParser()
parser.add_argument('--inline', action='store_true', default=False,
                    dest='inline', help='Print comments inline with trace')
parser.add_argument('--target', default='riscv',
                    help='Specify the target architecture')
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
registers = {}

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

        if not inTraceSim:
            insn = Instruction.fromLine(line)
            if insn is not None:
                if insn.offset == 0:
                    if inTrampoline:
                        current.trampoline.start = insn.pc
                    elif inBody:
                        current.start = insn.pc
                else:
                    if inTrampoline:
                        current.trampoline.end = insn.pc
                    elif inBody:
                        current.end = insn.pc
        else:
            insn = InstructionTrace.fromLine(line)
            if insn is not None:
                dest = insn.getDestinationReg()
                if dest is not None and insn.result is not None:
                    registers[dest] = insn.result

                if insn.isCall():
                    addr = insn.callTarget()
                    if addr in functions.keys():
                        func = functions[addr]
                        call = FunctionCall(func, addr, insn.result,
                                            registers['sp'], registers['fp'])
                        callStack.append(call)
                        print(
                            f"### {'  ' * call.indentLevel}Call {func.name} {insn.count}")
                        printArgs(call.indentLevel)
                    else:
                        func = unknownFunc
                        if nextLine and nextLine.startswith("Call to host function"):
                            func = hostFunc
                        call = FunctionCall(func, addr, insn.result,
                                            registers['sp'], registers['fp'])
                        callStack.append(call)
                        print(
                            f"### {'  ' * call.indentLevel}Call {func.name} {insn.count}")
                        printArgs(call.indentLevel)

                if insn.isReturn():
                    call = callStack.pop()
                    print(
                        f"### {'  ' * call.indentLevel}Return from {call.func.name} {insn.count}")
                    printReturnValues(call.indentLevel)
                    call.returnFrom(registers['ra'],
                                    registers['sp'], registers['fp'])

                if args.inline:
                    print(line, end='')

            if words[0] == "Returned":
                prefix = 'a' if args.target == 'riscv' else 'v'
                registers[f'{prefix}1'] = int(words[1], 16)
                registers[f'{prefix}0'] = int(words[3], 16)
                call = callStack.pop()
                print(
                    f"### {'  ' * call.indentLevel}Return from {call.func.name}")
                printReturnValues(call.indentLevel)
                call.returnFrom()
    line = nextLine

tracefile.close()
