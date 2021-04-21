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
  return 1;
}


static int setMode(lua_State *L) {
  return 0;
}


static int setEndian(lua_State *L) {
  return 0;
}


static int setBPW(lua_State *L) {
  return 0;
}


static int setSpeed(lua_State *L) {
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
