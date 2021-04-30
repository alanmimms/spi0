local BitStruct = {
  _width = 1,                   -- Prototype default width for each field we create
  _base = 0,                    -- Prototype default bit number for each field we create
}


function BitStruct:new(o)
  o = o or {}
  local newFields = {
    _base = o._base or 0,
    _width = o._width or 1,
    _fields = o._fields or {},
  }          -- Our new list of fields once all fixups are done

  self.__index = self
  setmetatable(o, self)

  local base = newFields._base          -- Bit number that is base of next field we encounter

  -- Iterate over requested field descriptors and turn them into real fields.
  for k,v in ipairs(o) do

    if math.type(v) == 'integer' then
      error('BitStruct:new cannot (yet) support integer values')
    elseif type(v) == 'boolean' then -- A single bit field
      error('BitStruct:new cannot (yet) support boolean values')
    elseif type(v) == 'table' then
      -- Build a descriptor for a set of fields starting at our current base bit number.
      local subTableDesc = {_base = base}
      table.move(v, 1, #v, 1, subTableDesc)

      -- Recurse to define fields specified in a descriptor that is itself a table.
      local subTable = BitStruct:new(subTableDesc)

      -- Add the subTable fields to our object being constructed.
      for subK, subV in pairs(subTable) do
        newFields[#newFields] = subV
      end
    elseif type(v) == 'string' then
      local w = o._parentWidth or 1 -- Parent's width (e.g., dword 32b) or one bit if no parent
      o[v] = BitStruct:new{_width=w, _base=base}
      base = base + w
    elseif type(v) == 'function' then
      local w, field = v(k, base)
      newFields[#newFields] = field
      print("type function returns", w, field)
      for wk,wv in pairs(w) do print(" wfield   ", wk, wv) end
      base = base + w
    end
  end

  -- Update width to span all of the fields we just processed.
  o._width = base - o._base

  return o
end


-- Create a constructor for a fixed sized word with subfields. This
-- can be used to create functions to make DWORDs, Short, Byte, etc.
-- and subdivide their bits into named fields. The resulting function
-- can then be used to declare the subfields as first class members of
-- the parent BitStruct structure.
--
-- The constructor creates a function to find each of the
-- constructor's child fields to their bit base and width, returning a
-- table with the fields configured this wayt aht can then be handled
-- as a "subTable" as if the table were a member of the list of fields
-- presented to the BitField:new() constructor.
function BitStruct.FixedWordFactory(wordWidth)

  return function(fields)       -- Creates list of fields in list passed to BitStruct:new()
    print("FixedWordFactory factory fields:")
    for k,v in pairs(fields) do print("    ", k,v) end

    return function(field, base) -- Binds a field to its true base and width as BitField:new() proceeds
      print("factory result field", field, " base", base, " wordWidth", wordWidth)
      print("factory result fields:")

      for k,v in pairs(fields) do
        print("   ", k,v)

        if (type(v) == 'table') then

          for k2,v2 in pairs(v) do
            print("        ", k2, v2)
          end
        end
      end

      return {
      _wordWidth=wordWidth,
      }
    end
  end
end



-- TESTS ################################################################
local dword = BitStruct:FixedWordFactory(32)
local short = BitStruct:FixedWordFactory(16)
local s = BitStruct:new{
  twoBit = {14,13},
  severalBits = {12,8},
  aByte = {7,0},
  dword{
    byte3={31,24},
    byte2={23,16},
    byte1={15,8},
    byte0={7,0},
  },
  short{
    s1={15,8},
    s0={7,0},
  },
}


for k,v in pairs(s) do print(k,v) end


return BitStruct
