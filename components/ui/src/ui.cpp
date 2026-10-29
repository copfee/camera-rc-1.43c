#include "rc/ui.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc/runtime.hpp"
#include "rc/strings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
LV_FONT_DECLARE(rc_font_20);
LV_FONT_DECLARE(rc_font_24);
LV_IMAGE_DECLARE(rc_fujifilm_logo);
LV_IMAGE_DECLARE(rc_xe5_logo);
LV_IMAGE_DECLARE(rc_shutter_idle);
LV_IMAGE_DECLARE(rc_shutter_pressed);
}

namespace rc {
namespace {
constexpr uint32_t kBlack = 0x000000, kSilver = 0xbcb9b0, kWhite = 0xf0ece1, kMuted = 0x8f908d;
constexpr uint32_t kGreen = 0x8bc5a1, kAmber = 0xd4ae76;
constexpr int kSwipe = 55;
constexpr uint32_t kFramePeriodMs = 16, kTouchPeriodMs = 8;
constexpr uint32_t kProgressPeriodMs = 1200;
constexpr uint32_t kHintVisibleMs = 10000, kHintFadeMs = 500;

const char* connection_text(ConnectionState state) {
    switch (state) {
    case ConnectionState::Unpaired:
        return text::unpaired;
    case ConnectionState::Pairing:
        return text::pairing;
    case ConnectionState::Waiting:
        return text::waiting;
    case ConnectionState::Preparing:
        return text::preparing;
    case ConnectionState::Ready:
        return text::ready;
    case ConnectionState::Reconnecting:
        return text::reconnecting;
    case ConnectionState::Disconnected:
        return text::disconnected;
    case ConnectionState::Error:
        return text::error;
    case ConnectionState::ProtocolBlocked:
        return text::protocol_title;
    }
    return text::unknown;
}

const char* notice_text(const ViewModel& view) {
    if (view.capture == CapturePhase::Holding)
        return text::holding;
    if (view.capture == CapturePhase::Focusing)
        return text::focusing;
    if (view.capture != CapturePhase::Idle)
        return text::photo_pending;
    switch (view.notice) {
    case Notice::NotConnected:
        return text::not_connected;
    case Notice::Busy:
        return text::busy;
    case Notice::CommandSent:
        return text::sent;
    case Notice::TransportFailure:
        return text::transport_error;
    case Notice::StorageFailure:
        return text::storage_error;
    case Notice::PairingTimeout:
        return text::timeout;
    default:
        return "";
    }
}

const char* stage_text(LinkStage s) {
    switch (s) {
    case LinkStage::Scanning:
        return text::stage_scan;
    case LinkStage::Connecting:
        return text::stage_connect;
    case LinkStage::Securing:
    case LinkStage::Confirming:
        return text::stage_secure;
    case LinkStage::Handshake:
        return text::stage_handshake;
    case LinkStage::Retry:
        return text::stage_retry;
    default:
        return text::unknown;
    }
}
const char* error_text(LinkError e) {
    switch (e) {
    case LinkError::Timeout:
        return text::err_timeout;
    case LinkError::Rejected:
        return text::err_rejected;
    case LinkError::Authentication:
        return text::err_auth;
    case LinkError::Gatt:
        return text::err_gatt;
    case LinkError::Storage:
        return text::storage_error;
    case LinkError::Overflow:
        return text::err_overflow;
    default:
        return text::err_unavailable;
    }
}
lv_obj_t* box(lv_obj_t* parent, int x, int y, int width, int height, uint32_t color = kBlack) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* label(lv_obj_t* parent, const char* value, int x, int y, int width,
                const lv_font_t* font = &rc_font_20, uint32_t color = kWhite) {
    auto* obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(obj, value);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* button(lv_obj_t* parent, int x, int y, int width, int height, uint32_t fill) {
    auto* obj = box(parent, x, y, width, height, fill);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(kSilver), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x333330), LV_STATE_PRESSED);
    lv_obj_set_style_opa(obj, LV_OPA_40, LV_STATE_DISABLED);
    return obj;
}

void set_enabled(lv_obj_t* obj, bool enabled) {
    if (enabled)
        lv_obj_remove_state(obj, LV_STATE_DISABLED);
    else
        lv_obj_add_state(obj, LV_STATE_DISABLED);
}

class RoundUI {
  public:
    explicit RoundUI(Board& board) : board_(board) {}
#ifdef RC_UI_PREVIEW
    void preview(bool info, bool details) {
        create();
        update();
        if (info)
            show_page(true);
        if (details)
            dialog(text::protocol_title, text::protocol_details, false);
    }
    void preview_update() { update(); }
#endif
    void run() {
        create();
        for (;;) {
            update();
            const uint32_t delay = lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(std::max<uint32_t>(2, std::min(delay, kTouchPeriodMs))));
        }
    }

  private:
    Board& board_;
    lv_obj_t *root_{}, *shoot_{}, *info_{}, *photo_{}, *status_{}, *hint_{};
    lv_obj_t *percent_{}, *voltage_{}, *charge_{}, *link_{}, *signal_{}, *connect_{};
    lv_obj_t *camera_name_{}, *address_{}, *address_caption_{}, *device_tail_{};
    lv_obj_t *connect_label_{}, *modal_{}, *repair_{};
    lv_obj_t *modal_title_{}, *progress_arc_{}, *progress_stage_{};
    lv_timer_t* progress_timer_{};
    uint32_t progress_started_{};
    lv_indev_t* input_{};
    Snapshot state_{};
    bool info_visible_{false}, gesture_down_{false}, swallow_{false}, cancelled_{false};
    bool photo_command_down_{false};
    int start_x_{0}, start_y_{0};
    DisplayPower power_{DisplayPower::Active};
    uint64_t next_update_{0};
    uint64_t hint_started_{0};
    LinkStage previous_stage_{LinkStage::Idle};
    uint8_t previous_count_{0};
    uint32_t previous_session_{0}, previous_scan_{0};
    enum class Overlay { None, Scan, Progress, Confirm, Error, Forget, Protocol };
    Overlay overlay_{Overlay::None};
    struct CandidateBinding {
        RoundUI* ui;
        uint32_t generation, id;
    };
    std::array<CandidateBinding, 16> bindings_{};
    std::array<lv_obj_t*, 16> candidate_names_{}, candidate_details_{};

    static RoundUI& self(lv_event_t* e) { return *static_cast<RoundUI*>(lv_event_get_user_data(e)); }
    static void touch_read(lv_indev_t* input, lv_indev_data_t* data) {
        auto& ui = *static_cast<RoundUI*>(lv_indev_get_user_data(input));
        const auto sample = ui.board_.touch();
        data->state = LV_INDEV_STATE_RELEASED;
        if (sample.error) {
            // An idle read failure must not swallow the next fresh gesture. Once a
            // gesture has started, keep cancelling until a valid release is seen.
            if (ui.gesture_down_) {
                post(Action::Cancel);
                ui.cancelled_ = ui.swallow_ = true;
                lv_indev_wait_release(input);
            }
            return;
        }
        if (sample.pressed) {
            data->point.x = sample.x;
            data->point.y = sample.y;
            if (!ui.gesture_down_) {
                ui.gesture_down_ = true;
                ui.cancelled_ = false;
                ui.start_x_ = sample.x;
                ui.start_y_ = sample.y;
                ui.swallow_ = ui.swallow_ || ui.state_.view.power == DisplayPower::Off;
                post(Action::Activity);
            }
            const int dx = sample.x - ui.start_x_, dy = sample.y - ui.start_y_;
            if (!ui.swallow_ && !ui.cancelled_ && std::abs(dx) >= kSwipe && std::abs(dx) > std::abs(dy)) {
                post(Action::Cancel);
                ui.cancelled_ = true;
                // Horizontal drags cancel held input without navigating or clicking.
                lv_indev_wait_release(input);
            }
            if (!ui.swallow_ && !ui.cancelled_)
                data->state = LV_INDEV_STATE_PRESSED;
        } else {
            ui.gesture_down_ = ui.swallow_ = ui.cancelled_ = false;
        }
    }

    static void photo_event(lv_event_t* e) {
        auto& ui = self(e);
        if (ui.cancelled_ || ui.swallow_ || ui.modal_)
            return;
        switch (lv_event_get_code(e)) {
        case LV_EVENT_PRESSED:
            ui.board_.shutter_sound();
            ui.photo_command_down_ = ui.state_.view.can_photo;
            if (ui.photo_command_down_)
                post(Action::PhotoDown);
            break;
        case LV_EVENT_RELEASED:
            if (ui.photo_command_down_)
                post(Action::PhotoUp);
            ui.photo_command_down_ = false;
            break;
        case LV_EVENT_PRESS_LOST:
            ui.photo_command_down_ = false;
            post(Action::Cancel);
            break;
        default:
            break;
        }
    }
    bool active() const {
        const auto c = state_.view.connection;
        return c == ConnectionState::Ready || c == ConnectionState::Waiting ||
               c == ConnectionState::Pairing || c == ConnectionState::Reconnecting ||
               c == ConnectionState::Preparing;
    }
    static void status_event(lv_event_t* e) {
        auto& ui = self(e);
        if (ui.state_.view.connection == ConnectionState::ProtocolBlocked)
            ui.dialog(text::protocol_title, text::protocol_details, false);
        else if (ui.state_.view.connection == ConnectionState::Ready)
            return;
        else if (ui.state_.link.stage != LinkStage::Idle && ui.state_.link.stage != LinkStage::Failed)
            ui.connection_overlay();
        else
            post(Action::Connect);
    }
    static void info_event(lv_event_t* e) { self(e).show_page(true); }
    static void shoot_event(lv_event_t* e) { self(e).show_page(false); }
    static void connect_event(lv_event_t* e) {
        auto& ui = self(e);
        post(ui.active() ? Action::Disconnect : Action::Connect);
    }
    static void repair_event(lv_event_t* e) { self(e).dialog(text::forget_title, text::forget_body, true); }
    static void close_event(lv_event_t* e) { self(e).close_dialog(); }
    static void stop_event(lv_event_t* e) {
        auto& ui = self(e);
        post(Action::Disconnect);
        ui.close_dialog();
    }
    static void retry_event(lv_event_t* e) {
        auto& ui = self(e);
        ui.close_dialog();
        post(Action::Connect);
    }
    static void rescan_event(lv_event_t*) { post(Action::Pair); }
    static void candidate_event(lv_event_t* e) {
        auto& b = *static_cast<CandidateBinding*>(lv_event_get_user_data(e));
        post(Action::Select, b.id, b.generation);
    }
    static void confirm_event(lv_event_t* e) {
        auto& ui = self(e);
        post(Action::Confirm, 0, ui.state_.link.session);
    }
    static void reject_event(lv_event_t* e) {
        auto& ui = self(e);
        post(Action::Reject, 0, ui.state_.link.session);
    }
    static void forget_event(lv_event_t* e) {
        auto& ui = self(e);
        ui.close_dialog();
        post(Action::Forget);
    }
    void close_dialog() {
        if (progress_timer_) {
            lv_timer_delete(progress_timer_);
            progress_timer_ = nullptr;
        }
        if (modal_) {
            lv_obj_delete(modal_);
            modal_ = nullptr;
            lv_obj_remove_flag(info_visible_ ? info_ : shoot_, LV_OBJ_FLAG_HIDDEN);
        }
        overlay_ = Overlay::None;
        modal_title_ = progress_arc_ = progress_stage_ = nullptr;
        candidate_names_.fill(nullptr);
        candidate_details_.fill(nullptr);
        post(Action::Cancel);
    }
    lv_obj_t* action_button(lv_obj_t* parent, const char* caption, int x, int y, int width, bool primary,
                            lv_event_cb_t cb) {
        auto* b = button(parent, x, y, width, 56, primary ? kWhite : kBlack);
        label(b, caption, 8, 15, width - 16, &rc_font_20, primary ? kBlack : kWhite);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, this);
        return b;
    }
    void begin_overlay(Overlay kind, const char* title) {
        close_dialog();
        overlay_ = kind;
        // Covered pages still receive state updates, without invalidating pixels behind the modal.
        lv_obj_add_flag(shoot_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(info_, LV_OBJ_FLAG_HIDDEN);
        modal_ = box(root_, 0, 0, 466, 466);
        lv_obj_add_flag(modal_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(modal_, LV_RADIUS_CIRCLE, 0);
        if (kind == Overlay::Progress) {
            auto* logo = lv_image_create(modal_);
            lv_image_set_src(logo, &rc_fujifilm_logo);
            lv_obj_set_pos(logo, 135, 63);
            lv_obj_remove_flag(logo, LV_OBJ_FLAG_CLICKABLE);
        } else {
            modal_title_ = label(modal_, title, 93, 74, 280, &rc_font_24);
        }
    }
    void dialog(const char* title, const char* body, bool confirmation) {
        begin_overlay(confirmation ? Overlay::Forget : Overlay::Protocol, title);
        label(modal_, body, 83, 155, 300, &rc_font_20, kMuted);
        if (confirmation)
            action_button(modal_, text::confirm, 123, 290, 220, true, forget_event);
        action_button(modal_, confirmation ? text::cancel : text::back, 123, 362, 220, false, close_event);
    }
    static void progress_tick(lv_timer_t* timer) {
        auto& ui = *static_cast<RoundUI*>(lv_timer_get_user_data(timer));
        // One clock and a fixed arc keep the loop seam continuous, including delayed UI ticks.
        const uint32_t phase =
            (lv_tick_elaps(ui.progress_started_) % kProgressPeriodMs) * 360 / kProgressPeriodMs;
        lv_arc_set_rotation(ui.progress_arc_, 270 + phase);
    }
    void connection_overlay() {
        const auto stage = state_.link.stage;
        if (stage == LinkStage::Scanning && !state_.link.saved) {
            begin_overlay(Overlay::Scan, text::scan_title);
            label(modal_, text::scan_body, 73, 110, 320, &rc_font_20, kMuted);
            auto* list = box(modal_, 85, 147, 296, 166);
            lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scroll_dir(list, LV_DIR_VER);
            lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
            if (!state_.link.count)
                label(list, text::scanning, 8, 50, 280, &rc_font_24, kAmber);
            for (size_t i = 0; i < state_.link.count; ++i) {
                const auto& c = state_.link.candidates[i];
                auto* row = box(list, 0, int(i) * 78, 296, 76);
                lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
                auto* name = label(row, c.name, 12, 7, 270, &rc_font_24);
                lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
                candidate_names_[i] = name;
                char detail[40];
                std::snprintf(detail, sizeof(detail), "%s     %d dBm", c.suffix, c.rssi);
                candidate_details_[i] = label(row, detail, 12, 39, 270, &rc_font_20, kMuted);
                bindings_[i] = {this, state_.link.scan_generation, c.id};
                lv_obj_add_event_cb(row, candidate_event, LV_EVENT_CLICKED, &bindings_[i]);
                box(row, 12, 75, 272, 1, 0x282828);
            }
            action_button(modal_, text::rescan, 95, 338, 130, false, rescan_event);
            action_button(modal_, text::cancel, 241, 338, 130, false, stop_event);
        } else if (stage == LinkStage::Confirming) {
            begin_overlay(Overlay::Confirm, text::secure_title);
            char pin[16];
            std::snprintf(pin, sizeof(pin), "%03lu %03lu",
                          static_cast<unsigned long>(state_.link.passkey / 1000),
                          static_cast<unsigned long>(state_.link.passkey % 1000));
            label(modal_, pin, 83, 146, 300, &lv_font_montserrat_48);
            label(modal_, text::secure_body, 83, 218, 300, &rc_font_20, kMuted);
            action_button(modal_, text::numbers_match, 123, 300, 220, true, confirm_event);
            action_button(modal_, text::reject, 123, 370, 220, false, reject_event);
        } else if (stage == LinkStage::Failed || state_.view.connection == ConnectionState::Error) {
            begin_overlay(Overlay::Error, text::connection_failed);
            label(modal_, error_text(state_.link.error), 83, 154, 300, &rc_font_24, kAmber);
            label(modal_, text::retry_body, 83, 222, 300, &rc_font_20, kMuted);
            action_button(modal_, text::retry_connection, 123, 300, 220, true, retry_event);
            action_button(modal_, text::back, 123, 370, 220, false, close_event);
        } else if (stage != LinkStage::Idle && stage != LinkStage::Ready) {
            if (overlay_ != Overlay::Progress) {
                begin_overlay(Overlay::Progress, state_.link.name);
                progress_arc_ = lv_arc_create(modal_);
                lv_obj_set_pos(progress_arc_, 197, 132);
                lv_obj_set_size(progress_arc_, 72, 72);
                lv_obj_remove_style(progress_arc_, nullptr, LV_PART_KNOB);
                lv_obj_remove_flag(progress_arc_, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_style_arc_color(progress_arc_, lv_color_hex(0x282828), LV_PART_MAIN);
                lv_obj_set_style_arc_color(progress_arc_, lv_color_hex(kAmber), LV_PART_INDICATOR);
                lv_obj_set_style_arc_width(progress_arc_, 4, LV_PART_MAIN);
                lv_obj_set_style_arc_width(progress_arc_, 4, LV_PART_INDICATOR);
                lv_arc_set_bg_angles(progress_arc_, 0, 360);
                lv_arc_set_angles(progress_arc_, 0, 90);
                lv_arc_set_rotation(progress_arc_, 270);
                progress_started_ = lv_tick_get();
                progress_timer_ = lv_timer_create(progress_tick, kFramePeriodMs, this);
                label(modal_, text::preparing, 83, 230, 300, &rc_font_24);
                progress_stage_ = label(modal_, stage_text(stage), 83, 268, 300, &rc_font_20, kAmber);
                action_button(modal_, text::cancel, 123, 362, 220, false, stop_event);
            }
            set_text(progress_stage_, stage_text(stage));
        } else if (overlay_ != Overlay::Forget && overlay_ != Overlay::Protocol)
            close_dialog();
    }

    void show_page(bool info) {
        if (info_visible_ == info)
            return;
        post(Action::Cancel);
        info_visible_ = info;
        lv_obj_add_flag(info ? shoot_ : info_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(info ? info_ : shoot_, LV_OBJ_FLAG_HIDDEN);
    }

    void page_link(lv_obj_t* page, const char* caption, lv_event_cb_t callback) {
        auto* link = box(page, 133, 402, 200, 56);
        lv_obj_add_flag(link, LV_OBJ_FLAG_CLICKABLE);
        label(link, caption, 8, 10, 184, &rc_font_20, kMuted);
        lv_obj_add_event_cb(link, callback, LV_EVENT_CLICKED, this);
    }

    lv_obj_t* row(lv_obj_t* content, const char* name, const char* value, int y,
                  lv_obj_t** caption = nullptr) {
        auto* line = box(content, 0, y + 44, 298, 1, 0x292929);
        (void)line;
        auto* key = label(content, name, 2, y + 10, 120, &rc_font_20, kMuted);
        lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_LEFT, 0);
        if (caption)
            *caption = key;
        auto* item = label(content, value, 120, y + 10, 176);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_height(item, lv_font_get_line_height(&rc_font_20));
        lv_label_set_long_mode(item, LV_LABEL_LONG_DOT);
        return item;
    }

    void create() {
        root_ = lv_screen_active();
        // Also apply at runtime for existing sdkconfig files that still use LVGL's 33ms default.
        lv_timer_set_period(lv_display_get_refr_timer(lv_obj_get_display(root_)), kFramePeriodMs);
        lv_timer_set_period(lv_anim_get_timer(), kFramePeriodMs);
        lv_obj_remove_style_all(root_);
        lv_obj_set_style_bg_color(root_, lv_color_hex(kBlack), 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(root_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(root_, true, 0);
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
        shoot_ = box(root_, 0, 0, 466, 466);
        auto* logo = lv_image_create(shoot_);
        lv_image_set_src(logo, &rc_fujifilm_logo);
        lv_obj_set_pos(logo, 135, 22);
        lv_obj_add_flag(logo, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(logo, 5);
        lv_obj_add_event_cb(logo, info_event, LV_EVENT_CLICKED, this);
        auto* model_logo = lv_image_create(shoot_);
        lv_image_set_src(model_logo, &rc_xe5_logo);
        lv_obj_set_pos(model_logo, 183, 73);
        lv_obj_remove_flag(model_logo, LV_OBJ_FLAG_CLICKABLE);
        status_ = label(shoot_, text::unpaired, 93, 420, 280, &rc_font_24, kMuted);
        lv_obj_add_flag(status_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(status_, 3);
        lv_obj_add_event_cb(status_, status_event, LV_EVENT_CLICKED, this);
        photo_ = lv_imagebutton_create(shoot_);
        lv_obj_set_pos(photo_, 103, 104);
        lv_obj_set_size(photo_, 260, 260);
        lv_imagebutton_set_src(photo_, LV_IMAGEBUTTON_STATE_RELEASED, nullptr, &rc_shutter_idle, nullptr);
        lv_imagebutton_set_src(photo_, LV_IMAGEBUTTON_STATE_PRESSED, nullptr, &rc_shutter_pressed, nullptr);
        lv_obj_add_event_cb(photo_, photo_event, LV_EVENT_ALL, this);
        hint_ = label(shoot_, "", 58, 384, 350, &rc_font_24, kMuted);
        info_ = box(root_, 0, 0, 466, 466);
        label(info_, text::status_title, 103, 44, 260, &rc_font_24);
        auto* content = box(info_, 84, 96, 298, 296);
        lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(content, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
        label(content, text::battery, 49, 0, 200, &rc_font_20, kMuted);
        percent_ = label(content, "--%", 49, 29, 200, &lv_font_montserrat_48);
        voltage_ = label(content, text::unknown, 49, 88, 200);
        charge_ = label(content, text::unknown, 49, 118, 200, &rc_font_20, kMuted);
        link_ = row(content, text::connection, text::unpaired, 137);
        signal_ = row(content, text::signal, text::unknown, 187);
        camera_name_ = row(content, text::bluetooth_name, text::unpaired, 237);
        device_tail_ = box(content, 0, 287, 298, 275);
        address_ = row(device_tail_, text::bluetooth_address, text::unpaired, 0, &address_caption_);
        row(device_tail_, text::firmware, "0.2.0", 50);
        connect_ = action_button(device_tail_, text::connect, 0, 111, 142, false, connect_event);
        connect_label_ = lv_obj_get_child(connect_, 0);
        repair_ = action_button(device_tail_, text::repair, 156, 111, 142, false, repair_event);
        auto* trademark = label(device_tail_, text::trademark, 2, 187, 294, &rc_font_20, kMuted);
        lv_label_set_long_mode(trademark, LV_LABEL_LONG_WRAP);
        page_link(info_, text::return_shoot, shoot_event);
        lv_obj_add_flag(info_, LV_OBJ_FLAG_HIDDEN);
        input_ = lv_indev_create();
        lv_indev_set_type(input_, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(input_, this);
        lv_indev_set_read_cb(input_, touch_read);
        lv_timer_set_period(lv_indev_get_read_timer(input_), kTouchPeriodMs);
    }

    static void set_text(lv_obj_t* object, const char* value) {
        if (std::strcmp(lv_label_get_text(object), value) != 0)
            lv_label_set_text(object, value);
    }
    void update() {
        const auto now = Board::milliseconds();
        if (now < next_update_)
            return;
        next_update_ = now + kFramePeriodMs;
        state_ = snapshot();
        if (state_.view.power != power_) {
            power_ = state_.view.power;
            board_.brightness(power_ == DisplayPower::Off ? 0 : power_ == DisplayPower::Dim ? 15 : 70);
        }
        // Keep the dimmed shutter touchable for local audio, without camera commands.
        const lv_opa_t photo_opacity = state_.view.can_photo ? LV_OPA_COVER : LV_OPA_40;
        if (lv_obj_get_style_opa(photo_, LV_PART_MAIN) != photo_opacity)
            lv_obj_set_style_opa(photo_, photo_opacity, 0);

        const auto connection = state_.view.connection;
        set_text(status_, connection_text(connection));
        const auto status_color = lv_color_hex(connection == ConnectionState::Ready ? kGreen
                                               : active()                           ? kAmber
                                                                                    : kMuted);
        if (!lv_color_eq(lv_obj_get_style_text_color(status_, LV_PART_MAIN), status_color))
            lv_obj_set_style_text_color(status_, status_color, 0);
        const char* hint = notice_text(state_.view);
        if (std::strcmp(lv_label_get_text(hint_), hint) != 0) {
            set_text(hint_, hint);
            hint_started_ = now;
        }
        const auto hint_elapsed = now - hint_started_;
        const lv_opa_t hint_opacity = hint_elapsed <= kHintVisibleMs ? static_cast<lv_opa_t>(LV_OPA_COVER)
            : hint_elapsed >= kHintVisibleMs + kHintFadeMs ? static_cast<lv_opa_t>(LV_OPA_TRANSP)
            : static_cast<lv_opa_t>((kHintVisibleMs + kHintFadeMs - hint_elapsed) * LV_OPA_COVER / kHintFadeMs);
        if (lv_obj_get_style_opa(hint_, LV_PART_MAIN) != hint_opacity)
            lv_obj_set_style_opa(hint_, hint_opacity, 0);
        set_text(link_, connection_text(connection));
        set_enabled(connect_, connection != ConnectionState::ProtocolBlocked);
        set_enabled(repair_, connection != ConnectionState::ProtocolBlocked);
        const bool active = connection == ConnectionState::Ready || connection == ConnectionState::Pairing ||
                            connection == ConnectionState::Waiting ||
                            connection == ConnectionState::Preparing ||
                            connection == ConnectionState::Reconnecting;
        set_text(connect_label_, active              ? text::disconnect
                                 : state_.link.saved ? text::connect
                                                     : text::scan_title);
        if (state_.percentage >= 0) {
            char percentage[12], voltage[20];
            std::snprintf(percentage, sizeof(percentage), "%d%%", state_.percentage);
            std::snprintf(voltage, sizeof(voltage), "%d.%02d V", state_.battery.millivolts / 1000,
                          (state_.battery.millivolts % 1000) / 10);
            set_text(percent_, percentage);
            set_text(voltage_, voltage);
        } else {
            set_text(percent_, "--%");
            set_text(voltage_, text::unknown);
        }
        set_text(charge_, state_.battery.charging ? text::charging : text::not_charging);
        if (state_.rssi_valid) {
            char signal[20];
            std::snprintf(signal, sizeof(signal), "%d dBm", state_.rssi);
            set_text(signal_, signal);
        } else
            set_text(signal_, text::unknown);
        const bool has_address = state_.link.address[0] != 0;
        set_text(camera_name_, state_.link.name[0] ? state_.link.name : text::unpaired);
        set_text(address_, has_address ? state_.link.address : text::unpaired);
        set_text(address_caption_, state_.link.address_saved ? text::saved_address : text::bluetooth_address);
        if (overlay_ == Overlay::Scan && state_.link.count == previous_count_ &&
            state_.link.scan_generation == previous_scan_) {
            for (size_t i = 0; i < state_.link.count; ++i) {
                const auto& c = state_.link.candidates[i];
                char detail[40];
                std::snprintf(detail, sizeof(detail), "%s     %d dBm", c.suffix, c.rssi);
                if (candidate_names_[i])
                    set_text(candidate_names_[i], c.name);
                if (candidate_details_[i])
                    set_text(candidate_details_[i], detail);
            }
        }
        const bool changed = previous_stage_ != state_.link.stage || previous_count_ != state_.link.count ||
                             previous_session_ != state_.link.session ||
                             previous_scan_ != state_.link.scan_generation;
        if (changed || overlay_ == Overlay::Progress) {
            if (overlay_ != Overlay::Forget && overlay_ != Overlay::Protocol)
                connection_overlay();
            previous_stage_ = state_.link.stage;
            previous_count_ = state_.link.count;
            previous_session_ = state_.link.session;
            previous_scan_ = state_.link.scan_generation;
        }
    }
};

#ifdef RC_UI_PREVIEW
RoundUI* preview_ui{};
#endif

void ui_task(void* argument) {
    RoundUI ui(*static_cast<Board*>(argument));
    ui.run();
}
} // namespace

void ui_start(Board& board) {
    // Board::init and this task are the only LVGL owners; worker never calls LVGL.
    const auto result = xTaskCreatePinnedToCore(ui_task, "round_ui", 8192, &board, 4, nullptr, 1);
    assert(result == pdPASS);
}
#ifdef RC_UI_PREVIEW
void ui_preview(Board& board, bool info, bool details) {
    // Keep callbacks alive until the headless renderer exits.
    preview_ui = new RoundUI(board);
    preview_ui->preview(info, details);
}
void ui_preview_update() {
    preview_ui->preview_update();
}
#endif
} // namespace rc
