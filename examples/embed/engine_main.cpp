#include <engine.hpp>
#include <iostream>

int main() {
    luac::engine::World world;
    auto &player = world.create("Embedded player", {0, 0});
    player.body.velocity = {42, 0};
    world.update(1.0);
    if (player.transform.position.x != 42) return 1;
    std::cout << "Embedded LuaC engine: " << player.transform.position.x << '\n';
}
