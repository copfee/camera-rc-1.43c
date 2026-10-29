#include "lvgl.h"
#include "rc/runtime.hpp"
#include "rc/strings.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace rc {
Snapshot preview_state;
std::vector<Action> actions;
Snapshot snapshot() {
    return preview_state;
}
struct Payload {
    uint32_t argument, context;
};
std::vector<Payload> payloads;
void post(Action action, uint32_t argument, uint32_t context) {
    payloads.push_back({argument, context});
    actions.push_back(action);
}
void ui_preview(Board&, bool, bool);
void ui_preview_update();
} // namespace rc

namespace {
std::vector<uint8_t> rgb(466 * 466 * 3);
std::vector<uint32_t> frame_times;
std::vector<lv_area_t> flush_areas;
lv_obj_t* find_label(lv_obj_t* parent, const char* text) {
    if (lv_obj_check_type(parent, &lv_label_class) && std::strcmp(lv_label_get_text(parent), text) == 0)
        return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        if (auto* found = find_label(lv_obj_get_child(parent, i), text))
            return found;
    }
    return nullptr;
}
lv_obj_t* find_photo(lv_obj_t* parent) {
    if (lv_obj_check_type(parent, &lv_imagebutton_class))
        return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        if (auto* found = find_photo(lv_obj_get_child(parent, i)))
            return found;
    }
    return nullptr;
}
lv_obj_t* find_progress_arc(lv_obj_t* parent) {
    if (lv_obj_check_type(parent, &lv_arc_class))
        return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        if (auto* found = find_progress_arc(lv_obj_get_child(parent, i)))
            return found;
    }
    return nullptr;
}
void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    flush_areas.push_back(*area);
    if (lv_display_flush_is_last(display))
        frame_times.push_back(lv_tick_get());
    auto* src = reinterpret_cast<uint16_t*>(pixels);
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            const auto pixel = *src++;
            const auto offset = (y * 466 + x) * 3;
            rgb[offset] = ((pixel >> 11) & 31) * 255 / 31;
            rgb[offset + 1] = ((pixel >> 5) & 63) * 255 / 63;
            rgb[offset + 2] = (pixel & 31) * 255 / 31;
        }
    }
    lv_display_flush_ready(display);
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "ui_preview output.ppm ready|blocked|status|details|pairing\n";
        return 1;
    }
    const std::string scenario = argv[2];
    const bool status_input = scenario == "connection-status";
    const bool identity_input = scenario == "device-identity";
    const bool layout_input = scenario == "shooting-layout" || scenario == "hint-fade";
    const bool cadence_input = scenario == "spinner-cadence" || scenario == "shutter-feedback" ||
                               scenario == "device-refresh" || scenario == "progress-continuity";
    const bool navigation_input = scenario == "button-navigation" || scenario == "no-swipe-navigation" ||
                                  scenario == "device-connect" || scenario == "device-forget";
    const bool progress_input = scenario == "connecting-animation" || scenario == "spinner-cadence" ||
                                scenario == "progress-continuity";
    const bool blocked_input =
        scenario == "blocked-swipe" || scenario == "blocked-status-tap" || scenario == "blocked-photo";
    const bool connection_input = scenario == "scan-select" || scenario == "scan-cancel" ||
                                  scenario == "confirm-yes" || scenario == "confirm-no" ||
                                  scenario == "retry";
    const bool interaction = layout_input || status_input || identity_input || cadence_input ||
                             navigation_input || progress_input || connection_input || blocked_input ||
                             scenario == "idle-touch-error" || scenario == "touch-error-recover" ||
                             scenario == "touch-shot" || scenario == "swipe-cancel" ||
                             scenario == "wake-touch" || scenario == "removed-video" ||
                             scenario == "touch-error" || scenario == "photo-hold";
    if (scenario != "ready" && scenario != "blocked" && scenario != "status" && scenario != "details" &&
        scenario != "pairing" && scenario != "connecting" && scenario != "confirm" && scenario != "error" &&
        scenario != "status-actions" && scenario != "disconnected" && !interaction)
        return 1;
    auto& state = rc::preview_state;
    const bool ready = scenario == "ready" || scenario == "status" ||
                       (interaction && !blocked_input && !connection_input && !progress_input);
    state.view.connection = ready ? rc::ConnectionState::Ready : rc::ConnectionState::ProtocolBlocked;
    state.view.can_photo = ready;
    if (ready) {
        state.link.stage = rc::LinkStage::Ready;
        state.link.saved = true;
        std::strcpy(state.link.name, "Fujifilm X-E5");
        std::strcpy(state.link.address, "aa:bb:cc:dd:ee:01");
    }
    if (scenario == "pairing" || scenario == "scan-select" || scenario == "scan-cancel") {
        state.view.connection = rc::ConnectionState::Pairing;
        state.link.stage = rc::LinkStage::Scanning;
        state.link.scan_generation = 7;
        state.link.count = 2;
        for (unsigned i = 0; i < 2; ++i) {
            auto& c = state.link.candidates[i];
            c.id = i + 1;
            std::strcpy(c.name, "X-E5");
            std::strcpy(c.suffix, i ? "BB:02" : "AA:01");
            c.rssi = i ? -68 : -54;
        }
    }
    if (scenario == "connecting" || progress_input) {
        state.view.connection = rc::ConnectionState::Preparing;
        state.link.stage = rc::LinkStage::Handshake;
        std::strcpy(state.link.name, "Fujifilm X-E5");
    }
    if (scenario == "confirm" || scenario == "confirm-yes" || scenario == "confirm-no") {
        state.view.connection = rc::ConnectionState::Preparing;
        state.link.stage = rc::LinkStage::Confirming;
        state.link.passkey = 482916;
        state.link.session = 13;
    }
    if (scenario == "error" || scenario == "retry") {
        state.view.connection = rc::ConnectionState::Error;
        state.link.stage = rc::LinkStage::Failed;
        state.link.error = rc::LinkError::Timeout;
    }

    if (scenario == "wake-touch")
        state.view.power = rc::DisplayPower::Off;
    if (scenario == "status-actions" || scenario == "disconnected" || scenario == "device-connect" ||
        scenario == "device-forget") {
        state.view.connection = rc::ConnectionState::Disconnected;
        state.view.can_photo = false;
        state.link.stage = rc::LinkStage::Idle;
        state.link.saved = true;
        state.link.address_saved = true;
        std::strcpy(state.link.name, "Fujifilm X-E5");
        std::strcpy(state.link.address, "aa:bb:cc:dd:ee:01");
    }
    if (scenario == "status" || scenario == "status-actions") {
        state.battery = {true, 3920, false};
        state.percentage = 73;
        state.rssi_valid = true;
        state.rssi = -54;
        std::strcpy(state.peer, "aa:bb:cc:dd:ee:01");
    }
    lv_init();
    auto* display = lv_display_create(466, 466);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    // Cadence tests use the firmware's partial rendering path and 40-row buffers.
    std::vector<uint16_t> buffer(466 * (cadence_input || status_input ? 40 : 466));
    lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size() * 2,
                           cadence_input || status_input ? LV_DISPLAY_RENDER_MODE_PARTIAL
                                                         : LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    rc::Board board;
    rc::ui_preview(board,
                   scenario == "status" || scenario == "status-actions" || scenario == "device-refresh" ||
                       identity_input,
                   scenario == "details");
    if (scenario == "status-actions") {
        auto* content = lv_obj_get_parent(find_label(lv_screen_active(), rc::text::battery));
        lv_obj_scroll_to_y(content, LV_COORD_MAX, LV_ANIM_OFF);
    }
    lv_refr_now(display);
    if (interaction) {
        auto* input = lv_indev_get_next(nullptr);
        const auto sample = [&](bool down, int x, int y, bool error = false) {
            rc::Board::sample = {down, error, x, y};
            lv_tick_inc(30);
            lv_indev_read(input);
            lv_obj_update_layout(lv_screen_active());
        };
        const auto advance_animation = [&](unsigned duration) {
            for (unsigned elapsed = 0; elapsed < duration; elapsed += 20) {
                lv_tick_inc(20);
                lv_anim_refr_now();
            }
            lv_obj_update_layout(lv_screen_active());
        };
        if (scenario == "idle-touch-error" || blocked_input) {
            sample(false, 0, 0, true);
            sample(false, 0, 0, true);
        }
        const auto tap = [&](int x, int y) {
            sample(true, x, y);
            sample(false, x, y);
        };
        const auto advance_timers = [&](unsigned duration) {
            for (unsigned elapsed = 0; elapsed < duration; ++elapsed) {
                lv_tick_inc(1);
                rc::ui_preview_update();
                lv_timer_handler();
            }
        };
        if (scenario == "hint-fade") {
            state.view.notice = rc::Notice::CommandSent;
            advance_timers(32);
            auto* hint = find_label(lv_screen_active(), rc::text::sent);
            if (!hint || lv_obj_get_style_opa(hint, LV_PART_MAIN) != LV_OPA_COVER)
                return 80;
            advance_timers(9900);
            if (lv_obj_get_style_opa(hint, LV_PART_MAIN) != LV_OPA_COVER)
                return 81;
            advance_timers(320);
            const auto fading = lv_obj_get_style_opa(hint, LV_PART_MAIN);
            if (fading == LV_OPA_COVER || fading == LV_OPA_TRANSP)
                return 82;
            advance_timers(400);
            if (lv_obj_get_style_opa(hint, LV_PART_MAIN) != LV_OPA_TRANSP)
                return 83;
            state.view.capture = rc::CapturePhase::Holding;
            advance_timers(32);
            if (std::strcmp(lv_label_get_text(hint), rc::text::holding) != 0 ||
                lv_obj_get_style_opa(hint, LV_PART_MAIN) != LV_OPA_COVER)
                return 84;
            advance_timers(10600);
            if (lv_obj_get_style_opa(hint, LV_PART_MAIN) != LV_OPA_TRANSP)
                return 85;
        } else if (layout_input) {
            auto* photo = find_photo(lv_screen_active());
            auto* shoot = lv_obj_get_parent(photo);
            auto* status = find_label(shoot, rc::text::ready);
            std::vector<lv_obj_t*> images;
            for (uint32_t i = 0; i < lv_obj_get_child_count(shoot); ++i) {
                auto* child = lv_obj_get_child(shoot, i);
                if (lv_obj_check_type(child, &lv_image_class))
                    images.push_back(child);
            }
            if (images.size() != 2 || !status)
                return 60;
            lv_area_t brand_area, model_area, photo_area, status_area;
            lv_obj_get_coords(images[0], &brand_area);
            lv_obj_get_coords(images[1], &model_area);
            lv_obj_get_coords(photo, &photo_area);
            lv_obj_get_coords(status, &status_area);
            if (brand_area.y2 >= model_area.y1 || model_area.y2 >= photo_area.y1 - 8 ||
                model_area.x1 + model_area.x2 != photo_area.x1 + photo_area.x2 ||
                lv_image_get_scale_x(images[1]) != lv_image_get_scale_y(images[1]) ||
                lv_obj_get_width(photo) != 260 || lv_obj_get_height(photo) != 260 ||
                photo_area.y2 >= status_area.y1 - 3 || status_area.y2 + 3 >= 466 ||
                !lv_obj_has_flag(images[0], LV_OBJ_FLAG_CLICKABLE) ||
                find_label(shoot, rc::text::status_title))
                return 61;
            // The supplied model image and the former status position must not accept camera input.
            tap(233, 79);
            tap(233, 98);
            if (rc::Board::shutter_sounds ||
                std::count(rc::actions.begin(), rc::actions.end(), rc::Action::Connect) ||
                std::count(rc::actions.begin(), rc::actions.end(), rc::Action::PhotoDown))
                return 62;
            state.view.notice = rc::Notice::CommandSent;
            advance_timers(32);
            auto* hint = find_label(shoot, rc::text::sent);
            lv_area_t hint_area;
            if (!hint)
                return 63;
            lv_obj_get_coords(hint, &hint_area);
            if (hint_area.y1 <= photo_area.y2 + 8 || hint_area.y2 >= status_area.y1 - 3 ||
                lv_obj_get_style_text_font(hint, LV_PART_MAIN) !=
                    lv_obj_get_style_text_font(status, LV_PART_MAIN))
                return 64;
            state.view.capture = rc::CapturePhase::Holding;
            advance_timers(32);
            if (!find_label(shoot, rc::text::holding))
                return 65;
        } else if (status_input) {
            auto* shoot =
                lv_obj_get_parent(find_photo(lv_screen_active()));
            auto* status = find_label(shoot, rc::text::ready);
            if (!status || !lv_obj_is_visible(status) ||
                !lv_color_eq(lv_obj_get_style_text_color(status, LV_PART_MAIN), lv_color_hex(0x8bc5a1)))
                return 40;
            const auto static_status = [&]() {
                advance_timers(160);
                frame_times.clear();
                advance_timers(500);
                return lv_anim_count_running() == 0 && frame_times.empty();
            };
            if (!static_status())
                return 41;
            tap(233, 420); // Connected status must not start another connection.
            if (std::count(rc::actions.begin(), rc::actions.end(), rc::Action::Connect))
                return 42;
            tap(233, 55);
            tap(233, 426);
            if (!lv_obj_is_visible(status) || !static_status())
                return 43;
            state.view.connection = rc::ConnectionState::Disconnected;
            state.view.can_photo = false;
            state.link.stage = rc::LinkStage::Idle;
            advance_timers(32);
            if (std::strcmp(lv_label_get_text(status), rc::text::disconnected) ||
                !lv_color_eq(lv_obj_get_style_text_color(status, LV_PART_MAIN), lv_color_hex(0x8f908d)) ||
                !static_status())
                return 44;
            tap(233, 420);
            if (std::count(rc::actions.begin(), rc::actions.end(), rc::Action::Connect) != 1)
                return 45;
            state.view.connection = rc::ConnectionState::Preparing;
            state.link.stage = rc::LinkStage::Handshake;
            advance_timers(32);
            if (!find_progress_arc(lv_screen_active()) ||
                std::count(rc::actions.begin(), rc::actions.end(), rc::Action::PhotoDown) ||
                std::count(rc::actions.begin(), rc::actions.end(), rc::Action::PhotoUp))
                return 46;
        } else if (identity_input) {
            auto* name = find_label(lv_screen_active(), state.link.name);
            auto* address_caption = find_label(lv_screen_active(), rc::text::bluetooth_address);
            auto* name_caption = find_label(lv_screen_active(), rc::text::bluetooth_name);
            if (!name || !name_caption || !address_caption)
                return 51;
            auto* address =
                lv_obj_get_child(lv_obj_get_parent(address_caption), lv_obj_get_index(address_caption) + 1);
            auto* content = lv_obj_get_parent(name);
            // Coordinates alone do not prove that the first frame painted the fixed chrome.
            const auto painted = [&](lv_obj_t* object) {
                lv_area_t area;
                lv_obj_get_coords(object, &area);
                unsigned pixels = 0;
                for (int y = std::max(0, area.y1); y <= std::min(465, area.y2); ++y)
                    for (int x = std::max(0, area.x1); x <= std::min(465, area.x2); ++x)
                        pixels += rgb[(y * 466 + x) * 3] > 60;
                return pixels > 30;
            };
            auto* info = lv_obj_get_parent(content);
            if (!painted(find_label(info, rc::text::status_title)) ||
                !painted(find_label(info, rc::text::return_shoot)))
                return 56;
            std::strcpy(state.link.name, "A camera with a deliberately long Bluetooth device name 1234567");
            std::strcpy(state.link.address, "aa:bb:cc:dd:ee:99");
            advance_timers(32);
            if (lv_obj_get_y(name) != lv_obj_get_y(name_caption) ||
                lv_obj_get_y(address) != lv_obj_get_y(address_caption) ||
                lv_obj_get_x(name) <= lv_obj_get_x(name_caption) ||
                lv_obj_get_x(address) <= lv_obj_get_x(address_caption) || lv_obj_get_height(name) >= 40 ||
                lv_label_get_long_mode(name) != LV_LABEL_LONG_DOT ||
                lv_label_get_long_mode(address) != LV_LABEL_LONG_DOT)
                return 52;
            state.link.address_saved = true;
            advance_timers(32);
            if (!find_label(content, rc::text::saved_address))
                return 53;
            lv_obj_scroll_to_y(content, LV_COORD_MAX, LV_ANIM_OFF);
            lv_obj_update_layout(content);
            auto* notice = find_label(content, rc::text::trademark);
            auto* forget = find_label(content, rc::text::repair);
            lv_area_t notice_area, forget_area;
            lv_obj_get_coords(notice, &notice_area);
            lv_obj_get_coords(forget, &forget_area);
            if (notice_area.y1 <= forget_area.y2 || notice_area.y2 > 392 || forget_area.y1 < 96)
                return 54;
            state.link.name[0] = state.link.address[0] = 0;
            state.link.saved = false;
            state.link.address_saved = false;
            advance_timers(32);
            if (std::strcmp(lv_label_get_text(name), rc::text::unpaired) ||
                std::strcmp(lv_label_get_text(address), rc::text::unpaired))
                return 55;
        } else if (cadence_input) {
            // Exercise the real LVGL timer scheduler, without forcing refreshes or animations.
            const auto reset_measurement = [&]() {
                frame_times.clear();
                flush_areas.clear();
            };
            const auto smooth_frames = [&]() {
                uint32_t max_gap = 0;
                for (size_t i = 1; i < frame_times.size(); ++i)
                    max_gap = std::max(max_gap, frame_times[i] - frame_times[i - 1]);
                std::cout << "Frames in 500ms: " << frame_times.size() << ", max gap: " << max_gap << "ms\n";
                if (frame_times.size() < 28)
                    return false;
                return max_gap <= 20;
            };
            const auto local_flushes = [&](int x1, int y1, int x2, int y2) {
                return std::all_of(flush_areas.begin(), flush_areas.end(), [&](const lv_area_t& area) {
                    const bool local = area.x1 >= x1 && area.y1 >= y1 && area.x2 <= x2 && area.y2 <= y2;
                    if (!local)
                        std::cerr << "Unexpected flush area: " << area.x1 << ',' << area.y1 << " to "
                                  << area.x2 << ',' << area.y2 << '\n';
                    return local;
                });
            };
            advance_timers(100);
            reset_measurement();
            if (scenario == "progress-continuity") {
                auto* arc = find_progress_arc(lv_screen_active());
                if (!arc)
                    return 30;
                auto* modal = lv_obj_get_parent(arc);
                unsigned deletions = 0;
                lv_obj_add_event_cb(
                    arc, [](lv_event_t* event) { ++*static_cast<unsigned*>(lv_event_get_user_data(event)); },
                    LV_EVENT_DELETE, &deletions);
                const auto timers = []() {
                    unsigned count = 0;
                    for (auto* timer = lv_timer_get_next(nullptr); timer; timer = lv_timer_get_next(timer))
                        ++count;
                    return count;
                };
                const auto running_timers = timers();
                const auto cancel_count =
                    std::count(rc::actions.begin(), rc::actions.end(), rc::Action::Cancel);
                const auto arc_pixels = [&]() {
                    unsigned count = 0;
                    for (int y = 132; y < 204; ++y) {
                        for (int x = 197; x < 269; ++x) {
                            const auto offset = (y * 466 + x) * 3;
                            if (rgb[offset] > 160 && rgb[offset + 1] > 110 && rgb[offset + 2] < 150)
                                ++count;
                        }
                    }
                    return count;
                };
                const auto visible_pixels = arc_pixels();
                auto rotation = lv_arc_get_rotation(arc);
                // Four complete loops, with stage and session changes away from the loop seam.
                for (unsigned frame = 0; frame < 300; ++frame) {
                    if (frame == 19)
                        state.link.stage = rc::LinkStage::Connecting;
                    if (frame == 37)
                        state.link.stage = rc::LinkStage::Securing;
                    if (frame == 58)
                        state.link.stage = rc::LinkStage::Handshake;
                    if (frame == 91) {
                        state.link.stage = rc::LinkStage::Retry;
                        ++state.link.session;
                    }
                    if (frame == 113) {
                        state.link.saved = true;
                        state.link.stage = rc::LinkStage::Scanning;
                        ++state.link.scan_generation;
                    }
                    if (frame == 137) {
                        state.link.stage = rc::LinkStage::Connecting;
                        ++state.link.count;
                    }
                    if (frame == 157)
                        std::strcpy(state.link.name, "X-E5 updated");
                    advance_timers(16);
                    if (deletions || find_progress_arc(lv_screen_active()) != arc ||
                        lv_obj_get_parent(arc) != modal || timers() != running_timers)
                        return 31;
                    const auto current = lv_arc_get_rotation(arc);
                    const auto step = (current - rotation + 360) % 360;
                    if (step < 4 || step > 6 || lv_arc_get_angle_start(arc) != 0 ||
                        lv_arc_get_angle_end(arc) != 90 || !visible_pixels ||
                        arc_pixels() < visible_pixels * 7 / 10 || arc_pixels() > visible_pixels * 13 / 10)
                        return 32;
                    rotation = current;
                }
                if (!find_label(modal, rc::text::stage_connect) || find_label(modal, "X-E5 updated") ||
                    !local_flushes(73, 60, 392, 357) || // Include the title font's glyph overhang.
                    std::count(rc::actions.begin(), rc::actions.end(), rc::Action::Cancel) != cancel_count) {
                    std::cerr << "Progress labels or local refresh changed across stages\n";
                    return 33;
                }
                // A delayed tick must catch up to the same clock instead of restarting a loop.
                advance_timers(32);
                const auto before_stall = lv_arc_get_rotation(arc);
                lv_tick_inc(137);
                rc::ui_preview_update();
                lv_timer_handler();
                const auto catchup = (lv_arc_get_rotation(arc) - before_stall + 360) % 360;
                if (catchup < 40 || catchup > 42)
                    return 34;
                tap(233, 388);
                advance_timers(150);
                reset_measurement();
                advance_timers(1600);
                if (deletions != 1 || find_progress_arc(lv_screen_active()) ||
                    timers() != running_timers - 1 || !frame_times.empty() ||
                    std::count(rc::actions.begin(), rc::actions.end(), rc::Action::Disconnect) != 1)
                    return 35;
                state.link.stage = rc::LinkStage::Idle;
                state.view.connection = rc::ConnectionState::Disconnected;
                advance_timers(32);
                // Every exit from progress must stop its timer, including protocol confirmation.
                for (const auto stage : {rc::LinkStage::Confirming, rc::LinkStage::Failed,
                                         rc::LinkStage::Ready, rc::LinkStage::Idle}) {
                    state.link.stage = rc::LinkStage::Connecting;
                    state.view.connection = rc::ConnectionState::Preparing;
                    advance_timers(32);
                    arc = find_progress_arc(lv_screen_active());
                    if (!arc || timers() != running_timers)
                        return 36;
                    lv_obj_add_event_cb(
                        arc,
                        [](lv_event_t* event) { ++*static_cast<unsigned*>(lv_event_get_user_data(event)); },
                        LV_EVENT_DELETE, &deletions);
                    const auto before_exit = deletions;
                    state.link.stage = stage;
                    state.view.connection = stage == rc::LinkStage::Failed  ? rc::ConnectionState::Error
                                            : stage == rc::LinkStage::Ready ? rc::ConnectionState::Ready
                                            : stage == rc::LinkStage::Idle ? rc::ConnectionState::Disconnected
                                                                           : rc::ConnectionState::Preparing;
                    advance_timers(32);
                    if (deletions != before_exit + 1 || find_progress_arc(lv_screen_active()) ||
                        timers() != running_timers - 1)
                        return 37;
                    if (stage == rc::LinkStage::Confirming &&
                        !find_label(lv_screen_active(), rc::text::numbers_match))
                        return 38;
                }
            } else if (scenario == "spinner-cadence") {
                advance_timers(500);
                if (!smooth_frames() || !local_flushes(180, 116, 286, 220))
                    return 20;
            } else if (scenario == "shutter-feedback") {
                auto* photo = find_photo(lv_screen_active());
                if (!photo || !lv_imagebutton_get_src_middle(photo, LV_IMAGEBUTTON_STATE_RELEASED) ||
                    !lv_imagebutton_get_src_middle(photo, LV_IMAGEBUTTON_STATE_PRESSED) ||
                    lv_imagebutton_get_src_middle(photo, LV_IMAGEBUTTON_STATE_RELEASED) ==
                        lv_imagebutton_get_src_middle(photo, LV_IMAGEBUTTON_STATE_PRESSED))
                    return 22;
                rc::Board::sample = {true, false, 233, 210};
                advance_timers(24);
                if (std::count(rc::actions.begin(), rc::actions.end(), rc::Action::PhotoDown) != 1 ||
                    !lv_obj_has_state(photo, LV_STATE_PRESSED) || frame_times.empty())
                    return 21;
                rc::Board::sample = {};
                advance_timers(32);
                if (std::count(rc::actions.begin(), rc::actions.end(), rc::Action::PhotoUp) != 1 ||
                    lv_obj_has_state(photo, LV_STATE_PRESSED) || !local_flushes(103, 104, 362, 363))
                    return 23;
            } else {
                advance_timers(500);
                if (!frame_times.empty()) // Unchanged snapshots must not repaint the shutter or device page.
                    return 26;
                state.percentage = 73;
                state.battery = {true, 3920, false};
                state.rssi_valid = true;
                state.rssi = -54;
                advance_timers(32);
                if (frame_times.empty() || !find_label(lv_screen_active(), "73%") ||
                    !local_flushes(84, 96, 381, 391))
                    return 27;
                auto* content = lv_obj_get_parent(find_label(lv_screen_active(), rc::text::battery));
                lv_obj_add_event_cb(
                    content,
                    [](lv_event_t* event) {
                        auto* animation = static_cast<lv_anim_t*>(lv_event_get_param(event));
                        if (animation) {
                            lv_anim_set_duration(animation, 600);
                            lv_anim_set_path_cb(animation, lv_anim_path_linear);
                        }
                    },
                    LV_EVENT_SCROLL_BEGIN, nullptr);
                lv_obj_scroll_to_y(content, LV_COORD_MAX, LV_ANIM_ON);
                reset_measurement();
                advance_timers(500);
                if (!smooth_frames() || !local_flushes(84, 96, 381, 391))
                    return 28;
                advance_timers(200);
                reset_measurement();
                advance_timers(500);
                if (!frame_times.empty())
                    return 29;
            }
        } else if (progress_input) {
            auto* spinner = find_progress_arc(lv_screen_active());
            if (!spinner)
                return 13;
            // Advance well beyond one cycle, then verify that the arc still moves.
            for (unsigned cycle = 0; cycle < 4; ++cycle) {
                advance_timers(1240);
                const auto angle = lv_arc_get_rotation(spinner);
                advance_timers(200);
                if (lv_arc_get_rotation(spinner) == angle)
                    return 14;
            }
            // A horizontal drag over Cancel must not dismiss the overlay.
            sample(true, 233, 388);
            sample(true, 310, 388);
            sample(false, 310, 388);
            if (!find_progress_arc(lv_screen_active()) ||
                std::count(rc::actions.begin(), rc::actions.end(), rc::Action::Disconnect))
                return 15;
            tap(233, 388);
            advance_timers(1600);
            if (find_progress_arc(lv_screen_active()) || lv_anim_count_running() != 0 ||
                std::count(rc::actions.begin(), rc::actions.end(), rc::Action::Disconnect) != 1)
                return 16;
        } else if (navigation_input) {
            auto* shoot =
                lv_obj_get_parent(find_photo(lv_screen_active()));
            auto* info =
                lv_obj_get_parent(lv_obj_get_parent(find_label(lv_screen_active(), rc::text::battery)));
            if (scenario == "no-swipe-navigation") {
                sample(true, 90, 233);
                sample(true, 170, 233);
                sample(false, 170, 233);
                advance_animation(240);
                if (!lv_obj_is_visible(shoot) || lv_obj_is_visible(info))
                    return 7;
                // Dragging on the footer must not turn into a navigation click.
                sample(true, 200, 426);
                sample(true, 280, 426);
                sample(false, 280, 426);
                if (!lv_obj_is_visible(shoot) || lv_obj_is_visible(info))
                    return 7;
            }
            tap(233, 55);
            if (lv_obj_is_visible(shoot) || !lv_obj_is_visible(info))
                return 8;
            auto* content = lv_obj_get_parent(find_label(info, rc::text::battery));
            if (scenario == "no-swipe-navigation") {
                sample(true, 370, 233);
                sample(true, 290, 233);
                sample(false, 290, 233);
                advance_animation(240);
                if (lv_obj_is_visible(shoot) || !lv_obj_is_visible(info))
                    return 9;
            }
            // Device details still scroll vertically; both actions travel with them.
            auto* connect = lv_obj_get_parent(find_label(
                info, state.link.stage == rc::LinkStage::Ready ? rc::text::disconnect : rc::text::connect));
            auto* forget = lv_obj_get_parent(find_label(info, rc::text::repair));
            if (lv_obj_get_parent(lv_obj_get_parent(connect)) != content ||
                lv_obj_get_parent(connect) != lv_obj_get_parent(forget) ||
                lv_obj_get_y(connect) != lv_obj_get_y(forget) ||
                lv_obj_get_x(connect) >= lv_obj_get_x(forget))
                return 17;
            sample(true, 233, 280);
            sample(true, 235, 240);
            sample(true, 237, 190);
            sample(false, 237, 190);
            advance_animation(400);
            if (lv_obj_get_scroll_y(content) <= 0 || !lv_obj_is_visible(info))
                return 11;
            if (scenario == "device-connect" || scenario == "device-forget") {
                lv_obj_scroll_to_y(content, LV_COORD_MAX, LV_ANIM_OFF);
                lv_obj_update_layout(lv_screen_active());
                lv_area_t area;
                lv_obj_get_coords(scenario == "device-connect" ? connect : forget, &area);
                tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
                if (scenario == "device-connect" &&
                    std::count(rc::actions.begin(), rc::actions.end(), rc::Action::Connect) != 1)
                    return 18;
                if (scenario == "device-forget") {
                    auto* title = find_label(lv_screen_active(), rc::text::forget_title);
                    if (!title || !lv_obj_is_visible(title) ||
                        std::count(rc::actions.begin(), rc::actions.end(), rc::Action::Forget))
                        return 19;
                    tap(233, 388);
                }
            }
            tap(233, 426);
            if (!lv_obj_is_visible(shoot) || lv_obj_is_visible(info) || rc::Board::shutter_sounds ||
                std::count(rc::actions.begin(), rc::actions.end(), rc::Action::PhotoDown) ||
                std::count(rc::actions.begin(), rc::actions.end(), rc::Action::PhotoUp))
                return 10;
        } else if (connection_input) {
            int x = 233, y = 330;
            if (scenario == "scan-select")
                y = 180;
            if (scenario == "scan-cancel") {
                x = 300;
                y = 360;
            }
            if (scenario == "confirm-no")
                y = 395;
            sample(true, x, y);
            sample(false, x, y);
            const rc::Action wanted = scenario == "scan-select"   ? rc::Action::Select
                                      : scenario == "scan-cancel" ? rc::Action::Disconnect
                                      : scenario == "confirm-yes" ? rc::Action::Confirm
                                      : scenario == "confirm-no"  ? rc::Action::Reject
                                                                  : rc::Action::Connect;
            auto it = std::find(rc::actions.begin(), rc::actions.end(), wanted);
            if (it == rc::actions.end())
                return 3;
            const auto index = size_t(it - rc::actions.begin());
            if (wanted == rc::Action::Select &&
                (rc::payloads[index].argument != 1 || rc::payloads[index].context != 7))
                return 4;
            if ((wanted == rc::Action::Confirm || wanted == rc::Action::Reject) &&
                rc::payloads[index].context != 13)
                return 5;
            if (rc::Board::shutter_sounds)
                return 6;
        } else {
            const int start_y = scenario == "removed-video"        ? 380
                                : scenario == "blocked-status-tap" ? 420
                                                                   : 210;
            sample(true, 233, start_y);
            if (scenario == "swipe-cancel")
                sample(true, 310, 210);
            if (scenario == "touch-error" || scenario == "touch-error-recover")
                sample(false, 0, 0, true);
            if (scenario == "touch-error-recover")
                sample(true, 233, 210);
            bool stayed_on_shoot = false;
            if (scenario == "blocked-swipe") {
                sample(true, 310, 210);
                advance_animation(240);
                auto* title = find_label(lv_screen_active(), rc::text::battery);
                stayed_on_shoot = title && !lv_obj_is_visible(title);
            }
            if (scenario == "removed-video") {
                lv_tick_inc(700);
                sample(true, 233, 380);
            }
            if (scenario == "photo-hold") {
                lv_tick_inc(1000);
                sample(true, 233, 210);
                lv_tick_inc(1000);
                sample(true, 233, 210);
            }
            sample(false, 233, start_y);
            if (scenario == "touch-error-recover") {
                // A held finger after the fault must stay cancelled; a new touch works.
                if (std::count(rc::actions.begin(), rc::actions.end(), rc::Action::PhotoUp) != 0)
                    return 2;
                sample(true, 233, 210);
                sample(false, 233, 210);
            }
            if (scenario == "blocked-swipe") {
                sample(true, 310, 210);
                sample(true, 233, 210);
                sample(false, 233, 210);
                advance_animation(240);
                auto* title = find_label(lv_screen_active(), rc::text::battery);
                stayed_on_shoot = stayed_on_shoot && title && !lv_obj_is_visible(title);
            }
            const auto count = [](rc::Action action) {
                return std::count(rc::actions.begin(), rc::actions.end(), action);
            };
            bool passed = false;
            if (scenario == "touch-shot" || scenario == "idle-touch-error" || scenario == "photo-hold")
                passed = count(rc::Action::PhotoDown) == 1 && count(rc::Action::PhotoUp) == 1;
            if (scenario == "touch-error-recover")
                passed = count(rc::Action::PhotoDown) == 2 && count(rc::Action::PhotoUp) == 1 &&
                         count(rc::Action::Cancel) >= 1;
            if (blocked_input) {
                auto* details = find_label(lv_screen_active(), rc::text::protocol_details);
                passed = (scenario == "blocked-swipe"   ? stayed_on_shoot
                          : scenario == "blocked-photo" ? true
                                                        : details && lv_obj_is_visible(details)) &&
                         count(rc::Action::PhotoDown) == 0 && count(rc::Action::PhotoUp) == 0;
            }
            if (scenario == "swipe-cancel" || scenario == "touch-error")
                passed = count(rc::Action::PhotoDown) == 1 && count(rc::Action::PhotoUp) == 0 &&
                         count(rc::Action::Cancel) >= 1;
            if (scenario == "wake-touch")
                passed = count(rc::Action::PhotoDown) == 0 && count(rc::Action::PhotoUp) == 0;
            if (scenario == "removed-video")
                passed = count(rc::Action::PhotoDown) == 0 && count(rc::Action::PhotoUp) == 0;
            // One local click per press, including the dimmed protocol-blocked shutter.
            // Wake-only gestures, status dialogs and movie clicks produce no shutter sound.
            const unsigned expected_sounds =
                scenario == "touch-error-recover" || scenario == "blocked-swipe" ? 2
                : scenario == "blocked-status-tap" || scenario == "wake-touch" || scenario == "removed-video"
                    ? 0
                    : 1;
            passed = passed && rc::Board::shutter_sounds == expected_sounds;
            if (!passed) {
                std::cerr << "UI input scenario failed: " << scenario << '\n';
                return 2;
            }
        }
    }
    std::ofstream file(argv[1], std::ios::binary);
    file << "P6\n466 466\n255\n";
    file.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    return file ? 0 : 1;
}
