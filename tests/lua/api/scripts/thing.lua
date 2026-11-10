local Thing = { type = "body", hp = 3, props = { w = 10, h = 10, body_type = "kinematic", groups = { "things" } } }
Thing.created, Thing.destroyed_count, Thing.enters = 0, 0, 0
function Thing:ready() Thing.created = Thing.created + 1 end
function Thing:destroyed() Thing.destroyed_count = Thing.destroyed_count + 1 end
function Thing:on_collision_enter(other) Thing.enters = Thing.enters + 1 end
function Thing:hit(n) self.hp = self.hp - n; return self.hp end
return Thing
