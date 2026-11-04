-- Host test of the Lua half of port/rc/tods/tods_pause_quit.inc (ToD pause menu QUIT TO MAIN MENU / QUIT TO DESKTOP, ENHANCEMENT).
-- run_pause_quit_test.sh extracts kMaker from the .inc into MAKER (a file) and runs this with a Lua 5.1-family interpreter (LuaJIT);
-- the game runs Lua 5.0.2, whose names used here (setfenv, getfenv, loadstring, table.getn, type) are the same.
-- The mock ListUpdater reproduces the shapes read off the game's chunk (the RE notes section 19): methods installed on the global
-- `self`, onInitialize sets the five menu indices and calls self:SetupMenus(), menuData[menu][i] = { text, callback }, handleSelect calls
-- item[2](self). No game data. Exit 0 = PASS.
local maker_path = assert(arg[1], "usage: test_pause_quit.lua MAKER_FILE")
local fails, checks = 0, 0
local function check(c, what) checks = checks + 1; if not c then fails = fails + 1; print("  FAIL: " .. what) end end

local signals, exits = {}, 0
local real_loadstring = loadstring
_G.loadstring = function(s, ...)                                        -- the host sees these through its parser hook
  local w = string.match(s, "^%-%-rc_pause_quit:(.*)$")
  if w then signals[#signals + 1] = w end
  return real_loadstring(s, ...)
end
_G.exit_pause_menu = function() exits = exits + 1 end                    -- the game's native (0x00035188)

local maker = assert(real_loadstring(assert(io.open(maker_path)):read("*a"), "maker"))()
check(type(maker) == "function", "the maker chunk returns a function")
check(maker("syntax error text") == "syntax error text", "a failed parse (error value) passes through unchanged")

-- The game's chunk as the presentation would run it: reads the global `self` of its environment.
local function list_updater_chunk()
  self.SetupMenus = function(t)
    t.menuData = {}
    t.menuData[t.MAIN_MENU_INDEX] = {}
    for i = 1, 8 do t.menuData[t.MAIN_MENU_INDEX][i] = { 32 + i, function() end } end
    t.menuData[t.OPTIONS_MENU_INDEX] = {}
    local names = { "onGameOptions", "onControls", "onMoves", "onHelp", "onSave", "onLoad" }
    for i = 1, 6 do t.menuData[t.OPTIONS_MENU_INDEX][i] = { 41 + i, function(u) u.picked = names[i] end } end
  end
  self.onInitialize = function(t)
    t.MAIN_MENU_INDEX, t.OPTIONS_MENU_INDEX, t.EXTRAS_MENU_INDEX, t.ARENA_MENU_INDEX, t.SPACE_MENU_INDEX = 1, 2, 3, 4, 5
    t:SetupMenus()
  end
end

-- The runtime gives the loaded function its own environment holding `self` (the worst case: not _G).
local inst = {}
local env = setmetatable({ self = inst }, { __index = _G })
local wrapped = maker(list_updater_chunk)
check(type(wrapped) == "function" and wrapped ~= list_updater_chunk, "maker(F) returns a wrapper")
setfenv(wrapped, env)
local ok, err = pcall(wrapped)
check(ok, "the wrapper runs the game's chunk in the presentation's environment (" .. tostring(err) .. ")")
check(type(inst.SetupMenus) == "function" and type(inst.onInitialize) == "function", "the game's methods are installed on self")
check(#signals == 1 and signals[1] == "installed", "one 'installed' signal")
check(rawget(_G, "self") == nil, "nothing leaks into _G")

inst:onInitialize()
local opt = inst.menuData[inst.OPTIONS_MENU_INDEX]
check(table.getn(opt) == 8, "OPTIONS has 8 rows (6 + 2), got " .. table.getn(opt))
check(opt[5][1] == 46 and opt[6][1] == 47, "the game's SAVE/LOAD rows keep their places and text tags")
check(opt[7][1] == "QUIT TO MAIN MENU" and opt[8][1] == "QUIT TO DESKTOP", "row 7 and 8 texts")
check(table.getn(inst.menuData[inst.MAIN_MENU_INDEX]) == 8, "the main page is untouched")
opt[5][2](inst); check(inst.picked == "onSave", "a game row still calls its own callback")
opt[7][2](inst)
check(exits == 1 and signals[2] == "title", "QUIT TO MAIN MENU: exit_pause_menu() then the 'title' signal")
opt[8][2](inst)
check(exits == 1 and signals[3] == "desktop", "QUIT TO DESKTOP: only the 'desktop' signal")
inst:onInitialize()                                                       -- a second SetupMenus rebuilds, still 8 rows
check(table.getn(inst.menuData[inst.OPTIONS_MENU_INDEX]) == 8, "a rebuilt OPTIONS page has 8 rows again")

-- A behaviour without SetupMenus (any other chunk) is run and left alone.
local other = {}
local w2 = maker(function() self.onActivate = function() end end)
setfenv(w2, setmetatable({ self = other }, { __index = _G }))
w2()
check(other.onActivate ~= nil and other.SetupMenus == nil and #signals == 3, "a chunk without SetupMenus is not touched")

print(string.format("pause-quit Lua test: %d checks, %d failed", checks, fails))
os.exit(fails == 0 and 0 or 1)
