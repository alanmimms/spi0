local JEDEC = require "jedec"
local SPIOPS = require "spiops"
local MAXBUF = 2048
local SPISpeed = 25*1000*1000

local sha = require "sha2"

-- WTF?

-- Dumping 64MB device at 50MHz (including NFS transfer of image)
-- real    0m51.671s
-- SHA256sum c5155232bdf2046ec2fb8a240b29ad7d005cbb726f7114a1760b70c51e9064f1

-- Dumping same device at 10MHz (including NFS transfer of image)
-- real    1m26.977s
-- SHA256sum f5a9df4efa70ced3cb97688881db765ecd8bf148124c47f155a00261d78677c4

-- Dumping same device at 1MHz (including NFS transfer of image) over and over
-- real    9m25.906s
-- SHA256sum 954d02a44c6096b5d0d17459ecc39ecfeb298a5c6fd23e1a1ff75c885733b2a0
-- SHA256sum b458a6bc515f1b448b8457133cabdb794ab5232547dffce4bbe8a0dc504ba222  spi0.fulldump-1MHz.001
-- SHA256sum efab313448dd37ba44d8097446c2b5bee9e707891896141d9b642bfcbbcd3534  spi0.fulldump-1MHz.002
-- SHA256sum 9da36847e0b70b9ede8bc9a6509ff2eca63a21cad1c2ce88a7f9630f2cbb05be  spi0.fulldump-1MHz.003
-- SHA256sum 8147f06d8782e0449eb14487580eb054818bbcd024bcbb78862ccebdf651df6d  spi0.fulldump-1MHz.004

-- Dumping same device at 20MHz (including NFS transfer of image) over and over
-- real    0m54.540s
-- SHA256sum 5289d571a2675765fd3d854dd15278c3b9e3a152e7dd857d2b79b331d79da4c8
-- SHA256sum e12f17ff9853ed8bdff931837eececbf37d28b32efbe23db4333f98230cd3208
-- SHA256sum 69c405dc3799ed64f454d9c817c4d929e40430974c8ea2fbf0a87122ea940e92


-- MT25QL512ABB8ESF-0SIT specific?
local READ_ID = 0x9E
local ENTER_4B_MODE, EXIT_4B_MODE = 0xB7, 0xE9
local READ_4B = 0x13


local chunkSize=16
function hexDump(s)
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


local fd = SPIOPS.doOpen("/dev/spidev0.0")
print("fd=", fd)
SPIOPS.setMode(fd, 0)
SPIOPS.setBPW(fd, 8)
SPIOPS.setSpeed(fd, SPISpeed)

local readIDBuf = SPIOPS.doCommand(fd, string.pack(">B", READ_ID), 512)
print("Read ID: length", #readIDBuf);
hexDump(readIDBuf)

local sfdpBuf = SPIOPS.doCommand(fd, JEDEC.SFDPCommand, 512)
print("SFDP: length", #sfdpBuf);
hexDump(sfdpBuf)


local READ_NONVOLATILE_CONFIG_REG = 0xB5
local nvcrBuf = SPIOPS.doCommand(fd, string.pack(">B", READ_NONVOLATILE_CONFIG_REG), 1)
print("Read Nonvolatile Configuration Register: length", #nvcrBuf);
hexDump(nvcrBuf)


local READ_VOLATILE_CONFIG_REG = 0x85
local vcrBuf = SPIOPS.doCommand(fd, string.pack(">B", READ_VOLATILE_CONFIG_REG), 1)
print("Read Volatile Configuration Register: length", #vcrBuf);
hexDump(vcrBuf)


local READ_ENHANCED_VOLATILE_CONFIG_REG = 0x65
local evcrBuf = SPIOPS.doCommand(fd, string.pack(">B", READ_ENHANCED_VOLATILE_CONFIG_REG), 1)
print("Read Enhanced Volatile Configuration Register: length", #evcrBuf);
hexDump(evcrBuf)


SPIOPS.doCommand(fd, ENTER_4B_MODE, 0)


if (false) then
local n
local count = 4000
for n=0,9 do
  local readBuf = SPIOPS.doCommand(fd, string.pack(">BI4", READ_4B, n*count), count)
  print(string.format("[%d] Read 0000: length=%d", n, #readBuf));
  local sha256 = sha.sha256(readBuf)
  print(string.format('[%d] SHA-256=', n), sha256)

  local fn = string.format('spi0.4000.%08d', n)
  local f = assert(io.open(fn, "wb"))
  f:write(readBuf)
  f:close()
end
end


if (true) then
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

    local readBuf = SPIOPS.doCommand(fd, string.pack(">BI4", READ_4B, addr), ChunkSize)
    f:write(readBuf)
  end

  f:close()
end

io.write('\n')
SPIOPS.doClose(fd)
