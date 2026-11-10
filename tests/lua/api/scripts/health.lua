local Health = {}
function Health.attach(node, max) node.max_hp, node.hp, node.inv = max, max, 0 end
function Health.damage(node, amount)
  if node.inv > 0 or node.hp <= 0 then return false end
  node.hp = node.hp - amount
  node.inv = node.inv_time or 0.8
  if node.on_hurt then node:on_hurt(amount) end
  if node.hp <= 0 and node.on_die then node:on_die() end
  return true
end
function Health.tick(node, dt) node.inv = math.max(0, node.inv - dt) end
return Health
