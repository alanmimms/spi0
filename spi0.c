#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>


typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long long u64;

typedef signed char s8;
typedef signed short s16;
typedef signed long s32;
typedef signed long long s64;


#define NELEM(A)        (sizeof(A)/sizeof(A[0]))


#define DEFOPTS                                                                                           \
  DEF1OPT("h", "help",    "                     | This help.", HelpOp)                                    \
  DEF1OPT("v", "verbose", "                     | Verbosely explain operations as they progress.", VerboseOp) \
  DEF1OPT("d", "device",  "pathname             | /dev/spidevX.Y device pathname", DeviceOp)              \
  DEF1OPT("E", "erase",   "base size            | Erase device range", EraseOp)                           \
  DEF1OPT("W", "write",   "filename base        | Write file to device at base", WriteOp)                 \
  DEF1OPT("V", "verify",  "filename base        | Verify device contains file content at base", VerifyOp) \
  DEF1OPT("R", "read",    "filename base size   | Read device content to file", ReadOp)

  
// Define an enum for our OPTYPE names
#define DEF1OPT(SOPT, LOPT, HELP, OPTYPE) OPTYPE,
typedef enum {DEFOPTS} OpType;
#undef DEF1OPT


#define DEF1OPT(SOPT, LOPT, HELP, OPTYPE) {SOPT, LOPT, HELP, OPTYPE},
static struct {
  char *sopt;
  char *lopt;
  char *help;
  OpType op;
} options[] = {
  DEFOPTS
};
#undef DEF1OPT


#define DEF1OPT(SOPT, LOPT, HELP, OPTYPE)  +1
static const unsigned NOPTS = 0 DEFOPTS;
#undef DEF1OPT


// Our program name from our command line.
static char *progName;

// Our verbosity level
static int verbose;


// File descriptor for the specified device.
static int spiFD = -1;


// Max buffer size we can use with /dev/spidev module
#define MAX_BUF_SIZE   4096


static const u8 jedecSFDP[] = {
  0x5A,                         /* JEDEC READ SFDP command */
  0, 0, 0,                      /* 24b address */
  0,                            /* 8b dummy */
};


// Maximal operation parameter list. Use what you need from here and add more when needed.
struct Op {
  OpType op;
  char *nameP;                  /* Name of file */
  u8 *imageP;                   /* Actual CONTENT of file */
  u32 base;                     /* Base offset in device to erase */
  u32 size;                     /* Size to erase */
};



// For each operation we're asked to do there is an element in this
// array.
static unsigned nOps = 0;
static struct Op *opsP = 0;     /* Array of Op structures for each operation to be done */


// Send the specified SPI command to the device and read up to
// `respBufLen` bytes as a response, returning the actual read length
// or negative if error.
static int commandAndResponse(u8 *cmdBufP, u32 cmdLen, u8 *respBufP, u32 respBufLen) {

  struct spi_ioc_transfer msg[] = {
    {.tx_buf = (u64)(void *)jedecSFDP, .len = cmdLen},
  };
  
}



#define MAX(x,y) ( \
    { __auto_type __x = (x); __auto_type __y = (y); \
      __x > __y ? __x : __y; })



static void usage(char *fmtP, char *p1) {
  int maxHelpW = 0;

  for (int k = 0; k < NOPTS; ++k) maxHelpW = MAX(maxHelpW, strlen(options[k].lopt));

  fprintf(stderr, "\nUsage:\n");

  for (int k = 0; k < NOPTS; ++k) {
    fprintf(stderr, "-%s,--%-*s%s\n", options[k].sopt, maxHelpW + 4, options[k].lopt, options[k].help);
  }

  fprintf(stderr, "\n"
        "The device name option must be specified before any operations.\n"
        "All numbers are specified in hexadecimal (leading 0x not required).\n"
        "No erase or verify operations are implicitly assumed, but\n"
        "operations may be specified as many times as needed. For example,\n"
        "to erase, write, and then verify a 1MB file `data` on `/dev/spidev0.0`, use\n"
        "\n"
        "    %s -d /dev/spidev0.0 -E 0,100000 -W data,0 -V data,0\n",
        progName);

  if (fmtP) {
    fprintf(stderr, "\n\n");
    fprintf(stderr, fmtP, p1);
    fprintf(stderr, "\n");
  }

  exit(-1);
}


// Read the contents of the specified file into a newly allocated
// dyanmic buffer and return it and its size is optionally returned
// via sizeP;
static u8 *readFile(char *fileNameP, u32 *sizeP) {
  struct stat statbuf;
  u8 *bufP = 0;
  int st;

  st = lstat(fileNameP, &statbuf);
  if (st < 0) usage("Unable to determine size of '%s'", fileNameP);
  bufP = (u8 *) malloc(statbuf.st_size);
  if (!bufP) usage("Unable to allocate space for content of '%s'", fileNameP);
  if (sizeP) *sizeP = statbuf.st_size;
  return bufP;
}



static void checkDev() {
  if (spiFD <= 0) usage("The -d or --device command line option is REQUIRED before any device operations.", NULL);
}


// Convert hex string to unsigned 32-bit value and return it. Allows
// a "0x" prefix but ignores it.
static u32 htoi(char *hexP) {
  char *originalStringP = hexP;
  u32 s = 0;

  if (hexP[0] == '0' && (hexP[1] == 'x' || hexP[1] == 'X')) hexP += 2; /* Ignore 0x prefix if present */

  while (*hexP) {
    int digit = *hexP++;
    if (digit >= '0' && digit <= '9') digit -= '0';
    else if (digit >= 'a' && digit <= 'f') digit -= 'a' - 10;
    else if (digit >= 'A' && digit <= 'F') digit -= 'A' - 10;
    else usage("Bad hex digit in '%s'", originalStringP);
    s <<= 4;
    s |= digit;
  }

  return s;
}



#define CHECK_MORE() do { if (argc <= 0) usage("Missing required command line parameter on '%s'", curOpt); } while (0)


static void parseCommandLine(int argc, char *argv[]) {
  int k = 0;
  char *curOpt;
  struct Op *op;

  // Parse our command line parameters
  while (argc > 0) {

    if (argv[0][0] == '-' && argv[0][1] == '-') { /* Long option */

      for (k = 0; k < NOPTS; ++k) {
        if (strcmp(options[k].lopt, argv[0] + 2) == 0) goto GotOption;
      }

      usage("Unknown long option '%s'", *argv); 
    } else if (argv[0][0] == '-') {               /* Short option */

      for (k = 0; k < NOPTS; ++k) {
        if (strcmp(options[k].sopt, argv[0] + 1) == 0) goto GotOption;
      }

      usage("Unknown short option '%s'", *argv); 
    } else {                                      /* Invalid command line option */
      usage("Bad option '%s'", argv[0]);
    }


  GotOption:
    /* Consume option name argv element but note it for error messages */
    curOpt = *argv++; --argc;

    // Make room on the end for a new operation element (works if ops is initially null).
    opsP = (struct Op *) realloc(opsP, ++nOps * sizeof(*opsP));
    if (!opsP) usage("Cannot allocate enough space for the requested list of operations", curOpt);

    op = opsP + nOps - 1;        /* Point at the new element */
    // Intially zero the new element
    bzero(op, sizeof(*op));
    op->op = options[k].op;      /* Note its operation type */

    switch (options[k].op) {
      //  DEF1OPT("h", "help", "This help", HelpOp)
    case HelpOp:
      usage(NULL, NULL);
      break;

    case VerboseOp:
      ++verbose;
      break;

      //  DEF1OPT("d", "device", "/dev/spidevX.Y device pathname", DeviceOp)
    case DeviceOp:
      op->nameP = *argv++; --argc;
      spiFD = open(op->nameP, O_RDWR);
      if (spiFD < 0) usage("Unable to open device '%s' for read/write", op->nameP);
      break;

      //  DEF1OPT("E", "erase", "Erase device range (base,size in hex)", EraseOp)
    case EraseOp:
      checkDev();
      CHECK_MORE(); op->base = htoi(*argv++); --argc;
      CHECK_MORE(); op->size = htoi(*argv++); --argc;
      break;

      //  DEF1OPT("W", "write", "Write file to device at base (file,base in hex)", WriteOp)
    case WriteOp:
      checkDev();
      CHECK_MORE(); op->nameP = *argv++; --argc;
      op->imageP = readFile(op->nameP, &op->size);
      CHECK_MORE(); op->base = htoi(*argv++); --argc;
      break;

      //  DEF1OPT("R", "read", "Read device range to file (file,base,size in hex)", ReadOp)
    case ReadOp:
      checkDev();
      CHECK_MORE(); op->nameP = *argv++; --argc;
      CHECK_MORE(); op->base = htoi(*argv++); --argc;
      CHECK_MORE(); op->size = htoi(*argv++); --argc;
      break;

      //  DEF1OPT("V", "verify", "Verify device contains file content at base (file,base in hex)", VerifyOp)
    case VerifyOp:
      checkDev();
      CHECK_MORE(); op->nameP = *argv++; --argc;
      op->imageP = readFile(op->nameP, &op->size);
      CHECK_MORE(); op->base = htoi(*argv++); --argc;
      break;

    default:
      usage("Internal error: Bad operation type from '%s'", argv[-1]);
      break;
    }
  }
}


static char *opToName(OpType op) {

  for (int n = 0; n < NOPTS; ++n) {
    if (options[n].op == op) return options[n].lopt;
  }

  return "??? unknown op ???";
}



int main(int argc, char *argv[]) {
  progName = *argv++;
  --argc;
  parseCommandLine(argc, argv);

  if (verbose) {

    for (int k; k < nOps; ++k) {
      struct Op *op = &opsP[k];

      fprintf(stderr, "%-10s: base=%08X  size=%08X  file='%s'\n",
              opToName(op->op),
              op->base,
              op->size,
              op->nameP ? op->nameP : "");
    }
  }

  return 0;
}
