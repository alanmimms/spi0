local JEDEC = require "jedec"
local SPIOPS = require "spiops"
local MAXBUF = 2048
local SPISpeed = 20*1000*1000

-- Dumping 64MB device at 50MHz (including NFS transfer of image)
-- real    0m51.671s
-- SHA256sum c5155232bdf2046ec2fb8a240b29ad7d005cbb726f7114a1760b70c51e9064f1

-- Dumping same device at 10MHz (including NFS transfer of image)
-- real    1m26.977s
-- SHA256sum f5a9df4efa70ced3cb97688881db765ecd8bf148124c47f155a00261d78677c4

-- Dumping same device at 1MHz (including NFS transfer of image)
-- real    9m25.906s
-- SHA256sum 954d02a44c6096b5d0d17459ecc39ecfeb298a5c6fd23e1a1ff75c885733b2a0

-- Dumping same device at 20MHz (including NFS transfer of image)
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

local readIDBuf = SPIOPS.doCommand(fd, string.pack(">B", READ_ID), 512 - 5)
print("Read ID: length", #readIDBuf);
hexDump(readIDBuf)

local sfdpBuf = SPIOPS.doCommand(fd, JEDEC.SFDPCommand, 512 - 5)
print("SFDP: length", #sfdpBuf);
hexDump(sfdpBuf)

SPIOPS.doCommand(fd, ENTER_4B_MODE, 0)
local readBuf = SPIOPS.doCommand(fd, string.pack(">BI4", READ_4B, 0), 512 - 5)
print("Read 0000: length", #readBuf);
hexDump(readBuf)


local DeviceSize = 64*1024*1024
local ChunkSize = 1024
local ProgressSize = 1*1024*1024
local dumpFileName = "spi0.fulldump"
local imageFile = assert(io.open(dumpFileName, "wb"))
local addr

print("Reading device and dumping to " .. dumpFileName)

for addr = 0, DeviceSize, ChunkSize do
  if addr % ProgressSize == 0 then print((addr // ProgressSize) .. "MB") end
  local readBuf = SPIOPS.doCommand(fd, string.pack(">BI4", READ_4B, addr), ChunkSize - 5)
  imageFile:write(readBuf)
end

imageFile:close()

SPIOPS.doClose(fd)
