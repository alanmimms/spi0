#!/usr/bin/env lua
local JEDEC = require "jedec"
local SPIOPS = require "spiops"
local posix = require "posix"

local MHz = 1000*1000
local SPISpeed = 25*MHz

local KB = 1024
local MB = KB*KB

-- MT25QL512ABB8ESF-0SIT specific?
local READ_ID = 0x9E
local ENTER_4B_MODE, EXIT_4B_MODE = 0xB7, 0xE9
local READ_4B = 0x13
local SECTOR_ERASE = 0xD8
local BULK_ERASE = 0xC7         -- This has 0xC7/0x60 in docs - which is it? Both? Either?
local READ_FLAG_STATUS = 0x70
local PROGRAM_PAGE_4B = 0x12


local DeviceSize = 64*MB
local SectorSize = 64*KB
local ProgramPageSize = 256
local SPIChunkSize = 2048

local devFD
local devInfo = {}
local devRange


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

local anyError = bFLAG_STATUS.eraseFailure | bFLAG_STATUS.programFailure | bFLAG_STATUS.protectionError;


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
                  {`read` [file] | `verify` [file] | `write` [file] | `erase`} )+

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


function doWriteEnableDisable(devFD, enable)
  local cmd = enable and 0x06 or 0x04
  SPIOPS.doCommand(devFD, string.pack('>B', cmd), 0)
end


function waitIdle(devFD, sleepTime)
  repeat
    if sleepTime then io.write('.') end
    local statusBuf = SPIOPS.doCommand(devFD, string.pack('>B', READ_FLAG_STATUS), 1)
    status = statusBuf:byte(1, 1)
--    io.write(string.format(' %02X', status))

    if (status & anyError) ~= 0 then
      print(string.format('Status indicates error: %02X', status))
      break
    end

    if sleepTime then posix.sleep(sleepTime) end
  until (status & bFLAG_STATUS.notBusy) ~= 0

  return status
end


-- Return a human readable version of the specified device range.
function rangeString(range)
  return string.format('0x%X..0x%X', range[1], range[2])
end


function doRead()
  local msg = 'Read operation requires a valid filename to write to'
  local file = table.remove(arg, 1) or usage(msg)
  local f = openChecked(file, "wb") or usage(msg)
  local prevMB = -1
  local addr

  io.stdout:setvbuf('no')
  print("Reading device " .. rangeString(devRange) .. " dumping to file '" .. file .. '"')

  for addr = devRange[1], devRange[2] - 1, SPIChunkSize do

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

  local f = openChecked(file, "rb")
  local data = f:read('a') or usage(msg)
  f:close()

  io.stdout:setvbuf('no')
  print(string.format('Writing device %s from file "%s" (%d bytes)', rangeString(devRange), file, #data))

  for addr = devRange[1], math.min(DeviceSize - 1, devRange[2] + #data - 1), ProgramPageSize do
    doWriteEnableDisable(devFD, true)             -- Enable writing

    if addr // MB ~= prevMB then
      prevMB = addr // MB
      local eol = (prevMB % 16 == 15) and '\n' or ''
      io.write(string.format("%3dMB%s", prevMB, eol))
    end

    -- Note this correctly handles files shorter than a multiple of a
    -- ProgramPageSize since data:sub() truncates buffer as needed.
    local chunk = data:sub(addr+1, addr+ProgramPageSize)
    SPIOPS.doCommand(devFD, string.pack(">BI4", PROGRAM_PAGE_4B, addr) .. chunk, 0)

    status = waitIdle(devFD)

    if (status & anyError) ~= 0 then
      print(string.format('Program operation terminated in error status %02X', status))
      break
    end
  end

  doWriteEnableDisable(devFD, false)             -- Disable writing
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
  print(string.format('Verifying device %s against file "%s"', rangeString(devRange), file))

  for addr = devRange[1], devRange[2] - 1, SPIChunkSize do

    if addr // MB ~= prevMB then
      prevMB = addr // MB
      local eol = (prevMB % 16 == 15) and '\n' or ''
      io.write(string.format("%3dMB%s", prevMB, eol))
    end

    local size = SPIChunkSize
    local fileBuf = f:read(size)
    if not fileBuf then break end               -- Exit verification loop on EOF
    size = math.min(size, #fileBuf)
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
          io.stderr:write(string.format('Device verify mismatch at 0x%X: was 0x%02X, should be 0x%02X\n',
              addr - devRange[1] + k - 1, devByte, fileByte))
          os.exit(false, true)
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
  local status

  doWriteEnableDisable(devFD, true)             -- Enable writing
  io.stdout:setvbuf('no')

  if devRange[2] - devRange[1] == DeviceSize then
    SPIOPS.doCommand(devFD, string.pack('>B', BULK_ERASE), 0)        -- Start bulk erase operation
    io.write('Erasing entire device')
    status = waitIdle(devFD, 2)
  else
    local addr
    local prevMB = -1

    io.write('Erasing range ' .. rangeString(devRange))

    for addr = devRange[1], devRange[2] - 1, SectorSize do
      if addr // MB ~= prevMB then
        prevMB = addr // MB
        local eol = (prevMB % 16 == 15) and '\n' or ''
        io.write(string.format("%3dMB%s", prevMB, eol))
      end

      SPIOPS.doCommand(devFD, string.pack('>BI4', SECTOR_ERASE, addr), 0)        -- Start sector erase operation
      status = waitIdle(devFD, 2)
    end
  end

  -- Disable writing again
  doWriteEnableDisable(devFD, false)
  io.write('\n')
  io.stdout:setvbuf('line')
  print(string.format('Ending status flag register=%02X (i.e., success)', status))
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
  }

  local types = {
    [0xBA] = '3V',
    [0xBB] = '1.8V',
  }

  local capacities = {
    [0x22] = 2048 // 8,
    [0x21] = 1024 // 8,
    [0x20] = 512 // 8,
    [0x19] = 256 // 8,
    [0x18] = 128 // 8,
    [0x17] = 64 // 8,
  }

  local readIDBuf = SPIOPS.doCommand(devFD, string.pack('>B', READ_ID), 20)
  devInfo.mfg, devInfo.type, devInfo.cap = readIDBuf:byte(1, 3)
  devInfo.mfgString = manufacturers[devInfo.mfg] or '?'
  devInfo.typeString = types[devInfo.type] or '?'
  devInfo.capString = capacities[devInfo.cap] or '?'
  print(string.format("Read ID: Manufacturer=%02X(%s) type=%02X(%s) capacity=%02X(%sMB)",
      devInfo.mfg, devInfo.mfgString, devInfo.type, devInfo.typeString, devInfo.cap, devInfo.capString))
  hexDump(readIDBuf)

  if (readIDBuf:byte(5) & 0x03) ~= 0x00 then
    usage('Extended Device Data says sector size is not uniform 64K which is all this program supports right now.')
  end

  devRange = {0, DeviceSize - 1}
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
    devRange = {0, DeviceSize}
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

  devRange = {base, top}
  return devRange
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
      default = doRange,
  })
end

SPIOPS.doClose(devFD)
