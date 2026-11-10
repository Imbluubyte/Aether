-- Shared game state module (loaded once via require).
local S = {}

function S.reset()
  S.score, S.kills, S.time = 0, 0, 0
  S.max_hp, S.hp = 5, 5
  S.over = false
  S.best = S.best or 0
end

S.reset()
return S
