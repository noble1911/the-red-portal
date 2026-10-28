local floatingnumbers = require("devilutionx.floatingnumbers")
local events = require("devilutionx.events")
local player = require("devilutionx.player")
local system = require("devilutionx.system")
local render = require("devilutionx.render")

local DAMAGE_TYPE = {
    PHYSICAL = 0,
    FIRE = 1,
    LIGHTNING = 2,
    MAGIC = 3,
    ACID = 4,
}

-- [RUNS MOD] Colour says whose damage it is, not its element: damage dealt is gold with a
-- sword, damage taken red with a skull, so the two never get confused.
local function get_damage_style(damage_val, base_color)
    local style = base_color
    
    local v = damage_val
    if v >= 64 * 300 then
        style = style | render.UiFlags.FontSize30
    elseif v >= 64 * 100 then
        style = style | render.UiFlags.FontSize24
    else
        style = style | render.UiFlags.FontSize12
    end

    return style
end

local function format_damage(damage_val)
    if damage_val > 0 and damage_val < 64 then
        return string.format("%.2f", damage_val / 64.0)
    else
        return tostring(math.floor(damage_val / 64))
    end
end

local accumulated_damage = {}
local MERGE_WINDOW_MS = 100

events.OnMonsterTakeDamage.add(function(monster, damage, damage_type)
    local id = monster.id
    local now = system.get_ticks()
    
    local entry = accumulated_damage[id]
    if entry and (now - entry.time) < MERGE_WINDOW_MS then
        entry.damage = entry.damage + damage
    else
        entry = { damage = damage, time = now }
        accumulated_damage[id] = entry
    end
    entry.time = now

    local text = format_damage(entry.damage)
    local style = get_damage_style(entry.damage, render.UiFlags.ColorGold)
    floatingnumbers.add(text, monster.position, style, id, false, "dealt")
end)

events.OnPlayerTakeDamage.add(function(_player, damage, damage_type)
    if _player == player.self() then
        local id = _player.id
        local now = system.get_ticks()
        
        local entry = accumulated_damage[id]
        if entry and (now - entry.time) < MERGE_WINDOW_MS then
            entry.damage = entry.damage + damage
        else
            entry = { damage = damage, time = now }
            accumulated_damage[id] = entry
        end
        entry.time = now

        local text = format_damage(entry.damage)
        local style = get_damage_style(entry.damage, render.UiFlags.ColorRed)
        floatingnumbers.add(text, _player.position, style, id, true, "taken")
    end
end)
