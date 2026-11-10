local Scene = {}
function Scene:ready()
  self.s = aether.node.create("sprite", {x = 10, y = 20, w = 16, h = 16, color = "red"})
  self.t = aether.node.create("label", {text = "hi", x = 5, y = 5})
end
function Scene:update(dt)
  self.s.x = self.s.x + dt
end
return Scene
