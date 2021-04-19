// TODO:
// * Implement existing operations.
// * Add mechanism to set SPI mode (CPOL/CPHA) for operations.
// * Add mechanism to set SPI clock rate.
// * Add mechanism to set bit order (Needed? Doesn't CFI define this?).
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
#define MAX(x,y)        ({ __auto_type __x = (x); __auto_type __y = (y); __x > __y ? __x : __y; })


#define DEFOPTS                                                                                                                                 \
  DEF1OPT("h", "help",    "                     | This help.", HelpOp)                                                                          \
  DEF1OPT("v", "verbose", "                     | Verbosely explain operations as they progress.", VerboseOp)                                   \
  DEF1OPT("n", "no-op", "                       | Parse command line and display what is to be done, but don't touch device.", DoNothingOp)     \
  DEF1OPT("d", "device",  "pathname             | Set /dev/spidevX.Y device pathname", DeviceOp)                                                \
  DEF1OPT("s", "speed",   "speed-in-hz          | Set SPI bus speed in (decimal) Hz", SpeedOp)                                                  \
  DEF1OPT("i", "identify","                     | Identify the SPI device and display its CFI SFDP data", IdentifyOp)                           \
  DEF1OPT("E", "erase",   "base size            | Erase device range", EraseOp)                                                                 \
  DEF1OPT("W", "write",   "filename base        | Write file to device at base", WriteOp)                                                       \
  DEF1OPT("V", "verify",  "filename base        | Verify device contains file content at base", VerifyOp)                                       \
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

// Flash indicating we should parse command line options and display what we found but do nothing to the device.
static int doNothing;


// File descriptor for the specified device.
static int spiFD = -1;


// Max buffer size we can use with /dev/spidev module
#define MAX_BUF_SIZE   4096


// I/O buffer we use for SPI interactions.
static char ioBuf[MAX_BUF_SIZE];


// Maximal operation parameter list. Use what you need from here and add more when needed.
struct Op {
  OpType op;
  char *nameP;                  /* Name of file */
  u8 *imageP;                   /* Actual CONTENT of file */
  u32 base;                     /* Base offset in device to erase */
  u32 size;                     /* Size to erase */
  u32 speed;                    /* Bus speed in Hz */
};



// For each operation we're asked to do there is an element in this
// array.
static unsigned nOps = 0;
static struct Op *opsP = 0;     /* Array of Op structures for each operation to be done */


static void usage(char *fmtP, char *p1) {
  int maxHelpW = 0;

  for (int k = 0; k < NOPTS; ++k) maxHelpW = MAX(maxHelpW, strlen(options[k].lopt));

  fprintf(stderr, "\nUsage:\n");

  for (int k = 0; k < NOPTS; ++k) {
    fprintf(stderr, "-%s,--%-*s%s\n", options[k].sopt, maxHelpW + 4, options[k].lopt, options[k].help);
  }

  fprintf(stderr, "\n"
        "The device name option must be specified before any operations.\n"
        "Base and size parameters are specified in hexadecimal (leading 0x not required).\n"
        "No erase or verify operations are implicitly assumed, but\n"
        "operations may be specified as many times as needed. For example,\n"
        "to erase, write, and then verify a 1MB file `data` on `/dev/spidev0.0`, use\n"
        "\n"
        "    %s -d /dev/spidev0.0 -S 10000000 -E 0,100000 -W data,0 -V data,0\n",
        progName);

  if (fmtP) {
    fprintf(stderr, "\n\n");
    fprintf(stderr, fmtP, p1);
    fprintf(stderr, "\n");
  }

  exit(-1);
}


static void perrorExit(char *msgP) {
  perror(msgP);
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



static void checkDev(void) {
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



static struct Op *appendToOperationsList(OpType opType, char *curOpt) {
  struct Op *op;

  // Make room on the end for a new operation element (works if ops is initially null).
  opsP = (struct Op *) realloc(opsP, ++nOps * sizeof(*opsP));
  if (!opsP) usage("Cannot allocate enough space for the requested list of operations", curOpt);

  op = opsP + nOps - 1;        /* Point at the new element */
  // Intially zero the new element
  bzero(op, sizeof(*op));
  op->op = opType;      /* Note its operation type */
  return op;
}



#define CHECK_MORE() do { if (argc <= 0) usage("Missing required command line parameter on '%s'", curOpt); } while (0)
#define CHECKST(ST, MSG) do { if ((ST) < 0) perror(MSG); } while (0)


static void parseCommandLine(int argc, char *argv[]) {
  int k = 0;
  char *curOpt;
  struct Op *op;
  int actionsCounter = 0;

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

    // Add a new entry onto the end of the opsP operations array.
    op = appendToOperationsList(options[k].op, curOpt);

    switch (options[k].op) {
    case HelpOp:
      usage(NULL, NULL);
      break;

    case VerboseOp:
      ++verbose;
      break;

    case DoNothingOp:
      ++doNothing;
      break;

    case DeviceOp:
      op->nameP = *argv++; --argc;
      if (spiFD >= 0) usage("Only set the device '%s' option once per invocation of this program", curOpt);
      spiFD = open(op->nameP, O_RDWR);
      if (spiFD < 0) usage("Unable to open device '%s' for read/write", op->nameP);
      break;

    case SpeedOp:
      op->speed = strtoul(*argv++, NULL, 10); --argc;
      break;

    case IdentifyOp:
      checkDev();
      ++actionsCounter;
      break;

    case EraseOp:
      checkDev();
      CHECK_MORE(); op->base = htoi(*argv++); --argc;
      CHECK_MORE(); op->size = htoi(*argv++); --argc;
      ++actionsCounter;
      break;

    case WriteOp:
      checkDev();
      CHECK_MORE(); op->nameP = *argv++; --argc;
      op->imageP = readFile(op->nameP, &op->size);
      CHECK_MORE(); op->base = htoi(*argv++); --argc;
      ++actionsCounter;
      break;

    case ReadOp:
      checkDev();
      CHECK_MORE(); op->nameP = *argv++; --argc;
      CHECK_MORE(); op->base = htoi(*argv++); --argc;
      CHECK_MORE(); op->size = htoi(*argv++); --argc;
      ++actionsCounter;
      break;

    case VerifyOp:
      checkDev();
      CHECK_MORE(); op->nameP = *argv++; --argc;
      op->imageP = readFile(op->nameP, &op->size);
      CHECK_MORE(); op->base = htoi(*argv++); --argc;
      ++actionsCounter;
      break;

    default:
      usage("Internal error: Bad operation type from '%s'", argv[-1]);
      break;
    }
  }

  // If no actions were specified, just identify the device.
  if (actionsCounter == 0) {
    checkDev();
    appendToOperationsList(IdentifyOp, "default identify operation");
  }
}


static char *opToName(OpType op) {

  for (int n = 0; n < NOPTS; ++n) {
    if (options[n].op == op) return options[n].lopt;
  }

  return "??? unknown op ???";
}


static void displayOp(struct Op *op) {
  fprintf(stderr, "%-10s: base=%08X  size=%08X  speed=%-9u  file='%s'\n",
          opToName(op->op),
          op->base,
          op->size,
          op->speed,
          op->nameP ? op->nameP : "");
}


// Send the specified SPI command to the device and read up to
// `respBufLen` bytes as a response, returning the actual read length
// or negative if error.
static int commandAndResponse(u8 *cmdBufP, u32 cmdLen, u8 *respBufP, u32 respBufLen) {
  struct spi_ioc_transfer msg[2];
  int st;

  msg[0].tx_buf = (u64)cmdBufP;
  msg[0].len = cmdLen;

  msg[1].rx_buf = (u64)respBufP;
  msg[1].len = respBufLen;

  st = ioctl(spiFD, respBufLen ? SPI_IOC_MESSAGE(2) : SPI_IOC_MESSAGE(1), msg);
  CHECKST(st, "commandAndResponse ioctl");
  return st;
}


#define GET1(P)  (*(P)++)

#define GET2(P)  ({ u16 _v = *(P)++;            \
    _v |= (u16) *(P)++ << 8;                  \
  })

#define GET3(P)  ({ u32 _v = *(P)++;            \
    _v |= (u32) *(P)++ << 8;                  \
    _v |= (u32) *(P)++ << 16;                 \
  })

#define GET4(P)  ({ u32 _v = *(P)++;            \
    _v |= (u32) *(P)++ << 8;                  \
    _v |= (u32) *(P)++ << 16;                 \
    _v |= (u32) *(P)++ << 24;                 \
  })


// Convert hex digit millivolts to floating point volts (as in VCC
// Supply Maximum/Minimum Voltage values in Macronix Flash parameter
// table from RDSFDP data.
static float hexMVtoV(u16 hex) {
  float v = 0.0;
  for (int k = 0; k < 16; k += 4) v = v * 10 + (float) ((hex >> k) & 0x0F);
  return v;
}


static void displayDeviceInfo(void) {
  u8 *p = ioBuf;

  u32 signature = GET4(p);
  u8 minorRev = GET1(p);
  u8 majorRev = GET1(p);
  (void) GET1(p);               /* Unused 07 */

  u8 jedecID = GET1(p);
  u8 jptMinorRev = GET1(p);
  u8 jptMajorRev = GET1(p);
  u8 jptLen = GET1(p);          /* In DWORDs */
  u32 jptPtr = GET3(p);
  (void) GET1(p);               /* Unused 0F */

  u8 mfgID = GET1(p);
  u8 mptMinorRev = GET1(p);
  u8 mptMajorRev = GET1(p);
  u8 mptLen = GET1(p);          /* In DWORDs */
  u32 mptPtr = GET3(p);
  (void) GET1(p);               /* Unused 17 */

  // JEDEC parameter table
  u8 b30 = GET1(p);
  u8 support4KErase = (b30 & 3) == 1;
  u8 writeGranularityGE64B = (b30 >> 2) & 1;
  u8 volatileSRWERequired = (b30 >> 3) & 1;
  u8 volatileSRWEOpcode = ((b30 >> 4) & 1) ? 0x06 : 0x05;

  u8 erase4KOpcode = GET1(p);

  u8 b32 = GET1(p);
  u8 fastRead112Support = b32 & 1;
  u8 address3ByteSupported = ((b32 >> 1) & 3) < 2;
  u8 address4ByteSupported = !!((b32 >> 1) & 2);
  u8 dtrClockingSupported = (b32 >> 3) & 1;
  u8 fastRead122Support = (b32 >> 4) & 1;
  u8 fastRead144Support = (b32 >> 5) & 1;
  u8 fastRead114Support = (b32 >> 6) & 1;

  (void) GET1(p);               /* Unused 33 */
  u32 density = GET4(p);

  u8 b38 = GET1(p);
  u8 fastRead144WS = b38 & 0x1F;
  u8 fastRead144ModeBits = b32 >> 5;

  u8 fastRead144Opcode = GET1(p);

  u8 b3a;
  u8 fastRead114WS = b3a & 0x1F;
  u8 fastRead114ModeBits = b3a >> 5;
  u8 fastRead114Opcode = GET1(p);

  u8 b3c;
  u8 fastRead112WS = b3c & 0x1F;
  u8 fastRead112ModeBits = b3c >> 5;
  u8 fastRead112Opcode = GET1(p);

  u8 b3e;
  u8 fastRead122WS = b3e & 0x1F;
  u8 fastRead122ModeBits = b3e >> 5;
  u8 fastRead122Opcode = GET1(p);

  u8 b40 = GET1(p);
  u8 fastRead222Supported = b40 & 1;
  u8 fastRead444Supported = (b40 >> 4) & 1;
  (void) GET3(p);               /* Unused 41-43 */
  (void) GET2(p);               /* Unused 44-45 */

  u8 b46;
  u8 fastRead222WS = b46 & 0x1F;
  u8 fastRead222ModeBits = b46 >> 5;
  u8 fastRead222Opcode = GET1(p);
  (void) GET2(p);               /* Unused 48-49 */

  u8 b4a;
  u8 fastRead444WS = b4a & 0x1F;
  u8 fastRead444ModeBits = b4a >> 5;
  u8 fastRead444Opcode = GET1(p);

  u8 b4c;
  u32 secType1Size = b4c ? 1u << b4c : 0;
  u8 secType1EraseOpcode = GET1(p);

  u8 b4e;
  u32 secType2Size = b4e ? 1u << b4e : 0;
  u8 secType2EraseOpcode = GET1(p);

  u8 b50;
  u32 secType3Size = b50 ? 1u << b50 : 0;
  u8 secType3EraseOpcode = GET1(p);

  u8 b52;
  u32 secType4Size = b52 ? 1u << b52 : 0;
  u8 secType4EraseOpcode = GET1(p);
  (void) GET1(p);               /* Unused 53 */
  (void) GET1(p);               /* Unused 54 */
  (void) GET1(p);               /* Unused 55 */
  (void) GET1(p);               /* Unused 56 */
  (void) GET1(p);               /* Unused 57 */
  (void) GET1(p);               /* Unused 58 */
  (void) GET1(p);               /* Unused 59 */
  (void) GET1(p);               /* Unused 5A */
  (void) GET1(p);               /* Unused 5B */
  (void) GET1(p);               /* Unused 5C */
  (void) GET1(p);               /* Unused 5D */
  (void) GET1(p);               /* Unused 5E */
  (void) GET1(p);               /* Unused 5F */

  // Macronix parameter table
  float vccMax = hexMVtoV(GET2(p));
  float vccMin = hexMVtoV(GET2(p));

  u16 b6564 = GET2(p);
  u8 hwResetPin = b6564 & 1;
  u8 hwHoldPin = (b6564 >> 1) & 1;
  u8 deepPowerDownSupported = (b6564 >> 2) & 1;
  u8 swResetSupported = (b6564 >> 3) & 1;
  u8 swResetOpcode = (b6564 >> 4) & 0xFF;
  u8 programSuspendResumeSupported = (b6564 >> 12) & 1;
  u8 EraseSuspendResumeSupported = (b6564 >> 13) & 1;
  u8 wrapAroundReadModeSupported = (b6564 >> 15) & 1;
  u8 wrapAroundReadModeOpcode = GET1(p);

  u8 b64 = GET1(p);
  u8 wrapAroundRead8Supported = b64 == 0x08 || b64 == 0x16 || b64 == 0x32 || b64 == 0x64;
  u8 wrapAroundRead16Supported = b64 == 0x16 || b64 == 0x32 || b64 == 0x64;
  u8 wrapAroundRead32Supported = b64 == 0x32 || b64 == 0x64;
  u8 wrapAroundRead64Supported = b64 == 0x64;

  u16 b6b68 = GET2(p);
  u8 individualBlockLockSupported = b6b68 & 1;
  u8 individualBlockLockNonvolatile = (b6b68 >> 1) & 1;
  u8 individualBlockLockOpcode = (b6b68 >> 2) & 0xFF;
  u8 securedOTPSupported = (b6b68 >> 11) & 1;
  u8 readLockSupported = (b6b68 >> 12) & 1;
  u8 permanentLockSupported = (b6b68 >> 13) & 1;
  (void) GET4(p);               /* Unused 6C-6F */

  if (verbose) {
  }

  fprintf(stderr, "\n");
}



static void executeOperations(void) {
  static u8 jedecSFDP[] = {
      0x5A, /* JEDEC READ SFDP command */
      0, 0, 0, /* 24b address */
      0,       /* 8b dummy */
  };


  for (int k = 0; k < nOps; ++k) {
    struct Op *op = &opsP[k];
    int len;

    if (verbose) displayOp(op);

    switch (op->op) {
    case SpeedOp:
      CHECKST(ioctl(spiFD, SPI_IOC_WR_MAX_SPEED_HZ, &op->speed), "ioctl SPI_IOC_WR_MAX_SPEED_HZ");
      break;

    case IdentifyOp:
      len = commandAndResponse(jedecSFDP, sizeof(jedecSFDP), ioBuf, sizeof(ioBuf));
      displayDeviceInfo();
      break;


    case EraseOp:
      break;


    case WriteOp:
      break;


    case VerifyOp:
      break;


    case ReadOp:
      break;

    default:                    /* Other commands do nothing in execute phase */
      break;
    }
  }
}


int main(int argc, char *argv[]) {
  progName = *argv++;
  --argc;
  parseCommandLine(argc, argv);
  if (!doNothing) executeOperations();
  return 0;
}

// Local Variables:
// mode: "C"
// c-style: "cc-mode"
// c-basic-offset: 2
// End:

