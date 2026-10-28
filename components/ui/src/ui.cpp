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
}

namespace rc {
namespace {
constexpr uint32_t kBlack = 0x000000, kSilver = 0xbcb9b0, kWhite = 0xf0ece1, kMuted = 0x8f908d;
constexpr uint32_t kGreen = 0x8bc5a1, kAmber = 0xd4ae76;
constexpr int kSwipe = 55;
constexpr int kPageWidth = 466;
constexpr uint32_t kPageSlideMs = 200;

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
        return text::shoot_hint;
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
            show_page(true, false);
        if (details)
            dialog(text::protocol_title, text::protocol_details, false);
    }
#endif
    void run() {
        create();
        for (;;) {
            update();
            const uint32_t delay = lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(std::max<uint32_t>(2, std::min<uint32_t>(delay, 15))));
        }
    }

  private:
    Board& board_;
    lv_obj_t *root_{}, *shoot_{}, *info_{}, *photo_{}, *status_{}, *hint_{};
    lv_obj_t *percent_{}, *voltage_{}, *charge_{}, *link_{}, *signal_{}, *peer_{}, *connect_{};
    lv_obj_t *connect_label_{}, *modal_{}, *repair_{};
    lv_indev_t* input_{};
    Snapshot state_{};
    bool info_visible_{false}, gesture_down_{false}, swallow_{false}, cancelled_{false};
    bool photo_command_down_{false};
    int start_x_{0}, start_y_{0};
    DisplayPower power_{DisplayPower::Active};
    uint64_t next_update_{0};
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
    lv_obj_t* model_{};
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
                // Modal dialogs never turn a swipe into navigation or a shutter release.
                if (!ui.modal_ && ((dx > 0 && !ui.info_visible_) || (dx < 0 && ui.info_visible_)))
                    ui.show_page(dx > 0);
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
            ui.show_page(true);
        else if (ui.state_.link.stage != LinkStage::Idle && ui.state_.link.stage != LinkStage::Failed)
            ui.connection_overlay();
        else
            post(Action::Connect);
    }
    static void info_event(lv_event_t* e) { self(e).show_page(true); }
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
        if (modal_) {
            lv_obj_delete(modal_);
            modal_ = nullptr;
        }
        overlay_ = Overlay::None;
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
        modal_ = box(root_, 0, 0, 466, 466);
        lv_obj_add_flag(modal_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(modal_, LV_RADIUS_CIRCLE, 0);
        label(modal_, title, 93, 74, 280, &rc_font_24);
    }
    void dialog(const char* title, const char* body, bool confirmation) {
        begin_overlay(confirmation ? Overlay::Forget : Overlay::Protocol, title);
        label(modal_, body, 83, 155, 300, &rc_font_20, kMuted);
        if (confirmation)
            action_button(modal_, text::confirm, 123, 290, 220, true, forget_event);
        action_button(modal_, confirmation ? text::cancel : text::back, 123, 362, 220, false, close_event);
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
            begin_overlay(Overlay::Progress, state_.link.name);
            auto* arc = lv_arc_create(modal_);
            lv_obj_set_pos(arc, 197, 132);
            lv_obj_set_size(arc, 72, 72);
            lv_arc_set_rotation(arc, 270);
            lv_arc_set_bg_angles(arc, 0, 360);
            lv_arc_set_angles(arc, 0, 90);
            lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
            lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_arc_color(arc, lv_color_hex(0x282828), LV_PART_MAIN);
            lv_obj_set_style_arc_color(arc, lv_color_hex(kAmber), LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
            lv_obj_set_style_arc_width(arc, 4, LV_PART_INDICATOR);
            label(modal_, text::preparing, 83, 230, 300, &rc_font_24);
            label(modal_, stage_text(stage), 83, 268, 300, &rc_font_20, kAmber);
            label(modal_, text::connection_steps, 73, 309, 320, &rc_font_20, kMuted);
            action_button(modal_, text::cancel, 123, 362, 220, false, stop_event);
        } else if (overlay_ != Overlay::Forget && overlay_ != Overlay::Protocol)
            close_dialog();
    }

    static void page_position(void* context, int32_t offset) {
        auto& ui = *static_cast<RoundUI*>(context);
        lv_obj_set_x(ui.shoot_, offset);
        lv_obj_set_x(ui.info_, offset - kPageWidth);
    }

    void finish_page_slide() { lv_obj_add_flag(info_visible_ ? shoot_ : info_, LV_OBJ_FLAG_HIDDEN); }

    void show_page(bool info, bool animate = true) {
        if (info_visible_ == info)
            return;
        post(Action::Cancel);
        // One shared offset keeps both pages on the horizontal axis. Restart from
        // the current position when another gesture reverses an unfinished slide.
        lv_anim_delete(this, page_position);
        info_visible_ = info;
        lv_obj_remove_flag(shoot_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(info_, LV_OBJ_FLAG_HIDDEN);
        const int target = info ? kPageWidth : 0;
        if (!animate) {
            page_position(this, target);
            finish_page_slide();
            return;
        }
        lv_anim_t slide;
        lv_anim_init(&slide);
        lv_anim_set_var(&slide, this);
        lv_anim_set_exec_cb(&slide, page_position);
        lv_anim_set_values(&slide, lv_obj_get_x(shoot_), target);
        lv_anim_set_duration(&slide, kPageSlideMs);
        lv_anim_set_path_cb(&slide, lv_anim_path_ease_out);
        lv_anim_set_completed_cb(
            &slide, [](lv_anim_t* animation) { static_cast<RoundUI*>(animation->var)->finish_page_slide(); });
        lv_anim_start(&slide);
    }

    lv_obj_t* row(lv_obj_t* content, const char* name, const char* value, int y) {
        auto* line = box(content, 0, y + 44, 298, 1, 0x292929);
        (void)line;
        auto* key = label(content, name, 2, y + 10, 120, &rc_font_20, kMuted);
        lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_LEFT, 0);
        auto* item = label(content, value, 120, y + 10, 176);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(item, LV_LABEL_LONG_DOT);
        return item;
    }

    void create() {
        root_ = lv_screen_active();
        lv_obj_remove_style_all(root_);
        lv_obj_set_style_bg_color(root_, lv_color_hex(kBlack), 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(root_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(root_, true, 0);
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
        shoot_ = box(root_, 0, 0, 466, 466);
        model_ = label(shoot_, "X-E5", 103, 40, 260, &lv_font_montserrat_28);
        status_ = label(shoot_, text::unpaired, 93, 82, 280, &rc_font_24, kMuted);
        lv_obj_add_flag(status_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(status_, 14);
        lv_obj_add_event_cb(status_, status_event, LV_EVENT_CLICKED, this);
        photo_ = button(shoot_, 121, 130, 224, 224, kWhite);
        lv_obj_set_style_border_width(photo_, 0, 0);
        lv_obj_set_style_outline_width(photo_, 2, 0);
        lv_obj_set_style_outline_pad(photo_, 6, 0);
        lv_obj_set_style_outline_color(photo_, lv_color_hex(0x555550), 0);
        auto* camera_icon = box(photo_, 76, 63, 72, 48, kWhite);
        lv_obj_set_style_radius(camera_icon, 8, 0);
        lv_obj_set_style_border_width(camera_icon, 3, 0);
        lv_obj_set_style_border_color(camera_icon, lv_color_hex(kBlack), 0);
        auto* mount = box(photo_, 98, 55, 28, 10, kBlack);
        auto* lens = box(camera_icon, 23, 11, 26, 26, kWhite);
        lv_obj_set_style_radius(lens, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(lens, 3, 0);
        lv_obj_set_style_border_color(lens, lv_color_hex(kBlack), 0);
        for (auto* o : {camera_icon, lens, mount})
            lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
        label(photo_, text::photo, 32, 132, 160, &rc_font_24, kBlack);
        lv_obj_add_event_cb(photo_, photo_event, LV_EVENT_ALL, this);
        hint_ = label(shoot_, text::shoot_hint, 58, 371, 350, &rc_font_20, kMuted);
        auto* devices = box(shoot_, 133, 402, 200, 56);
        lv_obj_add_flag(devices, LV_OBJ_FLAG_CLICKABLE);
        label(devices, text::status_title, 8, 10, 184, &rc_font_20, kMuted);
        lv_obj_add_event_cb(devices, info_event, LV_EVENT_CLICKED, this);
        info_ = box(root_, -kPageWidth, 0, 466, 466);
        label(info_, text::status_title, 103, 44, 260, &rc_font_24);
        auto* content = box(info_, 84, 96, 298, 232);
        lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(content, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
        label(content, text::battery, 49, 0, 200, &rc_font_20, kMuted);
        percent_ = label(content, "--%", 49, 29, 200, &lv_font_montserrat_48);
        voltage_ = label(content, text::unknown, 49, 88, 200);
        charge_ = label(content, text::unknown, 49, 118, 200, &rc_font_20, kMuted);
        link_ = row(content, text::connection, text::unpaired, 137);
        signal_ = row(content, text::signal, text::unknown, 187);
        peer_ = row(content, text::peer, text::unpaired, 237);
        row(content, text::firmware, "0.2.0", 287);
        repair_ = action_button(content, text::repair, 39, 348, 220, false, repair_event);
        label(content, "", 0, 418, 298);
        connect_ = button(info_, 123, 344, 220, 56, kBlack);
        connect_label_ = label(connect_, text::connect, 8, 15, 204);
        lv_obj_add_event_cb(connect_, connect_event, LV_EVENT_CLICKED, this);
        label(info_, text::left_hint, 103, 419, 260, &rc_font_20, kMuted);
        lv_obj_add_flag(info_, LV_OBJ_FLAG_HIDDEN);
        input_ = lv_indev_create();
        lv_indev_set_type(input_, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(input_, this);
        lv_indev_set_read_cb(input_, touch_read);
    }

    static void set_text(lv_obj_t* object, const char* value) {
        if (std::strcmp(lv_label_get_text(object), value) != 0)
            lv_label_set_text(object, value);
    }
    void update() {
        const auto now = Board::milliseconds();
        if (now < next_update_)
            return;
        next_update_ = now + 50;
        state_ = snapshot();
        if (state_.view.power != power_) {
            power_ = state_.view.power;
            board_.brightness(power_ == DisplayPower::Off ? 0 : power_ == DisplayPower::Dim ? 15 : 70);
        }
        // Keep the dimmed shutter touchable for local audio, without camera commands.
        lv_obj_set_style_opa(photo_, state_.view.can_photo ? LV_OPA_COVER : LV_OPA_40, 0);

        const auto connection = state_.view.connection;
        set_text(status_, connection_text(connection));
        lv_obj_set_style_text_color(status_,
                                    lv_color_hex(connection == ConnectionState::Ready ? kGreen
                                                 : active()                           ? kAmber
                                                                                      : kMuted),
                                    0);
        set_text(hint_, notice_text(state_.view));
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
        set_text(peer_, state_.peer[0] ? state_.peer : text::unpaired);
        set_text(model_, state_.link.name);
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
        if (changed) {
            if (overlay_ != Overlay::Forget && overlay_ != Overlay::Protocol)
                connection_overlay();
            previous_stage_ = state_.link.stage;
            previous_count_ = state_.link.count;
            previous_session_ = state_.link.session;
            previous_scan_ = state_.link.scan_generation;
        }
    }
};

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
    static auto* preview = new RoundUI(board);
    preview->preview(info, details);
}
#endif
} // namespace rc
