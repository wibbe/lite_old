-- put user settings here
-- this module will be loaded after everything else when the application starts

local keymap = require "core.keymap"
local config = require "core.config"
local style = require "core.style"
local common = require "core.common"

-- light theme:
--require "user.colors.monokai"

-- key binding:
-- keymap.add { ["ctrl+escape"] = "core:quit" }
keymap.add {
	-- Bindings to move between splits
	["alt+left"] = "root:switch-to-left",
	["alt+right"] = "root:switch-to-right",
	["alt+up"] = "root:switch-to-up",
	["alt+down"] = "root:switch-to-down",

	-- Bindings to make new splits
	["ctrl+alt+left"] = "root:split-left",
	["ctrl+alt+right"] = "root:split-right",
	["ctrl+alt+up"] = "root:split-up",
	["ctrl+alt+down"] = "root:split-down",

	["ctrl++"] = "root:grow",
	["ctrl+-"] = "root:shrink",
}

-- Ignore some files
config.ignore_files = {
}

-- Config
config.treeview_size = 250 * SCALE
config.line_height = 1.3
config.indent_size = 2
config.tab_type = "soft" -- "soft"

-- Change the styling some
style.font = renderer.font.load(EXEDIR .. "/data/fonts/font.ttf", 14.5 * SCALE)
style.icon_font = renderer.font.load(EXEDIR .. "/data/fonts/icons.ttf", 14.5 * SCALE)
style.code_font = renderer.font.load(EXEDIR .. "/data/fonts/monospace.ttf", 15.5 * SCALE)
style.scrollbar_size = common.round(10 * SCALE)

style.text = { common.color "#A7A7AC" }
style.dim = { common.color "#727277" }

