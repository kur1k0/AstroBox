#include "orbit/render_engine.h"
#include "orbit/body_size.h"

#include <algorithm>
#include <cmath>

namespace orbit {

namespace {

LV_DRAW_BUF_DEFINE_STATIC(g_orbit_canvas, RenderEngine::kWidth, RenderEngine::kHeight,
                          LV_COLOR_FORMAT_RGB565);
LV_DRAW_BUF_DEFINE_STATIC(g_orbit_full_canvas, RenderEngine::kFullWidth,
                          RenderEngine::kFullHeight, LV_COLOR_FORMAT_RGB565);

constexpr std::uint32_t kBackground = 0x05070B;
constexpr std::uint32_t kGrid = 0x1D2733;
constexpr std::uint32_t kCrosshair = 0x2D8CFF;
constexpr double kVectorPixelsPerSpeed = 16.0;

void draw_line(lv_layer_t &layer, int x1, int y1, int x2, int y2,
               lv_color_t color, int width = 1, lv_opa_t opacity = LV_OPA_COVER)
{
    lv_draw_line_dsc_t descriptor;
    lv_draw_line_dsc_init(&descriptor);
    descriptor.p1.x = x1;
    descriptor.p1.y = y1;
    descriptor.p2.x = x2;
    descriptor.p2.y = y2;
    descriptor.color = color;
    descriptor.width = width;
    descriptor.opa = opacity;
    descriptor.raw_end = 1;
    lv_draw_line(&layer, &descriptor);
}

void draw_disc(lv_layer_t &layer, int x, int y, int radius, lv_color_t color)
{
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = color;
    descriptor.bg_opa = LV_OPA_COVER;
    descriptor.radius = LV_RADIUS_CIRCLE;
    descriptor.border_opa = LV_OPA_TRANSP;
    lv_area_t area = {x - radius, y - radius, x + radius, y + radius};
    lv_draw_rect(&layer, &descriptor, &area);
}

void draw_target_box(lv_layer_t &layer, int x, int y, int half_size)
{
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_opa = LV_OPA_TRANSP;
    descriptor.border_color = lv_color_hex(0xFFFFFF);
    descriptor.border_opa = LV_OPA_COVER;
    descriptor.border_width = 1;
    descriptor.radius = 0;
    lv_area_t area = {x - half_size, y - half_size, x + half_size, y + half_size};
    lv_draw_rect(&layer, &descriptor, &area);
}

double visible_ray_length(int origin_x, int origin_y, double direction_x, double direction_y,
                          int viewport_width, int viewport_height)
{
    double length = 1.0e12;
    if (direction_x > 0.0)
        length = std::min(length, (viewport_width - 1.0 - origin_x) / direction_x);
    else if (direction_x < 0.0)
        length = std::min(length, (0.0 - origin_x) / direction_x);
    if (direction_y > 0.0)
        length = std::min(length, (viewport_height - 1.0 - origin_y) / direction_y);
    else if (direction_y < 0.0)
        length = std::min(length, (0.0 - origin_y) / direction_y);
    return std::max(length, 0.0);
}

} 

lv_obj_t *RenderEngine::create(lv_obj_t *parent, int x, int y)
{
    LV_DRAW_BUF_INIT_STATIC(g_orbit_canvas);
    LV_DRAW_BUF_INIT_STATIC(g_orbit_full_canvas);
    normal_x_ = x;
    normal_y_ = y;
    canvas_ = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(canvas_, &g_orbit_canvas);
    lv_obj_set_pos(canvas_, x, y);
    lv_obj_set_size(canvas_, kWidth, kHeight);
    lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    return canvas_;
}

void RenderEngine::set_minimal_view(bool enabled)
{
    minimal_view_ = enabled;
    if (!canvas_) return;
    lv_canvas_set_draw_buf(canvas_, enabled ? &g_orbit_full_canvas : &g_orbit_canvas);
    lv_obj_set_pos(canvas_, enabled ? 0 : normal_x_, enabled ? 0 : normal_y_);
    lv_obj_set_size(canvas_, enabled ? kFullWidth : kWidth,
                    enabled ? kFullHeight : kHeight);
}

RenderEngine::ScreenPoint RenderEngine::world_to_screen(const Vector2D &world,
                                                        const Camera &camera) const
{
    const int width = viewport_width();
    const int height = viewport_height();
    return {
        static_cast<int>(std::lround(width * 0.5 + (world.x - camera.center.x) * camera.zoom)),
        static_cast<int>(std::lround(height * 0.5 - (world.y - camera.center.y) * camera.zoom)),
    };
}

void RenderEngine::render(const BodyManager &manager, const Camera &camera, int selected_id,
                          const SpawnPreview *spawn_preview)
{
    if (!canvas_) return;
    lv_canvas_fill_bg(canvas_, lv_color_hex(kBackground), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);
    const int width = viewport_width();
    const int height = viewport_height();

    if (!minimal_view_) {
        const ScreenPoint origin = world_to_screen({0.0, 0.0}, camera);
        draw_line(layer, 0, origin.y, width - 1, origin.y, lv_color_hex(kGrid));
        draw_line(layer, origin.x, 0, origin.x, height - 1, lv_color_hex(kGrid));
    }

    if (trails_visible_) {
        for (const Body &body : manager.bodies()) {
            if (!body.active || body.trail_count < 2) continue;
            const lv_color_t trail_color = lv_color_darken(lv_color_hex(body.color), LV_OPA_50);
            for (std::size_t i = 1; i < body.trail_count; ++i) {
                const ScreenPoint a = world_to_screen(body.trail_at(i - 1), camera);
                const ScreenPoint b = world_to_screen(body.trail_at(i), camera);
                draw_line(layer, a.x, a.y, b.x, b.y, trail_color, 1, LV_OPA_70);
            }
        }

        if (!minimal_view_) {
            draw_line(layer, 0, height / 2, width - 1, height / 2,
                      lv_color_hex(kCrosshair));
            draw_line(layer, width / 2, 0, width / 2, height - 1,
                      lv_color_hex(kCrosshair));
        }
    }

    for (const Body &body : manager.bodies()) {
        if (!body.active) continue;
        const ScreenPoint point = world_to_screen(body.pos, camera);
        if (point.x < -24 || point.x > width + 24 ||
            point.y < -24 || point.y > height + 24) {
            continue;
        }
        const int radius = std::clamp(
            static_cast<int>(std::lround(
                visual_radius_for_mass(body.mass) * camera.body_zoom)), 0, 16);
        if (body.type == BodyType::Star && radius > 0) {
            draw_disc(layer, point.x, point.y, radius + std::max(1, radius / 2),
                      lv_color_darken(lv_color_hex(body.color), LV_OPA_40));
        }
        draw_disc(layer, point.x, point.y, radius, lv_color_hex(body.color));
        if (!minimal_view_ && body.id == selected_id)
            draw_target_box(layer, point.x, point.y, radius + 4);
    }

    if (!minimal_view_ && spawn_preview && spawn_preview->visible) {
        const ScreenPoint origin = world_to_screen(spawn_preview->position, camera);
        const double speed = std::sqrt(spawn_preview->velocity.length_squared());
        if (spawn_preview->vector_enabled && speed > 1.0e-6) {
            const double direction_x = spawn_preview->velocity.x / speed;
            const double direction_y = -spawn_preview->velocity.y / speed;
            const double logical_length = speed * kVectorPixelsPerSpeed;
            const double visible_length = std::min(
                logical_length,
                visible_ray_length(origin.x, origin.y, direction_x, direction_y,
                                   width, height));
            const int end_x = static_cast<int>(
                std::lround(origin.x + direction_x * visible_length));
            const int end_y = static_cast<int>(
                std::lround(origin.y + direction_y * visible_length));
            constexpr int head = 3;
            const lv_color_t red = lv_color_hex(0xFF3B30);
            draw_line(layer, origin.x, origin.y, end_x, end_y, red, 1);
            draw_line(layer, end_x, end_y,
                      static_cast<int>(std::lround(end_x - direction_x * head - direction_y * head)),
                      static_cast<int>(std::lround(end_y - direction_y * head + direction_x * head)),
                      red, 1);
            draw_line(layer, end_x, end_y,
                      static_cast<int>(std::lround(end_x - direction_x * head + direction_y * head)),
                      static_cast<int>(std::lround(end_y - direction_y * head - direction_x * head)),
                      red, 1);
        }

        const int radius = std::clamp(
            static_cast<int>(std::lround(
                visual_radius_for_mass(spawn_preview->mass) * camera.body_zoom)), 0, 16);
        draw_disc(layer, origin.x, origin.y, radius, lv_color_hex(spawn_preview->color));
        draw_target_box(layer, origin.x, origin.y, radius + 4);
    }

    lv_canvas_finish_layer(canvas_, &layer);
}

} 
