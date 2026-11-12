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
#include <unordered_set>
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
    Vec2 &operator-=(Vec2 other) { x -= other.x; y -= other.y; return *this; }
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
    Vec2 gravity{0, 0}; /* per-body override; World::gravity is added too */
    double angularVelocity = 0;
    double gravityScale = 1;
    double restitution = 0;
    double friction = 0;
    double linearDamping = 0;
    bool kinematic = false;
    bool solid = false; /* opt-in contact response; collision queries always work */
    bool grounded = false;
};
struct Sprite {
    Vec2 size{32, 32};
    double r = 0.30, g = 0.80, b = 1.0, a = 1.0;
    int layer = 0;
    std::string texture;
    double u0 = 0, v0 = 0, u1 = 1, v1 = 1;
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

class InputState {
public:
    void beginFrame() { pressed_.clear(); released_.clear(); clicked_.clear(); releasedButtons_.clear(); wheel_ = {}; }
    void setKey(int key, bool down) { if (down && keys_.insert(key).second) pressed_.insert(key); if (!down && keys_.erase(key)) released_.insert(key); }
    bool keyDown(int key) const { return keys_.count(key) != 0; }
    bool keyPressed(int key) const { return pressed_.count(key) != 0; }
    bool keyReleased(int key) const { return released_.count(key) != 0; }
    void setMousePosition(Vec2 value) { mouse_ = value; }
    Vec2 mousePosition() const { return mouse_; }
    void setMouseButton(int button, bool down) { if (down && buttons_.insert(button).second) clicked_.insert(button); if (!down && buttons_.erase(button)) releasedButtons_.insert(button); }
    bool mouseDown(int button) const { return buttons_.count(button) != 0; }
    bool mouseClicked(int button) const { return clicked_.count(button) != 0; }
    bool mouseReleased(int button) const { return releasedButtons_.count(button) != 0; }
    void setWheel(Vec2 value) { wheel_ += value; }
    Vec2 wheel() const { return wheel_; }
    void setTouch(std::uint64_t id, Vec2 value, bool down) { if (down) touches_[id] = value; else touches_.erase(id); }
    bool touchPosition(std::uint64_t id, Vec2 &value) const { auto at = touches_.find(id); if (at == touches_.end()) return false; value = at->second; return true; }
private:
    std::unordered_set<int> keys_, pressed_, released_, buttons_, clicked_, releasedButtons_;
    std::unordered_map<std::uint64_t, Vec2> touches_;
    Vec2 mouse_{}, wheel_{};
};

struct SceneText {
    std::string text;
    Vec2 position{};
    double size = 16;
    double r = 1, g = 1, b = 1, a = 1;
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
    void setGravity(Vec2 value) { gravity_ = value; }
    Vec2 gravity() const { return gravity_; }

    void update(double delta) {
        if (!running_ || delta <= 0) return;
        for (auto &entity : entities_) {
            if (!entity.active || entity.body.kinematic) continue;
            Body &body = entity.body;
            body.grounded = false;
            body.velocity += (gravity_ * body.gravityScale + body.gravity) * delta;
            body.velocity = body.velocity * std::max(0.0, 1.0 - body.linearDamping * delta);
            entity.transform.position += body.velocity * delta;
            entity.transform.rotation += body.angularVelocity * delta;
        }
        solveCollisions();
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
    InputState input;
    std::vector<SceneText> overlayText;
    void setText(std::size_t index, SceneText value) { if (overlayText.size() <= index) overlayText.resize(index + 1); overlayText[index] = std::move(value); }
    void clearText() { overlayText.clear(); }

private:
    void solveCollisions() {
        /* Sequential AABB contacts are sufficient for the small 2D games the
           engine targets. Dynamic/static contacts use minimum translation and
           restitution only when the body moves into the contact normal. */
        for (auto &entity : entities_) {
            if (!entity.active || entity.body.kinematic || !entity.body.solid) continue;
            for (auto &other : entities_) {
                if (other.id == entity.id || !other.active || !other.body.solid) continue;
                Rect a = entity.bounds(), b = other.bounds();
                if (!intersects(a, b)) continue;
                double left = b.x - (a.x + a.width), right = b.x + b.width - a.x;
                double up = b.y - (a.y + a.height), down = b.y + b.height - a.y;
                double moveX = std::abs(left) < std::abs(right) ? left : right;
                double moveY = std::abs(up) < std::abs(down) ? up : down;
                Vec2 normal{};
                if (std::abs(moveX) < std::abs(moveY)) normal.x = moveX < 0 ? -1 : 1;
                else normal.y = moveY < 0 ? -1 : 1;
                double share = other.body.kinematic ? 1.0 : 0.5;
                entity.transform.position += normal * (std::abs(moveX) < std::abs(moveY) ? std::abs(moveX) * share : std::abs(moveY) * share);
                if (!other.body.kinematic)
                    other.transform.position -= normal * (std::abs(moveX) < std::abs(moveY) ? std::abs(moveX) * 0.5 : std::abs(moveY) * 0.5);
                double normalSpeed = dot(entity.body.velocity, normal);
                if (normalSpeed < 0) {
                    double bounce = std::max(entity.body.restitution, other.body.restitution);
                    Vec2 tangentVelocity = entity.body.velocity - normal * normalSpeed;
                    entity.body.velocity = tangentVelocity * std::max(0.0, 1.0 - std::max(entity.body.friction, other.body.friction)) - normal * (normalSpeed * bounce);
                }
                if (dot(normal, normalized(gravity_)) > 0.5) entity.body.grounded = true;
            }
        }
    }
    Vec2 gravity_{};
    std::deque<Entity> entities_;
    EntityId nextId_ = 1;
    double time_ = 0, accumulator_ = 0, fixedDelta_ = 1.0 / 60.0;
    bool running_ = true;
};

} // namespace luac::engine
