local JEDEC = {}

-- Append the table `toAppend` to a copy of the existing table
-- `source` and return the result.
local function append(source, toAppend)
  local copy = table.move(source, 1, #source, 1, {}) -- Make a copy of `source`
  return table.move(toAppend, 1, #toAppend, #copy + 1, copy) -- Append `toAppend`
end


local function dword(fields)
  return fields
end


local function include(toInclude)
  return toInclude
end


-- Finish defining bit structure using a table created using elements
-- created by dword() and include() above.
--
-- E.g.,
--
-- local header = structure{
--   dword{
--     firstByte={7, 0},
--     secondByte={15, 8},
--     aNybble={19, 16},
--     aWideField={30,20, {[0]='first', [1]='second', [2]='third'}},
--     aBit=31},
--   dword{fullWordField={31,0}},
-- }
local function structure(fields)
  return fields
end



local function word24(fields)
  return fields
end


local function word16(fields)
  return fields
end


local function byte(fields)
  return fields
end



-- Definitions from JEDEC Standard No. JESD68.01.
local CFI = {
  queryModeAddr = 0x55,
  queryModeData = 0x98,
  readArrayModeData=0xFF,
  querySignature={'Q', 'R', 'Y'},
}
JEDEC.CFI = CFI


CFI.queryStructure = structure{
  word24{'signature'},
  word16{'primaryInterfaceID'},
  word16{'primaryExtendedQueryAddr'},
  word16{'alternativeInterfaceID'},
  word16{'alternativeExtendedQueryAddr'},

  byte{'vccMinProgEraseWrite'},
  byte{'vccMaxProgEraseWrite'},
  byte{'vppMinProgEraseWrite'},
  byte{'vppMaxProgEraseWrite'},
  byte{'typicalSmallProgramTime'},
  byte{'typicalMaxProgramTime'},
  byte{'typicalBlockEraseTime'},
  byte{'typicalFullChipEraseTime'},
  byte{'maxSmallProgramTime'},
  byte{'maxMaxProgramTime'},
  byte{'maxBlockEraseTime'},
  byte{'maxChipEraseTime'},
  
  byte{'deviceSize'},
  byte{'interfaceCode'},
  byte{'maxMultiByteProgram'},
  byte{'eraseBlockRegionCount'},
  dword{
    eraseBlock256B={31,16},
    eraseBlockCount={15,0},
  },

  -- .... XXX finish this ....
}


-- Definitions from JEDEC Standard No. 216D.01.
JEDEC.SFDPCommand = "\x5A\x00\x00\x00\x00";

JEDEC.SFDPheader = structure{
  dword{signature={31,0}},
  dword{
    accessProtocol={31,24},
    nph={23,16},
    major={15,8},
    minor={7,0},
  },
}

JEDEC.parameterHeader = structure{
  dword{
    length={31,24},
    major={23,16},
    minor={15,8},
    idLSB={7,0},
  },
}

JEDEC.basicFlashParameterHeader = structure{

  header=JEDEC.parameterHeader,

  dword{
    fastRead114=22,
    fastRead144=21,
    fastRead122=20,
    dtrClocking=19,
    addressBytes={18,17, {[0]='3BA', [1]='3BAor4BA', [2]='4BA'}},
    fastRead112=16,
    erase4KBop={15,8},
    volatileSRWEop={4,4, {[0]='50h', [1]='06h'}},
    volatileSRProtectVolatile=3,
    writeGranularity={2,2, {[0]='1byte', [1]='>=64B'}},
    blockSectorEraseSizes={1,0, {[1]='erase4KB', [3]='noErase4KB'}},
  },

  dword{density={31,0}},

  dword{
    fastRead114op={31,24},
    fastRead114MC={23,21},
    fastRead114WS={20,16},
    fastRead144op={15,8},
    fastRead144MC={7,5},
    fastRead144WS={4,0},
  },

  dword{
    fastRead112op={31,24},
    fastRead112MC={23,21},
    fastRead112WS={20,16},
    fastRead122op={15,8},
    fastRead122MC={7,5},
    fastRead122WS={4,0},
  },

  dword{
    fastRead444=4,
    fastRead222=0,
  },
  
  dword{
    fastRead222op={31,24},
    fastRead222MC={23,21},
    fastRead222WS={20,16},
  },

  dword{
    fastRead444op={31,24},
    fastRead444MC={23,21},
    fastRead444WS={20,16},
  },

  dword{
    eraseType2op={31,24},
    eraseType2Size={23,16},
    eraseType1op={15,8},
    eraseType1Size={7,0},
  },

  dword{
    eraseType4op={31,24},
    eraseType4Size={23,16},
    eraseType3op={15,8},
    eraseType3Size={7,0},
  },

  dword{
    eraseType4Units={31,30, {[0]='1ms', [1]='16ms', [2]='128ms', [3]='1s'}},
    eraseType4Count={29,25},
    eraseType3Units={24,23, {[0]='1ms', [1]='16ms', [2]='128ms', [3]='1s'}},
    eraseType3Count={22,18},
    eraseType2Units={17,16, {[0]='1ms', [1]='16ms', [2]='128ms', [3]='1s'}},
    eraseType2Count={15,11},
    eraseType1Units={10,9,  {[0]='1ms', [1]='16ms', [2]='128ms', [3]='1s'}},
    eraseType1Count={8,4},
    typicalToMaxEraseMultiplier={3,0},
  },

  dword{
    chipEraseUnits={30,29, {[0]='16ms', [1]='256ms', [2]='4s', [3]='64s'}},
    chipEraseCount={28,24},
    byteNProgramTypicalUnits={23,23, {[0]='1us', [1]='8us'}},
    byteNProgramTypicalCount={22,19},
    byte1ProgramTypicalUnits={18,18, {[0]='1us', [1]='8us'}},
    byte1ProgramTypicalCount={17,14},
    pageProgramTypicalUnits={13, 13, {[0]='8us', [1]='64us'}},
    pageProgramTypicalCount={12, 8},
    pageSize={7,4},
    pageOrByteProgramTypicalToMaxMultiplier={3,0},
  },

  dword{
    suspendResume=31,
    suspendInProgressEraseMaxLatencyUnits={30,29, {[0]='128ns', [1]='1us', [2]='8us', [3]='64us'}},
    suspendInProgressEraseMaxLatencyCount={28,24},
    eraseResumeToSuspend={23,20},
    suspendInProgressProgramMaxLatencyUnits={19,18, {[0]='128ns', [1]='1us', [2]='8us', [3]='64us'}},
    suspendInProgressProgramMaxLatencyCount={17,13},
    programResumeToSuspend={12,9},
    prohibitedDuringEraseSuspend={7,4},
    prohibitedDuringProgramSuspend={3,0},
  },

  dword{
    suspendOp={31,24},
    resumeOp={23,16},
    programSuspendOp={15,8},
    programResumeOp={7,0},
  },

  dword{
    deepPowerdown=31,
    deepPowerdownOp={30,23},
    deepPowerDownExitOp={22,15},
    exitDeepPowerdownDelayUnits={14,13, {[0]='128ns', [1]='1us', [2]='8us', [3]='64us'}},
    exitDeepPowerdownDelayCount={12,8},
    srPollingBusy70=3,
    srPollingBusy05=2,
  },

  dword{
    holdOrResetDisable=23,
    quadEnableRequirements={22,20},
    mode044Entry={19,16},
    mode044Exit={15,10},
    mode044=9,
    mode444Enable={8,4},
    mode444Disable={3,0},
  },

  dword{
    enter4BA={31,24},
    exit4BA={23,14},
    softResetAndRescue={18,8},
    sr1WriteMethods={6,0},
  },

  dword{
    fastRead118op={31,24},
    fastRead118MC={23,21},
    fastRead118WS={20,16},
    fastRead188op={15,8},
    fastRead188MC={7,5},
    fastRead188WS={4,0},
  },

  dword{
    byteOrderIn8D8D8D={31,31, {[0]='same as 1-1-1 mode', [1]='swapped vs 1-1-1 mode'}},
    dtr8D8D8DCommandExtension={30,29, {[0]='same as Command', [1]='inverse of Command', [3]='16-bit'}},
    dtrDataStrobeQPI=27,
    strDataStrobeQPI=26,
    strDataStrobeWaveform={25,24},
    spiResetProtocolJEDEC=23,
    variableOutputDriverStrength={28,18},
  },

  dword{
    octalEnableRequirements={22,20},
    mode088Entry={19,16},
    mode088Exit={15,10},
    mode088=9,
    mode888Enable={8,4},
    mode888Dsiable={3,0},
  },

  dword{
    max8D8D8DDataStrobeSpeed={31,28, {
        [1]='33MHz', [2]='50MHz', [3]='66MHz', [4]='80MHz', [5]='100MHz', [6]='133MHz', [7]='166MHz',
        [8]='200MHz', [9]='250MHz', [10]='266MHz', [11]='333MHz', [12]='400MHz', [14]='not characterized', [15]='not supported'},
    },
    max8D8D8DNotDataStrobeSpeed={27,24, {
        [1]='33MHz', [2]='50MHz', [3]='66MHz', [4]='80MHz', [5]='100MHz', [6]='133MHz', [7]='166MHz',
        [8]='200MHz', [9]='250MHz', [10]='266MHz', [11]='333MHz', [12]='400MHz', [14]='not characterized', [15]='not supported'},
    },
    max8S8S8SDataStrobeSpeed={23,20, {
        [1]='33MHz', [2]='50MHz', [3]='66MHz', [4]='80MHz', [5]='100MHz', [6]='133MHz', [7]='166MHz',
        [8]='200MHz', [9]='250MHz', [10]='266MHz', [11]='333MHz', [12]='400MHz', [14]='not characterized', [15]='not supported'},
    },
    max8S8S8SNotDataStrobeSpeed={19,16, {
        [1]='33MHz', [2]='50MHz', [3]='66MHz', [4]='80MHz', [5]='100MHz', [6]='133MHz', [7]='166MHz',
        [8]='200MHz', [9]='250MHz', [10]='266MHz', [11]='333MHz', [12]='400MHz', [14]='not characterized', [15]='not supported'},
    },

    max4S4D4DDataStrobeSpeed={15,12, {
        [1]='33MHz', [2]='50MHz', [3]='66MHz', [4]='80MHz', [5]='100MHz', [6]='133MHz', [7]='166MHz',
        [8]='200MHz', [9]='250MHz', [10]='266MHz', [11]='333MHz', [12]='400MHz', [14]='not characterized', [15]='not supported'},
    },
    max4S4D4DNotDataStrobeSpeed={11,8, {
        [1]='33MHz', [2]='50MHz', [3]='66MHz', [4]='80MHz', [5]='100MHz', [6]='133MHz', [7]='166MHz',
        [8]='200MHz', [9]='250MHz', [10]='266MHz', [11]='333MHz', [12]='400MHz', [14]='not characterized', [15]='not supported'},
    },

    max4S4S4SDataStrobeSpeed={7,4, {
        [1]='33MHz', [2]='50MHz', [3]='66MHz', [4]='80MHz', [5]='100MHz', [6]='133MHz', [7]='166MHz',
        [8]='200MHz', [9]='250MHz', [10]='266MHz', [11]='333MHz', [12]='400MHz', [14]='not characterized', [15]='not supported'},
    },
    max4S4S4SNotDataStrobeSpeed={3,0, {
        [1]='33MHz', [2]='50MHz', [3]='66MHz', [4]='80MHz', [5]='100MHz', [6]='133MHz', [7]='166MHz',
        [8]='200MHz', [9]='250MHz', [10]='266MHz', [11]='333MHz', [12]='400MHz', [14]='not characterized', [15]='not supported'},
    },
  },
}


return JEDEC
