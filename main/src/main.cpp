#include "compat/input_keys.h"
#include "keyboard_input.h"
#include "orbit/body_colors.h"
#include "orbit/body_size.h"
#include "orbit/body_manager.h"
#include "orbit/force_solver.h"
#include "orbit/physics_engine.h"
#include "orbit/presets.h"
#include "orbit/qr_scanner.h"
#include "orbit/render_engine.h"
#include "orbit/universe_codec.h"
#include "orbit/universe_qr.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/libs/qrcode/qrcodegen.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#if LV_USE_SDL
#include "lvgl/src/drivers/sdl/lv_sdl_keyboard.h"
#include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"
#endif

namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr std::uint32_t kInk = 0xE8EDF2;
constexpr std::uint32_t kPanel = 0x15181D;
constexpr std::uint32_t kPanelAlt = 0x22262D;
constexpr std::uint32_t kInputBlue = 0x0B2744;
constexpr std::uint32_t kAccent = 0x4ECDC4;
constexpr std::uint32_t kPaused = 0xFFE66D;
constexpr std::uint32_t kInvalid = 0xFF4D4D;
constexpr std::uint32_t kBlack = 0x000000;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr std::uint32_t kViewport = 0x05070B;
constexpr double kCameraStartSpeed = 80.0;
constexpr double kCameraMaxSpeed = 640.0;
constexpr double kCameraAccelerationSeconds = 1.5;
constexpr double kDisplayUnitScale = 100.0;
constexpr std::array<double, 7> kTimeScales = {0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 10.0};
constexpr std::array<const char *, 7> kTimeScaleLabels = {
    "1/10", "1/4", "1/2", "1/1", "2/1", "4/1", "10/1",
};
constexpr std::array<double, 6> kZoomRatios = {0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
constexpr std::array<const char *, 6> kZoomRatioLabels = {
    "1/2", "1/1", "2/1", "4/1", "8/1", "16/1",
};
constexpr long long kMinDisplayMass = 10;
constexpr long long kMaxDisplayMass = 100000;
constexpr double kVectorPixelsPerSpeed = 16.0;
constexpr double kVectorCursorSpeed = 160.0;
constexpr char kAboutImagePath[] = "A:assets/orbit/fsm.jpg";

enum class Direction : std::size_t {
    Up,
    Left,
    Down,
    Right,
    None,
};

enum class SpawnField : std::size_t {
    Mass,
    Name,
    Type,
    InitialVector,
    Vector,
    Color,
    Create,
};

enum class MenuPage {
    Main,
    Presets,
};

constexpr std::size_t kSpawnFieldCount = 7;
constexpr std::size_t kSpawnParameterCount = 6;
constexpr std::size_t kMainMenuItemCount = 5;
constexpr std::size_t kPresetMenuItemCount = 5;
constexpr std::size_t kMenuRowCount = kPresetMenuItemCount;
constexpr int kExportCanvasSize = 170;
constexpr int kMaximumQrVersion = 15;

LV_DRAW_BUF_DEFINE_STATIC(g_export_canvas_buffer, kExportCanvasSize,
                          kExportCanvasSize, LV_COLOR_FORMAT_RGB565);
LV_DRAW_BUF_DEFINE_STATIC(g_load_canvas_buffer, orbit::QrScanner::kPreviewWidth,
                          orbit::QrScanner::kPreviewHeight,
                          LV_COLOR_FORMAT_RGB565);

volatile sig_atomic_t g_quit_requested = 0;
orbit::BodyManager g_bodies;
orbit::PairwiseForceSolver g_force_solver;
orbit::PhysicsEngine g_physics(g_bodies, g_force_solver);
orbit::RenderEngine g_renderer;
orbit::Camera g_camera;
orbit::PresetId g_preset = orbit::PresetId::Empty;
orbit::PresetView g_preset_view = {"EMPTY UNIVERSE", 4.5};
orbit::QrScanner g_qr_scanner;
lv_obj_t *g_root = nullptr;
lv_obj_t *g_top_bar = nullptr;
lv_obj_t *g_status_label = nullptr;
lv_obj_t *g_body_list = nullptr;
lv_obj_t *g_bottom_bar = nullptr;
lv_obj_t *g_help_label = nullptr;
lv_obj_t *g_get_started_modal = nullptr;
lv_obj_t *g_get_started_label = nullptr;
lv_obj_t *g_help_modal = nullptr;
lv_obj_t *g_main_menu = nullptr;
lv_obj_t *g_main_menu_title = nullptr;
lv_obj_t *g_about_screen = nullptr;
lv_obj_t *g_about_text_view = nullptr;
lv_obj_t *g_about_text = nullptr;
lv_obj_t *g_about_image = nullptr;
lv_obj_t *g_export_screen = nullptr;
lv_obj_t *g_export_canvas = nullptr;
lv_obj_t *g_export_info = nullptr;
lv_obj_t *g_load_screen = nullptr;
lv_obj_t *g_load_canvas = nullptr;
lv_obj_t *g_load_status = nullptr;
lv_obj_t *g_spawn_editor = nullptr;
lv_obj_t *g_spawn_editor_title = nullptr;
lv_obj_t *g_spawn_editor_input = nullptr;
lv_obj_t *g_spawn_editor_error = nullptr;
lv_indev_t *g_keyboard_indev = nullptr;
lv_group_t *g_group = nullptr;
std::array<lv_obj_t *, orbit::BodyManager::kMaxBodies> g_body_rows = {};
std::array<lv_obj_t *, orbit::BodyManager::kMaxBodies> g_body_row_labels = {};
std::array<int, orbit::BodyManager::kMaxBodies> g_body_row_ids = {};
std::array<bool, 4> g_direction_down = {};
std::array<lv_obj_t *, kSpawnFieldCount> g_spawn_rows = {};
std::array<lv_obj_t *, kSpawnFieldCount> g_spawn_title_labels = {};
std::array<lv_obj_t *, kSpawnFieldCount> g_spawn_value_labels = {};
std::array<lv_obj_t *, kMenuRowCount> g_main_menu_rows = {};
std::array<lv_obj_t *, kMenuRowCount> g_main_menu_labels = {};
std::size_t g_body_row_count = 0;
std::size_t g_time_scale_index = 3;
std::size_t g_zoom_index = kZoomRatios.size() - 1;
std::size_t g_main_menu_selection = 0;
std::size_t g_spawn_color_index = orbit::kPlanetDefaultColor;
MenuPage g_main_menu_page = MenuPage::Main;
bool g_paused = false;
bool g_follow_selected = false;
bool g_help_visible = false;
bool g_main_menu_visible = false;
bool g_about_visible = false;
bool g_export_visible = false;
bool g_load_visible = false;
bool g_overlay_previous_paused = false;
bool g_minimal_view = false;
bool g_trails_visible = true;
bool g_get_started_visible = true;
bool g_get_started_key_blocked = false;
bool g_spawn_mode = false;
bool g_spawn_vector_enabled = false;
bool g_spawn_name_custom = false;
bool g_spawn_color_custom = false;
bool g_spawn_error = false;
bool g_spawn_editor_visible = false;
bool g_spawn_editor_replace = true;
bool g_spawn_vector_editing = false;
int g_selected_id = -1;
SpawnField g_spawn_field = SpawnField::Mass;
orbit::BodyType g_spawn_type = orbit::BodyType::Planet;
orbit::Vector2D g_spawn_position;
orbit::Vector2D g_spawn_velocity;
orbit::Vector2D g_spawn_cursor_pixels;
orbit::Camera g_spawn_camera;
double g_spawn_mass = 1.0;
std::array<char, 16> g_spawn_name = {};
std::array<char, 32> g_spawn_edit_buffer = {};
std::string g_load_last_scanner_status;
std::vector<std::string> g_export_qr_parts;
std::size_t g_export_qr_index = 0;
orbit::UniverseQrAssembler g_load_qr_assembler;
std::uint32_t g_last_frame_tick = 0;
std::uint32_t g_escape_down_tick = 0;
std::uint32_t g_camera_move_start_tick = 0;
std::array<std::uint8_t, qrcodegen_BUFFER_LEN_FOR_VERSION(kMaximumQrVersion)>
    g_qr_temp = {};
std::array<std::uint8_t, qrcodegen_BUFFER_LEN_FOR_VERSION(kMaximumQrVersion)>
    g_qr_data = {};

void rebuild_body_list();
void load_active_preset();
void activate_main_menu_item();
void redraw_scene();
void update_labels();

void apply_camera_zoom()
{
    const double ratio = kZoomRatios[g_zoom_index];
    g_camera.zoom = g_preset_view.base_zoom * ratio;
    g_camera.body_zoom = orbit::kFigureEightBaseZoom * ratio;
}

void request_quit()
{
    g_quit_requested = 1;
    LVGL_RUN_FLAGE = 0;
}

void handle_signal(int) { request_quit(); }

const char *getenv_default(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value ? value : fallback;
}

void plain_object(lv_obj_t *object)
{
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *make_band(lv_obj_t *parent, int x, int y, int width, int height,
                    std::uint32_t color)
{
    lv_obj_t *band = lv_obj_create(parent);
    plain_object(band);
    lv_obj_set_pos(band, x, y);
    lv_obj_set_size(band, width, height);
    lv_obj_set_style_bg_color(band, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    return band;
}

lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int width, int height,
                     const lv_font_t *font, std::uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    plain_object(label);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_set_style_text_line_space(label, 0, 0);
    return label;
}

const orbit::Body *first_active_body()
{
    for (const orbit::Body &body : g_bodies.bodies()) {
        if (body.active) return &body;
    }
    return nullptr;
}

void set_help_visible(bool visible)
{
    g_help_visible = visible;
    if (visible) {
        g_direction_down.fill(false);
        g_camera_move_start_tick = 0;
    }
    if (!g_help_modal) return;
    if (visible) {
        lv_obj_remove_flag(g_help_modal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_help_modal);
    }
    else {
        lv_obj_add_flag(g_help_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

void help_clicked_cb(lv_event_t *) { set_help_visible(!g_help_visible); }

void dismiss_get_started()
{
    if (!g_get_started_visible) return;
    g_get_started_visible = false;
    if (g_get_started_modal)
        lv_obj_add_flag(g_get_started_modal, LV_OBJ_FLAG_HIDDEN);
}

std::size_t current_menu_item_count()
{
    return g_main_menu_page == MenuPage::Main
               ? kMainMenuItemCount : kPresetMenuItemCount;
}

void refresh_main_menu()
{
    static constexpr const char *kMainItems[kMainMenuItemCount] = {
        "Presets", "Load", "Export", "About", "Exit",
    };
    static constexpr const char *kPresetItems[kPresetMenuItemCount] = {
        "Solar System", "3-Body Problem", "Figure-8 Orbit",
        "Milky Way", "Back",
    };
    const char *const *items = g_main_menu_page == MenuPage::Main
                                   ? kMainItems : kPresetItems;
    const std::size_t item_count = current_menu_item_count();
    if (g_main_menu) {
        lv_obj_set_height(g_main_menu, 122);
        lv_obj_center(g_main_menu);
    }
    if (g_main_menu_title)
        lv_label_set_text(g_main_menu_title,
                          g_main_menu_page == MenuPage::Main ? "MENU" : "PRESETS");
    for (std::size_t i = 0; i < kMenuRowCount; ++i) {
        if (!g_main_menu_rows[i]) continue;
        if (i >= item_count) {
            lv_obj_add_flag(g_main_menu_rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(g_main_menu_rows[i], LV_OBJ_FLAG_HIDDEN);
        if (g_main_menu_labels[i]) lv_label_set_text(g_main_menu_labels[i], items[i]);
        const bool selected = i == g_main_menu_selection;
        lv_obj_set_style_bg_color(g_main_menu_rows[i],
                                  lv_color_hex(selected ? kAccent : kPanelAlt), 0);
        if (g_main_menu_labels[i]) {
            lv_obj_set_style_text_color(g_main_menu_labels[i],
                                        lv_color_hex(selected ? kBlack : kInk), 0);
        }
    }
}

void set_main_menu_visible(bool visible)
{
    g_main_menu_visible = visible;
    if (visible) {
        dismiss_get_started();
        g_main_menu_page = MenuPage::Main;
        g_main_menu_selection = 0;
        g_direction_down.fill(false);
        g_camera_move_start_tick = 0;
        refresh_main_menu();
    }
    if (!g_main_menu) return;
    if (visible) {
        lv_obj_remove_flag(g_main_menu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_main_menu);
    }
    else {
        lv_obj_add_flag(g_main_menu, LV_OBJ_FLAG_HIDDEN);
    }
}

void select_main_menu_relative(int direction)
{
    const int count = static_cast<int>(current_menu_item_count());
    g_main_menu_selection = static_cast<std::size_t>(
        (static_cast<int>(g_main_menu_selection) + direction + count) % count);
    refresh_main_menu();
}

void main_menu_row_clicked_cb(lv_event_t *event)
{
    g_main_menu_selection = static_cast<std::size_t>(reinterpret_cast<intptr_t>(
        lv_event_get_user_data(event)));
    refresh_main_menu();
}

void set_about_text_y(void *object, int32_t y)
{
    lv_obj_set_y(static_cast<lv_obj_t *>(object), y);
}

void close_fullscreen_overlay()
{
    if (!g_about_visible && !g_export_visible && !g_load_visible) return;
    if (g_about_text) lv_anim_delete(g_about_text, set_about_text_y);
    if (g_load_visible) g_qr_scanner.stop();
    g_export_qr_parts.clear();
    g_export_qr_index = 0;
    g_load_qr_assembler.reset();
    g_about_visible = false;
    g_export_visible = false;
    g_load_visible = false;
    if (g_about_screen) lv_obj_add_flag(g_about_screen, LV_OBJ_FLAG_HIDDEN);
    if (g_export_screen) lv_obj_add_flag(g_export_screen, LV_OBJ_FLAG_HIDDEN);
    if (g_load_screen) lv_obj_add_flag(g_load_screen, LV_OBJ_FLAG_HIDDEN);
    g_paused = g_overlay_previous_paused;
    g_last_frame_tick = lv_tick_get();
}

void start_about_scroll()
{
    if (!g_about_text || !g_about_text_view) return;
    lv_anim_delete(g_about_text, set_about_text_y);
    lv_obj_update_layout(g_about_screen);
    const int32_t start_y = lv_obj_get_height(g_about_text_view);
    const int32_t end_y = -lv_obj_get_height(g_about_text);
    lv_obj_set_y(g_about_text, start_y);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, g_about_text);
    lv_anim_set_exec_cb(&animation, set_about_text_y);
    lv_anim_set_values(&animation, start_y, end_y);
    lv_anim_set_duration(&animation,
                         static_cast<std::uint32_t>(start_y - end_y) * 45U);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
}

void open_about_screen()
{
    if (!g_about_screen) return;
    set_main_menu_visible(false);
    g_overlay_previous_paused = g_paused;
    g_paused = true;
    g_about_visible = true;
    lv_obj_remove_flag(g_about_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_about_screen);
    start_about_scroll();
}

bool encode_export_qr(const std::string &text)
{
    return qrcodegen_encodeText(
        text.c_str(), g_qr_temp.data(), g_qr_data.data(),
        qrcodegen_Ecc_MEDIUM, 1, kMaximumQrVersion,
        qrcodegen_Mask_AUTO, false);
}

void draw_export_qr()
{
    lv_canvas_fill_bg(g_export_canvas, lv_color_hex(kWhite), LV_OPA_COVER);
    const int size = qrcodegen_getSize(g_qr_data.data());
    const int scale = std::max(1, kExportCanvasSize / (size + 8));
    const int total = (size + 8) * scale;
    const int origin = (kExportCanvasSize - total) / 2 + 4 * scale;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!qrcodegen_getModule(g_qr_data.data(), x, y)) continue;
            for (int py = 0; py < scale; ++py) {
                for (int px = 0; px < scale; ++px) {
                    lv_canvas_set_px(g_export_canvas, origin + x * scale + px,
                                     origin + y * scale + py,
                                     lv_color_hex(kBlack), LV_OPA_COVER);
                }
            }
        }
    }
    lv_obj_invalidate(g_export_canvas);
    if (g_export_qr_parts.size() <= 1U) {
        lv_label_set_text(g_export_info,
                          "EXPORT\n\nUse another device's Load mode to scan this QR code.\n\nEsc: Back");
    }
    else {
        lv_label_set_text_fmt(
            g_export_info,
            "LARGE UNIVERSE\n\nSplit into %zu QR codes.\n\nPART %zu / %zu\n\nZ/C: Previous / Next\nEsc: Back",
            g_export_qr_parts.size(), g_export_qr_index + 1U,
            g_export_qr_parts.size());
    }
    lv_obj_set_style_text_color(g_export_info, lv_color_hex(kInk), 0);
}

bool show_export_qr_part(std::size_t index)
{
    if (index >= g_export_qr_parts.size() ||
        !encode_export_qr(g_export_qr_parts[index])) {
        lv_canvas_fill_bg(g_export_canvas, lv_color_hex(kBlack), LV_OPA_COVER);
        lv_label_set_text(g_export_info, "EXPORT\n\nQR PART TOO LARGE\n\nEsc: Back");
        lv_obj_set_style_text_color(g_export_info, lv_color_hex(kInvalid), 0);
        return false;
    }
    g_export_qr_index = index;
    draw_export_qr();
    return true;
}

void change_export_qr_part(int direction)
{
    if (g_export_qr_parts.size() < 2U) return;
    const int count = static_cast<int>(g_export_qr_parts.size());
    const std::size_t next = static_cast<std::size_t>(
        (static_cast<int>(g_export_qr_index) + direction + count) % count);
    show_export_qr_part(next);
}

void open_export_screen()
{
    if (!g_export_screen || !g_export_canvas || !g_export_info) return;
    set_main_menu_visible(false);
    g_overlay_previous_paused = g_paused;
    g_paused = true;
    g_export_visible = true;
    g_export_qr_parts.clear();
    g_export_qr_index = 0;
    lv_obj_remove_flag(g_export_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_export_screen);

    const orbit::UniverseEncodeResult encoded =
        orbit::encode_universe(g_bodies, g_physics.config());
    if (!encoded) {
        lv_canvas_fill_bg(g_export_canvas, lv_color_hex(kBlack), LV_OPA_COVER);
        lv_label_set_text_fmt(g_export_info, "EXPORT\n\n%s", encoded.error.c_str());
        lv_obj_set_style_text_color(g_export_info, lv_color_hex(kInvalid), 0);
        return;
    }
    if (encode_export_qr(encoded.text)) {
        g_export_qr_parts.push_back(encoded.text);
        draw_export_qr();
        return;
    }

    const orbit::UniverseQrPartsResult split =
        orbit::make_universe_qr_parts(encoded.text);
    if (!split || split.parts.size() < 2U) {
        lv_canvas_fill_bg(g_export_canvas, lv_color_hex(kBlack), LV_OPA_COVER);
        lv_label_set_text_fmt(g_export_info, "EXPORT\n\n%s",
                              split ? "QR TOO LARGE" : split.error.c_str());
        lv_obj_set_style_text_color(g_export_info, lv_color_hex(kInvalid), 0);
        return;
    }
    g_export_qr_parts = split.parts;
    show_export_qr_part(0);
}

void set_load_status(const char *text, std::uint32_t color)
{
    if (!g_load_status) return;
    lv_label_set_text(g_load_status, text ? text : "");
    lv_obj_set_style_text_color(g_load_status, lv_color_hex(color), 0);
}

void open_load_screen()
{
    if (!g_load_screen || !g_load_canvas || !g_load_status) return;
    set_main_menu_visible(false);
    g_overlay_previous_paused = g_paused;
    g_paused = true;
    g_load_visible = true;
    g_load_last_scanner_status.clear();
    g_load_qr_assembler.reset();
    lv_canvas_fill_bg(g_load_canvas, lv_color_hex(kBlack), LV_OPA_COVER);
    set_load_status("LOAD\n\nOPENING CAMERA...\n\nPoint the camera at an Astro Box QR code.\n\nEsc: Cancel",
                    kInk);
    lv_obj_remove_flag(g_load_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_load_screen);
    g_qr_scanner.start();
}

double imported_base_zoom(const orbit::DecodedUniverse &universe,
                          orbit::Vector2D &center)
{
    if (universe.body_count == 0) {
        center = {};
        return 4.5;
    }
    double minimum_x = universe.bodies[0].pos.x;
    double maximum_x = minimum_x;
    double minimum_y = universe.bodies[0].pos.y;
    double maximum_y = minimum_y;
    for (std::size_t i = 1; i < universe.body_count; ++i) {
        minimum_x = std::min(minimum_x, universe.bodies[i].pos.x);
        maximum_x = std::max(maximum_x, universe.bodies[i].pos.x);
        minimum_y = std::min(minimum_y, universe.bodies[i].pos.y);
        maximum_y = std::max(maximum_y, universe.bodies[i].pos.y);
    }
    center = {(minimum_x + maximum_x) * 0.5, (minimum_y + maximum_y) * 0.5};
    const double half_width = std::max((maximum_x - minimum_x) * 0.55, 0.01);
    const double half_height = std::max((maximum_y - minimum_y) * 0.55, 0.01);
    const double fit_zoom = std::min(110.0 / half_width, 65.0 / half_height);
    return std::clamp(fit_zoom / kZoomRatios.back(), 1.0e-6, 10000.0);
}

bool import_universe(const orbit::DecodedUniverse &universe)
{
    orbit::BodyManager imported;
    for (std::size_t i = 0; i < universe.body_count; ++i) {
        const orbit::DecodedBody &body = universe.bodies[i];
        if (!imported.create({body.name.data(), body.pos, body.vel, body.mass,
                              orbit::radius_for_mass(body.mass), body.color,
                              body.type})) return false;
    }

    g_bodies = std::move(imported);
    g_physics.set_config(universe.config);
    g_physics.reset_clock();
    g_physics.initialize_accelerations();
    g_preset = orbit::PresetId::Empty;
    g_preset_view.name = "IMPORTED UNIVERSE";
    g_preset_view.base_zoom = imported_base_zoom(universe, g_camera.center);
    g_zoom_index = kZoomRatios.size() - 1U;
    apply_camera_zoom();
    g_follow_selected = false;
    const orbit::Body *body = first_active_body();
    g_selected_id = body ? body->id : -1;
    dismiss_get_started();
    rebuild_body_list();
    return true;
}

void update_load_screen()
{
    if (!g_load_visible) return;
    std::vector<std::uint16_t> preview;
    if (g_qr_scanner.consume_preview(preview) &&
        preview.size() == static_cast<std::size_t>(orbit::QrScanner::kPreviewWidth) *
                              orbit::QrScanner::kPreviewHeight) {
        std::memcpy(g_load_canvas_buffer.data, preview.data(),
                    preview.size() * sizeof(preview[0]));
        lv_obj_invalidate(g_load_canvas);
    }

    const std::string scanner_status = g_qr_scanner.status();
    if (scanner_status != g_load_last_scanner_status) {
        g_load_last_scanner_status = scanner_status;
        const std::string message = "LOAD\n\n" + scanner_status +
            "\n\nPoint the camera at an Astro Box QR code.\n\nEsc: Cancel";
        set_load_status(message.c_str(),
                        scanner_status.find("FAILED") != std::string::npos ||
                        scanner_status.find("NOT FOUND") != std::string::npos
                            ? kInvalid : kInk);
    }

    std::string text;
    if (!g_qr_scanner.consume_code(text)) return;
    std::string archive = text;
    if (orbit::is_multipart_universe_qr(text)) {
        const orbit::UniverseQrAcceptResult accepted =
            g_load_qr_assembler.accept(text);
        if (accepted.state == orbit::UniverseQrAcceptState::Error) {
            const std::string message = "LOAD\n\n" + accepted.error +
                "\n\nSCANNED " + std::to_string(accepted.received_count) + "/" +
                std::to_string(accepted.total_count) + "\n\nKeep scanning.\nEsc: Cancel";
            set_load_status(message.c_str(), kInvalid);
            return;
        }
        if (accepted.state != orbit::UniverseQrAcceptState::Complete) {
            const char *action = accepted.state == orbit::UniverseQrAcceptState::Duplicate
                                     ? "ALREADY SCANNED" : "SAVED";
            const std::string message = "LOAD\n\nPART " +
                std::to_string(accepted.part_index) + "/" +
                std::to_string(accepted.total_count) + " " + action +
                "\n\nSCANNED " + std::to_string(accepted.received_count) + "/" +
                std::to_string(accepted.total_count) + "\nNEXT: " +
                std::to_string(accepted.next_missing) +
                "\n\nScan next code.\nEsc: Cancel";
            set_load_status(message.c_str(), kInk);
            return;
        }
        archive = accepted.archive;
    }

    const orbit::UniverseDecodeResult decoded = orbit::decode_universe(archive);
    if (!decoded) {
        const std::string message = "LOAD\n\n" + decoded.error +
                                    "\n\nKeep scanning.\n\nEsc: Cancel";
        set_load_status(message.c_str(), kInvalid);
        return;
    }
    if (!import_universe(decoded.universe)) {
        set_load_status("LOAD\n\nUNIVERSE IS FULL\n\nKeep scanning.\n\nEsc: Cancel",
                        kInvalid);
        return;
    }
    close_fullscreen_overlay();
    update_labels();
    redraw_scene();
}

void set_minimal_view(bool minimal)
{
    g_minimal_view = minimal;
    g_direction_down.fill(false);
    g_camera_move_start_tick = 0;
    if (minimal) {
        set_help_visible(false);
        set_main_menu_visible(false);
    }
    g_renderer.set_minimal_view(minimal);

    for (lv_obj_t *object : {g_top_bar, g_body_list, g_bottom_bar}) {
        if (!object) continue;
        if (minimal) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_get_started_modal) {
        if (minimal || !g_get_started_visible)
            lv_obj_add_flag(g_get_started_modal, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(g_get_started_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

const char *body_type_name(orbit::BodyType type)
{
    switch (type) {
        case orbit::BodyType::Star: return "STAR";
        case orbit::BodyType::Planet: return "PLANET";
        case orbit::BodyType::Asteroid: return "ASTEROID";
        case orbit::BodyType::BlackHole: return "BLACK HOLE";
    }
    return "UNKNOWN";
}

double spawn_radius()
{
    return orbit::radius_for_mass(g_spawn_mass);
}

std::uint32_t spawn_color()
{
    return orbit::kBodyColors[g_spawn_color_index].rgb;
}

void set_default_spawn_name()
{
    const char *prefix = g_spawn_type == orbit::BodyType::Star ? "Star" : "Planet";
    int sequence = 1;
    for (;; ++sequence) {
        char candidate[16];
        std::snprintf(candidate, sizeof(candidate), "%s %d", prefix, sequence);
        const bool already_used = std::any_of(
            g_bodies.bodies().begin(), g_bodies.bodies().end(),
            [candidate](const orbit::Body &body) {
                return body.active && std::strcmp(body.name, candidate) == 0;
            });
        if (!already_used) {
            std::snprintf(g_spawn_name.data(), g_spawn_name.size(), "%s", candidate);
            break;
        }
    }
    g_spawn_name_custom = false;
}

const char *spawn_field_title(SpawnField field)
{
    switch (field) {
        case SpawnField::Mass: return "MASS (10 - 100000)";
        case SpawnField::Name: return "NAME (1 - 15 CHARS)";
        case SpawnField::Type: return "TYPE (STAR / PLANET)";
        case SpawnField::InitialVector: return "VECTOR (YES / NO)";
        case SpawnField::Vector: return "SET VECTOR";
        case SpawnField::Color: return "COLOR";
        case SpawnField::Create: return "CREATE BODY";
    }
    return "INPUT";
}

long long display_units(double value)
{
    return std::llround(value * kDisplayUnitScale);
}

void refresh_spawn_panel()
{
    if (!g_spawn_mode || !g_spawn_value_labels[0]) return;
    const double speed = std::sqrt(g_spawn_velocity.length_squared());
    char numeric_value[32];
    std::snprintf(numeric_value, sizeof(numeric_value), "%lld", display_units(g_spawn_mass));
    lv_label_set_text(g_spawn_value_labels[0], numeric_value);
    lv_label_set_text(g_spawn_value_labels[1], g_spawn_name.data());
    lv_label_set_text(g_spawn_value_labels[2],
                          g_spawn_type == orbit::BodyType::Star ? "STAR" : "PLANET");
    lv_label_set_text(g_spawn_value_labels[3],
                          g_spawn_vector_enabled ? "YES" : "NO");
    if (g_spawn_vector_enabled) {
        std::snprintf(numeric_value, sizeof(numeric_value), "%lld", display_units(speed));
        lv_label_set_text(g_spawn_value_labels[4], numeric_value);
    }
    else
        lv_label_set_text(g_spawn_value_labels[4], "0");
    lv_label_set_text(g_spawn_value_labels[5],
                      orbit::kBodyColors[g_spawn_color_index].name);
    lv_label_set_text(g_spawn_title_labels[6], g_spawn_error ? "FULL" : "CREATE");

    for (std::size_t i = 0; i < kSpawnFieldCount; ++i) {
        lv_obj_t *row = g_spawn_rows[i];
        if (!row) continue;
        const bool selected = i == static_cast<std::size_t>(g_spawn_field);
        const bool disabled = i == static_cast<std::size_t>(SpawnField::Vector) &&
                              !g_spawn_vector_enabled;
        lv_obj_set_style_bg_color(row,
                                  lv_color_hex(selected ? kAccent : kPanelAlt), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        const std::uint32_t title_color =
            i == static_cast<std::size_t>(SpawnField::Create) && g_spawn_error
                ? kInvalid : (disabled ? 0x708090 : (selected ? kBlack : kInk));
        if (g_spawn_title_labels[i])
            lv_obj_set_style_text_color(g_spawn_title_labels[i], lv_color_hex(title_color), 0);
        if (g_spawn_value_labels[i]) {
            const bool color_value = i == static_cast<std::size_t>(SpawnField::Color) &&
                                     !selected;
            lv_obj_set_style_text_color(g_spawn_value_labels[i],
                                        lv_color_hex(disabled ? 0x708090
                                            : (color_value ? spawn_color()
                                                           : (selected ? kBlack : kInk))), 0);
        }
    }
    lv_obj_t *selected_row =
        g_spawn_rows[static_cast<std::size_t>(g_spawn_field)];
    if (selected_row) lv_obj_scroll_to_view(selected_row, LV_ANIM_OFF);
}

void rebuild_spawn_panel()
{
    if (!g_body_list) return;
    lv_obj_clean(g_body_list);
    g_spawn_rows.fill(nullptr);
    g_spawn_title_labels.fill(nullptr);
    g_spawn_value_labels.fill(nullptr);

    static constexpr const char *kParameterNames[kSpawnParameterCount] = {
        "MASS", "NAME", "TYPE", "VECTOR", "VELOCITY", "COLOR",
    };
    for (std::size_t i = 0; i < kSpawnParameterCount; ++i) {
        lv_obj_t *row = make_band(g_body_list, 1, 1 + static_cast<int>(i) * 23,
                                  78, 22, kPanelAlt);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_t *title = make_label(row, 3, 0, 70, 12,
                                     &lv_font_montserrat_12, kInk);
        lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
        lv_label_set_text(title, kParameterNames[i]);
        lv_obj_t *value = make_label(row, 3, 11, 70, 10,
                                     &lv_font_montserrat_10, kInk);
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        g_spawn_rows[i] = row;
        g_spawn_title_labels[i] = title;
        g_spawn_value_labels[i] = value;
    }

    lv_obj_t *create_row = make_band(g_body_list, 1, 139, 78, 22, kPanelAlt);
    lv_obj_set_style_radius(create_row, 0, 0);
    lv_obj_t *create_label = make_label(create_row, 3, 4, 70, 14,
                                        &lv_font_montserrat_12, kInk);
    lv_obj_set_style_text_align(create_label, LV_TEXT_ALIGN_CENTER, 0);
    g_spawn_rows[static_cast<std::size_t>(SpawnField::Create)] = create_row;
    g_spawn_title_labels[static_cast<std::size_t>(SpawnField::Create)] = create_label;
    refresh_spawn_panel();
}

void set_spawn_editor_error(bool invalid)
{
    if (!g_spawn_editor_error) return;
    lv_label_set_text(g_spawn_editor_error, invalid ? "INVALID VALUE" : "");
    lv_obj_set_style_border_color(g_spawn_editor_input,
                                  lv_color_hex(invalid ? kInvalid : kWhite), 0);
}

void refresh_spawn_editor_input()
{
    if (g_spawn_editor_input) lv_label_set_text(g_spawn_editor_input, g_spawn_edit_buffer.data());
}

void open_spawn_editor()
{
    if (!g_spawn_editor ||
        (g_spawn_field != SpawnField::Mass && g_spawn_field != SpawnField::Name)) return;

    switch (g_spawn_field) {
        case SpawnField::Mass:
            std::snprintf(g_spawn_edit_buffer.data(), g_spawn_edit_buffer.size(), "%lld",
                          display_units(g_spawn_mass));
            break;
        case SpawnField::Name:
            std::snprintf(g_spawn_edit_buffer.data(), g_spawn_edit_buffer.size(), "%s", g_spawn_name.data());
            break;
        case SpawnField::Type:
        case SpawnField::InitialVector:
        case SpawnField::Vector:
        case SpawnField::Color:
        case SpawnField::Create:
            return;
    }

    g_spawn_editor_visible = true;
    g_spawn_editor_replace = true;
    lv_label_set_text(g_spawn_editor_title, spawn_field_title(g_spawn_field));
    refresh_spawn_editor_input();
    set_spawn_editor_error(false);
    lv_obj_remove_flag(g_spawn_editor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_spawn_editor);
}

void close_spawn_editor()
{
    g_spawn_editor_visible = false;
    set_spawn_editor_error(false);
    if (g_spawn_editor) lv_obj_add_flag(g_spawn_editor, LV_OBJ_FLAG_HIDDEN);
}

std::string trim_input(const char *input)
{
    std::string value = input ? input : "";
    const auto not_space = [](unsigned char character) { return !std::isspace(character); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool commit_spawn_editor()
{
    const std::string value = trim_input(g_spawn_edit_buffer.data());
    bool valid = false;

    switch (g_spawn_field) {
        case SpawnField::Mass: {
            errno = 0;
            char *end = nullptr;
            const long long mass = std::strtoll(value.c_str(), &end, 10);
            valid = !value.empty() && end && *end == '\0' && errno != ERANGE &&
                    mass >= kMinDisplayMass && mass <= kMaxDisplayMass;
            if (valid) g_spawn_mass = static_cast<double>(mass) / kDisplayUnitScale;
            break;
        }
        case SpawnField::Name:
            valid = !value.empty() && value.size() < g_spawn_name.size();
            if (valid) {
                std::snprintf(g_spawn_name.data(), g_spawn_name.size(), "%s", value.c_str());
                if (!g_spawn_editor_replace) g_spawn_name_custom = true;
            }
            break;
        case SpawnField::Type:
        case SpawnField::InitialVector:
        case SpawnField::Vector:
        case SpawnField::Color:
        case SpawnField::Create:
            break;
    }

    if (!valid) {
        set_spawn_editor_error(true);
        return false;
    }
    close_spawn_editor();
    refresh_spawn_panel();
    return true;
}

void append_spawn_editor_character(char key)
{
    const unsigned char value = static_cast<unsigned char>(key);
    if (value < 0x20U || value > 0x7EU) return;
    if (g_spawn_editor_replace) {
        g_spawn_edit_buffer.fill(0);
        g_spawn_editor_replace = false;
    }
    const std::size_t length = std::strlen(g_spawn_edit_buffer.data());
    if (length + 1 >= g_spawn_edit_buffer.size()) return;
    g_spawn_edit_buffer[length] = key;
    g_spawn_edit_buffer[length + 1] = '\0';
    set_spawn_editor_error(false);
    refresh_spawn_editor_input();
}

void erase_spawn_editor_character()
{
    g_spawn_editor_replace = false;
    const std::size_t length = std::strlen(g_spawn_edit_buffer.data());
    if (length > 0) g_spawn_edit_buffer[length - 1] = '\0';
    set_spawn_editor_error(false);
    refresh_spawn_editor_input();
}

void restore_spawn_camera()
{
    g_camera = g_spawn_camera;
    g_follow_selected = false;
}

void begin_spawn_mode()
{
    if (g_spawn_mode || !g_body_list) return;
    dismiss_get_started();
    g_spawn_camera = g_camera;
    g_spawn_mode = true;
    g_spawn_vector_enabled = false;
    g_spawn_name_custom = false;
    g_spawn_color_custom = false;
    g_spawn_error = false;
    g_spawn_editor_visible = false;
    g_spawn_vector_editing = false;
    g_spawn_field = SpawnField::Mass;
    g_spawn_type = orbit::BodyType::Planet;
    g_spawn_color_index = orbit::kPlanetDefaultColor;
    g_spawn_position = g_spawn_camera.center;
    g_spawn_velocity = {};
    g_spawn_cursor_pixels = {};
    g_spawn_mass = 1.0;
    g_direction_down.fill(false);
    g_camera_move_start_tick = 0;
    g_follow_selected = false;
    set_default_spawn_name();
    rebuild_spawn_panel();
}

void cancel_spawn_mode()
{
    if (!g_spawn_mode) return;
    close_spawn_editor();
    g_spawn_mode = false;
    g_spawn_vector_editing = false;
    g_direction_down.fill(false);
    g_camera_move_start_tick = 0;
    restore_spawn_camera();
    rebuild_body_list();
}

void select_spawn_field(int direction)
{
    int next = static_cast<int>(g_spawn_field);
    do {
        next = (next + direction + static_cast<int>(kSpawnFieldCount)) %
               static_cast<int>(kSpawnFieldCount);
    } while (!g_spawn_vector_enabled &&
             next == static_cast<int>(SpawnField::Vector));
    g_spawn_field = static_cast<SpawnField>(next);
    refresh_spawn_panel();
}

void begin_spawn_vector_editing()
{
    if (!g_spawn_vector_enabled) return;
    g_spawn_vector_editing = true;
    g_spawn_field = SpawnField::Vector;
    g_spawn_cursor_pixels = {
        g_spawn_velocity.x * kVectorPixelsPerSpeed,
        -g_spawn_velocity.y * kVectorPixelsPerSpeed,
    };
    g_direction_down.fill(false);
    g_camera_move_start_tick = 0;
    refresh_spawn_panel();
}

void finish_spawn_vector_editing()
{
    g_spawn_vector_editing = false;
    g_direction_down.fill(false);
    g_camera_move_start_tick = 0;
    refresh_spawn_panel();
}

void create_spawn_body()
{
    if (g_spawn_name[0] == '\0') set_default_spawn_name();
    const orbit::Vector2D velocity = g_spawn_vector_enabled
                                         ? g_spawn_velocity : orbit::Vector2D{};
    orbit::Body *body = g_bodies.create({
        g_spawn_name.data(), g_spawn_position, velocity, g_spawn_mass,
        spawn_radius(), spawn_color(), g_spawn_type,
    });
    if (!body) {
        g_spawn_error = true;
        refresh_spawn_panel();
        return;
    }

    g_selected_id = body->id;
    g_spawn_mode = false;
    g_spawn_vector_editing = false;
    g_direction_down.fill(false);
    g_camera_move_start_tick = 0;
    restore_spawn_camera();
    g_physics.initialize_accelerations();
    rebuild_body_list();
}

void activate_spawn_field()
{
    switch (g_spawn_field) {
        case SpawnField::Mass:
        case SpawnField::Name:
            open_spawn_editor();
            break;
        case SpawnField::Type:
            g_spawn_type = g_spawn_type == orbit::BodyType::Star
                               ? orbit::BodyType::Planet : orbit::BodyType::Star;
            if (!g_spawn_name_custom) set_default_spawn_name();
            if (!g_spawn_color_custom) {
                g_spawn_color_index = g_spawn_type == orbit::BodyType::Star
                                          ? orbit::kStarDefaultColor
                                          : orbit::kPlanetDefaultColor;
            }
            refresh_spawn_panel();
            break;
        case SpawnField::InitialVector:
            g_spawn_vector_enabled = !g_spawn_vector_enabled;
            refresh_spawn_panel();
            break;
        case SpawnField::Vector:
            begin_spawn_vector_editing();
            break;
        case SpawnField::Color:
            g_spawn_color_index = (g_spawn_color_index + 1U) % orbit::kBodyColors.size();
            g_spawn_color_custom = true;
            refresh_spawn_panel();
            break;
        case SpawnField::Create:
            create_spawn_body();
            break;
    }
}

orbit::SpawnPreview current_spawn_preview()
{
    orbit::SpawnPreview preview;
    preview.visible = g_spawn_mode;
    preview.vector_enabled = g_spawn_vector_enabled && g_spawn_vector_editing;
    preview.position = g_spawn_position;
    preview.velocity = g_spawn_vector_enabled ? g_spawn_velocity : orbit::Vector2D{};
    preview.mass = g_spawn_mass;
    preview.color = spawn_color();
    return preview;
}

void redraw_scene()
{
    const orbit::SpawnPreview preview = current_spawn_preview();
    g_renderer.render(g_bodies, g_camera, g_selected_id,
                      g_spawn_mode ? &preview : nullptr);
}

void toggle_trails_visible()
{
    g_trails_visible = !g_trails_visible;
    g_renderer.set_trails_visible(g_trails_visible);
    
    
    
    redraw_scene();
}

void update_body_row_text(std::size_t index)
{
    if (index >= g_body_row_count || !g_body_row_labels[index]) return;
    const orbit::Body *body = g_bodies.find(g_body_row_ids[index]);
    if (!body) return;

    lv_obj_t *label = g_body_row_labels[index];
    if (body->id == g_selected_id) {
        lv_obj_set_size(label, 72, 60);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        char details[128];
        std::snprintf(details, sizeof(details), "#%d %s\n%s\nX %+lld\nY %+lld\nM %lld",
                      body->id, body->name, body_type_name(body->type),
                      display_units(body->pos.x), display_units(body->pos.y),
                      display_units(body->mass));
        lv_label_set_text(label, details);
    }
    else {
        lv_obj_set_size(label, 72, 12);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text_fmt(label, "#%d %s", body->id, body->name);
    }
}

void refresh_body_list_selection()
{
    int y = 1;
    lv_obj_t *selected_row = nullptr;
    for (std::size_t i = 0; i < g_body_row_count; ++i) {
        lv_obj_t *row = g_body_rows[i];
        if (!row) continue;
        const bool selected = g_body_row_ids[i] == g_selected_id;
        const int height = selected ? 65 : 17;
        lv_obj_set_pos(row, 1, y);
        lv_obj_set_size(row, 78, height);
        y += height + 1;
        lv_obj_set_style_bg_color(row,
                                  lv_color_hex(selected ? kAccent : kPanelAlt), 0);
        lv_obj_t *label = g_body_row_labels[i];
        if (label) {
            lv_obj_set_style_text_color(label,
                                        lv_color_hex(selected ? kBlack : kInk), 0);
        }
        update_body_row_text(i);
        if (selected) selected_row = row;
    }
    if (selected_row) lv_obj_scroll_to_view(selected_row, LV_ANIM_OFF);
}

void body_row_clicked_cb(lv_event_t *event)
{
    g_selected_id = static_cast<int>(reinterpret_cast<intptr_t>(
        lv_event_get_user_data(event)));
    refresh_body_list_selection();
}

bool body_list_is_current()
{
    std::size_t index = 0;
    for (const orbit::Body &body : g_bodies.bodies()) {
        if (!body.active) continue;
        if (index >= g_body_row_count || g_body_row_ids[index] != body.id) return false;
        ++index;
    }
    return index == g_body_row_count;
}

void rebuild_body_list()
{
    if (!g_body_list) return;
    lv_obj_clean(g_body_list);
    g_spawn_rows.fill(nullptr);
    g_spawn_title_labels.fill(nullptr);
    g_spawn_value_labels.fill(nullptr);
    g_body_rows.fill(nullptr);
    g_body_row_labels.fill(nullptr);
    g_body_row_ids.fill(-1);
    g_body_row_count = 0;

    if (!g_bodies.find(g_selected_id)) {
        const orbit::Body *body = first_active_body();
        g_selected_id = body ? body->id : -1;
    }

    for (const orbit::Body &body : g_bodies.bodies()) {
        if (!body.active || g_body_row_count >= g_body_rows.size()) continue;

        lv_obj_t *row = lv_obj_create(g_body_list);
        plain_object(row);
        lv_obj_set_pos(row, 1, 1);
        lv_obj_set_size(row, 78, 17);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, body_row_clicked_cb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(body.id)));

        lv_obj_t *label = make_label(row, 3, 2, 72, 12,
                                     &lv_font_montserrat_10, kInk);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text_fmt(label, "#%d %s", body.id, body.name);

        g_body_rows[g_body_row_count] = row;
        g_body_row_labels[g_body_row_count] = label;
        g_body_row_ids[g_body_row_count] = body.id;
        ++g_body_row_count;
    }
    lv_obj_set_height(g_body_list, 140);
    refresh_body_list_selection();
}

void select_relative(int direction)
{
    int current = -1;
    for (std::size_t i = 0; i < g_bodies.bodies().size(); ++i) {
        if (g_bodies.bodies()[i].active && g_bodies.bodies()[i].id == g_selected_id) {
            current = static_cast<int>(i);
            break;
        }
    }
    for (std::size_t offset = 1; offset <= g_bodies.bodies().size(); ++offset) {
        const int size = static_cast<int>(g_bodies.bodies().size());
        const int candidate = (current + direction * static_cast<int>(offset) + size * 2) % size;
        if (g_bodies.bodies()[candidate].active) {
            g_selected_id = g_bodies.bodies()[candidate].id;
            refresh_body_list_selection();
            return;
        }
    }
}

void load_active_preset()
{
    g_preset_view = orbit::load_preset(g_preset, g_bodies, g_physics);
    if (g_preset != orbit::PresetId::Empty) dismiss_get_started();
    g_camera.center = {};
    g_zoom_index = kZoomRatios.size() - 1;
    apply_camera_zoom();
    g_follow_selected = false;
    const orbit::Body *body = first_active_body();
    g_selected_id = body ? body->id : -1;
    rebuild_body_list();
}

void activate_main_menu_item()
{
    if (g_main_menu_page == MenuPage::Main) {
        switch (g_main_menu_selection) {
            case 0:
                g_main_menu_page = MenuPage::Presets;
                g_main_menu_selection = 0;
                refresh_main_menu();
                break;
            case 1:
                open_load_screen();
                break;
            case 2:
                open_export_screen();
                break;
            case 3:
                open_about_screen();
                break;
            case 4:
                request_quit();
                break;
            default:
                break;
        }
        return;
    }

    switch (g_main_menu_selection) {
        case 0: g_preset = orbit::PresetId::SolarSystem; break;
        case 1: g_preset = orbit::PresetId::ThreeBody; break;
        case 2: g_preset = orbit::PresetId::FigureEight; break;
        case 3: g_preset = orbit::PresetId::MilkyWay; break;
        case 4:
            g_main_menu_page = MenuPage::Main;
            g_main_menu_selection = 0;
            refresh_main_menu();
            return;
        default:
            return;
    }
    set_main_menu_visible(false);
    load_active_preset();
}

void pan_camera(double x_pixels, double y_pixels)
{
    g_follow_selected = false;
    g_camera.center.x += x_pixels / g_camera.zoom;
    g_camera.center.y += y_pixels / g_camera.zoom;
}

char ascii_character(const key_item *item)
{
    std::uint32_t value = item->codepoint;
    if (value == 0 && item->utf8[0] && static_cast<unsigned char>(item->utf8[0]) < 0x80U)
        value = static_cast<unsigned char>(item->utf8[0]);
    if (value == 0 && item->keysym > 0 && item->keysym < 0x80U) value = item->keysym;
    if (value >= 0x80U) return 0;
    return static_cast<char>(value);
}

char ascii_key(const key_item *item)
{
    return static_cast<char>(std::tolower(
        static_cast<unsigned char>(ascii_character(item))));
}

Direction direction_from_key(const key_item *item)
{
    if (item->key_code == KEY_UP) return Direction::Up;
    if (item->key_code == KEY_LEFT) return Direction::Left;
    if (item->key_code == KEY_DOWN) return Direction::Down;
    if (item->key_code == KEY_RIGHT) return Direction::Right;

    switch (ascii_key(item)) {
        case 'f': return Direction::Up;
        case 'z': return Direction::Left;
        case 'x': return Direction::Down;
        case 'c': return Direction::Right;
        default: return Direction::None;
    }
}

bool any_direction_down()
{
    return std::any_of(g_direction_down.begin(), g_direction_down.end(),
                       [](bool down) { return down; });
}

void set_direction_state(Direction direction, bool down)
{
    if (direction == Direction::None) return;
    const bool was_moving = any_direction_down();
    g_direction_down[static_cast<std::size_t>(direction)] = down;
    const bool is_moving = any_direction_down();
    if (!was_moving && is_moving) g_camera_move_start_tick = lv_tick_get();
    else if (!is_moving) g_camera_move_start_tick = 0;
}

void update_camera_movement(double elapsed_seconds)
{
    if (!any_direction_down()) return;

    double x = (g_direction_down[static_cast<std::size_t>(Direction::Right)] ? 1.0 : 0.0) -
               (g_direction_down[static_cast<std::size_t>(Direction::Left)] ? 1.0 : 0.0);
    double y = (g_direction_down[static_cast<std::size_t>(Direction::Up)] ? 1.0 : 0.0) -
               (g_direction_down[static_cast<std::size_t>(Direction::Down)] ? 1.0 : 0.0);
    const double magnitude = std::hypot(x, y);
    if (magnitude <= 0.0) return;
    x /= magnitude;
    y /= magnitude;

    const double held_seconds = g_camera_move_start_tick
                                    ? static_cast<double>(lv_tick_elaps(g_camera_move_start_tick)) / 1000.0
                                    : 0.0;
    const double acceleration = std::min(held_seconds / kCameraAccelerationSeconds, 1.0);
    const double speed = kCameraStartSpeed +
                         (kCameraMaxSpeed - kCameraStartSpeed) * acceleration;
    pan_camera(x * speed * elapsed_seconds, y * speed * elapsed_seconds);
}

void update_spawn_vector(double elapsed_seconds)
{
    if (!g_spawn_mode || !g_spawn_vector_enabled ||
        !g_spawn_vector_editing || !any_direction_down()) return;

    double x = (g_direction_down[static_cast<std::size_t>(Direction::Right)] ? 1.0 : 0.0) -
               (g_direction_down[static_cast<std::size_t>(Direction::Left)] ? 1.0 : 0.0);
    double y = (g_direction_down[static_cast<std::size_t>(Direction::Down)] ? 1.0 : 0.0) -
               (g_direction_down[static_cast<std::size_t>(Direction::Up)] ? 1.0 : 0.0);
    const double input_magnitude = std::hypot(x, y);
    if (input_magnitude <= 0.0) return;
    x /= input_magnitude;
    y /= input_magnitude;
    g_spawn_cursor_pixels.x += x * kVectorCursorSpeed * elapsed_seconds;
    g_spawn_cursor_pixels.y += y * kVectorCursorSpeed * elapsed_seconds;
    g_spawn_velocity = {
        g_spawn_cursor_pixels.x / kVectorPixelsPerSpeed,
        -g_spawn_cursor_pixels.y / kVectorPixelsPerSpeed,
    };
    refresh_spawn_panel();
}

void adjust_time_scale(int direction)
{
    if (direction < 0 && g_time_scale_index > 0) --g_time_scale_index;
    else if (direction > 0 && g_time_scale_index + 1 < kTimeScales.size())
        ++g_time_scale_index;
}

void adjust_camera_zoom(int direction)
{
    if (direction < 0 && g_zoom_index > 0) --g_zoom_index;
    else if (direction > 0 && g_zoom_index + 1 < kZoomRatios.size())
        ++g_zoom_index;
    apply_camera_zoom();
}

void toggle_selected_tracking()
{
    const orbit::Body *selected = g_bodies.find(g_selected_id);
    if (!selected) {
        g_follow_selected = false;
        return;
    }
    g_follow_selected = !g_follow_selected;
    if (g_follow_selected) g_camera.center = selected->pos;
}

void delete_selected_body()
{
    if (!g_bodies.destroy(g_selected_id)) return;
    g_follow_selected = false;
    const orbit::Body *next = first_active_body();
    g_selected_id = next ? next->id : -1;
    g_physics.initialize_accelerations();
    rebuild_body_list();
}

void handle_character(char character)
{
    const char key = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    if (g_minimal_view) {
        switch (key) {
            case '1': set_minimal_view(false); break;
            case '2': toggle_trails_visible(); break;
            case 'e': set_main_menu_visible(!g_main_menu_visible); break;
            case 'j': toggle_selected_tracking(); break;
            case 'p': g_paused = !g_paused; break;
            case 'i': adjust_time_scale(-1); break;
            case 'o': adjust_time_scale(1); break;
            case 'l': select_relative(-1); break;
            case 'm': select_relative(1); break;
            case '8': adjust_camera_zoom(-1); break;
            case '9': adjust_camera_zoom(1); break;
            default: break;
        }
        return;
    }

    switch (key) {
        case '1': set_minimal_view(!g_minimal_view); break;
        case '2': toggle_trails_visible(); break;
        case 'r': load_active_preset(); break;
        case 'e': set_main_menu_visible(!g_main_menu_visible); break;
        case 'k': begin_spawn_mode(); break;
        case 'j': toggle_selected_tracking(); break;
        case 'p': g_paused = !g_paused; break;
        case 'i': adjust_time_scale(-1); break;
        case 'o': adjust_time_scale(1); break;
        case 'l': select_relative(-1); break;
        case 'm': select_relative(1); break;
        case 'n': delete_selected_body(); break;
        case '8': adjust_camera_zoom(-1); break;
        case '9': adjust_camera_zoom(1); break;
#if LV_USE_SDL
        case 'q': request_quit(); break;
#endif
        default: break;
    }
}

void handle_escape(const key_item *item)
{
    if (item->key_state == KBD_KEY_RELEASED) {
        const std::uint32_t elapsed = g_escape_down_tick ? lv_tick_elaps(g_escape_down_tick) : 0;
        g_escape_down_tick = 0;
        if (elapsed >= 850) request_quit();
        else if (g_about_visible || g_export_visible || g_load_visible)
            close_fullscreen_overlay();
        else if (g_main_menu_visible && g_main_menu_page == MenuPage::Presets) {
            g_main_menu_page = MenuPage::Main;
            g_main_menu_selection = 0;
            refresh_main_menu();
        }
        else if (g_main_menu_visible) set_main_menu_visible(false);
        else if (g_minimal_view) return;
        else if (g_spawn_editor_visible) close_spawn_editor();
        else if (g_help_visible) set_help_visible(false);
        else if (g_spawn_mode) cancel_spawn_mode();
        else {
            g_follow_selected = false;
            g_camera.center = {};
            g_zoom_index = kZoomRatios.size() - 1;
            apply_camera_zoom();
        }
        return;
    }
    if (g_escape_down_tick == 0) g_escape_down_tick = lv_tick_get();
    if (lv_tick_elaps(g_escape_down_tick) >= 850) request_quit();
}

void keyboard_event_cb(lv_event_t *event)
{
    key_item *item = static_cast<key_item *>(lv_event_get_param(event));
    if (!item) return;
    if (g_get_started_visible) {
        if (item->key_state != KBD_KEY_RELEASED) {
            dismiss_get_started();
            g_get_started_key_blocked = true;
        }
        return;
    }
    if (g_get_started_key_blocked) {
        if (item->key_state == KBD_KEY_RELEASED)
            g_get_started_key_blocked = false;
        return;
    }
    if (item->key_code == KEY_ESC) {
        handle_escape(item);
        return;
    }
    const Direction direction = direction_from_key(item);
    if (item->key_state == KBD_KEY_RELEASED) {
        set_direction_state(direction, false);
        return;
    }
    const bool pressed = item->key_state == KBD_KEY_PRESSED;
    const bool repeated = item->key_state == KBD_KEY_REPEATED;
    if (!pressed && !repeated) return;
    const char key = ascii_key(item);

    if (g_export_visible) {
        if (pressed && direction == Direction::Left) change_export_qr_part(-1);
        else if (pressed && direction == Direction::Right) change_export_qr_part(1);
        return;
    }
    if (g_about_visible || g_load_visible) return;

    if (g_spawn_mode) {
        if (g_spawn_editor_visible) {
            if (pressed && (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER)) {
                commit_spawn_editor();
                return;
            }
            if (item->key_code == KEY_BACKSPACE) {
                erase_spawn_editor_character();
                return;
            }
            append_spawn_editor_character(ascii_character(item));
            return;
        }

        if (g_spawn_vector_editing) {
            if (pressed && (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER)) {
                finish_spawn_vector_editing();
                return;
            }
            if (direction != Direction::None) set_direction_state(direction, true);
            return;
        }

        if (!pressed) return;
        if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER) {
            activate_spawn_field();
            return;
        }
        if (direction == Direction::Up) {
            select_spawn_field(-1);
            return;
        }
        if (direction == Direction::Down) {
            select_spawn_field(1);
            return;
        }
        return;
    }

    if (g_main_menu_visible) {
        if (!pressed) return;
        if (key == 'e') {
            set_main_menu_visible(false);
            return;
        }
        if (direction == Direction::Up) {
            select_main_menu_relative(-1);
            return;
        }
        if (direction == Direction::Down) {
            select_main_menu_relative(1);
            return;
        }
        if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER) {
            activate_main_menu_item();
            return;
        }
        return;
    }

    if (pressed && key == '0' && !g_minimal_view) {
        set_help_visible(!g_help_visible);
        return;
    }
    if (g_help_visible || !pressed) return;

    if (g_minimal_view) {
        if (key) handle_character(key);
        return;
    }

    if (direction != Direction::None) {
        set_direction_state(direction, true);
        return;
    }
    if (key) handle_character(key);
}

void update_labels()
{
    char status[128];
    std::snprintf(status, sizeof(status), "X:%+lld Y:%+lld Z:%s T:%s%s%s",
                  display_units(g_camera.center.x),
                  display_units(g_camera.center.y),
                  kZoomRatioLabels[g_zoom_index],
                  kTimeScaleLabels[g_time_scale_index],
                  g_follow_selected ? " TRACK" : "",
                  g_paused ? " PAUSE" : "");
    lv_label_set_text(g_status_label, status);
    lv_obj_set_style_text_color(g_status_label,
                                lv_color_hex(g_paused ? kPaused : kInk), 0);

    if (g_spawn_mode) {
        refresh_spawn_panel();
    }
    else if (!body_list_is_current()) rebuild_body_list();
    else {
        for (std::size_t i = 0; i < g_body_row_count; ++i) {
            if (g_body_row_ids[i] == g_selected_id) {
                update_body_row_text(i);
                break;
            }
        }
    }
}

void frame_timer_cb(lv_timer_t *)
{
    if (g_load_visible) {
        update_load_screen();
        return;
    }
    const std::uint32_t now = lv_tick_get();
    if (g_last_frame_tick == 0) g_last_frame_tick = now;
    const double elapsed = static_cast<double>(lv_tick_elaps(g_last_frame_tick)) / 1000.0;
    g_last_frame_tick = now;
    if (g_spawn_mode) update_spawn_vector(elapsed);
    else update_camera_movement(elapsed);
    if (!g_paused) g_physics.advance(elapsed * kTimeScales[g_time_scale_index]);

    if (g_follow_selected) {
        const orbit::Body *selected = g_bodies.find(g_selected_id);
        if (selected) g_camera.center = selected->pos;
        else g_follow_selected = false;
    }

    update_labels();
    redraw_scene();
}

void build_ui()
{
    g_root = lv_screen_active();
    plain_object(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(kViewport), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(g_root, keyboard_event_cb,
                        static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), nullptr);
    lv_obj_add_flag(g_root, LV_OBJ_FLAG_CLICKABLE);

    g_top_bar = make_band(g_root, 0, 0, 320, 16, kPanel);
    lv_obj_t *title = make_label(g_top_bar, 4, 2, 58, 12,
                                 &lv_font_montserrat_10, kAccent);
    lv_label_set_text(title, "ASTRO BOX");
    g_status_label = make_label(g_top_bar, 64, 2, 252, 12,
                                &lv_font_montserrat_10, kInk);
    lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_RIGHT, 0);

    g_renderer.create(g_root, 0, 16);
    g_body_list = make_band(g_root, 240, 16, 80, 140, kPanelAlt);
    lv_obj_add_flag(g_body_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_body_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_body_list, LV_SCROLLBAR_MODE_AUTO);

    g_bottom_bar = make_band(g_root, 0, 156, 80, 14, kPanel);
    lv_obj_add_flag(g_bottom_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_bottom_bar, help_clicked_cb, LV_EVENT_CLICKED, nullptr);
    g_help_label = make_label(g_bottom_bar, 4, 0, 72, 14,
                              &lv_font_montserrat_12, kInk);
    lv_label_set_text(g_help_label, "0: Help");
    lv_obj_add_flag(g_help_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_help_label, help_clicked_cb, LV_EVENT_CLICKED, nullptr);

    g_get_started_modal = lv_obj_create(g_root);
    plain_object(g_get_started_modal);
    lv_obj_set_size(g_get_started_modal, 276, 126);
    lv_obj_center(g_get_started_modal);
    lv_obj_set_style_bg_color(g_get_started_modal, lv_color_hex(kBlack), 0);
    lv_obj_set_style_bg_opa(g_get_started_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_get_started_modal, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_opa(g_get_started_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_get_started_modal, 1, 0);
    lv_obj_set_style_radius(g_get_started_modal, 2, 0);
    g_get_started_label = make_label(g_get_started_modal, 8, 13, 260, 76,
                                     &lv_font_montserrat_14, kWhite);
    lv_obj_set_style_text_align(g_get_started_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(g_get_started_label, 7, 0);
    lv_label_set_text(g_get_started_label,
                      "Press \"K\" to create a body.\n"
                      "Press \"E\" to open the menu.\n"
                      "Have fun!");
    lv_obj_t *dismiss_hint = make_label(g_get_started_modal, 8, 99, 260, 14,
                                        &lv_font_montserrat_10, kAccent);
    lv_obj_set_style_text_align(dismiss_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(dismiss_hint, "Press any key to close");

    g_help_modal = lv_obj_create(g_root);
    plain_object(g_help_modal);
    lv_obj_set_size(g_help_modal, 304, 154);
    lv_obj_center(g_help_modal);
    lv_obj_set_style_bg_color(g_help_modal, lv_color_hex(kBlack), 0);
    lv_obj_set_style_bg_opa(g_help_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_help_modal, lv_color_hex(kWhite), 0);
    lv_obj_set_style_border_opa(g_help_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_help_modal, 1, 0);
    lv_obj_set_style_radius(g_help_modal, 2, 0);
    lv_obj_t *help_title = make_label(g_help_modal, 8, 5, 288, 14,
                                      &lv_font_montserrat_12, kAccent);
    lv_obj_set_style_text_align(help_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(help_title, "CONTROLS");
    lv_obj_t *help_left = make_label(g_help_modal, 10, 23, 139, 124,
                                     &lv_font_montserrat_10, kWhite);
    lv_label_set_long_mode(help_left, LV_LABEL_LONG_WRAP);
    lv_label_set_text(help_left,
        "F/Z/X/C  Move, navigate, vector\n"
        "P  Pause / resume\n"
        "I/O  Simulation speed\n"
        "L/M  Select body\n"
        "J  Track selected body\n"
        "N  Delete selected body\n"
        "K  Create body\n"
        "8/9  Zoom out / in");
    lv_obj_t *help_right = make_label(g_help_modal, 157, 23, 137, 124,
                                      &lv_font_montserrat_10, kWhite);
    lv_label_set_long_mode(help_right, LV_LABEL_LONG_WRAP);
    lv_label_set_text(help_right,
        "E  Open / close menu\n"
        "Enter  Confirm / select\n"
        "Esc  Back / cancel\n"
        "Hold Esc  Exit app\n"
        "0  Help\n"
        "1  Hide / show UI\n"
        "2  Hide / show trails\n"
        "R  Reload preset");
    lv_obj_add_flag(g_help_modal, LV_OBJ_FLAG_HIDDEN);

    g_main_menu = lv_obj_create(g_root);
    plain_object(g_main_menu);
    lv_obj_set_size(g_main_menu, 160, 122);
    lv_obj_center(g_main_menu);
    lv_obj_set_style_bg_color(g_main_menu, lv_color_hex(kPanel), 0);
    lv_obj_set_style_bg_opa(g_main_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_main_menu, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_opa(g_main_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_main_menu, 1, 0);
    lv_obj_set_style_radius(g_main_menu, 2, 0);

    g_main_menu_title = make_label(g_main_menu, 8, 5, 144, 14,
                                   &lv_font_montserrat_12, kAccent);
    lv_obj_set_style_text_align(g_main_menu_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(g_main_menu_title, "MENU");
    for (std::size_t i = 0; i < kMenuRowCount; ++i) {
        lv_obj_t *row = make_band(g_main_menu, 6, 22 + static_cast<int>(i) * 19,
                                  148, 17, kPanelAlt);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, main_menu_row_clicked_cb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_t *label = make_label(row, 5, 2, 138, 12,
                                     &lv_font_montserrat_10, kInk);
        g_main_menu_rows[i] = row;
        g_main_menu_labels[i] = label;
    }
    refresh_main_menu();
    lv_obj_add_flag(g_main_menu, LV_OBJ_FLAG_HIDDEN);

    g_about_screen = make_band(g_root, 0, 0, kScreenWidth, kScreenHeight, kBlack);
    g_about_text_view = make_band(g_about_screen, 6, 6, 194, 158, kBlack);
    g_about_text = lv_label_create(g_about_text_view);
    plain_object(g_about_text);
    lv_obj_set_width(g_about_text, 186);
    lv_obj_set_height(g_about_text, LV_SIZE_CONTENT);
    lv_obj_set_x(g_about_text, 4);
    lv_obj_set_style_text_font(g_about_text, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_about_text, lv_color_hex(kWhite), 0);
    lv_obj_set_style_text_align(g_about_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(g_about_text, 2, 0);
    lv_obj_set_style_text_letter_space(g_about_text, 0, 0);
    lv_label_set_long_mode(g_about_text, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_about_text,
        "Author: kur1k0\n\n"
        "This application was built in accordance with the will of the Flying "
        "Spaghetti Monster.\n\n"
        "In the Beginning, His divine Noodly Appendages shaped the heavens, "
        "the earth, and the beer volcanoes. He calibrated every celestial "
        "orbit with precision, subtly weaving His mysteries into the fabric "
        "of creation.\n\n"
        "Through the sacred Eight I'd Really Rather You Didn'ts, He calls us "
        "to reject bigotry, empty dogma, and self-righteousness. Faith should "
        "never be a weapon, and honest inquiry must never be feared. We serve "
        "Him best through kindness, curiosity, and the simple joy of sharing "
        "life together.\n\n"
        "May His Noodly Appendages steady these celestial paths and bless all "
        "who explore them.\n\n"
        "RAmen.");

    lv_obj_t *image_panel = make_band(g_about_screen, 204, 8, 112, 154, kPanelAlt);
    lv_obj_t *image_placeholder = make_label(image_panel, 6, 54, 100, 42,
                                             &lv_font_montserrat_10, kAccent);
    lv_obj_set_style_text_align(image_placeholder, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(image_placeholder, "IMAGE\nfsm.jpg");
    g_about_image = lv_image_create(image_panel);
    plain_object(g_about_image);
    lv_obj_set_size(g_about_image, 112, 154);
    lv_obj_set_pos(g_about_image, 0, 0);
    lv_image_set_src(g_about_image, kAboutImagePath);
    lv_image_set_inner_align(g_about_image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_add_flag(g_about_screen, LV_OBJ_FLAG_HIDDEN);

    g_export_screen = make_band(g_root, 0, 0, kScreenWidth, kScreenHeight, kBlack);
    LV_DRAW_BUF_INIT_STATIC(g_export_canvas_buffer);
    g_export_canvas = lv_canvas_create(g_export_screen);
    plain_object(g_export_canvas);
    lv_canvas_set_draw_buf(g_export_canvas, &g_export_canvas_buffer);
    lv_obj_set_pos(g_export_canvas, 0, 0);
    lv_obj_set_size(g_export_canvas, kExportCanvasSize, kExportCanvasSize);
    lv_canvas_fill_bg(g_export_canvas, lv_color_hex(kWhite), LV_OPA_COVER);
    g_export_info = make_label(g_export_screen, 180, 16, 130, 138,
                               &lv_font_montserrat_10, kInk);
    lv_label_set_long_mode(g_export_info, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_export_info, "EXPORT");
    lv_obj_add_flag(g_export_screen, LV_OBJ_FLAG_HIDDEN);

    g_load_screen = make_band(g_root, 0, 0, kScreenWidth, kScreenHeight, kBlack);
    LV_DRAW_BUF_INIT_STATIC(g_load_canvas_buffer);
    g_load_canvas = lv_canvas_create(g_load_screen);
    plain_object(g_load_canvas);
    lv_canvas_set_draw_buf(g_load_canvas, &g_load_canvas_buffer);
    lv_obj_set_pos(g_load_canvas, 0, 1);
    lv_obj_set_size(g_load_canvas, orbit::QrScanner::kPreviewWidth,
                    orbit::QrScanner::kPreviewHeight);
    lv_canvas_fill_bg(g_load_canvas, lv_color_hex(kBlack), LV_OPA_COVER);
    lv_obj_t *load_panel = make_band(g_load_screen, orbit::QrScanner::kPreviewWidth,
                                     0, kScreenWidth - orbit::QrScanner::kPreviewWidth,
                                     kScreenHeight, kPanel);
    g_load_status = make_label(load_panel, 6, 8,
                               kScreenWidth - orbit::QrScanner::kPreviewWidth - 12,
                               kScreenHeight - 16, &lv_font_montserrat_10, kInk);
    lv_label_set_long_mode(g_load_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(g_load_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(g_load_status, "LOAD");
    lv_obj_add_flag(g_load_screen, LV_OBJ_FLAG_HIDDEN);

    g_spawn_editor = lv_obj_create(g_root);
    plain_object(g_spawn_editor);
    lv_obj_set_size(g_spawn_editor, 210, 96);
    lv_obj_center(g_spawn_editor);
    lv_obj_set_style_bg_color(g_spawn_editor, lv_color_hex(kBlack), 0);
    lv_obj_set_style_bg_opa(g_spawn_editor, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_spawn_editor, lv_color_hex(kWhite), 0);
    lv_obj_set_style_border_opa(g_spawn_editor, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_spawn_editor, 1, 0);
    lv_obj_set_style_radius(g_spawn_editor, 2, 0);

    g_spawn_editor_title = make_label(g_spawn_editor, 9, 7, 192, 13,
                                      &lv_font_montserrat_10, kWhite);
    g_spawn_editor_input = make_label(g_spawn_editor, 9, 26, 192, 24,
                                      &lv_font_montserrat_12, kWhite);
    lv_obj_set_style_bg_color(g_spawn_editor_input, lv_color_hex(kInputBlue), 0);
    lv_obj_set_style_bg_opa(g_spawn_editor_input, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_spawn_editor_input, lv_color_hex(kWhite), 0);
    lv_obj_set_style_border_opa(g_spawn_editor_input, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_spawn_editor_input, 1, 0);
    lv_obj_set_style_pad_left(g_spawn_editor_input, 4, 0);
    lv_obj_set_style_pad_top(g_spawn_editor_input, 4, 0);
    lv_label_set_long_mode(g_spawn_editor_input, LV_LABEL_LONG_CLIP);

    g_spawn_editor_error = make_label(g_spawn_editor, 9, 53, 192, 12,
                                      &lv_font_montserrat_10, kInvalid);
    lv_obj_t *editor_hint = make_label(g_spawn_editor, 9, 75, 192, 13,
                                       &lv_font_montserrat_10, kInk);
    lv_label_set_text(editor_hint, "Enter: OK   ESC: Cancel");
    lv_obj_add_flag(g_spawn_editor, LV_OBJ_FLAG_HIDDEN);

    g_group = lv_group_create();
    lv_group_add_obj(g_group, g_root);
    lv_group_focus_obj(g_root);
    if (g_keyboard_indev) lv_indev_set_group(g_keyboard_indev, g_group);

    load_active_preset();
    update_labels();
    g_renderer.render(g_bodies, g_camera, g_selected_id);
    lv_timer_create(frame_timer_cb, 16, nullptr);
}

int get_st7789v_fbdev(char *device_path, std::size_t buffer_size)
{
    if (!device_path || buffer_size == 0) return -1;
    FILE *framebuffers = std::fopen("/proc/fb", "r");
    if (!framebuffers) return -1;
    char line[256];
    int framebuffer_number = -1;
    while (std::fgets(line, sizeof(line), framebuffers)) {
        if (std::strstr(line, "fb_st7789v") &&
            std::sscanf(line, "%d", &framebuffer_number) == 1) break;
    }
    std::fclose(framebuffers);
    if (framebuffer_number < 0) return -1;
    std::snprintf(device_path, buffer_size, "/dev/fb%d", framebuffer_number);
    return 0;
}

#if LV_USE_EVDEV
int evdev_to_lv_key(std::uint16_t code)
{
    switch (code) {
        case KEY_UP: return LV_KEY_UP;
        case KEY_DOWN: return LV_KEY_DOWN;
        case KEY_RIGHT: return LV_KEY_RIGHT;
        case KEY_LEFT: return LV_KEY_LEFT;
        case KEY_ESC: return LV_KEY_ESC;
        case KEY_DELETE: return LV_KEY_DEL;
        case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
        case KEY_ENTER: return LV_KEY_ENTER;
        case KEY_TAB: return KEY_TAB;
        default: return code;
    }
}

void keypad_read_cb(lv_indev_t *, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
    pthread_mutex_lock(&keyboard_mutex);
    if (!STAILQ_EMPTY(&keyboard_queue)) {
        key_item *item = STAILQ_FIRST(&keyboard_queue);
        STAILQ_REMOVE_HEAD(&keyboard_queue, entries);
        if (g_root) lv_obj_send_event(g_root, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), item);
        data->key = evdev_to_lv_key(item->key_code);
        data->state = item->key_state == KBD_KEY_RELEASED
                          ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED;
        data->continue_reading = !STAILQ_EMPTY(&keyboard_queue);
        std::free(item);
    }
    pthread_mutex_unlock(&keyboard_mutex);
}

void lv_linux_indev_init()
{
    const char *keyboard_device = getenv_default(
        "LV_LINUX_KEYBOARD_DEVICE", "/dev/input/by-path/platform-3f804000.i2c-event");
    pthread_t thread_id;
    if (pthread_create(&thread_id, nullptr, keyboard_read_thread,
                       const_cast<char *>(keyboard_device)) == 0) pthread_detach(thread_id);
    g_keyboard_indev = lv_indev_create();
    lv_indev_set_type(g_keyboard_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(g_keyboard_indev, keypad_read_cb);
}
#endif

#if LV_USE_LINUX_FBDEV
bool lv_linux_disp_init()
{
    char detected_device[64] = {};
    const char *device = getenv_default("LV_LINUX_FBDEV_DEVICE", nullptr);
    if (!device && get_st7789v_fbdev(detected_device, sizeof(detected_device)) == 0)
        device = detected_device;
    if (!device) return false;
    lv_display_t *display = lv_linux_fbdev_create();
    if (!display) return false;
    lv_linux_fbdev_set_file(display, device);
    return true;
}
#if !LV_USE_EVDEV && !LV_USE_LIBINPUT
void lv_linux_indev_init() {}
#endif
#elif LV_USE_SDL
bool lv_linux_disp_init()
{
    lv_display_t *display = lv_sdl_window_create(kScreenWidth, kScreenHeight);
    if (!display) return false;
    lv_sdl_window_set_title(display, "Astro Box - Cardputer Zero");
    return true;
}
void lv_linux_indev_init()
{
    lv_sdl_mouse_create();
    g_keyboard_indev = lv_sdl_keyboard_create();
}
#else
#error Unsupported display configuration
#endif

} 

int main()
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    lv_init();
    if (!lv_linux_disp_init()) return 1;
    LV_EVENT_KEYBOARD = lv_event_register_id();
    lv_linux_indev_init();
    build_ui();
    while (!g_quit_requested) {
        lv_timer_handler();
        usleep(2000);
    }
    return 0;
}
