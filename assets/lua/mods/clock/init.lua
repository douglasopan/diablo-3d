local events = require("devilutionx.events")
local render = require("devilutionx.render")

events.GameDrawComplete.add(function()
  local text = os.date('%H:%M:%S', os.time())
  if render.hud_clock then
    render.hud_clock(text)
  else
    render.string(text, render.screen_width() - 76, 6)
  end
end)
