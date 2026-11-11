#include "engine.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace luac::engine;
    assert(std::abs(length(Vec2{3, 4}) - 5) < 1e-9);
    auto unit = normalized(Vec2{3, 4});
    assert(std::abs(unit.x - 0.6) < 1e-9 && std::abs(unit.y - 0.8) < 1e-9);

    World world;
    auto &player = world.create("Player", {0, 0});
    player.body.velocity = {10, -2};
    player.sprite.size = {10, 10};
    player.tags.insert("player");
    player.components["health"] = 100;
    auto &wall = world.create("Wall", {7, 0});
    wall.body.kinematic = true;
    wall.sprite.size = {10, 10};
    const auto playerId = player.id;
    const auto wallId = wall.id;
    assert(world.count() == 2 && world.withTag("player").size() == 1);

    world.pause();
    world.update(1);
    assert(world.find(playerId)->transform.position.x == 0);
    world.step();
    assert(world.find(playerId)->transform.position.x > 0);
    world.play();
    int steps = world.advance(0.1);
    assert(steps > 0 && world.time() > 0);
    assert(!world.collisions(playerId).empty());

    auto copyId = world.duplicate(playerId).id;
    assert(copyId != playerId && world.count() == 3);
    assert(world.destroy(copyId) && !world.destroy(copyId));
    assert(world.find(wallId)->body.kinematic);
    world.clear();
    assert(world.count() == 0 && world.time() == 0);
    std::cout << "Native engine world, components and simulation: OK\n";
}
