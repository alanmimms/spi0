#!/usr/bin/env lua
local JEDEC = require "jedec"
local SPIOPS = require "spiops"
local MAXBUF = 2048
local SPISpeed = 25*1000*1000

local sha = require "sha2"

-- MT25QL512ABB8ESF-0SIT specific?
local READ_ID = 0x9E
local ENTER_4B_MODE, EXIT_4B_MODE = 0xB7, 0xE9
local READ_4B = 0x13


local DeviceSize = 64*1024*1024
local ChunkSize = 2048
local MB = 1*1024*1024


-- Dump the specified string in hex and ASCII with address offsets in
-- blocks of 16 as has been done since the Big Bang.
function hexDump(s)
  local chunkSize=16
  local k = 0
  local startK = 0
  local line = ""

  function dumpLine()
    local ascii = string.gsub(string.sub(s, startK+1, k+1), '[^%g ]', '?')
    ascii = string.sub(ascii, 1, 8) .. ' ' .. string.sub(ascii, 9, -1)
    print(string.format('%-' .. (6+chunkSize*3) .. 's  |%s|', line, ascii))
  end

  while (k < #s) do

    if (k % chunkSize == 0) then
      line = string.format("%04X:", k)
      startK = k
    end

    line = line .. string.format(" %02X", string.byte(s, k+1))
    if (k % chunkSize == chunkSize/2 - 1) then line = line .. " " end
    if (k % chunkSize == chunkSize - 1) then dumpLine() end
    k = k + 1
  end

  if (k % chunkSize ~= 0) then dumpLine() end
end


function usage(msg)
  if not msg then msg = '' end
  io.stderr:write([[
Usage:
  spi0 {read,verify,write,erase} device [file]

Read, or read and verify, or erase and write, or simply erase the flash device on device (e.g., /dev/spidev0.0).
The `file` parameter is used for read, verify, and write operations.

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



function doRead(devFD, file)
  local f = openChecked(file, "wb")
  local lastMB = -1
  local addr

  print("Reading device and dumping to " .. file)
  io.stdout:setvbuf('no')

  for addr = 0, DeviceSize - 1, ChunkSize do

    if addr // MB ~= lastMB then
      local eol
      lastMB = addr // MB

      if (lastMB % 16 == 15) then
        eol = '\n'
      else
        eol = ''
      end
      
      io.write(string.format("%3dMB%s", lastMB, eol))
    end

    local readBuf = SPIOPS.doCommand(devFD, string.pack(">BI4", READ_4B, addr), ChunkSize)
    f:write(readBuf)
  end

  f:close()
  io.write('\n')
end


function doWrite(devFD, file)
  print('Write is not yet implemented')
end


function doVerify(devFD, file)
  print('Verify is not yet implemented')
end


function doErase(devFD)
  print('Erase is not yet implemented')
end




if #arg < 2 or #arg > 3 then usage('Must have two or three arguments') end

local op = arg[1]
local device = arg[2]
local file = arg[3]

if op == 'read' then
  if #arg ~= 3 then usage('Read requires three arguments') end
elseif op == 'write' then
  if #arg ~= 3 then usage('Write requires three arguments') end
elseif op == 'erase' then
  if #arg ~= 2 then usage('Erase requires two arguments') end
elseif op == 'verify' then
  if #arg ~= 3 then usage('Verify requires three arguments') end
else
  usage('Unknown operation "' .. op .. '". Must use read, write, erase, or verify.')
end

local devFD = SPIOPS.doOpen(device)
if not devFD then usage('Error opening device "' .. device .. '" for SPI operations.') end


SPIOPS.setMode(devFD, 0)
SPIOPS.setBPW(devFD, 8)
SPIOPS.setSpeed(devFD, SPISpeed)


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

local readIDBuf = SPIOPS.doCommand(devFD, string.pack(">B", READ_ID), 20)
local devMfg, devType, devCap = readIDBuf:byte(1, 3)
local devMfgString = manufacturers[devMfg] or '?'
local devTypeString = types[devType] or '?'
local devCapString = capacities[devCap] or '?'
print(string.format("Read ID: Manufacturer=%02X(%s) type=%02X(%s) capacity=%02X(%sMB)",
    devMfg, devMfgString, devType, devTypeString, devCap, devCapString))
hexDump(readIDBuf)

if (readIDBuf:byte(5) & 0x03) ~= 0x00 then
  usage('Extended Device Data says sector size is not uniform 64K which is all this program supports right now.')
end


if false then
  local sfdpBuf = SPIOPS.doCommand(devFD, JEDEC.SFDPCommand, 512)
  print("SFDP: length", #sfdpBuf);
  hexDump(sfdpBuf)
end


SPIOPS.doCommand(devFD, ENTER_4B_MODE, 0)

if op == 'read' then doRead(devFD, file)
elseif op == 'write' then doWrite(devFD, file)
elseif op == 'verify' then doVerify(devFD, file)
elseif op == 'erase' then doErase(devFD)
  usage('Unknown "op" value "' .. op .. '"')
end


if false then
local n
local count = 4000
for n=0,9 do
  local readBuf = SPIOPS.doCommand(devFD, string.pack(">BI4", READ_4B, n*count), count)
  print(string.format("[%d] Read 0000: length=%d", n, #readBuf));
  local sha256 = sha.sha256(readBuf)
  print(string.format('[%d] SHA-256=', n), sha256)

  local fn = string.format('spi0.4000.%08d', n)
  local f = assert(io.open(fn, "wb"))
  f:write(readBuf)
  f:close()
end
end


if false then
  local DeviceSize = 64*1024*1024
  local ChunkSize = 2048
  local MB = 1*1024*1024
  local dumpFileName = "spi0.fulldump"
  local f = assert(io.open(dumpFileName, "wb"))
  local lastMB = -1
  local addr

  print("Reading device and dumping to " .. dumpFileName)
  io.stdout:setvbuf('no')

  for addr = 0, DeviceSize - 1, ChunkSize do

    if addr // MB ~= lastMB then
      local eol
      lastMB = addr // MB

      if (lastMB % 16 == 15) then
        eol = '\n'
      else
        eol = ''
      end
      
      io.write(string.format("%3dMB%s", lastMB, eol))
    end

    local readBuf = SPIOPS.doCommand(devFD, string.pack(">BI4", READ_4B, addr), ChunkSize)
    f:write(readBuf)
  end

  f:close()
end

SPIOPS.doClose(devFD)
