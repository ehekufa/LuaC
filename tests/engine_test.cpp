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
    player.body.solid = true;
    player.sprite.size = {10, 10};
    player.tags.insert("player");
    player.components["health"] = 100;
    auto &wall = world.create("Wall", {12, 0});
    wall.body.kinematic = true;
    wall.body.solid = true;
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
    int steps = world.advance(0.2);
    assert(steps > 0 && world.time() > 0);
    assert(world.find(playerId)->transform.position.x < 7);

    auto copyId = world.duplicate(playerId).id;
    assert(copyId != playerId && world.count() == 3);
    assert(world.destroy(copyId) && !world.destroy(copyId));
    assert(world.find(wallId)->body.kinematic);
    World legacy;
    auto &mover = legacy.create("Mover", {0, 0});
    mover.body.velocity = {10, 0};
    legacy.create("Obstacle", {10, 0});
    legacy.update(0.5);
    assert(mover.transform.position.x == 5); /* non-solid keeps legacy motion */
    assert(!legacy.collisions(mover.id).empty());

    world.clear();
    auto &ball = world.create("Ball", {0, 0});
    ball.sprite.size = {2, 2};
    ball.body.restitution = 0.5;
    ball.body.solid = true;
    world.setGravity({0, 10});
    auto &floor = world.create("Floor", {0, 6});
    floor.body.kinematic = true;
    floor.body.solid = true;
    floor.sprite.size = {20, 2};
    for (int i = 0; i < 120; ++i) world.advance(1.0 / 60.0);
    assert(ball.body.grounded || ball.transform.position.y < 5);
    assert(ball.transform.position.y <= 4.0001);
    assert(std::abs(ball.body.velocity.y) < 20);
    world.input.beginFrame();
    world.input.setKey(32, true);
    assert(world.input.keyDown(32) && world.input.keyPressed(32));
    world.input.beginFrame();
    assert(world.input.keyDown(32) && !world.input.keyPressed(32));
    world.input.setMousePosition({12, 34});
    world.input.setMouseButton(0, true);
    assert(world.input.mousePosition().x == 12 && world.input.mouseClicked(0));
    world.input.setTouch(7, {4, 5}, true);
    Vec2 touch;
    assert(world.input.touchPosition(7, touch) && touch.y == 5);
    world.input.setWheel({1, 2});
    world.input.setWheel({0, 3});
    assert(world.input.wheel().y == 5);
    world.input.setMouseButton(0, false);
    assert(world.input.mouseReleased(0));
    world.setText(0, SceneText{"Привет", {4, 8}, 18});
    assert(world.overlayText.size() == 1 && world.overlayText[0].text == "Привет");
    world.clearText();
    assert(world.overlayText.empty());
    world.clear();
    assert(world.count() == 0 && world.time() == 0);
    std::cout << "Native engine world, components and simulation: OK\n";
}
