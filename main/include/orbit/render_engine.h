#pragma once

#include "orbit/body_manager.h"
#include "lvgl/lvgl.h"

namespace orbit {

struct Camera {
    Vector2D center;
    double zoom = 72.0;
    double body_zoom = 72.0;
};

struct SpawnPreview {
    bool visible = false;
    bool vector_enabled = false;
    Vector2D position;
    Vector2D velocity;
    double mass = 1.0;
    std::uint32_t color = 0x4ECDC4;
};

class RenderEngine {
public:
    static constexpr int kWidth = 240;
    static constexpr int kHeight = 140;
    static constexpr int kFullWidth = 320;
    static constexpr int kFullHeight = 170;

    lv_obj_t *create(lv_obj_t *parent, int x, int y);
    void set_minimal_view(bool enabled);
    void set_trails_visible(bool visible) { trails_visible_ = visible; }
    void render(const BodyManager &bodies, const Camera &camera, int selected_id,
                const SpawnPreview *spawn_preview = nullptr);

private:
    struct ScreenPoint {
        int x;
        int y;
    };

    ScreenPoint world_to_screen(const Vector2D &world, const Camera &camera) const;
    int viewport_width() const { return minimal_view_ ? kFullWidth : kWidth; }
    int viewport_height() const { return minimal_view_ ? kFullHeight : kHeight; }
    lv_obj_t *canvas_ = nullptr;
    int normal_x_ = 0;
    int normal_y_ = 0;
    bool minimal_view_ = false;
    bool trails_visible_ = true;
};

} 
