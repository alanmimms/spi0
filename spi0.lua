local JEDEC = require "jedec"
local SPIOPS = require "spiops"
local MAXBUF = 2048


local chunkSize=16
function hexDump(s)
  local k = 0
  local line = ""

  while (k < #s) do

    if (k % chunkSize == 0) then

      if (#line ~= 0) then
        print(line)
      end

      line = string.format("%04X:", k)
    end

    line = line .. string.format(" %02X", string.byte(s, k+1))

    if (k % chunkSize == chunkSize - 1) then
      line = line .. "  " .. '"' .. string.gsub(s, '[^%g ]', '?') .. '"'
    end

    k = k + 1
  end
end


local fd = SPIOPS.doOpen("/dev/spidev0.0")
print("fd=", fd)
SPIOPS.setMode(fd, 0)
SPIOPS.setBPW(fd, 8)
SPIOPS.setSpeed(fd, 1000000)

local sfdp = SPIOPS.doCommand(fd, JEDEC.SFDPCommand, MAXBUF)
print("Response length", #sfdp);

hexDump(sfdp)


SPIOPS.doClose(fd)
