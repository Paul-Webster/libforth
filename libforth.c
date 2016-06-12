/** @file       libforth.c
 *  @brief      A FORTH library, written in a literate style
 *  @author     Richard James Howe.
 *  @copyright  Copyright 2015,2016 Richard James Howe.
 *  @license    LGPL v2.1 or later version
 *  @email      howe.r.j.89@gmail.com 
 *  @todo add a system for adding arbitrary C functions to the system via
 *  plugins 
 *  @todo Add file access utilities
 *  @todo Turn this file into a literate-style document, as in Jonesforth
 *  @todo Change license?
 *
 *  This file implements the core Forth interpreter, it is written in portable
 *  C99. The file contains a virtual machine that can interpret threaded Forth
 *  code and a simple compiler for the virtual machine, which is one of its
 *  instructions. The interpreter can be embedded in another application and
 *  there should be no problem instantiating multiple instances of the
 *  interpreter.
 *
 *  For more information about Forth see:
 *
 *  - https://en.wikipedia.org/wiki/Forth_%28programming_language%29
 *  - Thinking Forth by Leo Brodie
 *  - Starting Forth by Leo Brodie
 *
 *  The antecedent of this interpreter:
 *  - http://www.ioccc.org/1992/buzzard.2.c
 *
 *  cxxforth, a literate Forth written in C++
 *  - https://github.com/kristopherjohnson/cxxforth
 *
 *  Jones Forth, a literate Forth written in x86 assembly:
 *  - https://rwmj.wordpress.com/2010/08/07/jonesforth-git-repository/
 *  - https://github.com/AlexandreAbreu/jonesforth (backup)
 *
 *  A Forth processor:
 *  - http://www.excamera.com/sphinx/fpga-j1.html
 *  And my Forth processor based on this one:
 *  - https://github.com/howerj/fyp
 *
 *  The repository should also contain:
 *  
 *  - "readme.md"  : a Forth manual, and generic project information
 *  - "forth.fth"  : basic Forth routines and startup code
 *  - "libforth.h" : The header contains the API documentation
 *
 *  The structure of this file is as follows:
 *
 *  1) Headers and configuration macros
 *  2) Enumerations and constants
 *  3) Helping functions for the compiler
 *  4) API related functions and Initialization code
 *  5) The Forth virtual machine itself
 *  6) An example main function called main_forth and support
 *  functions
 *
 *  Each section will be explained in detail as it is encountered.
 *
 *  An attempt has been made to make this document flow, as both a source 
 *  code document and as a description of how the forth kernel works. 
 *  This is helped by the fact that the program is quite small and compact 
 *  without being written in obfuscated C. It is, however, compact, and can be
 *  quite difficult to understand regardless of code quality. There are a
 *  number of behaviors programmers from a C background will not be familiar
 *  with.
 *
 *  @todo Given an overview of Forth execution, and different forth concepts,
 *  such as threaded code, the dictionary, RPN, 
 *  @todo Talk about how Forth programs achieve compactness with implicit
 *  behavior (like AWK and Perl do), by taking their input from a default
 *  source and by how it does parameter passing
 *
 *  Glossary of Terms:
 *
 *  VM             - Virtual Machine
 *  Cell           - The Virtual Machines natural Word Size, on a 32 bit
 *                 machine the Cell will be 32 bits wide
 *  Word           - In Forth a Word refers to a function, and not the
 *                 usual meaning of an integer that is the same size as
 *                 the machines underlying word size, this can cause confusion
 *  API            - Application Program Interface
 *  interpreter    - as in byte code interpreter, largely synonymous with virtual
 *                 machine as is used here
 *  REPL           - Read-Evaluate-Print-Loop, this Forth actually provides
 *                 something more like a "REL", or Read-Evaluate-Loop (as printing
 *                 has to be done explicitly), but the interpreter is interactive 
 *                 which is the important point
 *  RPN            - Reverse Polish Notation (see 
 *                   https://en.wikipedia.org/wiki/Reverse_Polish_notation)
 *  The stack      - Forth implementations have at least two stacks, one for
 *                 storing variables and another for control flow and temporary
 *                 variables, when the term "stack" is used on its own and with
 *                 no other context it refers to the "variable stack" and not
 *                 the "return stack". This "variable stack" is used for
 *                 passing parameters into and return values to functions.
 *  Return stack   - Most programming languages have a call stack, C has one
 *                 but not one that the programmer can directly access, in
 *                 Forth manipulating the return stack is often used. 
 **/

/* ============================ Section 1 ================================== */
/*                     Headers and configurations macros                     */

/* This file implements a Forth library, so a Forth interpreter can be embedded
 * in another application, as such many of the functions in this file are
 * exported, and are documented in the "libforth.h" header */
#include "libforth.h" 

/* We try to make good use of the C library as even microcontrollers
 * have enough space for a reasonable implementation of it, although
 * it might require some setup. The only time allocations are explicitly
 * done is when the virtual machine image is initialized, after this
 * the VM does not allocate any more memory. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <time.h>

/* Traditionally Forth implementations were the only program running on
 * the (micro)computer, running on processors orders of magnitude slower
 * than this one, as such checks to make sure memory access was in bounds
 * did not make sense and the implementation had to have access to the
 * entire machines limited memory.
 *
 * To aide debugging and to help ensure correctness the "ck" macro, a wrapper
 * around the function "check_bounds", is called for most memory accesses
 * that the virtual machine makes. */
#ifndef NDEBUG
#define ck(c) check_bounds(o, c, __LINE__)
#else
#define ck(c) (c) /*disables checks and debug mode*/
#endif

#define DEFAULT_CORE_SIZE   (32 * 1024) /**< default VM size*/

/* Blocks will be encountered and explained later, they have a fixed size which
 * has been standardized to 1024 */
#define BLOCK_SIZE          (1024u) /**< size of forth block in bytes */

/* When we are reading input to be parsed we need a space to hold that
 * input, the offset to this area is into a field called "m" in "struct forth", 
 * defined later, the offset is a multiple of cells and not chars.  */
#define STRING_OFFSET       (32u)   /**< offset into memory of string buffer*/

#define MAX_WORD_LENGTH     (32u)   /**< max word length, must be < 255 */

#define DICTIONARY_START    (STRING_OFFSET + MAX_WORD_LENGTH) /**< start of dic */

/* Later we will encounter a field called MISC, a field in every Word
 * definition and is always present in the Words header. This field contains
 * multiple values at different bit offsets, only the lower 16 bits of this
 * cell are ever used. */

#define WORD_LENGTH_OFFSET  (8)  /**< bit offset for word length start*/
#define WORD_LENGTH(FIELD1) (((FIELD1) >> WORD_LENGTH_OFFSET) & 0xff)
#define WORD_HIDDEN(FIELD1) ((FIELD1) & 0x80) /**< is a forth word hidden? */

#define INSTRUCTION_MASK    (0x7f)

#define instruction(k)      ((k) & INSTRUCTION_MASK)

#define VERIFY(X)           do { if(!(X)) { abort(); } } while(0)

/* The IS_BIG_ENDIAN macro looks complicated, however all it does is determine
 * the endianess of the machine using trickery.
 *
 * See:
 * - https://stackoverflow.com/questions/2100331/c-macro-definition-to-determine-big-endian-or-little-endian-machine
 * - https://en.wikipedia.org/wiki/Endianness 
 */
#define IS_BIG_ENDIAN       (!(union { uint16_t u16; unsigned char c; }){ .u16 = 1 }.c)

/* When designing a binary format, which this interpreter uses and saves to
 * disk, it is imperative that certain information is saved to disk - one of
 * those pieces of information is the version of the interpreter. Something
 * such as this may seem trivial, but only once you start to deploy
 * applications to different machines and to different users does it become
 * apparent how important this is. */
#define CORE_VERSION        (0x02u) /**< version of the forth core file */

/* ============================ Section 2 ================================== */
/*                     Enumerations and Constants                            */

/**
 * This following string is a forth program that gets called when creating a
 * new Forth environment, it is not actually the very first program that gets
 * run, but it is run before the user gets a chance to do anything. 
 *
 * The program is kept as small as possible, but is dependent on the virtual
 * machine image being set up correctly with a few other words being defined
 * first, they will be described as they are encountered. Suffice to say,
 * before this program is executed the following happens:
 *   1) The virtual machine image is initialized
 *   2) All the virtual machine primitives are defined
 *   3) All registers are named and some constants defined
 *   4) ";" is defined
 *
 * Of note, words such as "if", "else", "then", and even comments - "(" -, are
 * not actually Forth primitives, there are defined in terms of other Forth
 * words.
 *
 * @todo Explain Forth word evaluation here
 * @todo Explain as many definitions as possible
 *
 **/
static const char *initial_forth_program = "                       \n\
: here h @ ;                                                       \n\
: [ immediate 0 state ! ;                                          \n\
: ] 1 state ! ;                                                    \n\
: >mark here 0 , ;                                                 \n\
: :noname immediate -1 , here 2 , ] ;                              \n\
: if immediate ' ?branch , >mark ;                                 \n\
: else immediate ' branch , >mark swap dup here swap - swap ! ;    \n\
: then immediate dup here swap - swap ! ;                          \n\
: 2dup over over ;                                                 \n\
: begin immediate here ;                                           \n\
: until immediate ' ?branch , here - , ;                           \n\
: '\\n' 10 ;                                                       \n\
: ')' 41 ;                                                         \n\
: cr '\\n' emit ;                                                  \n\
: ( immediate begin key ')' = until ; ( We can now use comments! ) \n\
: rot >r swap r> swap ;                                            \n\
: -rot rot rot ;                                                   \n\
: tuck swap over ;                                                 \n\
: nip swap drop ;                                                  \n\
: :: [ find : , ] ;                                                \n\
: allot here + h ! ; ";

/* We can serialize the Forth virtual machine image, saving it to disk so we
 * can load it again later. When saving the image to disk it is important
 * to be able to identify the file somehow, and to identify various
 * properties of the image.
 *
 * Unfortunately each image is not portable to machines with different
 * cell sizes (determined by "sizeof(forth_cell_t)") and different endianess,
 * and it is not trivial to convert them due to implementation details.
 *
 * "enum header" names all of the different fields in the header.
 *
 * The first four fields (MAGIC0...MAGIC3) are magic numbers which identify 
 * the file format, so utilities like "file" on Unix systems can differentiate
 * binary formats from each other.
 *
 * CELL_SIZE is the size of the virtual machine cell used to create the image.
 *
 * VERSION is used to both represent the version of the Forth interpreter and
 * the version of the file format.
 *
 * ENDIAN is the endianess of the VM
 *
 * MAGIC7 is the last magic number.
 *
 * When loading the image the magic numbers are checked as well as
 * compatibility between the saved image and the compiled Forth interpreter. */
enum header { 
	MAGIC0, 
	MAGIC1, 
	MAGIC2, 
	MAGIC3, 
	CELL_SIZE, 
	VERSION, 
	ENDIAN, 
	MAGIC7 
};

/* The header itself, this will be copied into the forth_t structure on
 * initialization, the ENDIAN field is filled in then as it seems impossible
 * to determine the endianess of the target at compile time. */
static const uint8_t header[MAGIC7+1] = {
	[MAGIC0]    = 0xFF,
	[MAGIC1]    = '4',
	[MAGIC2]    = 'T',
	[MAGIC3]    = 'H',
	[CELL_SIZE] = sizeof(forth_cell_t), 
	[VERSION]   = CORE_VERSION,
	[ENDIAN]    = -1,       
	[MAGIC7]    = 0xFF
};

/* This is the main structure used by the virtual machine
 * 
 * The structure is defined here and not in the header to hide the
 * implementation details it, all API functions are passed an opaque pointer to
 * the structure (see https://en.wikipedia.org/wiki/Opaque_pointer).
 *
 * Only three fields are serialized to the file saved to disk:
 *
 *   1) header
 *   2) core_size
 *   3) m
 *
 * And they are done so in that order, "core_size" and "m" are save in whatever
 * endianess the machine doing the saving is done in, however "core_size" is
 * converted to a "uint64_t" before being save to disk so it is not of a
 * variable size. "m" is a flexible array member "core_size" number of members.
 *
 * The "m" field is the virtual machines working memory, it has its own
 * internal structure which includes registers, stacks and a dictionary of
 * defined words.
 *
 * @todo Explain more about the structure of "m"
 */
struct forth { /**< FORTH environment, values marked '~~' are serialized in order*/
	uint8_t header[sizeof(header)]; /**< ~~ header for reloadable core file */
	forth_cell_t core_size;  /**< ~~ size of VM (converted to uint64_t for serialization)*/
	jmp_buf *on_error;   /**< place to jump to on error */
	uint8_t *s;          /**< convenience pointer for string input buffer */
	char hex_fmt[16];    /**< calculated hex format*/
	forth_cell_t *S;     /**< stack pointer */
	forth_cell_t m[];    /**< ~~ Forth Virtual Machine memory */
};

/* There are a number of registers available to the virtual machine, they are
 * actually indexes into the virtual machines main memory, put there so it can
 * access them. 
 *
 * @todo explain more registers
 * */
enum registers {          /**< virtual machine registers */
	DIC         =  6, /**< dictionary pointer */
	RSTK        =  7, /**< return stack pointer */
	STATE       =  8, /**< interpreter state; compile or command mode*/
	BASE        =  9, /**< base conversion variable */
	PWD         = 10, /**< pointer to previous word */
	SOURCE_ID   = 11, /**< input source selector */
	SIN         = 12, /**< string input pointer*/
	SIDX        = 13, /**< string input index*/
	SLEN        = 14, /**< string input length*/ 
	START_ADDR  = 15, /**< pointer to start of VM */
	FIN         = 16, /**< file input pointer */
	FOUT        = 17, /**< file output pointer */
	STDIN       = 18, /**< file pointer to stdin */
	STDOUT      = 19, /**< file pointer to stdout */
	STDERR      = 20, /**< file pointer to stderr */
	ARGC        = 21, /**< argument count */
	ARGV        = 22, /**< arguments */
	DEBUG       = 23, /**< turn debugging on/off if enabled*/
	INVALID     = 24, /**< if non zero, this interpreter is invalid */
	TOP         = 25, /**< *stored* version of top of stack */
	INSTRUCTION = 26, /**< *stored* version of instruction pointer*/
	STACK_SIZE  = 27, /**< size of the stacks */
	START_TIME  = 28, /**< start time in milliseconds */
};

/** @brief enum input_stream contains the possible value of the SOURCE_ID
 *  register
 *
 *  Input in Forth systems traditionally (tradition is a word we will keep using
 *  here, generally in the context of programming it means justification for
 *  cruft) came from either one of two places, the keyboard that the programmer
 *  was typing at, interactively, or from some kind of non volatile store, such
 *  as a floppy disk. Our C program has no (easy and portable) way of
 *  interacting directly with the keyboard, instead it could interact with a
 *  file handle such as stdin, or read from a string. This is what we do in
 *  this interpreter.
 *
 *  A word in Forth called "SOURCE-ID" can be used to query what the input
 *  device currently is, the values expected are zero for interactive
 *  interpretation, or minus one (minus one, or all bits set, is used to
 *  represent truth conditions in most Forths, we are a bit more liberal in our
 *  definition of true) for string input. These are the possible values that
 *  the SOURCE_ID register can take. 
 *
 *  Note that the meaning is slightly different in our Forth to what is meant 
 *  traditionally, just because this program is taking input from stdin (or
 *  possibly another file handle), does not mean that this program is being 
 *  run interactively, it could possibly be part of a Unix pipe. As such this 
 *  interpreter defaults to being as silent as possible.
 */
enum input_stream { 
	FILE_IN, 
	STRING_IN = -1 
};

/* Instead of using numbers to refer to registers, it is better to refer to
 * them by name instead, these strings each correspond in turn to enumeration
 * called "registers" */
static const char *register_names[] = { "h", "r", "`state", "base", "pwd",
"`source-id", "`sin", "`sidx", "`slen", "`start-address", "`fin", "`fout", "`stdin",
"`stdout", "`stderr", "`argc", "`argv", "`debug", "`invalid", "`top", "`instruction",
"`stack-size", "`start-time", NULL };

/** @brief enum for all virtual machine instructions
 *
 * "enum instructions" contains each virtual machine instruction, a valid
 * instruction is less than LAST. One of the core ideas of Forth is that
 * given a small set of primitives it is possible to build up a high level
 * language, given only these primitives it is possible to add conditional
 * statements, case statements, arrays and strings, even though they do not
 * exist as instructions here.
 *
 * Most of these instructions are quite simple (such as; pop two items off the 
 * variable stack, add them and push the result for ADD) however others are a 
 * great deal more complex and will take a few paragraphs to explain fully
 * (such as READ, or how IMMEDIATE interacts with the virtual machines
 * execution). */
enum instructions { PUSH,COMPILE,RUN,DEFINE,IMMEDIATE,READ,LOAD,STORE,
SUB,ADD,AND,OR,XOR,INV,SHL,SHR,MUL,DIV,LESS,MORE,EXIT,EMIT,KEY,FROMR,TOR,BRANCH,
QBRANCH, PNUM, QUOTE,COMMA,EQUAL,SWAP,DUP,DROP,OVER,TAIL,BSAVE,BLOAD,FIND,PRINT,
DEPTH,CLOCK,LAST }; 

/** @brief names of all named instructions, with a few exceptions
 *
 * So that we can compile programs we need ways of referring to the basic
 * programming constructs provided by the virtual machine, theses words are fed
 * into the C function "compile" in a process described later. They do not name
 * all virtual machine instructions, PUSH, COMPILE, RUN, DEFINE, IMMEDIATE and
 * LAST are missing (the first five virtual machine instructions and the very
 * last pseudo instruction).
 *
 * PUSH, COMPILE, and RUN are all "invisible words", they are instructions that
 * do not have a name as they are not needed directly but are used by the
 * execution environment internally.
 *
 * DEFINE and IMMEDIATE are both immediate words, they will have to be compiled
 * separately from the rest of the instructions.
 *
 * LAST is not an instruction, but only a marker of the last enumeration used
 * in "enum instructions", so it does not get a name.
 * 
 */
static const char *instruction_names[] = { "read","@","!","-","+","and","or",
"xor","invert","lshift","rshift","*","/","u<","u>","exit","emit","key","r>",
">r","branch","?branch", "pnum","'", ",","=", "swap","dup","drop", "over", "tail",
"bsave","bload", "find", "print","depth","clock", NULL }; 

/* ============================ Section 3 ================================== */
/*                  Helping Functions For The Compiler                       */

/** @brief  get a char from string input or a file 
 *  @param  o   forth image containing information about current input stream
 *  @return int same value as fgetc or getchar
 *
 *  This Forth interpreter only has a few mechanisms for I/O, one of these is
 *  to fetch an individual character of input from either a string or a file
 *  which can be set either with knowledge of the implementation from within
 *  the virtual machine, or via the API presented to the programmer. The C
 *  functions "forth_init", "forth_set_file_input" and 
 *  "forth_set_string_input" set up and manipulate the input of the
 *  interpreter. These functions act on the following registers:
 *
 *  SOURCE_ID - The current input source (SIN or FIN)
 *  SIN       - String INput
 *  SIDX      - String InDeX
 *  SLEN      - String LENgth
 *  FIN       - File   INput
 *
 *  Note that either SIN or FIN might not both be valid, one will be but the
 *  other might not, this makes manipulating these values hazardous. The input
 *  functions "forth_get_char" and "forth_get_word" both take their input
 *  streams implicitly via the registers contained within the Forth execution
 *  environment passed in to those functions.
 */
static int forth_get_char(forth_t *o) 
{ 
	switch(o->m[SOURCE_ID]) {
	case FILE_IN:   return fgetc((FILE*)(o->m[FIN]));
	case STRING_IN: return o->m[SIDX] >= o->m[SLEN] ? EOF : ((char*)(o->m[SIN]))[o->m[SIDX]++];
	default:        return EOF;
	}
} 

/* This function reads in a space delimited word, limited to MAX_WORD_LENGTH,
 * the word is put into the pointer "*p", due to the simple nature of Forth
 * this is as complex as parsing and lexing gets. It can either read from
 * a file handle or a string, like forth_get_char() */
static int forth_get_word(forth_t *o, uint8_t *p) 
{ /*get a word (space delimited, up to 31 chars) from a FILE* or string-in*/ 
	int n = 0;
	char fmt[16] = { 0 };
	sprintf(fmt, "%%%ds%%n", MAX_WORD_LENGTH - 1);
	switch(o->m[SOURCE_ID]) {
	case FILE_IN:   return fscanf((FILE*)(o->m[FIN]), fmt, p, &n);
	case STRING_IN:
		if(sscanf((char *)&(((char*)(o->m[SIN]))[o->m[SIDX]]), fmt, p, &n) < 0)
			return EOF;
		return o->m[SIDX] += n, n;
	default:       return EOF;
	}
} 

/** @brief compile a Forth word header into the dictionary
 *  @param o    Forth environment to do the compilation in
 *  @param code virtual machine instruction for that word
 *  @param str  name of Forth word
 *
 *  The function "compile" is not that complicated in itself, however it
 *  requires an understanding of the structure of a Forth word definition and
 *  the behavior of the Forth run time.
 *
 *  In all Forth implementations there exists a concept of "the dictionary",
 *  although they may be implemented in different ways the usual way is as a
 *  linked list of words, starting with the latest defined word and ending with
 *  a special terminating value. Words cannot be arbitrarily deleted, and
 *  operation is largely append only. Each word or Forth function that has been
 *  defined can be looked up in this dictionary, and dependent on whether it is
 *  an immediate word or a compiling word, and whether we are in command or
 *  compile mode different actions are taken when we have found the word we are
 *  looking for in our Read-Evaluate-Loop.
 *
 *  | <-- Start of VM memory
 *  |                 | <-- Start of dictionary  
 *                    |               
 *  .------------.    |  .------.      .------.             .-------------.
 *  | Terminator | <---- | Word | <--- | Word | < -- ... -- | Latest Word |
 *  .------------.    |  .------.      .------.             .-------------.
 *                    |                                          ^
 *                    |                                          |
 *                    |                                      PWD Register
 *  
 *  The PWD registers points to the latest defined word, a search starts from
 *  here and works it way backwards (allowing us replace old definitions by
 *  appending new ones with the same name only), the terminator
 *
 *  Our word header looks like this:
 *
 *  .-----------.-----.------.--------.------------.
 *  | Word Name | PWD | MISC | CODE-2 | Data Field |
 *  .-----------.-----.------.--------.------------.
 *
 *   - "CODE-2" and the "Data Field" are optional and the "Data Field" is of
 *   variable length.
 *   - "Word Name" is a variable length field whose size is recorded in the
 *     MISC field.
 *
 *   And the MISC field is a composite to save space containing a virtual
 *   machine instruction, the hidden bit and the length of the Word Name string
 *   as an offset in cells from PWD field. The field looks like this:
 *
 *   -----.-------------------.------------.-------------.  
 *    ... | 16 ........... 8  |    9       | 7 ....... 0 | 
 *    ... |  Word Name Size   | Hidden Bit | Instruction |
 *   -----.-------------------.------------.-------------.
 * 
 *   The maximum value for the Word Name field is determined by the Word Name
 *   Size field and a few other constants in the interpreter.
 *
 *   The hidden bit is not used in the "compile" function, but is used
 *   elsewhere to hide a word definition from the word search.
 *
 *   The "Instruction" tells the interpreter what to do with the Word
 *   definition when it is found and how to interpret "CODE-2" and the 
 *   "Data Field" if they exist.
 *       
 *   @todo More explanation, hidden words, ...                   
 */
static void compile(forth_t *o, forth_cell_t code, const char *str) 
{ /* create a new forth word header */
	assert(o && code < LAST);
	forth_cell_t *m = o->m, header = m[DIC], l = 0;
	/*FORTH header structure*/
	strcpy((char *)(o->m + header), str); /* 0: Copy the new FORTH word into the new header */
	l = strlen(str) + 1;
	l = (l + (sizeof(forth_cell_t) - 1)) & ~(sizeof(forth_cell_t) - 1); /* align up to sizeof word */
	l = l/sizeof(forth_cell_t);
	m[DIC] += l; /* Add string length in words to header (STRLEN) */

	m[m[DIC]++] = m[PWD];     /*0 + STRLEN: Pointer to previous words header*/
	m[PWD] = m[DIC] - 1;      /*   Update the PWD register to new word */
	m[m[DIC]++] = (l << WORD_LENGTH_OFFSET) | code; /*1: size of words name and code field */
}

/** @brief implement the Forth block I/O mechanism
 *  @param o       virtual machine image to do the block I/O in
 *  @param poffset offset into o->m field to load or save
 *  @param id      Identification of block to read or write
 *  @param rw      Mode of operation 'r' == read, 'w' == write
 *  @return negative number on failure, zero on success
 *
 * Forth traditionally uses blocks as its method of storing data and code to
 * disk, each block is BLOCK_SIZE characters long (which should be 1024
 * characters). The reason for such a simple method is that many early Forth
 * systems ran on microcomputers which did not have an operating system as
 * they are now known, but only a simple monitor program and a programming 
 * language, as such there was no file system either. Each block was loaded 
 * from disk and then evaluated.
 *
 * The "blockio" function implements this simple type of interface, and can
 * load and save blocks to disk. 
 */
static int blockio(forth_t *o, forth_cell_t poffset, forth_cell_t id, char rw) 
{ /* simple block I/O, could be replaced with making fopen/fclose available to interpreter */
	char name[16] = {0}; /* XXXX + ".blk" + '\0' + a little spare change */
	FILE *file = NULL;
	size_t n;
	if(((forth_cell_t)poffset) > ((o->core_size * sizeof(forth_cell_t)) - BLOCK_SIZE))
		return -1;
	sprintf(name, "%04x.blk", (int)id);
	if(!(file = fopen(name, rw == 'r' ? "rb" : "wb"))) { /**@todo loading should always succeed */
		fprintf(stderr, "( error 'file-open \"%s : could not open file\" )\n", name);
		return -1;
	}
	n = rw == 'w' ? fwrite(((char*)o->m) + poffset, 1, BLOCK_SIZE, file):
			fread (((char*)o->m) + poffset, 1, BLOCK_SIZE, file);
	fclose(file);
	return n == BLOCK_SIZE ? 0 : -1;
} 

/**@brief turn a string into a number using a base and return an error code to
 * indicate success or failure, the results of the conversion are stored in n,
 * even if the conversion failed.
 * @param  base base to convert string from, valid values are 0, and 2-26
 * @param  n    out parameter, the result of the conversion is stored here
 * @param  s    string to convert
 * @return int return code indicating failure or success */
static int numberify(int base, forth_cell_t *n, const char *s)  
{ /*returns non zero if conversion was successful*/
	char *end = NULL;
	errno = 0;
	*n = strtol(s, &end, base);
	return !errno && *s != '\0' && *end == '\0';
}

/** @brief case insensitive string comparison
 *  @param  a   first string to compare
 *  @param  b   second string
 *  @return int zero if both strings are the same, non zero otherwise (same as
 *  strcmp, only insensitive to case)
 *
 *  Forths are usually case insensitive and are required to be (or at least
 *  accept only uppercase characters only) by many of the standards for Forth.
 *  As an aside I do not believe case insensitivity is a good idea as it
 *  complicates interfaces and creates as much confusion as it tries to solve
 *  (not only that different case letters do convey information). However,
 *  in keeping with other implementations, this Forth is also made insensitive
 *  to case "DUP" is treated the same as "dup" and "Dup".
 *
 *  This comparison function, istrcmp, is only used in one place however, in
 *  the C function "forth_find", replacing it with "strcmp" will bring back the
 *  more logical, case sensitive, behavior.
 */
static int istrcmp(const uint8_t *a, const uint8_t *b)
{ /* case insensitive string comparison */
	for(; ((*a == *b) || (tolower(*a) == tolower(*b))) && *a && *b; a++, b++)
		;
	return tolower(*a) - tolower(*b);
}

/* "forth_find" finds a word in the dictionary and if it exists it returns a
 * pointer to its PWD field. If it is not found it will return zero, also of
 * notes is the fact that it will skip words that are hidden, that is the
 * hidden bit in the MISC field of a word is set. The structure of the
 * dictionary has already been explained, so there should be no surprises in
 * this word. */
forth_cell_t forth_find(forth_t *o, const char *s) 
{ /* find a word in the Forth dictionary, which is a linked list, skipping hidden words */
	forth_cell_t *m = o->m, w = m[PWD], len = WORD_LENGTH(m[w+1]);
	for (;w > DICTIONARY_START && (WORD_HIDDEN(m[w+1]) || istrcmp((uint8_t*)s,(uint8_t*)(&o->m[w - len])));) {
		w = m[w]; 
		len = WORD_LENGTH(m[w+1]);
	}
	return w > DICTIONARY_START ? w+1 : 0;
}

/**@brief  print out a forth cell as a number, the output base being determined
 *         by the BASE registers
 * @param  o     an initialized forth environment (contains BASE register and
 *               output streams)
 * @param  f     value to print out
 * @return int   same return value as fprintf
 * @todo   print out bases other than 10 and 16 */
static int print_cell(forth_t *o, forth_cell_t f)
{ /**@todo print out bases other than 10 and 16 */ 
	char *fmt = o->m[BASE] == 16 ? o->hex_fmt : "%" PRIuCell;
	return fprintf((FILE*)(o->m[FOUT]), fmt, f);
}

/* ============================ Section 4 ================================== */
/*              API related functions and Initialization code                */

void forth_set_file_input(forth_t *o, FILE *in) 
{
	assert(o && in); 
	o->m[SOURCE_ID] = FILE_IN;
	o->m[FIN]       = (forth_cell_t)in; 
}

void forth_set_file_output(forth_t *o, FILE *out) 
{  
	assert(o && out); 
	o->m[FOUT] = (forth_cell_t)out; 
}

void forth_set_string_input(forth_t *o, const char *s) 
{ 
	assert(o && s);
	o->m[SIDX] = 0;              /*m[SIDX] == current character in string*/
	o->m[SLEN] = strlen(s) + 1;  /*m[SLEN] == string len*/
	o->m[SOURCE_ID] = STRING_IN; /*read from string, not a file handle*/
	o->m[SIN] = (forth_cell_t)s; /*sin  == pointer to string input*/
}

int forth_eval(forth_t *o, const char *s) 
{ 
	assert(o && s); 
	forth_set_string_input(o, s); 
	return forth_run(o);
}

int forth_define_constant(forth_t *o, const char *name, forth_cell_t c)
{
	char e[MAX_WORD_LENGTH+32] = {0};
	assert(o && strlen(name) < MAX_WORD_LENGTH);
	sprintf(e, ": %31s %" PRIuCell " ; \n", name, c);
	return forth_eval(o, e);
}

static void forth_make_default(forth_t *o, size_t size, FILE *in, FILE *out)
{ /* set defaults for a forth structure for initialization or reload */
	o->core_size     = size;
	o->m[STACK_SIZE] = size / 64 > 64 ? size / 64 : 64;
	o->s             = (uint8_t*)(o->m + STRING_OFFSET); /*string store offset into CORE, skip registers*/
	o->m[FOUT]       = (forth_cell_t)out;
	o->m[START_ADDR] = (forth_cell_t)&(o->m);
	o->m[STDIN]      = (forth_cell_t)stdin;
	o->m[STDOUT]     = (forth_cell_t)stdout;
	o->m[STDERR]     = (forth_cell_t)stderr;
	o->m[RSTK]       = size - o->m[STACK_SIZE];     /*set up return stk pointer*/
	o->m[START_TIME] = (1000 * clock()) / CLOCKS_PER_SEC;
	o->m[ARGC] = o->m[ARGV] = 0;
	o->S             = o->m + size - (2 * o->m[STACK_SIZE]); /*set up variable stk pointer*/
	sprintf(o->hex_fmt, "0x%%0%d"PRIxCell, (int)(sizeof(forth_cell_t)*2));
	o->on_error      = calloc(sizeof(jmp_buf), 1);
	forth_set_file_input(o, in);  /*set up input after our eval*/
}

static void make_header(uint8_t *dst)
{
	memcpy(dst, header, sizeof(header));
	dst[ENDIAN] = !IS_BIG_ENDIAN; /*fill in endianess*/
}

forth_t *forth_init(size_t size, FILE *in, FILE *out) 
{ 
	assert(in && out);
	forth_cell_t *m, i, w, t;
	forth_t *o;
	VERIFY(size >= MINIMUM_CORE_SIZE);
	if(!(o = calloc(1, sizeof(*o) + sizeof(forth_cell_t)*size))) 
		return NULL;
	forth_make_default(o, size, in, out);
	make_header(o->header);
	m = o->m;       /*a local variable only for convenience*/

	/* The next section creates a word that calls READ, then TAIL, then itself*/
	o->m[PWD]   = 0;  /*special terminating pwd value*/
	t = m[DIC] = DICTIONARY_START; /*initial dictionary offset, skip registers and string offset*/
	m[m[DIC]++] = TAIL; /*Add a TAIL instruction that can be called*/
	w = m[DIC];         /*Save current offset, it will contain the READ instruction */
	m[m[DIC]++] = READ; /*create a special word that reads in FORTH*/
	m[m[DIC]++] = RUN;  /*call the special word recursively*/
	o->m[INSTRUCTION] = m[DIC]; /*instruction stream points to our special word*/
	m[m[DIC]++] = w;    /*call to READ word*/
	m[m[DIC]++] = t;    /*call to TAIL*/
	m[m[DIC]++] = o->m[INSTRUCTION] - 1; /*recurse*/

	compile(o, DEFINE,    ":");         /*immediate word*/
	compile(o, IMMEDIATE, "immediate"); /*immediate word*/
	for(i = 0, w = READ; instruction_names[i]; i++) /*compiling words*/
		compile(o, COMPILE, instruction_names[i]), m[m[DIC]++] = w++;
	/* the next eval is the absolute minimum needed for a sane environment */
	VERIFY(forth_eval(o, ": state 8 exit : ; immediate ' exit , 0 state ! ;") >= 0);
	for(i = 0; register_names[i]; i++) /* name all registers */ 
		VERIFY(forth_define_constant(o, register_names[i], i+DIC) >= 0);
	VERIFY(forth_eval(o, initial_forth_program) >= 0);
	VERIFY(forth_define_constant(o, "size",          sizeof(forth_cell_t)) >= 0);
	VERIFY(forth_define_constant(o, "stack-start",   size - (2 * o->m[STACK_SIZE])) >= 0);
	VERIFY(forth_define_constant(o, "max-core",      size) >= 0);

	forth_set_file_input(o, in);  /*set up input after our eval*/
	return o;
}

/* This is a crude method that should only be used for debugging purposes, it
 * simply dumps the forth structure to disk, including any padding which the
 * compiler might have inserted. This dump cannot be reloaded */
int forth_dump_core(forth_t *o, FILE *dump) 
{ 
	assert(o && dump);
	size_t w = sizeof(*o) + sizeof(forth_cell_t) * o->core_size;
	return w != fwrite(o, 1, w, dump) ? -1: 0; 
}

/* We can save the virtual machines working memory in a way, called
 * serialization, such that we can load the saved file back in and continue
 * execution using this save environment. Only the three previously mentioned
 * fields are serialized; "m", "core_size" and the "header". */
int forth_save_core(forth_t *o, FILE *dump) 
{ 
	assert(o && dump);
	uint64_t r1, r2, r3, core_size = o->core_size;
	r1 = fwrite(o->header,  1, sizeof(o->header), dump);
	r2 = fwrite(&core_size, sizeof(core_size), 1, dump);
	r3 = fwrite(o->m,       1, sizeof(forth_cell_t) * o->core_size, dump);
	if(r1 + r2 + r3 != (sizeof(o->header) + 1 + sizeof(forth_cell_t) * o->core_size))
		return -1;
	return 0;
}

/* Logically if we can save the core for future reuse, then we must have a
 * function for loading the core back in, this function returns a reinitialized
 * Forth object. Validation on the object is performed to make sure that it is
 * a valid object and not some other random file, endianess, core_size, cell
 * size and the headers magic constants field are all checked to make sure they
 * are correct and compatible with this interpreter.
 *
 * "forth_make_default" is called to replace any instances of pointers stored
 * in registers which are now invalid after we have loaded the file from disk. */
forth_t *forth_load_core(FILE *dump)
{ /* load a forth core dump for execution */
	uint8_t actual[sizeof(header)] = {0}, expected[sizeof(header)] = {0};
	forth_t *o = NULL;
	uint64_t w = 0, core_size = 0;
	make_header(expected);
	if(sizeof(actual) != fread(actual, 1, sizeof(actual), dump))
		goto fail; /* no header */
	if(memcmp(expected, actual, sizeof(header))) 
		goto fail; /* invalid or incompatible header */
	if(1 != fread(&core_size, sizeof(core_size), 1, dump) || core_size < MINIMUM_CORE_SIZE)
		goto fail; /* no header, or size too small */
	w = sizeof(*o) + (sizeof(forth_cell_t) * core_size);
	if(!(o = calloc(w, 1)))
		goto fail; /* object too big */
	w = sizeof(forth_cell_t) * core_size;
	if(w != fread(o->m, 1, w, dump))
		goto fail; /* not enough bytes in file */
	o->core_size = core_size;
	memcpy(o->header, actual, sizeof(o->header));
	forth_make_default(o, core_size, stdin, stdout);
	return o;
fail:
	free(o);
	return NULL;
}

void forth_free(forth_t *o) 
{ 
	assert(o); 
	free(o->on_error);
	free(o); 
}

/* "forth_push", "forth_pop" and "forth_stack_position" are the main ways an
 * application programmer can interact with the Forth interpreter. Usually this
 * tutorial talks about how the interpreter and virtual machine work, about how
 * compilation and command modes work, and the internals of a Forth 
 * implementation. However this project does not just present an ordinary Forth
 * interpreter, the interpreter can be embedded into other applications, and it
 * is possible be running multiple instances Forth interpreters in the same
 * process.
 *
 * The project provides an API which other programmers can use to do this, one
 * mechanism that needs to be provided is the ability to move data into and out
 * of the interpreter, these C level functions are how this mechanism is
 * achieved. They move data between a C program and a paused Forth interpreters
 * variable stack. */

void forth_push(forth_t *o, forth_cell_t f)
{
	assert(o && o->S < o->m + o->core_size);
	*++(o->S) = o->m[TOP];
	o->m[TOP] = f;
}

forth_cell_t forth_pop(forth_t *o)
{
	assert(o && o->S > o->m);
	forth_cell_t f = o->m[TOP];
	o->m[TOP] = *(o->S)--;
	return f;
}

forth_cell_t forth_stack_position(forth_t *o)
{ 
	assert(o);
	return o->S - (o->m + o->core_size - (2 * o->m[STACK_SIZE]));
}

/* "check_bounds" is used to both check that a memory access performed by the
 * virtual machine is within range and as a crude method of debugging the
 * interpreter (if it is enabled). The function is not called directly but
 * is instead wrapped in with the "ck" macro, it can be removed completely
 * with compile time defines, removing the check and the debugging code. */
static forth_cell_t check_bounds(forth_t *o, forth_cell_t f, unsigned line) 
{
	if(o->m[DEBUG])
		fprintf(stderr, "\t( debug\t0x%" PRIxCell "\t%u )\n", f, line);
	if(((forth_cell_t)f) >= o->core_size) {
		fprintf(stderr, "( fatal \"bounds check failed: %" PRIuCell " >= %zu\" )\n", f, (size_t)o->core_size);
		longjmp(*o->on_error, 1);
	}
	return f; 
}

/* ============================ Section 5 ================================== */
/*                      The Forth Virtual Machine                            */

/* The largest function in the file, which implements the forth virtual
 * machine, everything else in this file is just fluff and support for this
 * function. This is the Forth virtual machine, it implements a threaded
 * code interpreter (see https://en.wikipedia.org/wiki/Threaded_code, and
 * https://www.complang.tuwien.ac.at/forth/threaded-code.html).
 *
 */
int forth_run(forth_t *o) 
{ 
	assert(o);
	if(o->m[INVALID] || setjmp(*o->on_error))
		return -(o->m[INVALID] = 1);
	
	forth_cell_t *m = o->m, pc, *S = o->S, I = o->m[INSTRUCTION], f = o->m[TOP], w;

	/* The for loop and the switch statement here form the basis of our
	 * thread code interpreter*/
	for(;(pc = m[ck(I++)]);) { 

	INNER:  assert((S > m) && (S < (m + o->core_size)));

		switch (w = instruction(m[ck(pc++)])) {
		case PUSH:    *++S = f;     f = m[ck(I++)];          break;
		case COMPILE: m[ck(m[DIC]++)] = pc;                  break; /* this needs to be moved into READ */
		case RUN:     m[ck(++m[RSTK])] = I; I = pc;          break;
		case DEFINE:  
			/* DEFINE backs the Forth word ':', which is an 
			 * immediate word, it reads in a new word name, creates
			 * a header for that word and enters into COMPILE mode,
			 * where all words (baring immediate words) are
			 * compiled into the dictionary instead of being
			 * executed. */
			      m[STATE] = 1; /* compile mode */
                              if(forth_get_word(o, o->s) < 0)
                                      goto end;
                              compile(o, COMPILE, (char*)o->s); 
                              m[ck(m[DIC]++)] = RUN;                 break;
		case IMMEDIATE: 
			      m[DIC] -= 2; /* move to first code field */
			      m[m[DIC]] &= ~INSTRUCTION_MASK; /* zero instruction */
			      m[m[DIC]] |= RUN; /* set instruction to RUN */
			      m[DIC]++; /* compilation start here */ break;
		case READ: 
			      /* the READ instruction, an instruction that
			       * usually does not belong in a virtual machine,
			       * forms the basis of Forths interactive nature.
			       *
			       * It attempts to do the follow:
			       * 
			       * Lookup a space delimited string in the Forth
			       * dictionary, if it is found and we are in 
			       * command mode we execute it, if we are in
			       * compile mode and the word is a compiling word
			       * we compile a pointer to it in the dictionary,
			       * if not we execute it.
			       *
			       * If it is not a word in the dictionary we
			       * attempt to treat it as a number, if it is a
			       * number (using the BASE register to determine
			       * the base of it) then if we are in command mode
			       * we push the number to the variable stack, else
			       * if we are in compile mode we compile the
			       * literal into the dictionary.
			       *
			       * If it is neither a word nor a number,
			       * regardless of mode, we emit a diagnostic.
			       *
			       * This is the most complex word in the Forth
			       * virtual machine, there is a good case for it
			       * being moved outside of it, and perhaps this
			       * will happen. You will notice that the above
			       * description did not include any looping, as
			       * such there is a driver for the interpreter
			       * which must be made and initialized in
			       * "forth_init", a simple word that calls READ in
			       * a loop (actually tail recursively).
			       */
				if(forth_get_word(o, o->s) < 0)
					goto end;
				if ((w = forth_find(o, (char*)o->s)) > 1) {
					pc = w;
					if (!m[STATE] && instruction(m[ck(pc)]) == COMPILE)
						pc++; /* in command mode, execute word */
					goto INNER;
				} else if(!numberify(o->m[BASE], &w, (char*)o->s)) {
					fprintf(stderr, "( error \"%s is not a word\" )\n", o->s);
					break;
				}
				if (m[STATE]) { /* must be a number then */
					m[m[DIC]++] = 2; /*fake word push at m[2]*/
					m[ck(m[DIC]++)] = w;
				} else { /* push word */
					*++S = f;
					f = w;
				} break;
		/* Most of the following Forth instructions are simple Forth
		 * words, each one with an uncomplicated Forth word which is
		 * implemented by the corresponding instruction (such as LOAD
		 * and "@", STORE and "!", EXIT and "exit", and ADD and "+").
		 *
		 * However, the reason for these words existing, and under what
		 * circumstances some of the can be used is a different matter,
		 * the COMMA and TAIL word will require some explaining, but
		 * ADD, SUB and DIV will not. */
		case LOAD:    f = m[ck(f)];                          break;
		case STORE:   m[ck(f)] = *S--; f = *S--;             break;
		case SUB:     f = *S-- - f;                          break;
		case ADD:     f = *S-- + f;                          break;
		case AND:     f = *S-- & f;                          break;
		case OR:      f = *S-- | f;                          break;
		case XOR:     f = *S-- ^ f;                          break;
		case INV:     f = ~f;                                break;
		case SHL:     f = *S-- << f;                         break;
		case SHR:     f = (forth_cell_t)*S-- >> f;           break;
		case MUL:     f = *S-- * f;                          break;
		case DIV:     if(f) 
				      f = *S-- / f; 
			      else /* should throw exception */
				      fputs("( error \"x/0\" )\n", stderr); 
			                                             break;
		case LESS:    f = *S-- < f;                          break;
		case MORE:    f = *S-- > f;                          break;
		case EXIT:    I = m[ck(m[RSTK]--)];                  break;
		case EMIT:    fputc(f, (FILE*)(o->m[FOUT])); f = *S--; break;
		case KEY:     *++S = f; f = forth_get_char(o);       break;
		case FROMR:   *++S = f; f = m[ck(m[RSTK]--)];        break;
		case TOR:     m[ck(++m[RSTK])] = f; f = *S--;        break;
		case BRANCH:  I += m[ck(I)];                         break;
		case QBRANCH: I += f == 0 ? m[I] : 1; f = *S--;      break;
		case PNUM:    print_cell(o, f); f = *S--;            break;
		case QUOTE:   *++S = f;     f = m[ck(I++)];          break;
		case COMMA:   m[ck(m[DIC]++)] = f; f = *S--;         break;
		case EQUAL:   f = *S-- == f;                         break;
		case SWAP:    w = f;  f = *S--;   *++S = w;          break;
		case DUP:     *++S = f;                              break;
		case DROP:    f = *S--;                              break;
		case OVER:    w = *S; *++S = f; f = w;               break;
		case TAIL:    m[RSTK]--;                             break;
		/* The blockio function interface has been made specially so
		 * that it is easy to add block functionality to the Forth 
		 * interpreter.*/
		case BSAVE:   f = blockio(o, *S--, f, 'w');          break;
		case BLOAD:   f = blockio(o, *S--, f, 'r');          break;
		/* FIND is a natural factor of READ, we add it to the Forth
		 * interpreter as it already exits, it looks up a Forth word in
		 * the dictionary and returns a pointer to that word if it
		 * found.*/
		case FIND:    *++S = f;
			      if(forth_get_word(o, o->s) < 0) 
				      goto end;
			      f = forth_find(o, (char*)o->s);
			      f = f < DICTIONARY_START ? 0 : f;      break;
		/* PRINT is a word that could be removed from the interpreter,
		 * as it could be implemented in terms of looping and emit, it
		 * prints out an ASCII delimited string to the output stream.
		 *
		 * There is a bit of an impedance mismatch between how Forth
		 * treats strings and how most programming languages treat
		 * them. Most higher level languages are built upon the C
		 * runtime, so at some level support NUL terminated strings,
		 * however Forth uses strings that include a pointer to the
		 * string and the strings length instead. As more C functions
		 * are added the difference in string treatment will become
		 * more apparent. Due to this difference it is always best to
		 * NUL terminate strings in Forth code even if they are stored
		 * with their length */
		case PRINT:   fputs(((char*)m)+f, (FILE*)(o->m[FOUT])); 
			      f = *S--;                              break;
		/* DEPTH is added because the stack is not directly accessible
		 * by the virtual machine (mostly for code readability
		 * reasons), normally it would have no way of knowing where the
		 * variable stack pointer is, which is needed to implement
		 * Forth words such as ".s" - which prints out all the items on
		 * the stack. */
		case DEPTH:   w = S - (m + o->core_size - (2 * o->m[STACK_SIZE]));
			      *++S = f;
			      f = w;                                 break;
		/* CLOCK allows for a very primitive and wasteful (depending on
		 * how the C library implements "clock") timing mechanism, it
		 * has the advantage of being largely portable */
		case CLOCK:   *++S = f;
			      f = ((1000 * clock()) - o->m[START_TIME]) / CLOCKS_PER_SEC;
			                                             break;
		/* This should never happen, and if it does it is an indication
		 * that virtual machine memory has been corrupted somehow */
		default:      
			fprintf(stderr, "( fatal 'illegal-op %" PRIuCell " )\n", w);
			longjmp(*o->on_error, 1);
		}
	}
	/* We must save the stack pointer and the top of stack when we exit the
	 * interpreter so the C functions like "forth_pop" work correctly. If the
	 * forth_t object has been invalidated (because something when very
	 * wrong), we do not have to jump to "end" as functions like
	 * "forth_pop" should not be called on the invalidated object any
	 * longer.*/
end:	o->S = S;
	o->m[TOP] = f;
	return 0;
}

/* ============================ Section 6 ================================== */
/*    An example main function called main_forth and support functions       */

/* This section is not needed to understand how Forth works, or how the C API
 * into the Forth interpreter works. It provides a function which uses all
 * the functions available to the API programmer in order to create an example
 * program that implements a Forth interpreter with a Command Line Interface.
 *
 * This program can be used as a filter in a Unix pipe chain, or as a
 * standalone interpreter for Forth. It tries to follow the Unix philosophy and
 * way of doing things (see
 * http://www.catb.org/esr/writings/taoup/html/ch01s06.html and
 * https://en.wikipedia.org/wiki/Unix_philosophy). Whether this is achieved
 * is a matter of opinion. There are a few things this interpreter does
 * differently to most Forth interpreters that support this philosophy however,
 * it is silent by default and does not clutter up the output window with "ok",
 * or by printing a banner at start up (which would contain no useful
 * information whatsoever). It is simple, and only does one thing (but does it 
 * do it well?).*/

static void fclose_input(FILE **in)
{
	if(*in && (*in != stdin))
		fclose(*in);
	*in = stdin;
}

void forth_set_args(forth_t *o, int argc, char **argv)
{ /* currently this is of little use to the interpreter */
	assert(o);
	o->m[ARGC] = argc;
	o->m[ARGV] = (forth_cell_t)argv;
}

/* main_forth implements a Forth interpreter which is a wrapper around the C
 * API, there is an assumption that main_forth will be the only thing running
 * in a process (it does not seem sensible to run multiple instances of it at
 * the same time - it is just for demonstration purposes), as such the only
 * error handling should do is to die after printing an error message if an
 * error occurs, the "fopen_or_die" is an example of this philosophy, one which
 * does not apply to functions like "forth_run" (which makes attempts to
 * recover from a sensible error). */
static FILE *fopen_or_die(const char *name, char *mode) 
{
	errno = 0;
	FILE *file = fopen(name, mode);
	if(!file) {
		fprintf(stderr, "( fatal 'file-open \"%s: %s\" )\n", name, errno ? strerror(errno): "unknown");
		exit(EXIT_FAILURE);
	}
	return file;
}

static void usage(const char *name)
{
	fprintf(stderr, "usage: %s [-s file] [-e string] [-l file] [-t] [-h] [-m size] [-] files\n", name);
}

/* We try to keep the interface to the example program as simple as possible, so
 * there are few options and they are largely uncomplicated. What they do
 * should come as no surprise to an experienced Unix programmer, it is
 * important to pick option names that they would expect (for example "-l" for
 * loading, "-e" for evaluation, and not using "-h" for help would be a hanging
 * offense).*/
static void help(void)
{
	static const char help_text[] = "\
Forth: A small forth interpreter build around libforth\n\n\
\t-h        print out this help and exit unsuccessfully\n\
\t-e string evaluate a string\n\
\t-s file   save state of forth interpreter to file\n\
\t-d        save state to 'forth.core'\n\
\t-l file   load previously saved state from file\n\
\t-m size   specify forth memory size in kilobytes (cannot be used with '-l')\n\
\t-t        process stdin after processing forth files\n\
\t-         stop processing options\n\n\
Options must come before files to execute\n\n";
	fputs(help_text, stderr);
}

/* "main_forth" is the second largest function is this file, but is not as
 * complex as forth_run (currently the largest and most complex function), it
 * brings together all the API functions offered by this library and provides
 * a quick way for programmers to implement a working Forth interpreter for
 * testing purposes.
 *
 * This make implementing a Forth interpreter as simple as:
 *
 * ==== main.c =============================
 *
 *   #include "libforth.h"
 *
 *   int main(int argc, char **argv) 
 *   {
 * 	  return main_forth(argc, argv);
 *   }
 *
 * ==== main.c =============================
 *
 * To keep things simple options are parsed first then arguments like files,
 * although some options take arguments immediately after them.
 *
 */
int main_forth(int argc, char **argv) 
{
	FILE *in = NULL, *dump = NULL;
	int save = 0, readterm = 0, mset = 0, eval = 0, rval = 0, i = 1, c = 0;
	static const size_t kbpc = 1024/sizeof(forth_cell_t); /*kilobytes per cell*/
	static const char *dump_name = "forth.core";
	forth_cell_t core_size = DEFAULT_CORE_SIZE;
	forth_t *o = NULL;
	/* This loop processes any options that may have been passed to the
	 * program, it looks for arguments beginning with '-' and attempts to
	 * process that option, if the argument does not start with '-' the
	 * option processing stops. It is a simple mechanism for processing
	 * program arguments and there are better ways of doing it (such as
	 * "getopt" and "getopts"), but by using them we sacrifice portability. */
	for(i = 1; i < argc && argv[i][0] == '-'; i++)
		switch(argv[i][1]) { 
		case '\0': goto done; /* stop argument processing */
		case 'h':  usage(argv[0]); help(); return -1;
		case 't':  readterm = 1; break;
		case 'e':  if(i >= (argc - 1))
				   goto fail;
			   if(!(o = o ? o : forth_init(core_size, stdin, stdout))) {
				   fprintf(stderr, "error: initialization failed\n");
				   return -1;
			   }
			   if(forth_eval(o, argv[++i]) < 0)
				   goto end;
			   eval = 1;
			   break;
		case 's':  if(i >= (argc - 1))
				   goto fail;
			   dump_name = argv[++i];
		case 'd':  /*use default name*/
			   save = 1; 
			   break;
		case 'm':  if(o || (i >= argc - 1) || !numberify(10, &core_size, argv[++i]))
				   goto fail;
			   if((core_size *= kbpc) < MINIMUM_CORE_SIZE) {
				   fprintf(stderr, "error: -m too small (minimum %zu)\n", MINIMUM_CORE_SIZE / kbpc);
				   return -1;
			   }
			   mset = 1;
			   break;
		case 'l':  if(o || mset || (i >= argc - 1))
				   goto fail;
			   if(!(o = forth_load_core(dump = fopen_or_die(argv[++i], "rb")))) {
				   fprintf(stderr, "error: %s: core load failed\n", argv[i]);
				   return -1;
			   }
			   fclose(dump);
			   break;
		default:
		fail:
			fprintf(stderr, "error: invalid arguments\n");
			usage(argv[0]);
			return -1;
		}
done:
	readterm = (!eval && i == argc) || readterm; /* if no files are given, read stdin */
	if(!o && !(o = forth_init(core_size, stdin, stdout))) {
		fprintf(stderr, "error: forth initialization failed\n");
		return -1;
	}
	forth_set_args(o, argc, argv);
	for(; i < argc; i++) { /* process all files on command line */
		forth_set_file_input(o, in = fopen_or_die(argv[i], "rb"));
		if((c = fgetc(in)) == '#') /*shebang line '#!', core files could also be detected */  
			while(((c = forth_get_char(o)) > 0) && (c != '\n'));
		else if(c == EOF)
			goto close;
		else
			ungetc(c, in);
		if((rval = forth_run(o)) < 0) 
			goto end;
close:	
		fclose_input(&in);
	}
	if(readterm) { /* if '-t' or no files given, read from stdin */
		forth_set_file_input(o, stdin);
		rval = forth_run(o);
	}
end:	
	fclose_input(&in);
	/* If the save option has been given we only want to save valid core
	 * files, we might want to make an option to force saving of core files
	 * for debugging purposes, but in general we do not want to over write
	 * valid previously saved state with invalid data. */
	if(save) { /* save core file */
		if(rval || o->m[INVALID]) {
			fprintf(stderr, "error: refusing to save invalid core\n");
			return -1;
		}
		if(forth_save_core(o, dump = fopen_or_die(dump_name, "wb"))) {
			fprintf(stderr, "error: core file save to '%s' failed\n", dump_name);
			rval = -1;
		}
		fclose(dump); 
	}
	/* Whilst the following "forth_free" is not strictly necessary, there
	 * is often a debate that comes up making short lived programs or
	 * programs whose memory use stays either constant or only goes up,
	 * when these programs exit it is not necessary to clean up the
	 * environment and in some case (although not this one) it can
	 * significantly slow down the exit of the program for no reason.
	 * However not freeing the memory after use does not play nice with
	 * programs that detect memory leaks, like Valgrind. Either way, we
	 * free the memory used here, but only if no other errors have occurred
	 * before hand. */
	forth_free(o);
	return rval;
}

