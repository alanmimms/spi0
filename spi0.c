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
#include <assert.h>
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


// Speed of SPI bus in Hz
static u32 spiSpeed = 1*1000*1000;


// File descriptor for the specified device.
static int spiFD = -1;


// Max buffer size we can use with /dev/spidev module
#define MAX_BUF_SIZE   4096


// I/O buffer we use for SPI interactions.
static u8 ioBuf[MAX_BUF_SIZE/2];


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
      spiSpeed = op->speed = strtoul(*argv++, NULL, 10); --argc;
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
// or negative if error. If `respBufLen` is zero the operation sends
// data but doesn't expect a reply.
static int commandAndResponse(u8 *cmdBufP, u32 cmdLen, u8 *respBufP, u32 respBufLen) {
  struct spi_ioc_transfer msg[2];
  int st;

  bzero(msg, sizeof(msg));
  msg[0].tx_buf = (u64)cmdBufP;
  msg[0].len = cmdLen;
  msg[0].speed_hz = spiSpeed;

  msg[1].rx_buf = (u64)respBufP;
  msg[1].len = respBufLen;
  msg[1].speed_hz = spiSpeed;

  st = ioctl(spiFD, respBufLen ? SPI_IOC_MESSAGE(2) : SPI_IOC_MESSAGE(1), msg);
  CHECKST(st, "commandAndResponse ioctl");
  return st;
}


// Like strcat(destP, toAppendP) except it returns the pointer to the
// NUL it sticks onto the end of the destination string so you can
// continue appending there.
static char *strappend(char *destP, char *toAppendP) {
  return strcat(destP, toAppendP) + strlen(toAppendP);
}


// Dump the specified byte array in hex in "standard" format.
#define DUMP_LINE_BYTES 16
static void dumpHex(u8 *bufP, u32 size) {

  for (int k = 0; k < size; ++k) {
    if (k % DUMP_LINE_BYTES == 0) fprintf(stderr, "%s%06X:", k > 0 ? "\n" : "", k);
    if (k % DUMP_LINE_BYTES == DUMP_LINE_BYTES/2) fprintf(stderr, " ");
    fprintf(stderr, " %02X", bufP[k]);
    if (k % 4 == 3) fprintf(stderr, " ");
  }

  fprintf(stderr, "\n");
}


struct FormattableListElement {
  char *descP;
  int flag;
};


// For each element append the description string if the flag is true.
// All appended items in the list are separated by "," as in normal
// English usage.
static char *formatList(struct FormattableListElement elems[]) {
  static char formatBuf[4096];
  char *p = formatBuf;
  *p = 0;

  for (int k = 0; elems[k].descP; ++k) {

    if (elems[k].flag) {
      if (p != formatBuf) p = strappend(p, ", ");
      p = strappend(p, elems[k].descP);
    }
  }

  return formatBuf;
}


#define MKLIST(L...)       formatList((struct FormattableListElement []) {L, {NULL, 0}})


#define GET1(P)  (*(P)++)

#define GET2(P)  ({ u16 _v = *(P)++;            \
    _v |= (u16) *(P)++ << 8;                    \
  })

#define GET3(P)  ({ u32 _v = *(P)++;            \
    _v |= (u32) *(P)++ << 8;                    \
    _v |= (u32) *(P)++ << 16;                   \
  })

#define GET4(P)  ({ u32 _v = *(P)++;            \
    _v |= (u32) *(P)++ << 8;                    \
    _v |= (u32) *(P)++ << 16;                   \
    _v |= (u32) *(P)++ << 24;                   \
  })


// Convert hex digit millivolts to floating point volts (as in VCC
// Supply Maximum/Minimum Voltage values in Macronix Flash parameter
// table from RDSFDP data.
static float hexMVtoV(u16 v) {
  return
    1.0 * ((v >> 12) & 0x0F) +
    0.1 * ((v >> 8) & 0x0F) +
    0.01 * ((v >> 4) & 0x0F) +
    0.001 * ((v >> 0) & 0x0F);
}


struct FastRead {
  u8 ws;
  u8 modeBits;
  u8 opcode;
};


static u8 *getFastRead(char *nameP, struct FastRead *frP, u8 *p) {
  u8 b = GET1(p);
  frP->ws = b & 0x1F;
  frP->modeBits = b >> 5;
  frP->opcode = GET1(p);
  return p;
}


static void formatFastRead(char *nameP, struct FastRead *frP) {
  fprintf(stderr, "JEDEC fast read %s: %d wait states, mode bits %ssupported, %02X opcode\n",
          nameP, frP->ws, frP->modeBits ? "" : "not ", frP->opcode);
}


// Dump the contents of a JEDEC standard parameter table header at `p`.
static void dumpParamHeader(u8 *p, int n) {
  u16 id = GET1(p);
  u8 minor = GET1(p);
  u8 major = GET1(p);
  u8 length = GET1(p);
  u32 ptp = GET3(p);
  u8 idMSB = GET1(p);           /* JEDEC JESD216 rev A added second byte of vendor ID */
  fprintf(stderr, "Parameter table #%d id %02X%02X (%02X.%02X) %02X dwords at offset %06X\n",
          n, idMSB, id, major, minor, length, ptp);
}


#pragma scalar_storage_order little-endian
struct JEDECSFDPHeader {
  unsigned signature;           /* 0x50444653 = Little endian 'S','F','D','P' */
  unsigned minor: 8;
  unsigned major: 8;
  unsigned nph: 8;
  unsigned accessProtocol: 8;
};


struct JEDECBasicFlashParameterHeader {
  unsigned idLSB: 8;
  unsigned minor: 8;
  unsigned major: 8;
  unsigned dwords: 8;

  unsigned ptp: 24;
  unsigned idMSB: 8;
};


struct JEDECBasicFlashHeader {
  unsigned legacyEraseSizes: 2; /* 00: reserved, 01: 4KB supported, 10: reserved, 11: 4KB not supported */
  unsigned writeBufferGE64B: 1;
  unsigned blockProtectAlwaysVolatile: 1;
  unsigned use06WE: 1;          /* Legacy: 0x50 vs 0x06 for write enable */
};


#pragma scalar_storage_order default



static void displayDeviceInfo(void) {
  u8 *p = ioBuf;
#define CHECKOFFSET(SB) assert(p - ioBuf == (SB))

  u32 signature = GET4(p);
  u8 minorRev = GET1(p);
  u8 majorRev = GET1(p);
  u8 nParamHeaders = GET1(p) + 1;
  (void) GET1(p);               /* Unused 07 */

  CHECKOFFSET(0x08);
  u8 jedecID = GET1(p);
  u8 jptMinorRev = GET1(p);
  u8 jptMajorRev = GET1(p);
  u8 jptLen = GET1(p);          /* In DWORDs */
  u32 jptPtr = GET3(p);
  (void) GET1(p);               /* Unused 0F */

  CHECKOFFSET(0x10);
  u8 mfgID = GET1(p);
  u8 mptMinorRev = GET1(p);
  u8 mptMajorRev = GET1(p);
  u8 mptLen = GET1(p);          /* In DWORDs */
  u32 mptPtr = GET3(p);
  (void) GET1(p);               /* Unused 17 */

  // JEDEC parameter table
  p = ioBuf + jptPtr;
  CHECKOFFSET(jptPtr);
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

  struct FastRead fastRead144, fastRead114, fastRead112, fastRead122, fastRead222, fastRead444;
  p = getFastRead("1-4-4", &fastRead144, p);
  p = getFastRead("1-1-4", &fastRead114, p);
  p = getFastRead("1-1-2", &fastRead112, p);
  p = getFastRead("1-2-2", &fastRead122, p);

  CHECKOFFSET(0x40);
  u8 b40 = GET1(p);
  u8 fastRead222Supported = b40 & 1;
  u8 fastRead444Supported = (b40 >> 4) & 1;
  (void) GET3(p);               /* Unused 41-43 */
  (void) GET2(p);               /* Unused 44-45 */

  CHECKOFFSET(0x46);
  p = getFastRead("2-2-2", &fastRead222, p);
  (void) GET2(p);               /* Unused 48-49 */
  p = getFastRead("4-4-4", &fastRead444, p);

  CHECKOFFSET(0x4C);
  u8 b4c = GET1(p);
  u32 secType1Size = b4c ? 1u << b4c : 0;
  u8 secType1EraseOpcode = GET1(p);

  u8 b4e = GET1(p);
  u32 secType2Size = b4e ? 1u << b4e : 0;
  u8 secType2EraseOpcode = GET1(p);

  u8 b50 = GET1(p);
  u32 secType3Size = b50 ? 1u << b50 : 0;
  u8 secType3EraseOpcode = GET1(p);

  u8 b52 = GET1(p);
  u32 secType4Size = b52 ? 1u << b52 : 0;
  u8 secType4EraseOpcode = GET1(p);

  // Macronix parameter table
  p = ioBuf + mptPtr;
  CHECKOFFSET(mptPtr);
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
    fprintf(stderr, "\n");
    dumpHex(ioBuf, 512);
    fprintf(stderr, "\n");
    fprintf(stderr, "SFDP %02X.%02X signature=%08X with %d parameter headers\n", majorRev, minorRev, signature, nParamHeaders);
    for (int ptn = 0; ptn < nParamHeaders; ++ptn) dumpParamHeader(ioBuf + 8 + ptn * 8, ptn);

    fprintf(stderr, "\n");
    fprintf(stderr, "JEDEC block/sector 4KB erase %ssupported\n", support4KErase ? "" : "not ");
    fprintf(stderr, "JEDEC write granularity %s\n", writeGranularityGE64B ? ">= 64B:" : "1B");
    fprintf(stderr, "JEDEC volatile WE opcode to write volatile SRs is %s\n",
            volatileSRWERequired ? (volatileSRWEOpcode ? "06" : "50") : "not required");
    fprintf(stderr, "JEDEC 4KB Erase opcode %02X\n", erase4KOpcode);
    fprintf(stderr, "JEDEC support for %s\n",
            MKLIST({"1-1-2 fast read", fastRead112Support},
                   {"1-1-4 fast read", fastRead114Support},
                   {"1-2-2 fast read", fastRead122Support},
                   {"1-4-4 fast read", fastRead144Support},
                   {"3BA", address3ByteSupported},
                   {"4BA", address4ByteSupported}));
    fprintf(stderr, "JEDEC flash memory density %08X (%gMb)\n", density, (float) (density+1) / 1024.0/1024.0);
    formatFastRead("1-4-4", &fastRead144);
    formatFastRead("1-1-4", &fastRead114);
    formatFastRead("1-1-2", &fastRead112);
    formatFastRead("1-2-2", &fastRead122);
    formatFastRead("2-2-2", &fastRead222);
    formatFastRead("4-4-4", &fastRead444);

    fprintf(stderr, "JEDEC Sector Type 1 size %08X opcode %02X\n", secType1Size, secType1EraseOpcode);
    fprintf(stderr, "JEDEC Sector Type 2 size %08X opcode %02X\n", secType2Size, secType2EraseOpcode);
    fprintf(stderr, "JEDEC Sector Type 3 size %08X opcode %02X\n", secType3Size, secType3EraseOpcode);
    fprintf(stderr, "JEDEC Sector Type 4 size %08X opcode %02X\n", secType4Size, secType4EraseOpcode);

    // Macronix parameter table
    fprintf(stderr, "\n");
    fprintf(stderr, "Vendor VCC range %.2fV to %.2fV\n", vccMin, vccMax);

#if 0
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
#endif

  }

  fprintf(stderr, "\n");
}



static void executeOperations(void) {
  static u8 jedecSFDP[] = {
      0x5A,        /* JEDEC JESD216D.01 READ SFDP command */
      0, 0, 0,     /* 24b address */
      0,           /* 8b dummy */
  };

  u8 mode = SPI_MODE_0;
  u8 bits = 8;

  CHECKST(ioctl(spiFD, SPI_IOC_WR_MODE, &mode), "ioctl SPI_IOC_WR_MODE");
  CHECKST(ioctl(spiFD, SPI_IOC_WR_BITS_PER_WORD, &bits), "ioctl SPI_IOC_WR_BITS_PER_WORD");

  for (int k = 0; k < nOps; ++k) {
    struct Op *op = &opsP[k];
    int len;

    if (verbose) displayOp(op);

    switch (op->op) {
    case SpeedOp:
      CHECKST(ioctl(spiFD, SPI_IOC_WR_MAX_SPEED_HZ, &op->speed), "ioctl SPI_IOC_WR_MAX_SPEED_HZ");
      spiSpeed = op->speed;
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

