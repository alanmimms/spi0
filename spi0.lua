#!/usr/bin/env lua
local JEDEC = require "jedec"
local SPIOPS = require "spiops"

local MHz = 1000*1000
local SPISpeed = 25*MHz

local KB = 1024
local MB = KB*KB

-- MT25QL512ABB8ESF-0SIT specific?
local READ_ID = 0x9F
local READ_SFDP = 0x5A
local ENTER_4B_MODE, EXIT_4B_MODE = 0xB7, 0xE9
local READ_4B = 0x13
local BLOCK_ERASE = 0xDC       -- FOUR BYTE version of this command
local BULK_ERASE = 0xC7         -- This has 0xC7/0x60 in docs - which is it? Both? Either?
local READ_STATUS = 0x05
local READ_FLAG_STATUS = 0x70
local PROGRAM_PAGE_4B = 0x12
local WRITE_ENABLE = 0x06
local WRITE_DISABLE = 0x04


local DeviceSize = 64*MB
local BlockSize = 64*KB
local ProgramPageSize = 256
local SPIChunkSize = 2048

local devFD
local devInfo = {}
local fullDevRange = {bot=0, top=DeviceSize}
local devRange = fullDevRange

local bSTATUS = {
  writeDisabled = 0x80,
  bp = 0x5E,
  nvAtBottom = 0x20,
  writeEnabled = 0x02,
  busy = 0x01,
}

local bFLAG_STATUS = {
  notBusy = 0x80,
  eraseSuspend = 0x40,
  eraseFailure = 0x20,
  programFailure = 0x10,
  reserved = 0x08,
  programSuspend = 0x04,
  protectionError = 0x02,
  fourByteAddressing = 0x01,
}

local anyFlagStatusError = bFLAG_STATUS.eraseFailure | bFLAG_STATUS.programFailure | bFLAG_STATUS.protectionError;


-- Dump the specified string in hex and ASCII with address offsets in
-- blocks of 16 as has been done since the Big Bang.
function hexDump(s)
  local rowSize=16
  local k = 0
  local startK = 0
  local line = ""

  function dumpLine()
    local ascii = string.gsub(string.sub(s, startK+1, k+1), '[^%g ]', '?')
    ascii = string.sub(ascii, 1, 8) .. ' ' .. string.sub(ascii, 9, -1)
    print(string.format('%-' .. (6+rowSize*3) .. 's  |%' .. (rowSize+1) .. 's|', line, ascii))
  end

  while (k < #s) do

    if (k % rowSize == 0) then
      line = string.format("%04X:", k)
      startK = k
    end

    line = line .. string.format(" %02X", string.byte(s, k+1))
    if (k % rowSize == rowSize/2 - 1) then line = line .. " " end
    if (k % rowSize == rowSize - 1) then dumpLine() end
    k = k + 1
  end

  if (k % rowSize ~= 0) then dumpLine() end
end


function usage(msg)
  if not msg then msg = '' end
  io.stderr:write([[
Usage:
  `spi0` device ( [{bottom`..`top | base`+`size | `all`}]
                  {`read` [file] |
                   `verify` [file] |
                   `write` [file] |
                   `erase`,
                   `sfdp`} )+

device (e.g., /dev/spidev0.0) must be specified.

If present, the optional range specifies a subset of bytes in the
device to operate on starting at `base` or `bottom` through `top`-1 or
`base`+`size`-1. If the range is not a multiple of the device's erase
block size it is an error and the operation will not be done. Once a
range is specified it applies to all subsequent operations on the
command line. Using the keyword `all` implies the range should
encompass the full device, which is also the initial default.

Values for `bottom`, `top`, `base`, and `size` are

  [`0x`] digits+ {empty | `K` | `M`}

If `0x` is present the digits are interpreted as hexadecimal and
otherwise as decimal. If `K` (1024) or `M` (1024*1024) are present
they are interepreted as multipliers for the value.

The `top` or `size` may be specified as the keyword `end` to indicate
the range extends to the end of the device.

Any number of operations can be specified. All operations do only what
they say. You must usually erase and region before you can write it.

The `sfdp` operation reads the device's JEDEC "Serial Flash
Discoverable Parameter" region and dumps (512 bytes of) it.

For example:

  To erase the first 64K of the device on /dev/spidev0.0 use
    spi0 /dev/spidev0.0 0..64K erase

  To erase the region from 256K to 256K+64K use
    spi0 /dev/spidev0.0 256K+64K erase

  To erase the entire device use
    spi0 /dev/spidev0.0 all erase
  (or `all` may be left off since it is the default.)

  To erase the region and then write the contents of kernel.img (up to
  6MB in size) to the device starting at 4MB use
    spi0/dev/spidev0.0 4M+6M erase write kernel.img

  To read the first, third, and fifth megabytes of the device and save them to files use
    spi0/dev/spidev0.0 1M+1M read first.img 3M+1M read third.img 5M+1M read fifth.img

]], msg, '\n')
  os.exit(false, true)
end


function openChecked(f, mode)
  local op = 'reading'
  if mode:sub(1, 1) == 'w' then op = 'writing' end
  local f = io.open(f, mode)
  if not f then usage('Cannot open "' .. f .. ' for ' .. op) end
  return f
end


function enableWrite(devFD)
  SPIOPS.doCommand(devFD, string.pack('>B', WRITE_ENABLE), 0)
end


function disableWrite(devFD)
  SPIOPS.doCommand(devFD, string.pack('>B', WRITE_DISABLE), 0)
end


function readStatus()
  local statusBuf = SPIOPS.doCommand(devFD, string.pack('>B', READ_STATUS), 1)
  return statusBuf:byte(1, 1)
end


function readFlagStatus()
  local statusBuf = SPIOPS.doCommand(devFD, string.pack('>B', READ_FLAG_STATUS), 1)
  return statusBuf:byte(1, 1)
end


function waitIdle(devFD, usToWait)
  local status, flagStatus

  while (true) do
    status = readStatus()
    flagStatus = readFlagStatus()

    if (flagStatus & anyFlagStatusError) ~= 0 then
      print(string.format('FlagStatus register indicates error: %02X', flagStatus))
      break
    end

    if (status & bSTATUS.busy) == 0 then break end

    if usToWait >= 100*1000 then
      SPIOPS.usSleep(usToWait)
      io.write('.')
    end
  end

  return status, flagStatus
end


-- Return a human readable version of the specified device range.
function rangeString(range)
  return string.format('0x%X..0x%X', range.bot, range.top)
end


function doRead()
  local msg = 'Read operation requires a valid filename to write to'
  local file = table.remove(arg, 1) or usage(msg)
  local f = openChecked(file, "wb") or usage(msg)
  local prevMB = -1
  local addr

  io.stdout:setvbuf('no')
  print(string.format('Reading range %s dumping to file "%s"', rangeString(devRange), file))

  for addr = devRange.bot, devRange.top - 1, SPIChunkSize do

    if addr // MB ~= prevMB then
      prevMB = addr // MB
      local eol = (prevMB % 16 == 15) and '\n' or ''
      io.write(string.format("%3dMB%s", prevMB, eol))
    end

    local readBuf = SPIOPS.doCommand(devFD, string.pack(">BI4", READ_4B, addr), SPIChunkSize)
    f:write(readBuf)
  end

  f:close()
  io.write('\n')
  io.stdout:setvbuf('line')
end


function doWrite()
  local msg = 'Write operation requires a valid filename to read image from'
  local file = table.remove(arg, 1) or usage(msg)
  local prevMB = -1
  local addr

  local f = openChecked(file, "rb") or usage(msg)
  local fileSize = f:seek("end")
  f:seek("set", 0)              -- Rewind back to start of file

  io.stdout:setvbuf('no')
  print(string.format('Writing range %s from file "%s" (%d bytes)', rangeString(devRange), file, fileSize))

  for addr = devRange.bot, devRange.top - 1, ProgramPageSize do
    enableWrite(devFD)

    if addr // MB ~= prevMB then
      prevMB = addr // MB
      local eol = (prevMB % 16 == 15) and '\n' or ''
      io.write(string.format("%3dMB%s", prevMB, eol))
    end

    local fileBuf = f:read(ProgramPageSize)
    if not fileBuf then break end               -- Exit verification loop on EOF
    local size = #fileBuf
    SPIOPS.doCommand(devFD, string.pack(">BI4", PROGRAM_PAGE_4B, addr) .. fileBuf, 0)

    local status, flagStatus = waitIdle(devFD, 50)

    if (flagStatus & anyFlagStatusError) ~= 0 then
      print(string.format('\nProgram operation terminated in error - Flag Status Reg=%02X', flagStatus))
      break
    end
  end

  f:close()
  disableWrite(devFD)
  io.write('\n')
  io.stdout:setvbuf('line')
end


function doVerify()
  local msg = 'Verify operation requires a valid filename to compare to'
  local file = table.remove(arg, 1) or usage(msg)
  local f = openChecked(file, "rb") or usage(msg)
  local prevMB = -1
  local addr

  io.stdout:setvbuf('no')
  print(string.format('Verifying range %s against file "%s"', rangeString(devRange), file))

  for addr = devRange.bot, devRange.top - 1, SPIChunkSize do

    if addr // MB ~= prevMB then
      prevMB = addr // MB
      local eol = (prevMB % 16 == 15) and '\n' or ''
      io.write(string.format("%3dMB%s", prevMB, eol))
    end

    local fileBuf = f:read(SPIChunkSize)
    if not fileBuf then break end               -- Exit verification loop on EOF
    local size = #fileBuf
    local readBuf = SPIOPS.doCommand(devFD, string.pack(">BI4", READ_4B, addr), size)

    -- If fast compare says they are not identical drill down and find
    -- out where mismatch lies.
    if readBuf ~= dataBuf then

      -- Otherwise find the mismatch
      local k
      for k = 1, size do
        local devByte = readBuf:sub(k, k):byte()
        local fileByte = fileBuf:sub(k, k):byte()

        if devByte ~= fileByte then
          io.stderr:write(string.format('\nDevice verify mismatch at 0x%X: was 0x%02X, should be 0x%02X\n',
              addr, devByte, fileByte))
        end
      end
    end
  end

  f:close()
  io.write('\n')
  print('Device verification successful')
  io.stdout:setvbuf('line')
end


function doErase()
  local status, flagStatus

  enableWrite(devFD)
  io.stdout:setvbuf('no')

  if devRange.top - devRange.bot == DeviceSize then
    print('Erasing entire device')
    SPIOPS.doCommand(devFD, string.pack('>B', BULK_ERASE), 0)        -- Start bulk erase operation
    status, flagStatus = waitIdle(devFD, 500*1000)
  else
    local addr
    local prevMB = -1

    print('Erasing range ' .. rangeString(devRange))

    for addr = devRange.bot, devRange.top - 1, BlockSize do
      if addr // MB ~= prevMB then
        prevMB = addr // MB
        local eol = (prevMB % 16 == 15) and '\n' or ''
        io.write(string.format("%3dMB%s", prevMB, eol))
      end

      SPIOPS.doCommand(devFD, string.pack('>BI4', BLOCK_ERASE, addr), 0)        -- Start block erase operation
      status, flagStatus = waitIdle(devFD, 50)
      print(string.format('status=%02X flagStatus=%02X', status, flagStatus))

      if (flagStatus & anyFlagStatusError) ~= 0 then
        print(string.format('ERROR: status=%02X flagStatus=%02X', status, flagStatus))
        os.exit(false, true)
      end
    end
  end

  disableWrite(devFD)
  io.write('\n')
  io.stdout:setvbuf('line')
  print(string.format('Ending Flag Status Reg=%02X (i.e., success)', flagStatus))
end


-- This eats the device name from the `arg` list and validates it for use.
function setupDevice()
  local device = table.remove(arg, 1) or usage('Device name must be specified')
  devFD = SPIOPS.doOpen(device) or usage('Error opening device "' .. device .. '" for SPI operations.')

  SPIOPS.setMode(devFD, 0)
  SPIOPS.setBPW(devFD, 8)
  SPIOPS.setSpeed(devFD, SPISpeed)
  SPIOPS.doCommand(devFD, string.pack('>B', ENTER_4B_MODE), 0)

  local manufacturers = {
    [0x20] = 'Micron',
    [0xC2] = 'Macronix',
  }

  local mfgTypes = {
    [0x201A] = 'MX25L51245G 3V 512Mb',
    [0xBA22] = '3V 2Gb',
    [0xBA21] = '3V 1Gb',
    [0xBA20] = '3V 512Mb',
    [0xBA19] = '3V 256Mb',
    [0xBA18] = '3V 128Mb',
    [0xBA17] = '3V 64Mb',
    [0xBB22] = '1.8V 2Gb',
    [0xBB21] = '1.8V 1Gb',
    [0xBB20] = '1.8V 512Mb',
    [0xBB19] = '1.8V 256Mb',
    [0xBB18] = '1.8V 128Mb',
    [0xBB17] = '1.8V 64Mb',
  }
  
  local readIDBuf = SPIOPS.doCommand(devFD, string.pack('>B', READ_ID), 20)
  local typeMSB, typeLSB
  devInfo.mfg, typeMSB, typeLSB = readIDBuf:byte(1, 3)
  devInfo.mfgString = manufacturers[devInfo.mfg] or '?'
  devInfo.mfgType = (typeMSB << 8) | typeLSB
  devInfo.mfgTypeString = mfgTypes[devInfo.mfgType] or '?'
  print(string.format("Read ID: Manufacturer='%s'(%02X) type='%s'(%04X)",
      devInfo.mfgString, devInfo.mfg, devInfo.mfgTypeString, devInfo.mfgType))
  hexDump(readIDBuf)

  if (readIDBuf:byte(5) & 0x03) ~= 0x00 then
    usage('Extended Device Data says block size is not uniform 64K which is all this program supports right now.')
  end

  devRange = fullDevRange
end



-- A simple table-driven switch statement. The value `op` is used to
-- index a table of `cases` where a specially named element called
-- `default` is used if no case matches. The table contains a
-- function(op, p2) whose return value is returned by the switch
-- "statement".
function switch(op, cases, p2)
  local case = cases[op] or cases.default or function() return nil end
  return case(op, p2)
end


-- Convert a range numerical value to a number of bytes. If `v` is
-- `end` returns DeviceSize.
function fixRangeSize(v)
  if v == 'end' then return DeviceSize end

  local digits, unit = v:match('(0[xX]%x+)([kKmM]?)')
  if not digits then digits, unit =v:match('(%d+)([kKmM]?)') end
  if not digits then usage('Unrecognized number syntax in range "' .. v ..'"') end

  local multiplier = {k=KB, K=KB, m=MB, M=MB}
  return tonumber(digits) * (unit and multiplier[unit] or 1)
end


-- Handle range specifications. Since this is called by the default
-- switch case in the arg loop below the range is already gobbled and
-- we have no additional parameters to gobble and check.
function doRange(range)
  if range == 'all' then
    devRange = fullDevRange
    return devRange
  end

  -- If we get here it's either a base..top or an base+size style range.
  local baseString, op, endString = range:match('(%w+)([%.%+]+)(%w+)')
  if not baseString or not op or not endString then
    usage('Unrecognized operation or bad range specification syntax "' .. range .. '"')
  end
  
  local base = fixRangeSize(baseString)
  local top =
    (op == '+') and (base + math.min(DeviceSize - base, fixRangeSize(endString))) or
    (op == '..') and (math.min(DeviceSize, fixRangeSize(endString))) or
    usage('Unrecognized syntax for range "' .. range .. '"')

  devRange = {bot=base, top=top}
  return devRange
end


function doSFDP()
  local sfdpBuf = SPIOPS.doCommand(devFD, string.pack('>BI4', READ_SFDP, 0), 512)
  print('SFDP data:')
  hexDump(sfdpBuf)

end


setupDevice()

-- Walk the command line parmaeters, gobbling the next keyword as we
-- go and dispatching to the handler for the keyword which must gobble
-- (and checks validity of) any parameters it requires.
while true do
  local op = table.remove(arg, 1)
  if not op then break end

  switch(op, {
      read = doRead,
      write = doWrite,
      erase = doErase,
      verify = doVerify,
      sfdp = doSFDP,
      default = doRange,
  })
end

SPIOPS.doClose(devFD)
