local JEDEC = require "jedec"
local CFI = JEDEC.CFI
local SPIOPS = require "spiops"
local MAXBUF = 2048


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
SPIOPS.setSpeed(fd, 1000000)

local sfdp = SPIOPS.doCommand(fd, JEDEC.SFDPCommand, MAXBUF - 5)
print("Response length", #sfdp);

hexDump(sfdp)


SPIOPS.doClose(fd)
