local SPIOPS = require "spiops"

fd = SPIOPS.doOpen("/dev/spidev0.0")
print("fd=", fd);
