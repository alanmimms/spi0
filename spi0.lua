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
The `file` parameter is used for verfiy and write operations.

]], msg)
  os.exit(false, true)
end



if #args < 3 or #args > 4 then usage('Must have three or four arguments') end

local op = args[2]
local device = args[3]
local file = args[4]

if op == 'read' then
  if #args ~= 3 then usage('Write requires three arguments') end
elseif op == 'write' then
  if #args ~= 4 then usage('Write requires four arguments') end
elseif op == 'erase' then
  if #args ~= 3 then usage('Write requires three arguments') end
elseif op == 'verify' then
  if #args ~= 4 then usage('Verify requires four arguments') end
else
  usage('Unknown operation "' .. op .. '". Must use read, write, erase, or verify.')
end

local devFD = SPIOPS.doOpen(device)
if not devFD then usage('Error opening device "' .. device .. '" for SPI operations.')


SPIOPS.setMode(devFD, 0)
SPIOPS.setBPW(devFD, 8)
SPIOPS.setSpeed(devFD, SPISpeed)


local readIDBuf = SPIOPS.doCommand(devFD, string.pack(">B", READ_ID), 20)
print(string.format("Read ID: Manufacturer=%02X type=%02X capacity=%02X", readIDBuf.byte(1, 2, 3));
hexDump(readIDBuf)

local sfdpBuf = SPIOPS.doCommand(devFD, JEDEC.SFDPCommand, 512)
print("SFDP: length", #sfdpBuf);
hexDump(sfdpBuf)


SPIOPS.doCommand(devFD, ENTER_4B_MODE, 0)


if (false) then
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


if (false) then
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

io.write('\n')
SPIOPS.doClose(devFD)
