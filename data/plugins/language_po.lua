local syntax = require "core.syntax"

syntax.add {
  files = { "%.po$", "%.txt$" },
  headers = "msgstr \"\"",
  comment = "#",
  patterns = {
    { pattern = { '"', '"' },         type = "string"   },
  },
  symbols = {
    ["msgid"]    = "keyword",
    ["msdstr"]  = "keyword",
  }
}
