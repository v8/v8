/* This is JavaScriptCore's variant of the PCRE library. While this library
started out as a copy of PCRE, many of the features of PCRE have been
removed. This library now supports only the regular expression features
required by the JavaScript language specification, and has only the functions
needed by JavaScriptCore and the rest of WebKit.

                 Originally written by Philip Hazel
           Copyright (c) 1997-2006 University of Cambridge
    Copyright (C) 2002, 2004, 2006, 2007 Apple Inc. All rights reserved.
    Copyright (C) 2007 Eric Seidel <eric@webkit.org>

-----------------------------------------------------------------------------
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    * Neither the name of the University of Cambridge nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------------
*/

/* This module contains jsRegExpExecute(), the externally visible function
that does pattern matching using an NFA algorithm, following the rules from
the JavaScript specification. There are also some supporting functions. */

#include "config.h"

#include "pcre_internal.h"

#include "ASCIICType.h"

#include <ctype.h>
#include <limits.h>
#include <string.h> /* for memcpy */

#ifdef __GNUC__
#define USE_COMPUTED_GOTO_FOR_MATCH_RECURSION
//#define USE_COMPUTED_GOTO_FOR_MATCH_OPCODE_LOOP
#endif

/* Avoid warnings on Windows. */
#undef min
#undef max

#ifndef USE_COMPUTED_GOTO_FOR_MATCH_RECURSION
typedef int ReturnLocation;
#else
typedef void* ReturnLocation;
#endif

/* Structure for building a chain of data for holding the values of
the subject pointer at the start of each bracket, used to detect when
an empty string has been matched by a bracket to break infinite loops. */
template <typename Char>
struct BracketChainNode {
    BracketChainNode<Char>* previousBracket;
    const Char* bracketStart;
};

template <typename Char>
struct MatchFrame {
    ReturnLocation returnLocation;
    struct MatchFrame<Char>* previousFrame;

    /* Function arguments that may change */
    struct {
        const Char* subjectPtr;
        const unsigned char* instructionPtr;
        int offsetTop;
        BracketChainNode<Char>* bracketChain;
    } args;


    /* PCRE uses "fake" recursion built off of gotos, thus
     stack-based local variables are not safe to use.  Instead we have to
     store local variables on the current MatchFrame. */
    struct {
        const unsigned char* data;
        const unsigned char* startOfRepeatingBracket;
        const Char* subjectPtrAtStartOfInstruction; // Several instrutions stash away a subjectPtr here for later compare
        const unsigned char* instructionPtrAtStartOfOnce;

        int repeatOthercase;

        int ctype;
        int fc;
        int fi;
        int length;
        int max;
        int number;
        int offset;
        int saveOffset1;
        int saveOffset2;
        int saveOffset3;

        BracketChainNode<Char> bracketChainNode;
    } locals;
};

/* Structure for passing "static" information around between the functions
doing traditional NFA matching, so that they are thread-safe. */

template <typename Char>
struct MatchData {
  int*   offsetVector;         /* Offset vector */
  int    offsetEnd;            /* One past the end */
  int    offsetMax;            /* The maximum usable for return data */
  bool   offsetOverflow;       /* Set if too many extractions */
  const Char*  startSubject;         /* Start of the subject string */
  const Char*  endSubject;           /* End of the subject string */
  const Char*  endMatchPtr;         /* Subject position at end match */
  int    endOffsetTop;        /* Highwater mark at end of match */
  bool   multiline;
  bool   ignoreCase;
};

/* The maximum remaining length of subject we are prepared to search for a
req_byte match. */

#define REQ_BYTE_MAX 1000

/* The below limit restricts the number of "recursive" match calls in order to
avoid spending exponential time on complex regular expressions. */

static const unsigned matchLimit = 100000;

#ifdef DEBUG
/*************************************************
*        Debugging function to print chars       *
*************************************************/

/* Print a sequence of chars in printable format, stopping at the end of the
subject if the requested.

Arguments:
  p           points to characters
  length      number to print
  isSubject  true if printing from within md.startSubject
  md          pointer to matching data block, if isSubject is true
*/

template <typename Char>
static void pchars(const Char* p, int length, bool isSubject, const MatchData& md)
{
    if (isSubject && length > md.endSubject - p)
        length = md.endSubject - p;
    while (length-- > 0) {
        int c;
        if (isprint(c = *(p++)))
            printf("%c", c);
        else if (c < 256)
            printf("\\x%02x", c);
        else
            printf("\\x{%x}", c);
    }
}
#endif

/*************************************************
*          Match a back-reference                *
*************************************************/

/* If a back reference hasn't been set, the length that is passed is greater
than the number of characters left in the string, so the match fails.

Arguments:
  offset      index into the offset vector
  subjectPtr        points into the subject
  length      length to be matched
  md          points to match data block

Returns:      true if matched
*/

template <typename Char>
static bool matchRef(int offset, const Char* subjectPtr, int length, const MatchData<Char>& md)
{
    const Char* p = md.startSubject + md.offsetVector[offset];

#ifdef DEBUG
    if (subjectPtr >= md.endSubject)
        printf("matching subject <null>");
    else {
        printf("matching subject ");
        pchars(subjectPtr, length, true, md);
    }
    printf(" against backref ");
    pchars(p, length, false, md);
    printf("\n");
#endif

    /* Always fail if not enough characters left */

    if (length > md.endSubject - subjectPtr)
        return false;

    /* Separate the caselesss case for speed */

    if (md.ignoreCase) {
        while (length-- > 0) {
            Char c = *p++;
            int othercase = kjs_pcre_ucp_othercase(c);
            Char d = *subjectPtr++;
            if (c != d && othercase != d)
                return false;
        }
    }
    else {
        while (length-- > 0)
            if (*p++ != *subjectPtr++)
                return false;
    }

    return true;
}

#ifndef USE_COMPUTED_GOTO_FOR_MATCH_RECURSION

/* Use numbered labels and switch statement at the bottom of the match function. */

#define RMATCH_WHERE(num) num
#define RRETURN_LABEL RRETURN_SWITCH

#else

/* Use GCC's computed goto extension. */

/* For one test case this is more than 40% faster than the switch statement.
We could avoid the use of the num argument entirely by using local labels,
but using it for the GCC case as well as the non-GCC case allows us to share
a bit more code and notice if we use conflicting numbers.*/

#define RMATCH_WHERE(num) &&RRETURN_##num
#define RRETURN_LABEL *stack.currentFrame->returnLocation

#endif

#define RECURSIVE_MATCH_COMMON(num) \
    goto RECURSE;\
    RRETURN_##num: \
    stack.popCurrentFrame();

#define RECURSIVE_MATCH(num, ra, rb) \
    do { \
        stack.pushNewFrame((ra), (rb), RMATCH_WHERE(num)); \
        RECURSIVE_MATCH_COMMON(num) \
    } while (0)

#define RECURSIVE_MATCH_STARTNG_NEW_GROUP(num, ra, rb) \
    do { \
        stack.pushNewFrame((ra), (rb), RMATCH_WHERE(num)); \
        startNewGroup(stack.currentFrame); \
        RECURSIVE_MATCH_COMMON(num) \
    } while (0)

#define RRETURN goto RRETURN_LABEL

#define RRETURN_NO_MATCH do { isMatch = false; RRETURN; } while (0)

/*************************************************
*         Match from current position            *
*************************************************/

/* On entry instructionPtr points to the first opcode, and subjectPtr to the first character
in the subject string, while substringStart holds the value of subjectPtr at the start of the
last bracketed group - used for breaking infinite loops matching zero-length
strings. This function is called recursively in many circumstances. Whenever it
returns a negative (error) response, the outer match() call must also return the
same response.

Arguments:
   subjectPtr        pointer in subject
   instructionPtr       position in code
   offsetTop  current top pointer
   md          pointer to "static" info for the match

Returns:       1 if matched          )  these values are >= 0
               0 if failed to match  )
               a negative error value if aborted by an error condition
                 (e.g. stopped by repeated call or recursion limit)
*/

static const unsigned FRAMES_ON_STACK = 16;

template <typename Char>
struct MatchStack {
    MatchStack()
        : framesEnd(frames + FRAMES_ON_STACK)
        , currentFrame(frames)
        , size(1) // match() creates accesses the first frame w/o calling pushNewFrame
    {
        ASSERT((sizeof(frames) / sizeof(frames[0])) == FRAMES_ON_STACK);
    }

    MatchFrame<Char> frames[FRAMES_ON_STACK];
    MatchFrame<Char>* framesEnd;
    MatchFrame<Char>* currentFrame;
    unsigned size;

    inline bool canUseStackBufferForNextFrame()
    {
        return size < FRAMES_ON_STACK;
    }

    inline MatchFrame<Char>* allocateNextFrame()
    {
        if (canUseStackBufferForNextFrame())
            return currentFrame + 1;
        return new MatchFrame<Char>;
    }

    inline void pushNewFrame(const unsigned char* instructionPtr, BracketChainNode<Char>* bracketChain, ReturnLocation returnLocation)
    {
        MatchFrame<Char>* newframe = allocateNextFrame();
        newframe->previousFrame = currentFrame;

        newframe->args.subjectPtr = currentFrame->args.subjectPtr;
        newframe->args.offsetTop = currentFrame->args.offsetTop;
        newframe->args.instructionPtr = instructionPtr;
        newframe->args.bracketChain = bracketChain;
        newframe->returnLocation = returnLocation;
        size++;

        currentFrame = newframe;
    }

    inline void popCurrentFrame()
    {
        MatchFrame<Char>* oldFrame = currentFrame;
        currentFrame = currentFrame->previousFrame;
        if (size > FRAMES_ON_STACK)
            delete oldFrame;
        size--;
    }

    void popAllFrames()
    {
        while (size)
            popCurrentFrame();
    }
};

template <typename Char>
static int matchError(int errorCode, MatchStack<Char>& stack)
{
    stack.popAllFrames();
    return errorCode;
}

/* Get the next UTF-8 character, not advancing the pointer, incrementing length
 if there are extra bytes. This is called when we know we are in UTF-8 mode. */

static inline void getUTF8CharAndIncrementLength(int& c, const unsigned char* subjectPtr, int& len)
{
    c = *subjectPtr;
    if ((c & 0xc0) == 0xc0) {
        int gcaa = kjs_pcre_utf8_table4[c & 0x3f];  /* Number of additional bytes */
        int gcss = 6 * gcaa;
        c = (c & kjs_pcre_utf8_table3[gcaa]) << gcss;
        for (int gcii = 1; gcii <= gcaa; gcii++) {
            gcss -= 6;
            c |= (subjectPtr[gcii] & 0x3f) << gcss;
        }
        len += gcaa;
    }
}

template <typename Char>
static inline void startNewGroup(MatchFrame<Char>* currentFrame)
{
    /* At the start of a bracketed group, add the current subject pointer to the
     stack of such pointers, to be re-instated at the end of the group when we hit
     the closing ket. When match() is called in other circumstances, we don't add to
     this stack. */

    currentFrame->locals.bracketChainNode.previousBracket = currentFrame->args.bracketChain;
    currentFrame->locals.bracketChainNode.bracketStart = currentFrame->args.subjectPtr;
    currentFrame->args.bracketChain = &currentFrame->locals.bracketChainNode;
}

// FIXME: "minimize" means "not greedy", we should invert the callers to ask for "greedy" to be less confusing
static inline void repeatInformationFromInstructionOffset(short instructionOffset, bool& minimize, int& minimumRepeats, int& maximumRepeats)
{
    // Instruction offsets are based off of OP_CRSTAR, OP_STAR, OP_TYPESTAR, OP_NOTSTAR
    static const char minimumRepeatsFromInstructionOffset[] = { 0, 0, 1, 1, 0, 0 };
    static const int maximumRepeatsFromInstructionOffset[] = { INT_MAX, INT_MAX, INT_MAX, INT_MAX, 1, 1 };

    ASSERT(instructionOffset >= 0);
    ASSERT(instructionOffset <= (OP_CRMINQUERY - OP_CRSTAR));

    minimize = (instructionOffset & 1); // this assumes ordering: Instruction, MinimizeInstruction, Instruction2, MinimizeInstruction2
    minimumRepeats = minimumRepeatsFromInstructionOffset[instructionOffset];
    maximumRepeats = maximumRepeatsFromInstructionOffset[instructionOffset];
}

template <typename Char>
static int match(const Char* subjectPtr, const unsigned char* instructionPtr, int offsetTop, MatchData<Char>& md)
{
    bool isMatch = false;
    int min;
    bool minimize = false; /* Initialization not really needed, but some compilers think so. */
    unsigned matchCount = 0;

    MatchStack<Char> stack;

    /* The opcode jump table. */
#ifdef USE_COMPUTED_GOTO_FOR_MATCH_OPCODE_LOOP
#define EMIT_JUMP_TABLE_ENTRY(opcode) &&LABEL_OP_##opcode,
    static void* opcodeJumpTable[256] = { FOR_EACH_OPCODE(EMIT_JUMP_TABLE_ENTRY) };
#undef EMIT_JUMP_TABLE_ENTRY
#endif

    /* One-time setup of the opcode jump table. */
#ifdef USE_COMPUTED_GOTO_FOR_MATCH_OPCODE_LOOP
    for (int i = 255; !opcodeJumpTable[i]; i--)
        opcodeJumpTable[i] = &&CAPTURING_BRACKET;
#endif

#ifdef USE_COMPUTED_GOTO_FOR_MATCH_RECURSION
    // Shark shows this as a hot line
    // Using a static const here makes this line disappear, but makes later access hotter (not sure why)
    stack.currentFrame->returnLocation = &&RETURN;
#else
    stack.currentFrame->returnLocation = 0;
#endif
    stack.currentFrame->args.subjectPtr = subjectPtr;
    stack.currentFrame->args.instructionPtr = instructionPtr;
    stack.currentFrame->args.offsetTop = offsetTop;
    stack.currentFrame->args.bracketChain = 0;
    startNewGroup(stack.currentFrame);

    /* This is where control jumps back to to effect "recursion" */

RECURSE:
    if (++matchCount > matchLimit)
        return matchError(JSRegExpErrorHitLimit, stack);

    /* Now start processing the operations. */

#ifndef USE_COMPUTED_GOTO_FOR_MATCH_OPCODE_LOOP
    while (true)
#endif
    {

#ifdef USE_COMPUTED_GOTO_FOR_MATCH_OPCODE_LOOP
#define BEGIN_OPCODE(opcode) LABEL_OP_##opcode
#define NEXT_OPCODE goto *opcodeJumpTable[*stack.currentFrame->args.instructionPtr]
#else
#define BEGIN_OPCODE(opcode) case OP_##opcode
#define NEXT_OPCODE continue
#endif

#ifdef USE_COMPUTED_GOTO_FOR_MATCH_OPCODE_LOOP
        NEXT_OPCODE;
#else
        switch (*stack.currentFrame->args.instructionPtr)
#endif
        {
            /* Non-capturing bracket: optimized */

            BEGIN_OPCODE(BRA):
            NON_CAPTURING_BRACKET:
                DPRINTF(("start bracket 0\n"));
                do {
                    RECURSIVE_MATCH_STARTNG_NEW_GROUP(2, stack.currentFrame->args.instructionPtr + 1 + LINK_SIZE, stack.currentFrame->args.bracketChain);
                    if (isMatch)
                        RRETURN;
                    stack.currentFrame->args.instructionPtr += getLinkValue(stack.currentFrame->args.instructionPtr + 1);
                } while (*stack.currentFrame->args.instructionPtr == OP_ALT);
                DPRINTF(("bracket 0 failed\n"));
                RRETURN;

            /* Skip over large extraction number data if encountered. */

            BEGIN_OPCODE(BRANUMBER):
                stack.currentFrame->args.instructionPtr += 3;
                NEXT_OPCODE;

            /* End of the pattern. */

            BEGIN_OPCODE(END):
                md.endMatchPtr = stack.currentFrame->args.subjectPtr;          /* Record where we ended */
                md.endOffsetTop = stack.currentFrame->args.offsetTop;   /* and how many extracts were taken */
                isMatch = true;
                RRETURN;

            /* Assertion brackets. Check the alternative branches in turn - the
             matching won't pass the KET for an assertion. If any one branch matches,
             the assertion is true. Lookbehind assertions have an OP_REVERSE item at the
             start of each branch to move the current point backwards, so the code at
             this level is identical to the lookahead case. */

            BEGIN_OPCODE(ASSERT):
                do {
                    RECURSIVE_MATCH_STARTNG_NEW_GROUP(6, stack.currentFrame->args.instructionPtr + 1 + LINK_SIZE, NULL);
                    if (isMatch)
                        break;
                    stack.currentFrame->args.instructionPtr += getLinkValue(stack.currentFrame->args.instructionPtr + 1);
                } while (*stack.currentFrame->args.instructionPtr == OP_ALT);
                if (*stack.currentFrame->args.instructionPtr == OP_KET)
                    RRETURN_NO_MATCH;

                /* Continue from after the assertion, updating the offsets high water
                 mark, since extracts may have been taken during the assertion. */

                advanceToEndOfBracket(stack.currentFrame->args.instructionPtr);
                stack.currentFrame->args.instructionPtr += 1 + LINK_SIZE;
                stack.currentFrame->args.offsetTop = md.endOffsetTop;
                NEXT_OPCODE;

            /* Negative assertion: all branches must fail to match */

            BEGIN_OPCODE(ASSERT_NOT):
                do {
                    RECURSIVE_MATCH_STARTNG_NEW_GROUP(7, stack.currentFrame->args.instructionPtr + 1 + LINK_SIZE, NULL);
                    if (isMatch)
                        RRETURN_NO_MATCH;
                    stack.currentFrame->args.instructionPtr += getLinkValue(stack.currentFrame->args.instructionPtr + 1);
                } while (*stack.currentFrame->args.instructionPtr == OP_ALT);

                stack.currentFrame->args.instructionPtr += 1 + LINK_SIZE;
                NEXT_OPCODE;

            /* An alternation is the end of a branch; scan along to find the end of the
             bracketed group and go to there. */

            BEGIN_OPCODE(ALT):
                advanceToEndOfBracket(stack.currentFrame->args.instructionPtr);
                NEXT_OPCODE;

            /* BRAZERO and BRAMINZERO occur just before a bracket group, indicating
             that it may occur zero times. It may repeat infinitely, or not at all -
             i.e. it could be ()* or ()? in the pattern. Brackets with fixed upper
             repeat limits are compiled as a number of copies, with the optional ones
             preceded by BRAZERO or BRAMINZERO. */

            BEGIN_OPCODE(BRAZERO): {
                stack.currentFrame->locals.startOfRepeatingBracket = stack.currentFrame->args.instructionPtr + 1;
                RECURSIVE_MATCH_STARTNG_NEW_GROUP(14, stack.currentFrame->locals.startOfRepeatingBracket, stack.currentFrame->args.bracketChain);
                if (isMatch)
                    RRETURN;
                advanceToEndOfBracket(stack.currentFrame->locals.startOfRepeatingBracket);
                stack.currentFrame->args.instructionPtr = stack.currentFrame->locals.startOfRepeatingBracket + 1 + LINK_SIZE;
                NEXT_OPCODE;
            }

            BEGIN_OPCODE(BRAMINZERO): {
                stack.currentFrame->locals.startOfRepeatingBracket = stack.currentFrame->args.instructionPtr + 1;
                advanceToEndOfBracket(stack.currentFrame->locals.startOfRepeatingBracket);
                RECURSIVE_MATCH_STARTNG_NEW_GROUP(15, stack.currentFrame->locals.startOfRepeatingBracket + 1 + LINK_SIZE, stack.currentFrame->args.bracketChain);
                if (isMatch)
                    RRETURN;
                stack.currentFrame->args.instructionPtr++;
                NEXT_OPCODE;
            }

            /* End of a group, repeated or non-repeating. If we are at the end of
             an assertion "group", stop matching and return 1, but record the
             current high water mark for use by positive assertions. Do this also
             for the "once" (not-backup up) groups. */

            BEGIN_OPCODE(KET):
            BEGIN_OPCODE(KETRMIN):
            BEGIN_OPCODE(KETRMAX):
                stack.currentFrame->locals.instructionPtrAtStartOfOnce = stack.currentFrame->args.instructionPtr - getLinkValue(stack.currentFrame->args.instructionPtr + 1);
                stack.currentFrame->locals.subjectPtrAtStartOfInstruction = stack.currentFrame->args.bracketChain->bracketStart;

                /* Back up the stack of bracket start pointers. */

                stack.currentFrame->args.bracketChain = stack.currentFrame->args.bracketChain->previousBracket;

                if (*stack.currentFrame->locals.instructionPtrAtStartOfOnce == OP_ASSERT || *stack.currentFrame->locals.instructionPtrAtStartOfOnce == OP_ASSERT_NOT) {
                    md.endOffsetTop = stack.currentFrame->args.offsetTop;
                    isMatch = true;
                    RRETURN;
                }

                /* In all other cases except a conditional group we have to check the
                 group number back at the start and if necessary complete handling an
                 extraction by setting the offsets and bumping the high water mark. */

                stack.currentFrame->locals.number = *stack.currentFrame->locals.instructionPtrAtStartOfOnce - OP_BRA;

                /* For extended extraction brackets (large number), we have to fish out
                 the number from a dummy opcode at the start. */

                if (stack.currentFrame->locals.number > EXTRACT_BASIC_MAX)
                    stack.currentFrame->locals.number = get2ByteValue(stack.currentFrame->locals.instructionPtrAtStartOfOnce + 2 + LINK_SIZE);
                stack.currentFrame->locals.offset = stack.currentFrame->locals.number << 1;

#ifdef DEBUG
                printf("end bracket %d", stack.currentFrame->locals.number);
                printf("\n");
#endif

                /* Test for a numbered group. This includes groups called as a result
                 of recursion. Note that whole-pattern recursion is coded as a recurse
                 into group 0, so it won't be picked up here. Instead, we catch it when
                 the OP_END is reached. */

                if (stack.currentFrame->locals.number > 0) {
                    if (stack.currentFrame->locals.offset >= md.offsetMax)
                        md.offsetOverflow = true;
                    else {
                        md.offsetVector[stack.currentFrame->locals.offset] =
                        md.offsetVector[md.offsetEnd - stack.currentFrame->locals.number];
                        md.offsetVector[stack.currentFrame->locals.offset+1] = stack.currentFrame->args.subjectPtr - md.startSubject;
                        if (stack.currentFrame->args.offsetTop <= stack.currentFrame->locals.offset)
                            stack.currentFrame->args.offsetTop = stack.currentFrame->locals.offset + 2;
                    }
                }

                /* For a non-repeating ket, just continue at this level. This also
                 happens for a repeating ket if no characters were matched in the group.
                 This is the forcible breaking of infinite loops as implemented in Perl
                 5.005. If there is an options reset, it will get obeyed in the normal
                 course of events. */

                if (*stack.currentFrame->args.instructionPtr == OP_KET || stack.currentFrame->args.subjectPtr == stack.currentFrame->locals.subjectPtrAtStartOfInstruction) {
                    stack.currentFrame->args.instructionPtr += 1 + LINK_SIZE;
                    NEXT_OPCODE;
                }

                /* The repeating kets try the rest of the pattern or restart from the
                 preceding bracket, in the appropriate order. */

                if (*stack.currentFrame->args.instructionPtr == OP_KETRMIN) {
                    RECURSIVE_MATCH(16, stack.currentFrame->args.instructionPtr + 1 + LINK_SIZE, stack.currentFrame->args.bracketChain);
                    if (isMatch)
                        RRETURN;
                    RECURSIVE_MATCH_STARTNG_NEW_GROUP(17, stack.currentFrame->locals.instructionPtrAtStartOfOnce, stack.currentFrame->args.bracketChain);
                    if (isMatch)
                        RRETURN;
                } else { /* OP_KETRMAX */
                    RECURSIVE_MATCH_STARTNG_NEW_GROUP(18, stack.currentFrame->locals.instructionPtrAtStartOfOnce, stack.currentFrame->args.bracketChain);
                    if (isMatch)
                        RRETURN;
                    RECURSIVE_MATCH(19, stack.currentFrame->args.instructionPtr + 1 + LINK_SIZE, stack.currentFrame->args.bracketChain);
                    if (isMatch)
                        RRETURN;
                }
                RRETURN;

            /* Start of subject. */

            BEGIN_OPCODE(CIRC):
                if (stack.currentFrame->args.subjectPtr != md.startSubject)
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr++;
                NEXT_OPCODE;

            /* After internal newline if multiline. */

            BEGIN_OPCODE(BOL):
                if (stack.currentFrame->args.subjectPtr != md.startSubject && !isNewline(stack.currentFrame->args.subjectPtr[-1]))
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr++;
                NEXT_OPCODE;

            /* End of subject. */

            BEGIN_OPCODE(DOLL):
                if (stack.currentFrame->args.subjectPtr < md.endSubject)
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr++;
                NEXT_OPCODE;

            /* Before internal newline if multiline. */

            BEGIN_OPCODE(EOL):
                if (stack.currentFrame->args.subjectPtr < md.endSubject && !isNewline(*stack.currentFrame->args.subjectPtr))
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr++;
                NEXT_OPCODE;

            /* Word boundary assertions */

            BEGIN_OPCODE(NOT_WORD_BOUNDARY):
            BEGIN_OPCODE(WORD_BOUNDARY): {
                bool currentCharIsWordChar = false;
                bool previousCharIsWordChar = false;

                if (stack.currentFrame->args.subjectPtr > md.startSubject)
                    previousCharIsWordChar = isWordChar(stack.currentFrame->args.subjectPtr[-1]);
                if (stack.currentFrame->args.subjectPtr < md.endSubject)
                    currentCharIsWordChar = isWordChar(*stack.currentFrame->args.subjectPtr);

                /* Now see if the situation is what we want */
                bool wordBoundaryDesired = (*stack.currentFrame->args.instructionPtr++ == OP_WORD_BOUNDARY);
                if (wordBoundaryDesired ? currentCharIsWordChar == previousCharIsWordChar : currentCharIsWordChar != previousCharIsWordChar)
                    RRETURN_NO_MATCH;
                NEXT_OPCODE;
            }

            /* Match a single character type; inline for speed */

            BEGIN_OPCODE(NOT_NEWLINE):
                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                    RRETURN_NO_MATCH;
                if (isNewline(*stack.currentFrame->args.subjectPtr++))
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr++;
                NEXT_OPCODE;

            BEGIN_OPCODE(NOT_DIGIT):
                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                    RRETURN_NO_MATCH;
                if (isASCIIDigit(*stack.currentFrame->args.subjectPtr++))
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr++;
                NEXT_OPCODE;

            BEGIN_OPCODE(DIGIT):
                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                    RRETURN_NO_MATCH;
                if (!isASCIIDigit(*stack.currentFrame->args.subjectPtr++))
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr++;
                NEXT_OPCODE;

            BEGIN_OPCODE(NOT_WHITESPACE):
                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                    RRETURN_NO_MATCH;
                if (isSpaceChar(*stack.currentFrame->args.subjectPtr++))
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr++;
                NEXT_OPCODE;

            BEGIN_OPCODE(WHITESPACE):
                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                    RRETURN_NO_MATCH;
                if (!isSpaceChar(*stack.currentFrame->args.subjectPtr++))
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr++;
                NEXT_OPCODE;

            BEGIN_OPCODE(NOT_WORDCHAR):
                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                    RRETURN_NO_MATCH;
                if (isWordChar(*stack.currentFrame->args.subjectPtr++))
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr++;
                NEXT_OPCODE;

            BEGIN_OPCODE(WORDCHAR):
                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                    RRETURN_NO_MATCH;
                if (!isWordChar(*stack.currentFrame->args.subjectPtr++))
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr++;
                NEXT_OPCODE;

            /* Match a back reference, possibly repeatedly. Look past the end of the
             item to see if there is repeat information following. The code is similar
             to that for character classes, but repeated for efficiency. Then obey
             similar code to character type repeats - written out again for speed.
             However, if the referenced string is the empty string, always treat
             it as matched, any number of times (otherwise there could be infinite
             loops). */

            BEGIN_OPCODE(REF):
                stack.currentFrame->locals.offset = get2ByteValue(stack.currentFrame->args.instructionPtr + 1) << 1;               /* Doubled ref number */
                stack.currentFrame->args.instructionPtr += 3;                                 /* Advance past item */

                /* If the reference is unset, set the length to be longer than the amount
                 of subject left; this ensures that every attempt at a match fails. We
                 can't just fail here, because of the possibility of quantifiers with zero
                 minima. */

                if (stack.currentFrame->locals.offset >= stack.currentFrame->args.offsetTop || md.offsetVector[stack.currentFrame->locals.offset] < 0)
                    stack.currentFrame->locals.length = 0;
                else
                    stack.currentFrame->locals.length = md.offsetVector[stack.currentFrame->locals.offset+1] - md.offsetVector[stack.currentFrame->locals.offset];

                /* Set up for repetition, or handle the non-repeated case */

                switch (*stack.currentFrame->args.instructionPtr) {
                    case OP_CRSTAR:
                    case OP_CRMINSTAR:
                    case OP_CRPLUS:
                    case OP_CRMINPLUS:
                    case OP_CRQUERY:
                    case OP_CRMINQUERY:
                        repeatInformationFromInstructionOffset(*stack.currentFrame->args.instructionPtr++ - OP_CRSTAR, minimize, min, stack.currentFrame->locals.max);
                        break;

                    case OP_CRRANGE:
                    case OP_CRMINRANGE:
                        minimize = (*stack.currentFrame->args.instructionPtr == OP_CRMINRANGE);
                        min = get2ByteValue(stack.currentFrame->args.instructionPtr + 1);
                        stack.currentFrame->locals.max = get2ByteValue(stack.currentFrame->args.instructionPtr + 3);
                        if (stack.currentFrame->locals.max == 0)
                            stack.currentFrame->locals.max = INT_MAX;
                        stack.currentFrame->args.instructionPtr += 5;
                        break;

                    default:               /* No repeat follows */
                        if (!matchRef(stack.currentFrame->locals.offset, stack.currentFrame->args.subjectPtr, stack.currentFrame->locals.length, md))
                            RRETURN_NO_MATCH;
                        stack.currentFrame->args.subjectPtr += stack.currentFrame->locals.length;
                        NEXT_OPCODE;
                }

                /* If the length of the reference is zero, just continue with the
                 main loop. */

                if (stack.currentFrame->locals.length == 0)
                    NEXT_OPCODE;

                /* First, ensure the minimum number of matches are present. */

                for (int i = 1; i <= min; i++) {
                    if (!matchRef(stack.currentFrame->locals.offset, stack.currentFrame->args.subjectPtr, stack.currentFrame->locals.length, md))
                        RRETURN_NO_MATCH;
                    stack.currentFrame->args.subjectPtr += stack.currentFrame->locals.length;
                }

                /* If min = max, continue at the same level without recursion.
                 They are not both allowed to be zero. */

                if (min == stack.currentFrame->locals.max)
                    NEXT_OPCODE;

                /* If minimizing, keep trying and advancing the pointer */

                if (minimize) {
                    for (stack.currentFrame->locals.fi = min;; stack.currentFrame->locals.fi++) {
                        RECURSIVE_MATCH(20, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                        if (isMatch)
                            RRETURN;
                        if (stack.currentFrame->locals.fi >= stack.currentFrame->locals.max || !matchRef(stack.currentFrame->locals.offset, stack.currentFrame->args.subjectPtr, stack.currentFrame->locals.length, md))
                            RRETURN;
                        stack.currentFrame->args.subjectPtr += stack.currentFrame->locals.length;
                    }
                    /* Control never reaches here */
                }

                /* If maximizing, find the longest string and work backwards */

                else {
                    stack.currentFrame->locals.subjectPtrAtStartOfInstruction = stack.currentFrame->args.subjectPtr;
                    for (int i = min; i < stack.currentFrame->locals.max; i++) {
                        if (!matchRef(stack.currentFrame->locals.offset, stack.currentFrame->args.subjectPtr, stack.currentFrame->locals.length, md))
                            break;
                        stack.currentFrame->args.subjectPtr += stack.currentFrame->locals.length;
                    }
                    while (stack.currentFrame->args.subjectPtr >= stack.currentFrame->locals.subjectPtrAtStartOfInstruction) {
                        RECURSIVE_MATCH(21, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                        if (isMatch)
                            RRETURN;
                        stack.currentFrame->args.subjectPtr -= stack.currentFrame->locals.length;
                    }
                    RRETURN_NO_MATCH;
                }
                /* Control never reaches here */

            /* Match a bit-mapped character class, possibly repeatedly. This op code is
             used when all the characters in the class have values in the range 0-255,
             and either the matching is caseful, or the characters are in the range
             0-127 when UTF-8 processing is enabled. The only difference between
             OP_CLASS and OP_NCLASS occurs when a data character outside the range is
             encountered.

             First, look past the end of the item to see if there is repeat information
             following. Then obey similar code to character type repeats - written out
             again for speed. */

            BEGIN_OPCODE(NCLASS):
            BEGIN_OPCODE(CLASS):
                stack.currentFrame->locals.data = stack.currentFrame->args.instructionPtr + 1;                /* Save for matching */
                stack.currentFrame->args.instructionPtr += 33;                     /* Advance past the item */

                switch (*stack.currentFrame->args.instructionPtr) {
                    case OP_CRSTAR:
                    case OP_CRMINSTAR:
                    case OP_CRPLUS:
                    case OP_CRMINPLUS:
                    case OP_CRQUERY:
                    case OP_CRMINQUERY:
                        repeatInformationFromInstructionOffset(*stack.currentFrame->args.instructionPtr++ - OP_CRSTAR, minimize, min, stack.currentFrame->locals.max);
                        break;

                    case OP_CRRANGE:
                    case OP_CRMINRANGE:
                        minimize = (*stack.currentFrame->args.instructionPtr == OP_CRMINRANGE);
                        min = get2ByteValue(stack.currentFrame->args.instructionPtr + 1);
                        stack.currentFrame->locals.max = get2ByteValue(stack.currentFrame->args.instructionPtr + 3);
                        if (stack.currentFrame->locals.max == 0)
                            stack.currentFrame->locals.max = INT_MAX;
                        stack.currentFrame->args.instructionPtr += 5;
                        break;

                    default:               /* No repeat follows */
                        min = stack.currentFrame->locals.max = 1;
                        break;
                }

                /* First, ensure the minimum number of matches are present. */

                for (int i = 1; i <= min; i++) {
                    if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                        RRETURN_NO_MATCH;
                    int c = *stack.currentFrame->args.subjectPtr++;
                    if (c > 255) {
                        if (stack.currentFrame->locals.data[-1] == OP_CLASS)
                            RRETURN_NO_MATCH;
                    } else {
                        if (!(stack.currentFrame->locals.data[c / 8] & (1 << (c & 7))))
                            RRETURN_NO_MATCH;
                    }
                }

                /* If max == min we can continue with the main loop without the
                 need to recurse. */

                if (min == stack.currentFrame->locals.max)
                    NEXT_OPCODE;

                /* If minimizing, keep testing the rest of the expression and advancing
                 the pointer while it matches the class. */
                if (minimize) {
                    for (stack.currentFrame->locals.fi = min;; stack.currentFrame->locals.fi++) {
                        RECURSIVE_MATCH(22, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                        if (isMatch)
                            RRETURN;
                        if (stack.currentFrame->locals.fi >= stack.currentFrame->locals.max || stack.currentFrame->args.subjectPtr >= md.endSubject)
                            RRETURN;
                        int c = *stack.currentFrame->args.subjectPtr++;
                        if (c > 255) {
                            if (stack.currentFrame->locals.data[-1] == OP_CLASS)
                                RRETURN;
                        } else {
                            if ((stack.currentFrame->locals.data[c/8] & (1 << (c&7))) == 0)
                                RRETURN;
                        }
                    }
                    /* Control never reaches here */
                }
                /* If maximizing, find the longest possible run, then work backwards. */
                else {
                    stack.currentFrame->locals.subjectPtrAtStartOfInstruction = stack.currentFrame->args.subjectPtr;

                    for (int i = min; i < stack.currentFrame->locals.max; i++) {
                        if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                            break;
                        int c = *stack.currentFrame->args.subjectPtr;
                        if (c > 255) {
                            if (stack.currentFrame->locals.data[-1] == OP_CLASS)
                                break;
                        } else {
                            if (!(stack.currentFrame->locals.data[c / 8] & (1 << (c & 7))))
                                break;
                        }
                        ++stack.currentFrame->args.subjectPtr;
                    }
                    for (;;) {
                        RECURSIVE_MATCH(24, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                        if (isMatch)
                            RRETURN;
                        if (stack.currentFrame->args.subjectPtr-- == stack.currentFrame->locals.subjectPtrAtStartOfInstruction)
                            break;        /* Stop if tried at original pos */
                    }

                    RRETURN;
                }
                /* Control never reaches here */

            /* Match an extended character class. */

            BEGIN_OPCODE(XCLASS):
                stack.currentFrame->locals.data = stack.currentFrame->args.instructionPtr + 1 + LINK_SIZE;                /* Save for matching */
                stack.currentFrame->args.instructionPtr += getLinkValue(stack.currentFrame->args.instructionPtr + 1);                      /* Advance past the item */

                switch (*stack.currentFrame->args.instructionPtr) {
                    case OP_CRSTAR:
                    case OP_CRMINSTAR:
                    case OP_CRPLUS:
                    case OP_CRMINPLUS:
                    case OP_CRQUERY:
                    case OP_CRMINQUERY:
                        repeatInformationFromInstructionOffset(*stack.currentFrame->args.instructionPtr++ - OP_CRSTAR, minimize, min, stack.currentFrame->locals.max);
                        break;

                    case OP_CRRANGE:
                    case OP_CRMINRANGE:
                        minimize = (*stack.currentFrame->args.instructionPtr == OP_CRMINRANGE);
                        min = get2ByteValue(stack.currentFrame->args.instructionPtr + 1);
                        stack.currentFrame->locals.max = get2ByteValue(stack.currentFrame->args.instructionPtr + 3);
                        if (stack.currentFrame->locals.max == 0)
                            stack.currentFrame->locals.max = INT_MAX;
                        stack.currentFrame->args.instructionPtr += 5;
                        break;

                    default:               /* No repeat follows */
                        min = stack.currentFrame->locals.max = 1;
                }

                /* First, ensure the minimum number of matches are present. */

                for (int i = 1; i <= min; i++) {
                    if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                        RRETURN_NO_MATCH;
                    int c = *stack.currentFrame->args.subjectPtr++;
                    if (!kjs_pcre_xclass(c, stack.currentFrame->locals.data))
                        RRETURN_NO_MATCH;
                }

                /* If max == min we can continue with the main loop without the
                 need to recurse. */

                if (min == stack.currentFrame->locals.max)
                    NEXT_OPCODE;

                /* If minimizing, keep testing the rest of the expression and advancing
                 the pointer while it matches the class. */

                if (minimize) {
                    for (stack.currentFrame->locals.fi = min;; stack.currentFrame->locals.fi++) {
                        RECURSIVE_MATCH(26, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                        if (isMatch)
                            RRETURN;
                        if (stack.currentFrame->locals.fi >= stack.currentFrame->locals.max || stack.currentFrame->args.subjectPtr >= md.endSubject)
                            RRETURN;
                        int c = *stack.currentFrame->args.subjectPtr++;
                        if (!kjs_pcre_xclass(c, stack.currentFrame->locals.data))
                            RRETURN;
                    }
                    /* Control never reaches here */
                }

                /* If maximizing, find the longest possible run, then work backwards. */

                else {
                    stack.currentFrame->locals.subjectPtrAtStartOfInstruction = stack.currentFrame->args.subjectPtr;
                    for (int i = min; i < stack.currentFrame->locals.max; i++) {
                        if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                            break;
                        int c = *stack.currentFrame->args.subjectPtr;
                        if (!kjs_pcre_xclass(c, stack.currentFrame->locals.data))
                            break;
                        ++stack.currentFrame->args.subjectPtr;
                    }
                    for(;;) {
                        RECURSIVE_MATCH(27, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                        if (isMatch)
                            RRETURN;
                        if (stack.currentFrame->args.subjectPtr-- == stack.currentFrame->locals.subjectPtrAtStartOfInstruction)
                            break;        /* Stop if tried at original pos */
                    }
                    RRETURN;
                }

                /* Control never reaches here */

            /* Match a single character, casefully */

            BEGIN_OPCODE(CHAR):
                stack.currentFrame->locals.length = 1;
                stack.currentFrame->args.instructionPtr++;
                getUTF8CharAndIncrementLength(stack.currentFrame->locals.fc, stack.currentFrame->args.instructionPtr, stack.currentFrame->locals.length);
                stack.currentFrame->args.instructionPtr += stack.currentFrame->locals.length;
                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                    RRETURN_NO_MATCH;
                if (stack.currentFrame->locals.fc != *stack.currentFrame->args.subjectPtr++)
                    RRETURN_NO_MATCH;
                NEXT_OPCODE;

            /* Match a single character, caselessly */

            BEGIN_OPCODE(CHAR_IGNORING_CASE): {
                stack.currentFrame->locals.length = 1;
                stack.currentFrame->args.instructionPtr++;
                getUTF8CharAndIncrementLength(stack.currentFrame->locals.fc, stack.currentFrame->args.instructionPtr, stack.currentFrame->locals.length);
                stack.currentFrame->args.instructionPtr += stack.currentFrame->locals.length;
                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                    RRETURN_NO_MATCH;
                int dc = *stack.currentFrame->args.subjectPtr++;
                if (stack.currentFrame->locals.fc != dc && kjs_pcre_ucp_othercase(stack.currentFrame->locals.fc) != dc)
                    RRETURN_NO_MATCH;
                NEXT_OPCODE;
            }

            /* Match a single ASCII character. */

            BEGIN_OPCODE(ASCII_CHAR):
                if (md.endSubject == stack.currentFrame->args.subjectPtr)
                    RRETURN_NO_MATCH;
                if (*stack.currentFrame->args.subjectPtr != stack.currentFrame->args.instructionPtr[1])
                    RRETURN_NO_MATCH;
                ++stack.currentFrame->args.subjectPtr;
                stack.currentFrame->args.instructionPtr += 2;
                NEXT_OPCODE;

            /* Match one of two cases of an ASCII letter. */

            BEGIN_OPCODE(ASCII_LETTER_IGNORING_CASE):
                if (md.endSubject == stack.currentFrame->args.subjectPtr)
                    RRETURN_NO_MATCH;
                if ((*stack.currentFrame->args.subjectPtr | 0x20) != stack.currentFrame->args.instructionPtr[1])
                    RRETURN_NO_MATCH;
                ++stack.currentFrame->args.subjectPtr;
                stack.currentFrame->args.instructionPtr += 2;
                NEXT_OPCODE;

            /* Match a single character repeatedly; different opcodes share code. */

            BEGIN_OPCODE(EXACT):
                min = stack.currentFrame->locals.max = get2ByteValue(stack.currentFrame->args.instructionPtr + 1);
                minimize = false;
                stack.currentFrame->args.instructionPtr += 3;
                goto REPEATCHAR;

            BEGIN_OPCODE(UPTO):
            BEGIN_OPCODE(MINUPTO):
                min = 0;
                stack.currentFrame->locals.max = get2ByteValue(stack.currentFrame->args.instructionPtr + 1);
                minimize = *stack.currentFrame->args.instructionPtr == OP_MINUPTO;
                stack.currentFrame->args.instructionPtr += 3;
                goto REPEATCHAR;

            BEGIN_OPCODE(STAR):
            BEGIN_OPCODE(MINSTAR):
            BEGIN_OPCODE(PLUS):
            BEGIN_OPCODE(MINPLUS):
            BEGIN_OPCODE(QUERY):
            BEGIN_OPCODE(MINQUERY):
                repeatInformationFromInstructionOffset(*stack.currentFrame->args.instructionPtr++ - OP_STAR, minimize, min, stack.currentFrame->locals.max);

                /* Common code for all repeated single-character matches. We can give
                 up quickly if there are fewer than the minimum number of characters left in
                 the subject. */

            REPEATCHAR:

                stack.currentFrame->locals.length = 1;
                getUTF8CharAndIncrementLength(stack.currentFrame->locals.fc, stack.currentFrame->args.instructionPtr, stack.currentFrame->locals.length);
                if (min * (stack.currentFrame->locals.fc > 0xFFFF ? 2 : 1) > md.endSubject - stack.currentFrame->args.subjectPtr)
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr += stack.currentFrame->locals.length;

                if (stack.currentFrame->locals.fc <= 0xFFFF) {
                    int othercase = md.ignoreCase ? kjs_pcre_ucp_othercase(stack.currentFrame->locals.fc) : -1;

                    for (int i = 1; i <= min; i++) {
                        if (*stack.currentFrame->args.subjectPtr != stack.currentFrame->locals.fc && *stack.currentFrame->args.subjectPtr != othercase)
                            RRETURN_NO_MATCH;
                        ++stack.currentFrame->args.subjectPtr;
                    }

                    if (min == stack.currentFrame->locals.max)
                        NEXT_OPCODE;

                    if (minimize) {
                        stack.currentFrame->locals.repeatOthercase = othercase;
                        for (stack.currentFrame->locals.fi = min;; stack.currentFrame->locals.fi++) {
                            RECURSIVE_MATCH(28, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                            if (isMatch)
                                RRETURN;
                            if (stack.currentFrame->locals.fi >= stack.currentFrame->locals.max || stack.currentFrame->args.subjectPtr >= md.endSubject)
                                RRETURN;
                            if (*stack.currentFrame->args.subjectPtr != stack.currentFrame->locals.fc && *stack.currentFrame->args.subjectPtr != stack.currentFrame->locals.repeatOthercase)
                                RRETURN;
                            ++stack.currentFrame->args.subjectPtr;
                        }
                        /* Control never reaches here */
                    } else {
                        stack.currentFrame->locals.subjectPtrAtStartOfInstruction = stack.currentFrame->args.subjectPtr;
                        for (int i = min; i < stack.currentFrame->locals.max; i++) {
                            if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                                break;
                            if (*stack.currentFrame->args.subjectPtr != stack.currentFrame->locals.fc && *stack.currentFrame->args.subjectPtr != othercase)
                                break;
                            ++stack.currentFrame->args.subjectPtr;
                        }
                        while (stack.currentFrame->args.subjectPtr >= stack.currentFrame->locals.subjectPtrAtStartOfInstruction) {
                            RECURSIVE_MATCH(29, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                            if (isMatch)
                                RRETURN;
                            --stack.currentFrame->args.subjectPtr;
                        }
                        RRETURN_NO_MATCH;
                    }
                    /* Control never reaches here */
                } else {
                    /* No case on surrogate pairs, so no need to bother with "othercase". */

                    for (int i = 1; i <= min; i++) {
                        if (*stack.currentFrame->args.subjectPtr != stack.currentFrame->locals.fc)
                            RRETURN_NO_MATCH;
                        stack.currentFrame->args.subjectPtr += 2;
                    }

                    if (min == stack.currentFrame->locals.max)
                        NEXT_OPCODE;

                    if (minimize) {
                        for (stack.currentFrame->locals.fi = min;; stack.currentFrame->locals.fi++) {
                            RECURSIVE_MATCH(30, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                            if (isMatch)
                                RRETURN;
                            if (stack.currentFrame->locals.fi >= stack.currentFrame->locals.max || stack.currentFrame->args.subjectPtr >= md.endSubject)
                                RRETURN;
                            if (*stack.currentFrame->args.subjectPtr != stack.currentFrame->locals.fc)
                                RRETURN;
                            stack.currentFrame->args.subjectPtr += 2;
                        }
                        /* Control never reaches here */
                    } else {
                        stack.currentFrame->locals.subjectPtrAtStartOfInstruction = stack.currentFrame->args.subjectPtr;
                        for (int i = min; i < stack.currentFrame->locals.max; i++) {
                            if (stack.currentFrame->args.subjectPtr > md.endSubject - 2)
                                break;
                            if (*stack.currentFrame->args.subjectPtr != stack.currentFrame->locals.fc)
                                break;
                            stack.currentFrame->args.subjectPtr += 2;
                        }
                        while (stack.currentFrame->args.subjectPtr >= stack.currentFrame->locals.subjectPtrAtStartOfInstruction) {
                            RECURSIVE_MATCH(31, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                            if (isMatch)
                                RRETURN;
                            stack.currentFrame->args.subjectPtr -= 2;
                        }
                        RRETURN_NO_MATCH;
                    }
                    /* Control never reaches here */
                }
                /* Control never reaches here */

            /* Match a negated single one-byte character. */

            BEGIN_OPCODE(NOT): {
                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                    RRETURN_NO_MATCH;
                stack.currentFrame->args.instructionPtr++;
                int c = *stack.currentFrame->args.subjectPtr++;
                if (md.ignoreCase) {
                    if (c < 128)
                        c = toLowerCase(c);
                    if (toLowerCase(*stack.currentFrame->args.instructionPtr++) == c)
                        RRETURN_NO_MATCH;
                } else {
                    if (*stack.currentFrame->args.instructionPtr++ == c)
                        RRETURN_NO_MATCH;
                }
                NEXT_OPCODE;
            }

            /* Match a negated single one-byte character repeatedly. This is almost a
             repeat of the code for a repeated single character, but I haven't found a
             nice way of commoning these up that doesn't require a test of the
             positive/negative option for each character match. Maybe that wouldn't add
             very much to the time taken, but character matching *is* what this is all
             about... */

            BEGIN_OPCODE(NOTEXACT):
                min = stack.currentFrame->locals.max = get2ByteValue(stack.currentFrame->args.instructionPtr + 1);
                minimize = false;
                stack.currentFrame->args.instructionPtr += 3;
                goto REPEATNOTCHAR;

            BEGIN_OPCODE(NOTUPTO):
            BEGIN_OPCODE(NOTMINUPTO):
                min = 0;
                stack.currentFrame->locals.max = get2ByteValue(stack.currentFrame->args.instructionPtr + 1);
                minimize = *stack.currentFrame->args.instructionPtr == OP_NOTMINUPTO;
                stack.currentFrame->args.instructionPtr += 3;
                goto REPEATNOTCHAR;

            BEGIN_OPCODE(NOTSTAR):
            BEGIN_OPCODE(NOTMINSTAR):
            BEGIN_OPCODE(NOTPLUS):
            BEGIN_OPCODE(NOTMINPLUS):
            BEGIN_OPCODE(NOTQUERY):
            BEGIN_OPCODE(NOTMINQUERY):
                repeatInformationFromInstructionOffset(*stack.currentFrame->args.instructionPtr++ - OP_NOTSTAR, minimize, min, stack.currentFrame->locals.max);

            /* Common code for all repeated single-byte matches. We can give up quickly
             if there are fewer than the minimum number of bytes left in the
             subject. */

            REPEATNOTCHAR:
                if (min > md.endSubject - stack.currentFrame->args.subjectPtr)
                    RRETURN_NO_MATCH;
                stack.currentFrame->locals.fc = *stack.currentFrame->args.instructionPtr++;

                /* The code is duplicated for the caseless and caseful cases, for speed,
                 since matching characters is likely to be quite common. First, ensure the
                 minimum number of matches are present. If min = max, continue at the same
                 level without recursing. Otherwise, if minimizing, keep trying the rest of
                 the expression and advancing one matching character if failing, up to the
                 maximum. Alternatively, if maximizing, find the maximum number of
                 characters and work backwards. */

                DPRINTF(("negative matching %c{%d,%d}\n", stack.currentFrame->locals.fc, min, stack.currentFrame->locals.max));

                if (md.ignoreCase) {
                    if (stack.currentFrame->locals.fc < 128)
                        stack.currentFrame->locals.fc = toLowerCase(stack.currentFrame->locals.fc);

                    for (int i = 1; i <= min; i++) {
                        int d = *stack.currentFrame->args.subjectPtr++;
                        if (d < 128)
                            d = toLowerCase(d);
                        if (stack.currentFrame->locals.fc == d)
                            RRETURN_NO_MATCH;
                    }

                    if (min == stack.currentFrame->locals.max)
                        NEXT_OPCODE;

                    if (minimize) {
                        for (stack.currentFrame->locals.fi = min;; stack.currentFrame->locals.fi++) {
                            RECURSIVE_MATCH(38, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                            if (isMatch)
                                RRETURN;
                            int d = *stack.currentFrame->args.subjectPtr++;
                            if (d < 128)
                                d = toLowerCase(d);
                            if (stack.currentFrame->locals.fi >= stack.currentFrame->locals.max || stack.currentFrame->args.subjectPtr >= md.endSubject || stack.currentFrame->locals.fc == d)
                                RRETURN;
                        }
                        /* Control never reaches here */
                    }

                    /* Maximize case */

                    else {
                        stack.currentFrame->locals.subjectPtrAtStartOfInstruction = stack.currentFrame->args.subjectPtr;

                        for (int i = min; i < stack.currentFrame->locals.max; i++) {
                            if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                                break;
                            int d = *stack.currentFrame->args.subjectPtr;
                            if (d < 128)
                                d = toLowerCase(d);
                            if (stack.currentFrame->locals.fc == d)
                                break;
                            ++stack.currentFrame->args.subjectPtr;
                        }
                        for (;;) {
                            RECURSIVE_MATCH(40, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                            if (isMatch)
                                RRETURN;
                            if (stack.currentFrame->args.subjectPtr-- == stack.currentFrame->locals.subjectPtrAtStartOfInstruction)
                                break;        /* Stop if tried at original pos */
                        }

                        RRETURN;
                    }
                    /* Control never reaches here */
                }

                /* Caseful comparisons */

                else {
                    for (int i = 1; i <= min; i++) {
                        int d = *stack.currentFrame->args.subjectPtr++;
                        if (stack.currentFrame->locals.fc == d)
                            RRETURN_NO_MATCH;
                    }

                    if (min == stack.currentFrame->locals.max)
                        NEXT_OPCODE;

                    if (minimize) {
                        for (stack.currentFrame->locals.fi = min;; stack.currentFrame->locals.fi++) {
                            RECURSIVE_MATCH(42, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                            if (isMatch)
                                RRETURN;
                            int d = *stack.currentFrame->args.subjectPtr++;
                            if (stack.currentFrame->locals.fi >= stack.currentFrame->locals.max || stack.currentFrame->args.subjectPtr >= md.endSubject || stack.currentFrame->locals.fc == d)
                                RRETURN;
                        }
                        /* Control never reaches here */
                    }

                    /* Maximize case */

                    else {
                        stack.currentFrame->locals.subjectPtrAtStartOfInstruction = stack.currentFrame->args.subjectPtr;

                        for (int i = min; i < stack.currentFrame->locals.max; i++) {
                            if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                                break;
                            int d = *stack.currentFrame->args.subjectPtr;
                            if (stack.currentFrame->locals.fc == d)
                                break;
                            ++stack.currentFrame->args.subjectPtr;
                        }
                        for (;;) {
                            RECURSIVE_MATCH(44, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                            if (isMatch)
                                RRETURN;
                            if (stack.currentFrame->args.subjectPtr-- == stack.currentFrame->locals.subjectPtrAtStartOfInstruction)
                                break;        /* Stop if tried at original pos */
                        }

                        RRETURN;
                    }
                }
                /* Control never reaches here */

            /* Match a single character type repeatedly; several different opcodes
             share code. This is very similar to the code for single characters, but we
             repeat it in the interests of efficiency. */

            BEGIN_OPCODE(TYPEEXACT):
                min = stack.currentFrame->locals.max = get2ByteValue(stack.currentFrame->args.instructionPtr + 1);
                minimize = true;
                stack.currentFrame->args.instructionPtr += 3;
                goto REPEATTYPE;

            BEGIN_OPCODE(TYPEUPTO):
            BEGIN_OPCODE(TYPEMINUPTO):
                min = 0;
                stack.currentFrame->locals.max = get2ByteValue(stack.currentFrame->args.instructionPtr + 1);
                minimize = *stack.currentFrame->args.instructionPtr == OP_TYPEMINUPTO;
                stack.currentFrame->args.instructionPtr += 3;
                goto REPEATTYPE;

            BEGIN_OPCODE(TYPESTAR):
            BEGIN_OPCODE(TYPEMINSTAR):
            BEGIN_OPCODE(TYPEPLUS):
            BEGIN_OPCODE(TYPEMINPLUS):
            BEGIN_OPCODE(TYPEQUERY):
            BEGIN_OPCODE(TYPEMINQUERY):
                repeatInformationFromInstructionOffset(*stack.currentFrame->args.instructionPtr++ - OP_TYPESTAR, minimize, min, stack.currentFrame->locals.max);

                /* Common code for all repeated single character type matches. Note that
                 in UTF-8 mode, '.' matches a character of any length, but for the other
                 character types, the valid characters are all one-byte long. */

            REPEATTYPE:
                stack.currentFrame->locals.ctype = *stack.currentFrame->args.instructionPtr++;      /* Code for the character type */

                /* First, ensure the minimum number of matches are present. Use inline
                 code for maximizing the speed, and do the type test once at the start
                 (i.e. keep it out of the loop). Also we can test that there are at least
                 the minimum number of characters before we start. */

                if (min > md.endSubject - stack.currentFrame->args.subjectPtr)
                    RRETURN_NO_MATCH;
                if (min > 0) {
                    switch (stack.currentFrame->locals.ctype) {
                        case OP_NOT_NEWLINE:
                            for (int i = 1; i <= min; i++) {
                                if (isNewline(*stack.currentFrame->args.subjectPtr))
                                    RRETURN_NO_MATCH;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        case OP_NOT_DIGIT:
                            for (int i = 1; i <= min; i++) {
                                if (isASCIIDigit(*stack.currentFrame->args.subjectPtr))
                                    RRETURN_NO_MATCH;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        case OP_DIGIT:
                            for (int i = 1; i <= min; i++) {
                                if (!isASCIIDigit(*stack.currentFrame->args.subjectPtr))
                                    RRETURN_NO_MATCH;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        case OP_NOT_WHITESPACE:
                            for (int i = 1; i <= min; i++) {
                                if (isSpaceChar(*stack.currentFrame->args.subjectPtr))
                                    RRETURN_NO_MATCH;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        case OP_WHITESPACE:
                            for (int i = 1; i <= min; i++) {
                                if (!isSpaceChar(*stack.currentFrame->args.subjectPtr))
                                    RRETURN_NO_MATCH;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        case OP_NOT_WORDCHAR:
                            for (int i = 1; i <= min; i++) {
                                if (isWordChar(*stack.currentFrame->args.subjectPtr))
                                    RRETURN_NO_MATCH;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        case OP_WORDCHAR:
                            for (int i = 1; i <= min; i++) {
                                if (!isWordChar(*stack.currentFrame->args.subjectPtr))
                                    RRETURN_NO_MATCH;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        default:
                            ASSERT_NOT_REACHED();
                            return matchError(JSRegExpErrorInternal, stack);
                    }  /* End switch(stack.currentFrame->locals.ctype) */
                }

                /* If min = max, continue at the same level without recursing */

                if (min == stack.currentFrame->locals.max)
                    NEXT_OPCODE;

                /* If minimizing, we have to test the rest of the pattern before each
                 subsequent match. */

                if (minimize) {
                    for (stack.currentFrame->locals.fi = min;; stack.currentFrame->locals.fi++) {
                        RECURSIVE_MATCH(48, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                        if (isMatch)
                            RRETURN;
                        if (stack.currentFrame->locals.fi >= stack.currentFrame->locals.max || stack.currentFrame->args.subjectPtr >= md.endSubject)
                            RRETURN;

                        int c = *stack.currentFrame->args.subjectPtr++;
                        switch (stack.currentFrame->locals.ctype) {
                            case OP_NOT_NEWLINE:
                                if (isNewline(c))
                                    RRETURN;
                                break;

                            case OP_NOT_DIGIT:
                                if (isASCIIDigit(c))
                                    RRETURN;
                                break;

                            case OP_DIGIT:
                                if (!isASCIIDigit(c))
                                    RRETURN;
                                break;

                            case OP_NOT_WHITESPACE:
                                if (isSpaceChar(c))
                                    RRETURN;
                                break;

                            case OP_WHITESPACE:
                                if (!isSpaceChar(c))
                                    RRETURN;
                                break;

                            case OP_NOT_WORDCHAR:
                                if (isWordChar(c))
                                    RRETURN;
                                break;

                            case OP_WORDCHAR:
                                if (!isWordChar(c))
                                    RRETURN;
                                break;

                            default:
                                ASSERT_NOT_REACHED();
                                return matchError(JSRegExpErrorInternal, stack);
                        }
                    }
                    /* Control never reaches here */
                }

                /* If maximizing it is worth using inline code for speed, doing the type
                 test once at the start (i.e. keep it out of the loop). */

                else {
                    stack.currentFrame->locals.subjectPtrAtStartOfInstruction = stack.currentFrame->args.subjectPtr;  /* Remember where we started */

                    switch (stack.currentFrame->locals.ctype) {
                        case OP_NOT_NEWLINE:
                            for (int i = min; i < stack.currentFrame->locals.max; i++) {
                                if (stack.currentFrame->args.subjectPtr >= md.endSubject || isNewline(*stack.currentFrame->args.subjectPtr))
                                    break;
                                stack.currentFrame->args.subjectPtr++;
                            }
                            break;

                        case OP_NOT_DIGIT:
                            for (int i = min; i < stack.currentFrame->locals.max; i++) {
                                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                                    break;
                                int c = *stack.currentFrame->args.subjectPtr;
                                if (isASCIIDigit(c))
                                    break;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        case OP_DIGIT:
                            for (int i = min; i < stack.currentFrame->locals.max; i++) {
                                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                                    break;
                                int c = *stack.currentFrame->args.subjectPtr;
                                if (!isASCIIDigit(c))
                                    break;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        case OP_NOT_WHITESPACE:
                            for (int i = min; i < stack.currentFrame->locals.max; i++) {
                                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                                    break;
                                int c = *stack.currentFrame->args.subjectPtr;
                                if (isSpaceChar(c))
                                    break;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        case OP_WHITESPACE:
                            for (int i = min; i < stack.currentFrame->locals.max; i++) {
                                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                                    break;
                                int c = *stack.currentFrame->args.subjectPtr;
                                if (!isSpaceChar(c))
                                    break;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        case OP_NOT_WORDCHAR:
                            for (int i = min; i < stack.currentFrame->locals.max; i++) {
                                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                                    break;
                                int c = *stack.currentFrame->args.subjectPtr;
                                if (isWordChar(c))
                                    break;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        case OP_WORDCHAR:
                            for (int i = min; i < stack.currentFrame->locals.max; i++) {
                                if (stack.currentFrame->args.subjectPtr >= md.endSubject)
                                    break;
                                int c = *stack.currentFrame->args.subjectPtr;
                                if (!isWordChar(c))
                                    break;
                                ++stack.currentFrame->args.subjectPtr;
                            }
                            break;

                        default:
                            ASSERT_NOT_REACHED();
                            return matchError(JSRegExpErrorInternal, stack);
                    }

                    /* stack.currentFrame->args.subjectPtr is now past the end of the maximum run */

                    for (;;) {
                        RECURSIVE_MATCH(52, stack.currentFrame->args.instructionPtr, stack.currentFrame->args.bracketChain);
                        if (isMatch)
                            RRETURN;
                        if (stack.currentFrame->args.subjectPtr-- == stack.currentFrame->locals.subjectPtrAtStartOfInstruction)
                            break;        /* Stop if tried at original pos */
                    }

                    /* Get here if we can't make it match with any permitted repetitions */

                    RRETURN;
                }
                /* Control never reaches here */

            BEGIN_OPCODE(CRMINPLUS):
            BEGIN_OPCODE(CRMINQUERY):
            BEGIN_OPCODE(CRMINRANGE):
            BEGIN_OPCODE(CRMINSTAR):
            BEGIN_OPCODE(CRPLUS):
            BEGIN_OPCODE(CRQUERY):
            BEGIN_OPCODE(CRRANGE):
            BEGIN_OPCODE(CRSTAR):
                ASSERT_NOT_REACHED();
                return matchError(JSRegExpErrorInternal, stack);

#ifdef USE_COMPUTED_GOTO_FOR_MATCH_OPCODE_LOOP
            CAPTURING_BRACKET:
#else
            default:
#endif
                /* Opening capturing bracket. If there is space in the offset vector, save
                 the current subject position in the working slot at the top of the vector. We
                 mustn't change the current values of the data slot, because they may be set
                 from a previous iteration of this group, and be referred to by a reference
                 inside the group.

                 If the bracket fails to match, we need to restore this value and also the
                 values of the final offsets, in case they were set by a previous iteration of
                 the same bracket.

                 If there isn't enough space in the offset vector, treat this as if it were a
                 non-capturing bracket. Don't worry about setting the flag for the error case
                 here; that is handled in the code for KET. */

                ASSERT(*stack.currentFrame->args.instructionPtr > OP_BRA);

                stack.currentFrame->locals.number = *stack.currentFrame->args.instructionPtr - OP_BRA;

                /* For extended extraction brackets (large number), we have to fish out the
                 number from a dummy opcode at the start. */

                if (stack.currentFrame->locals.number > EXTRACT_BASIC_MAX)
                    stack.currentFrame->locals.number = get2ByteValue(stack.currentFrame->args.instructionPtr + 2 + LINK_SIZE);
                stack.currentFrame->locals.offset = stack.currentFrame->locals.number << 1;

#ifdef DEBUG
                printf("start bracket %d subject=", stack.currentFrame->locals.number);
                pchars(stack.currentFrame->args.subjectPtr, 16, true, md);
                printf("\n");
#endif

                if (stack.currentFrame->locals.offset < md.offsetMax) {
                    stack.currentFrame->locals.saveOffset1 = md.offsetVector[stack.currentFrame->locals.offset];
                    stack.currentFrame->locals.saveOffset2 = md.offsetVector[stack.currentFrame->locals.offset + 1];
                    stack.currentFrame->locals.saveOffset3 = md.offsetVector[md.offsetEnd - stack.currentFrame->locals.number];

                    DPRINTF(("saving %d %d %d\n", stack.currentFrame->locals.saveOffset1, stack.currentFrame->locals.saveOffset2, stack.currentFrame->locals.saveOffset3));
                    md.offsetVector[md.offsetEnd - stack.currentFrame->locals.number] = stack.currentFrame->args.subjectPtr - md.startSubject;

                    do {
                        RECURSIVE_MATCH_STARTNG_NEW_GROUP(1, stack.currentFrame->args.instructionPtr + 1 + LINK_SIZE, stack.currentFrame->args.bracketChain);
                        if (isMatch)
                            RRETURN;
                        stack.currentFrame->args.instructionPtr += getLinkValue(stack.currentFrame->args.instructionPtr + 1);
                    } while (*stack.currentFrame->args.instructionPtr == OP_ALT);

                    DPRINTF(("bracket %d failed\n", stack.currentFrame->locals.number));

                    md.offsetVector[stack.currentFrame->locals.offset] = stack.currentFrame->locals.saveOffset1;
                    md.offsetVector[stack.currentFrame->locals.offset + 1] = stack.currentFrame->locals.saveOffset2;
                    md.offsetVector[md.offsetEnd - stack.currentFrame->locals.number] = stack.currentFrame->locals.saveOffset3;

                    RRETURN;
                }

                /* Insufficient room for saving captured contents */

                goto NON_CAPTURING_BRACKET;
        }

        /* Do not stick any code in here without much thought; it is assumed
         that "continue" in the code above comes out to here to repeat the main
         loop. */

    } /* End of main loop */

    ASSERT_NOT_REACHED();

#ifndef USE_COMPUTED_GOTO_FOR_MATCH_RECURSION

RRETURN_SWITCH:
    switch (stack.currentFrame->returnLocation) {
        case 0: goto RETURN;
        case 1: goto RRETURN_1;
        case 2: goto RRETURN_2;
        case 6: goto RRETURN_6;
        case 7: goto RRETURN_7;
        case 14: goto RRETURN_14;
        case 15: goto RRETURN_15;
        case 16: goto RRETURN_16;
        case 17: goto RRETURN_17;
        case 18: goto RRETURN_18;
        case 19: goto RRETURN_19;
        case 20: goto RRETURN_20;
        case 21: goto RRETURN_21;
        case 22: goto RRETURN_22;
        case 24: goto RRETURN_24;
        case 26: goto RRETURN_26;
        case 27: goto RRETURN_27;
        case 28: goto RRETURN_28;
        case 29: goto RRETURN_29;
        case 30: goto RRETURN_30;
        case 31: goto RRETURN_31;
        case 38: goto RRETURN_38;
        case 40: goto RRETURN_40;
        case 42: goto RRETURN_42;
        case 44: goto RRETURN_44;
        case 48: goto RRETURN_48;
        case 52: goto RRETURN_52;
    }

    ASSERT_NOT_REACHED();
    return matchError(JSRegExpErrorInternal, stack);

#endif

RETURN:
    return isMatch;
}


/*************************************************
*         Execute a Regular Expression           *
*************************************************/

/* This function applies a compiled re to a subject string and picks out
portions of the string if it matches. Two elements in the vector are set for
each substring: the offsets to the start and end of the substring.

Arguments:
  re              points to the compiled expression
  extra_data      points to extra data or is NULL
  subject         points to the subject string
  length          length of subject string (may contain binary zeros)
  start_offset    where to start in the subject string
  options         option bits
  offsets         points to a vector of ints to be filled in with offsets
  offsetcount     the number of elements in the vector

Returns:          > 0 => success; value is the number of elements filled in
                  = 0 => success, but offsets is not big enough
                   -1 => failed to match
                 < -1 => some kind of unexpected problem
*/

template <typename Char>
static void tryFirstByteOptimization(const Char*& subjectPtr, const Char* endSubject, int first_byte, bool first_byte_caseless, bool useMultiLineFirstCharOptimization, const Char* originalSubjectStart)
{
    // If first_byte is set, try scanning to the first instance of that byte
    // no need to try and match against any earlier part of the subject string.
    if (first_byte >= 0) {
        Char first_char = first_byte;
        if (first_byte_caseless)
            while (subjectPtr < endSubject) {
                int c = *subjectPtr;
                if (c > 127)
                    break;
                if (toLowerCase(c) == first_char)
                    break;
                subjectPtr++;
            }
        else {
            while (subjectPtr < endSubject && *subjectPtr != first_char)
                subjectPtr++;
        }
    } else if (useMultiLineFirstCharOptimization) {
        /* Or to just after \n for a multiline match if possible */
        // I'm not sure why this != originalSubjectStart check is necessary -- ecs 11/18/07
        if (subjectPtr > originalSubjectStart) {
            while (subjectPtr < endSubject && !isNewline(subjectPtr[-1]))
                subjectPtr++;
        }
    }
}

template <typename Char>
static bool tryRequiredByteOptimization(const Char*& subjectPtr, const Char* endSubject, int req_byte, int req_byte2, bool req_byte_caseless, bool hasFirstByte, const Char*& reqBytePtr)
{
    /* If req_byte is set, we know that that character must appear in the subject
     for the match to succeed. If the first character is set, req_byte must be
     later in the subject; otherwise the test starts at the match point. This
     optimization can save a huge amount of backtracking in patterns with nested
     unlimited repeats that aren't going to match. Writing separate code for
     cased/caseless versions makes it go faster, as does using an autoincrement
     and backing off on a match.

     HOWEVER: when the subject string is very, very long, searching to its end can
     take a long time, and give bad performance on quite ordinary patterns. This
     showed up when somebody was matching /^C/ on a 32-megabyte string... so we
     don't do this when the string is sufficiently long.
    */

    if (req_byte >= 0 && endSubject - subjectPtr < REQ_BYTE_MAX) {
        const Char* p = subjectPtr + (hasFirstByte ? 1 : 0);

        /* We don't need to repeat the search if we haven't yet reached the
         place we found it at last time. */

        if (p > reqBytePtr) {
            if (req_byte_caseless) {
                while (p < endSubject) {
                    int pp = *p++;
                    if (pp == req_byte || pp == req_byte2) {
                        p--;
                        break;
                    }
                }
            } else {
                while (p < endSubject) {
                    if (*p++ == req_byte) {
                        p--;
                        break;
                    }
                }
            }

            /* If we can't find the required character, break the matching loop */

            if (p >= endSubject)
                return true;

            /* If we have found the required character, save the point where we
             found it, so that we don't search again next time round the loop if
             the start hasn't passed this character yet. */

            reqBytePtr = p;
        }
    }
    return false;
}

template <typename Char>
int jsRegExpExecute(const JSRegExp* re,
                    const Char* subject, int length, int start_offset, int* offsets,
                    int offsetcount)
{
    ASSERT(re);
    ASSERT(subject);
    ASSERT(offsetcount >= 0);
    ASSERT(offsets || offsetcount == 0);

    MatchData<Char> matchBlock;
    matchBlock.startSubject = subject;
    matchBlock.endSubject = matchBlock.startSubject + length;
    const Char* endSubject = matchBlock.endSubject;

    matchBlock.multiline = (re->options & MatchAcrossMultipleLinesOption);
    matchBlock.ignoreCase = (re->options & IgnoreCaseOption);

    /* If the expression has got more back references than the offsets supplied can
     hold, we get a temporary chunk of working store to use during the matching.
     Otherwise, we can use the vector supplied, rounding down its size to a multiple
     of 3. */

    int ocount = offsetcount - (offsetcount % 3);

    // FIXME: This is lame that we have to second-guess our caller here.
    // The API should change to either fail-hard when we don't have enough offset space
    // or that we shouldn't ask our callers to pre-allocate in the first place.
    bool using_temporary_offsets = false;
    if (re->top_backref > 0 && re->top_backref >= ocount/3) {
        ocount = re->top_backref * 3 + 3;
        matchBlock.offsetVector = new int[ocount];
        if (!matchBlock.offsetVector)
            return JSRegExpErrorNoMemory;
        using_temporary_offsets = true;
    } else
        matchBlock.offsetVector = offsets;

    matchBlock.offsetEnd = ocount;
    matchBlock.offsetMax = (2*ocount)/3;
    matchBlock.offsetOverflow = false;

    /* Compute the minimum number of offsets that we need to reset each time. Doing
     this makes a huge difference to execution time when there aren't many brackets
     in the pattern. */

    int resetcount = 2 + re->top_bracket * 2;
    if (resetcount > offsetcount)
        resetcount = ocount;

    /* Reset the working variable associated with each extraction. These should
     never be used unless previously set, but they get saved and restored, and so we
     initialize them to avoid reading uninitialized locations. */

    if (matchBlock.offsetVector) {
        int* iptr = matchBlock.offsetVector + ocount;
        int* iend = iptr - resetcount/2 + 1;
        while (--iptr >= iend)
            *iptr = -1;
    }

    /* Set up the first character to match, if available. The first_byte value is
     never set for an anchored regular expression, but the anchoring may be forced
     at run time, so we have to test for anchoring. The first char may be unset for
     an unanchored pattern, of course. If there's no first char and the pattern was
     studied, there may be a bitmap of possible first characters. */

    bool first_byte_caseless = false;
    int first_byte = -1;
    if (re->options & UseFirstByteOptimizationOption) {
        first_byte = re->first_byte & 255;
        if ((first_byte_caseless = (re->first_byte & REQ_IGNORE_CASE)))
            first_byte = toLowerCase(first_byte);
    }

    /* For anchored or unanchored matches, there may be a "last known required
     character" set. */

    bool req_byte_caseless = false;
    int req_byte = -1;
    int req_byte2 = -1;
    if (re->options & UseRequiredByteOptimizationOption) {
        req_byte = re->req_byte & 255; // FIXME: This optimization could be made to work for UTF16 chars as well...
        req_byte_caseless = (re->req_byte & REQ_IGNORE_CASE);
        req_byte2 = flipCase(req_byte);
    }

    /* Loop for handling unanchored repeated matching attempts; for anchored regexs
     the loop runs just once. */

    const Char* startMatch = subject + start_offset;
    const Char* reqBytePtr = startMatch - 1;
    bool useMultiLineFirstCharOptimization = re->options & UseMultiLineFirstByteOptimizationOption;

    do {
        /* Reset the maximum number of extractions we might see. */
        if (matchBlock.offsetVector) {
            int* iptr = matchBlock.offsetVector;
            int* iend = iptr + resetcount;
            while (iptr < iend)
                *iptr++ = -1;
        }

        tryFirstByteOptimization(startMatch, endSubject, first_byte, first_byte_caseless, useMultiLineFirstCharOptimization, matchBlock.startSubject + start_offset);
        if (tryRequiredByteOptimization(startMatch, endSubject, req_byte, req_byte2, req_byte_caseless, first_byte >= 0, reqBytePtr))
            break;

        /* When a match occurs, substrings will be set for all internal extractions;
         we just need to set up the whole thing as substring 0 before returning. If
         there were too many extractions, set the return code to zero. In the case
         where we had to get some local store to hold offsets for backreferences, copy
         those back references that we can. In this case there need not be overflow
         if certain parts of the pattern were not used. */

        /* The code starts after the JSRegExp block and the capture name table. */
        const unsigned char* start_code = (const unsigned char*)(re + 1);

        int returnCode = match<Char>(startMatch, start_code, 2, matchBlock);

        /* When the result is no match, advance the pointer to the next character
         and continue. */
        if (returnCode == 0) {
            startMatch++;
            continue;
        }

        if (returnCode != 1) {
            ASSERT(returnCode == JSRegExpErrorHitLimit || returnCode == JSRegExpErrorNoMemory);
            DPRINTF((">>>> error: returning %d\n", returnCode));
            return returnCode;
        }

        /* We have a match! Copy the offset information from temporary store if
         necessary */

        if (using_temporary_offsets) {
            if (offsetcount >= 4) {
                memcpy(offsets + 2, matchBlock.offsetVector + 2, (offsetcount - 2) * sizeof(int));
                DPRINTF(("Copied offsets from temporary memory\n"));
            }
            if (matchBlock.endOffsetTop > offsetcount)
                matchBlock.offsetOverflow = true;

            DPRINTF(("Freeing temporary memory\n"));
            delete [] matchBlock.offsetVector;
        }

        returnCode = matchBlock.offsetOverflow ? 0 : matchBlock.endOffsetTop / 2;

        if (offsetcount < 2)
            returnCode = 0;
        else {
            offsets[0] = startMatch - matchBlock.startSubject;
            offsets[1] = matchBlock.endMatchPtr - matchBlock.startSubject;
        }

        DPRINTF((">>>> returning %d\n", returnCode));
        return returnCode;
    } while (!(re->options & IsAnchoredOption) && startMatch <= endSubject);

    if (using_temporary_offsets) {
        DPRINTF(("Freeing temporary memory\n"));
        delete [] matchBlock.offsetVector;
    }

    DPRINTF((">>>> returning PCRE_ERROR_NOMATCH\n"));
    return JSRegExpErrorNoMatch;
}

template
int jsRegExpExecute<unsigned short>(const JSRegExp* re,
                                    const unsigned short* subject,
                                    int length, int start_offset,
                                    int* offsets, int offsetcount);

template
int jsRegExpExecute<char>(const JSRegExp* re,
                          const char* subject,
                          int length, int start_offset,
                          int* offsets, int offsetcount);
