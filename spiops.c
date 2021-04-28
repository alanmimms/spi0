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

#include "lua.h"
#include "lauxlib.h"

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


static int doOpen(lua_State *L) {
  int n = lua_gettop(L);
  if (n != 1) luaL_error(L, "doOpen requires exactly one string parameter: the device pathname");
  const char *path = lua_tostring(L, 1);
  int fd = open(path, O_RDWR);  /* Do the open */
  lua_pushinteger(L, fd);        /* Push resulting file descriptor number or -1 for error */
  return 1;
}


static int doClose(lua_State *L) {
  int n = lua_gettop(L);
  if (n != 1) luaL_error(L, "doClose requires exactly one numeric parameter: the file handle");
  const int fd = lua_tointeger(L, 1);
  close(fd);
  return 0;
}


static int doCommand(lua_State *L) {
  struct spi_ioc_transfer msg[2];

  if (lua_gettop(L) != 3) luaL_error(L, "doCommand requires: integer fileHandle, string txBuf, integer rxLength");
  int fd = lua_tointeger(L, 1);
  size_t txLen;
  const void *txBuf = lua_tolstring(L, 2, &txLen);
  size_t rxLen = lua_tointeger(L, 3);

  void *rxBuf = calloc(rxLen + 1, 1);
  int st;

  if (!rxBuf) luaL_error(L, "doCommand failed to allocate space for requested rx length");

  bzero(msg, sizeof(msg));
  msg[0].tx_buf = (u64) txBuf;
  msg[0].len = txLen;
  msg[1].rx_buf = (u64) rxBuf;
  msg[1].len = rxLen;

  st = ioctl(fd, rxLen == 0 ? SPI_IOC_MESSAGE(1) : SPI_IOC_MESSAGE(2), msg);
  if (st < 0) luaL_error(L, "bad ioctl() return status from SPI operation");
//  fprintf(stderr, "doCommand rxLen=%d, msg[1].len, st=%d\n", rxLen, msg[1].len, st);

  lua_pushlstring(L, rxBuf, st - txLen);
  free(rxBuf);
  return 1;
}


static int setMode(lua_State *L) {
  int n = lua_gettop(L);
  if (n != 2) luaL_error(L, "setMode requires exactly two parameters: integer fileHandled, integer theMode");
  int fd = lua_tointeger(L, 1);
  u8 mode = lua_tointeger(L, 2);
  int st = ioctl(fd, SPI_IOC_WR_MODE, &mode);
  if (st < 0) luaL_error(L, "bad ioctl() return status from SPI setMode operation");
  return 0;
}


static int setEndian(lua_State *L) {
  int n = lua_gettop(L);
  if (n != 2) luaL_error(L, "setEndian requires exactly two parameters: integer fileHandled, boolean useLSBfirst");
  int fd = lua_tointeger(L, 1);
  u8 lsbFirst = lua_toboolean(L, 2);
  int st = ioctl(fd, SPI_IOC_WR_LSB_FIRST, &lsbFirst);
  if (st < 0) luaL_error(L, "bad ioctl() return status from SPI setEndian operation");
  return 0;
}


static int setBPW(lua_State *L) {
  int n = lua_gettop(L);
  if (n != 2) luaL_error(L, "setBPW requires exactly two parameters: integer fileHandled, integer bitsPerWord");
  int fd = lua_tointeger(L, 1);
  u8 bpw = lua_tointeger(L, 2);
  int st = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bpw);
  if (st < 0) luaL_error(L, "bad ioctl() return status from SPI setBPW operation");
  return 0;
}


static int setSpeed(lua_State *L) {
  int n = lua_gettop(L);
  if (n != 2) luaL_error(L, "setSpeed requires exactly two parameters: integer fileHandled, integer spiBusSpeedInHz");
  int fd = lua_tointeger(L, 1);
  u32 speed = lua_tointeger(L, 2);
  int st = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
  if (st < 0) luaL_error(L, "bad ioctl() return status from SPI setSpeed operation");
  return 0;
}


static const struct luaL_Reg spiops[] = {
  {"doOpen", doOpen},           /* doOpen(pathNameToOpen) returns file handle for subsequent calls */
  {"doClose", doClose},         /* doClose(fileHandle) */
  {"doCommand", doCommand},     /* doCommand(fileHandle, txBuf, rxLen) returns received bytes as a string */
  {"setMode", setMode},         /* setMode(fileHandle, modeNumber) */
  {"setEndian", setEndian},     /* setEndian(fileHandle, lsbFirstBoolean) */
  {"setBPW", setBPW},           /* setBPW(fileHandle, bitsPerWord) */
  {"setSpeed", setSpeed},       /* setSpeed(fileHandle, spiBusSpeedInHz) */
  {NULL, NULL},                 /* End of list */
};


int luaopen_spiops(lua_State *L) {
  luaL_newlib(L, spiops);
  return 1;
}

// Local Variables:
// mode: "C"
// c-style: "cc-mode"
// c-basic-offset: 2
// End:
