#pragma once

/* LuaC native scene engine. Header-only C++17, independent of Vulkan and of
   the Lua runtime. Applications can link LuaC::engine without using Studio. */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace luac::engine {

using EntityId = std::uint64_t;
constexpr EntityId invalidEntity = 0;

struct Vec2 {
    double x = 0, y = 0;
    Vec2 operator+(Vec2 other) const { return {x + other.x, y + other.y}; }
    Vec2 operator-(Vec2 other) const { return {x - other.x, y - other.y}; }
    Vec2 operator*(double scale) const { return {x * scale, y * scale}; }
    Vec2 &operator+=(Vec2 other) { x += other.x; y += other.y; return *this; }
};
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double length(Vec2 value) { return std::hypot(value.x, value.y); }
inline double distance(Vec2 a, Vec2 b) { return length(a - b); }
inline Vec2 normalized(Vec2 value) { double n = length(value); return n > 0 ? value * (1.0 / n) : Vec2{}; }

struct Transform {
    Vec2 position{};
    double rotation = 0;
    double scale = 1;
};
struct Body {
    Vec2 velocity{};
    double angularVelocity = 0;
    bool kinematic = false;
};
struct Sprite {
    Vec2 size{32, 32};
    double r = 0.30, g = 0.80, b = 1.0, a = 1.0;
    int layer = 0;
};
struct Rect {
    double x = 0, y = 0, width = 0, height = 0;
};
inline bool intersects(Rect a, Rect b) {
    return a.x < b.x + b.width && a.x + a.width > b.x &&
           a.y < b.y + b.height && a.y + a.height > b.y;
}
struct Camera {
    Vec2 position{};
    double zoom = 1;
    Vec2 worldToScreen(Vec2 point) const { return (point - position) * zoom; }
    Vec2 screenToWorld(Vec2 point) const { return point * (1.0 / zoom) + position; }
};

struct Entity {
    EntityId id = invalidEntity;
    std::string name;
    bool active = true;
    Transform transform;
    Body body;
    Sprite sprite;
    std::set<std::string> tags;
    std::unordered_map<std::string, double> components;

    Rect bounds() const {
        double width = sprite.size.x * transform.scale;
        double height = sprite.size.y * transform.scale;
        return {transform.position.x - width / 2, transform.position.y - height / 2, width, height};
    }
    bool hasTag(const std::string &tag) const { return tags.find(tag) != tags.end(); }
};

class World {
public:
    Entity &create(const std::string &name = "Entity", Vec2 position = {}) {
        entities_.push_back(Entity{});
        Entity &entity = entities_.back();
        entity.id = nextId_++;
        entity.name = name;
        entity.transform.position = position;
        return entity;
    }
    Entity &duplicate(EntityId id) {
        Entity *source = find(id);
        if (!source) throw std::out_of_range("unknown engine entity");
        Entity copy = *source;
        entities_.push_back(std::move(copy));
        Entity &entity = entities_.back();
        entity.id = nextId_++;
        entity.name += " copy";
        return entity;
    }
    bool destroy(EntityId id) {
        auto at = std::find_if(entities_.begin(), entities_.end(), [id](const Entity &e) { return e.id == id; });
        if (at == entities_.end()) return false;
        entities_.erase(at);
        return true;
    }
    Entity *find(EntityId id) {
        for (auto &entity : entities_) if (entity.id == id) return &entity;
        return nullptr;
    }
    const Entity *find(EntityId id) const {
        for (const auto &entity : entities_) if (entity.id == id) return &entity;
        return nullptr;
    }
    std::deque<Entity> &entities() { return entities_; }
    const std::deque<Entity> &entities() const { return entities_; }
    std::size_t count() const { return entities_.size(); }
    void clear() { entities_.clear(); nextId_ = 1; time_ = 0; accumulator_ = 0; }

    void play() { running_ = true; }
    void pause() { running_ = false; }
    bool running() const { return running_; }
    double time() const { return time_; }
    double fixedDelta() const { return fixedDelta_; }
    void setFixedDelta(double value) { if (value > 0) fixedDelta_ = value; }

    void update(double delta) {
        if (!running_ || delta <= 0) return;
        for (auto &entity : entities_) {
            if (!entity.active || entity.body.kinematic) continue;
            entity.transform.position += entity.body.velocity * delta;
            entity.transform.rotation += entity.body.angularVelocity * delta;
        }
        time_ += delta;
    }
    int advance(double frameDelta) {
        if (!running_ || frameDelta <= 0) return 0;
        /* Bound catch-up work so a suspended Studio window cannot cause a
           spiral of death. Simulation itself remains deterministic. */
        accumulator_ += std::min(frameDelta, 0.25);
        int steps = 0;
        while (accumulator_ + 1e-12 >= fixedDelta_ && steps < 15) {
            update(fixedDelta_);
            accumulator_ -= fixedDelta_;
            ++steps;
        }
        return steps;
    }
    void step() {
        bool wasRunning = running_;
        running_ = true;
        update(fixedDelta_);
        running_ = wasRunning;
    }
    std::vector<EntityId> collisions(EntityId id) const {
        std::vector<EntityId> result;
        const Entity *source = find(id);
        if (!source) return result;
        for (const auto &other : entities_)
            if (other.id != id && other.active && intersects(source->bounds(), other.bounds())) result.push_back(other.id);
        return result;
    }
    std::vector<Entity *> withTag(const std::string &tag) {
        std::vector<Entity *> result;
        for (auto &entity : entities_) if (entity.hasTag(tag)) result.push_back(&entity);
        return result;
    }
    Camera camera;

private:
    std::deque<Entity> entities_;
    EntityId nextId_ = 1;
    double time_ = 0, accumulator_ = 0, fixedDelta_ = 1.0 / 60.0;
    bool running_ = true;
};

} // namespace luac::engine
