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
} // namespace rc

namespace {
std::vector<uint8_t> rgb(466 * 466 * 3);
lv_obj_t* find_label(lv_obj_t* parent, const char* text) {
    if (lv_obj_check_type(parent, &lv_label_class) && std::strcmp(lv_label_get_text(parent), text) == 0)
        return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        if (auto* found = find_label(lv_obj_get_child(parent, i), text))
            return found;
    }
    return nullptr;
}
void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
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
    const bool navigation_input = scenario == "horizontal-navigation" || scenario == "horizontal-reverse";
    const bool blocked_input =
        scenario == "blocked-swipe" || scenario == "blocked-status-tap" || scenario == "blocked-photo";
    const bool connection_input = scenario == "scan-select" || scenario == "scan-cancel" ||
                                  scenario == "confirm-yes" || scenario == "confirm-no" ||
                                  scenario == "retry";
    const bool interaction = navigation_input || connection_input || blocked_input ||
                             scenario == "idle-touch-error" || scenario == "touch-error-recover" ||
                             scenario == "touch-shot" || scenario == "swipe-cancel" ||
                             scenario == "wake-touch" || scenario == "removed-video" ||
                             scenario == "touch-error" || scenario == "photo-hold";
    if (scenario != "ready" && scenario != "blocked" && scenario != "status" && scenario != "details" &&
        scenario != "pairing" && scenario != "connecting" && scenario != "confirm" && scenario != "error" &&
        !interaction)
        return 1;
    auto& state = rc::preview_state;
    const bool ready =
        scenario == "ready" || scenario == "status" || (interaction && !blocked_input && !connection_input);
    state.view.connection = ready ? rc::ConnectionState::Ready : rc::ConnectionState::ProtocolBlocked;
    state.view.can_photo = ready;
    if (ready) {
        state.link.stage = rc::LinkStage::Ready;
        state.link.saved = true;
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
    if (scenario == "connecting") {
        state.view.connection = rc::ConnectionState::Preparing;
        state.link.stage = rc::LinkStage::Handshake;
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
    if (scenario == "status") {
        state.battery = {true, 3920, false};
        state.percentage = 73;
        state.rssi_valid = true;
        state.rssi = -54;
        std::strcpy(state.peer, "aa:bb:cc:dd:ee:01");
    }
    lv_init();
    auto* display = lv_display_create(466, 466);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    std::vector<uint16_t> buffer(466 * 466);
    lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    rc::Board board;
    rc::ui_preview(board, scenario == "status", scenario == "details");
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
        if (navigation_input) {
            auto* shoot =
                lv_obj_get_parent(lv_obj_get_parent(find_label(lv_screen_active(), rc::text::photo)));
            auto* info =
                lv_obj_get_parent(lv_obj_get_parent(find_label(lv_screen_active(), rc::text::battery)));
            const auto horizontal_frame = [&]() {
                const int offset = lv_obj_get_x(shoot);
                return offset > 0 && offset < 466 && lv_obj_get_x(info) == offset - 466 &&
                       lv_obj_get_y(shoot) == 0 && lv_obj_get_y(info) == 0;
            };
            sample(true, 90, 233);
            sample(true, 170, 233);
            sample(false, 170, 233);
            advance_animation(60);
            if (!horizontal_frame()) {
                std::cerr << "Expected an intermediate horizontal page transition\n";
                return 7;
            }
            if (scenario == "horizontal-navigation") {
                advance_animation(240);
                if (lv_obj_is_visible(shoot) || !lv_obj_is_visible(info) || lv_obj_get_x(info) != 0)
                    return 8;
                // Vertical gestures still scroll device details without moving pages.
                auto* content = lv_obj_get_parent(find_label(info, rc::text::battery));
                sample(true, 233, 280);
                sample(true, 235, 240);
                sample(true, 237, 190);
                sample(false, 237, 190);
                advance_animation(400);
                if (lv_obj_get_scroll_y(content) <= 0 || lv_obj_get_x(info) != 0 || lv_obj_get_y(info) != 0 ||
                    !lv_obj_is_visible(info) || lv_obj_is_visible(shoot))
                    return 11;
            }
            // Reverse both a completed transition and one still in progress.
            sample(true, 370, 233);
            const int previous_offset = lv_obj_get_x(shoot);
            sample(true, 290, 245);
            if (lv_obj_get_x(shoot) != previous_offset)
                return 12;
            sample(false, 290, 245);
            advance_animation(40);
            if (!horizontal_frame())
                return 9;
            advance_animation(240);
            if (!lv_obj_is_visible(shoot) || lv_obj_is_visible(info) || lv_obj_get_x(shoot) != 0 ||
                rc::Board::shutter_sounds ||
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
                                : scenario == "blocked-status-tap" ? 90
                                                                   : 210;
            sample(true, 233, start_y);
            if (scenario == "swipe-cancel")
                sample(true, 310, 210);
            if (scenario == "touch-error" || scenario == "touch-error-recover")
                sample(false, 0, 0, true);
            if (scenario == "touch-error-recover")
                sample(true, 233, 210);
            bool navigated = false;
            if (scenario == "blocked-swipe") {
                sample(true, 310, 210);
                advance_animation(240);
                auto* title = find_label(lv_screen_active(), rc::text::battery);
                navigated = title && lv_obj_is_visible(title);
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
                navigated = navigated && title && !lv_obj_is_visible(title);
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
                passed = (scenario == "blocked-swipe"   ? navigated
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
                scenario == "touch-error-recover" ? 2
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
