#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
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


typedef enum {EraseOp, WriteOp, VerifyOp, ReadOp, HelpOp, DeviceOp} OpType;
#define DEFOPTS                                                                                           \
  DEF1OPT("h", "help",    "                     | This help.", HelpOp)                                    \
  DEF1OPT("d", "device",  "pathname             | /dev/spidevX.Y device pathname", DeviceOp)              \
  DEF1OPT("E", "erase",   "base size            | Erase device range", EraseOp)                           \
  DEF1OPT("W", "write",   "filename base        | Write file to device at base", WriteOp)                 \
  DEF1OPT("V", "verify",  "filename base        | Verify device contains file content at base", VerifyOp) \
  DEF1OPT("R", "read",    "filename base size   | Read device content to file", ReadOp)

  
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


// File descriptor for the specified device.
static int spiFD = -1;


// Max buffer size we can use with /dev/spidev module
#define MAX_BUF_SIZE   4096


static const u8 jedecSFDP[] = {
  0x5A,                         /* JEDEC READ SFDP command */
  0, 0, 0,                      /* 24b address */
  0,                            /* 8b dummy */
};


// All of these XxxOp structs must start with an `op` member which is
// then followed by the op-specific parameters.
struct EraseOp {
  OpType op;
  u32 base;                     /* Base offset in device to erase */
  u32 size;                     /* Size to erase */
};


struct WriteOp {
  OpType op;
  u8 *imageP;                   /* Actual CONTENT of file */
  u32 size;                     /* Size of file */
};


struct VerifyOp {
  OpType op;
  u8 *imageP;                   /* Actual CONTENT of file */
  u32 size;                     /* Size of file */
};

struct ReadOp {
  OpType op;
  char *fileNameP;              /* Name of file */
  u32 base;                     /* Base offset in device to read image from */
  u32 size;                     /* Size to read */
};


// For each operation we're asked to do there is an element in this
// array.
static unsigned nOps = 0;
static struct AnyOp {
  OpType op;
} **ops;


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
  char *nameP;
  u32 base;
  u32 size;

  // Parse our command line parameters
  while (argc > 0) {

    if (argv[0][0] == '-' && argv[0][1] == '-') { /* Long option */

      for (k = 0; k < NOPTS; ++k) {
        if (strcmp(options[k].lopt, argv[0] + 2) == 0) goto GotOption;
      }

      usage("Unknown long option '--%s'", argv[0]); 
    } else if (argv[0][0] == '-') {               /* Short option */

      for (k = 0; k < NOPTS; ++k) {
        if (strcmp(options[k].sopt, argv[0] + 1) == 0) goto GotOption;
      }

      usage("Unknown short option '-%s'", argv[0]); 
    } else {                                      /* Invalid command line option */
      usage("Bad option '%s'", argv[0]);
    }


  GotOption:
    /* Consume option name argv element but note it for error messages */
    curOpt = *argv++; --argc;

    switch (options[k].op) {
      //  DEF1OPT("h", "help", "This help", HelpOp)
    case HelpOp:
      usage(NULL, NULL);
      break;

      //  DEF1OPT("d", "device", "/dev/spidevX.Y device pathname", DeviceOp)
    case DeviceOp:
      nameP = *argv++; --argc;
      spiFD = open(nameP, O_RDWR);
      if (spiFD < 0) usage("Unable to open device '%s' for read/write", nameP);
      break;

      //  DEF1OPT("E", "erase", "Erase device range (base,size in hex)", EraseOp)
    case EraseOp:
      checkDev();
      CHECK_MORE(); base = htoi(*argv++); --argc;
      CHECK_MORE(); size = htoi(*argv++); --argc;
      break;

      //  DEF1OPT("W", "write", "Write file to device at base (file,base in hex)", WriteOp)
    case WriteOp:
      checkDev();
      CHECK_MORE(); nameP = *argv++; --argc;
      CHECK_MORE(); base = htoi(*argv++); --argc;
      break;

      //  DEF1OPT("R", "read", "Read device range to file (file,base,size in hex)", ReadOp)
    case ReadOp:
      checkDev();
      CHECK_MORE(); nameP = *argv++; --argc;
      CHECK_MORE(); base = htoi(*argv++); --argc;
      CHECK_MORE(); size = htoi(*argv++); --argc;
      break;

      //  DEF1OPT("V", "verify", "Verify device contains file content at base (file,base in hex)", VerifyOp)
    case VerifyOp:
      checkDev();
      CHECK_MORE(); nameP = *argv++; --argc;
      CHECK_MORE(); base = htoi(*argv++); --argc;
      break;

    default:
      usage("Internal error: Bad operation type from '%s'", argv[-1]);
      break;
    }
  }
}



int main(int argc, char *argv[]) {
  progName = *argv++;
  --argc;
  parseCommandLine(argc, argv);
  return 0;
}
