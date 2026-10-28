#include "rc/controller.hpp"
namespace rc {
Controller::Controller(ICameraAdapter& camera, Timing timing) : camera_(camera), timing_(timing) {}
void Controller::start(Millis now) {
    activity(now);
    if (!camera_.protocol_available()) {
        view_.connection = ConnectionState::ProtocolBlocked;
        view_.notice = Notice::ProtocolMissing;
    } else if (camera_.has_saved_peer())
        connect(now);
    else
        view_.connection = ConnectionState::Unpaired;
}
void Controller::reset_capture() {
    owner_.reset();
    pending_ = 0;
    held_ = focus_done_ = report_sent_ = false;
    view_.capture = CapturePhase::Idle;
    due_at_ = 0;
}
void Controller::fail(Result result) {
    camera_.disconnect();
    reset_capture();
    view_.connection = ConnectionState::Error;
    view_.notice = result == Result::StorageError ? Notice::StorageFailure : Notice::TransportFailure;
}
void Controller::pair(Millis now) {
    activity(now);
    cancel(now);
    if (!camera_.protocol_available()) {
        start(now);
        return;
    }
    const auto r = camera_.start_pairing();
    if (r != Result::Ok) {
        fail(r);
        return;
    }
    reset_capture();
    view_.connection = ConnectionState::Pairing;
    view_.notice = Notice::None;
}
void Controller::connect(Millis now) {
    if (!camera_.has_saved_peer()) {
        pair(now);
        return;
    }
    cancel(now);
    activity(now);
    const auto r = camera_.connect_saved();
    if (r != Result::Ok) {
        fail(r);
        return;
    }
    reset_capture();
    view_.connection = ConnectionState::Waiting;
    view_.notice = Notice::None;
}
void Controller::disconnect(Millis now) {
    activity(now);
    camera_.disconnect();
    reset_capture();
    view_.connection = ConnectionState::Disconnected;
    view_.notice = Notice::None;
}
void Controller::forget(Millis now) {
    disconnect(now);
    const auto r = camera_.forget();
    if (r != Result::Ok) {
        fail(r);
        return;
    }
    view_.connection = ConnectionState::Unpaired;
}
bool Controller::send(CameraCommand command) {
    pending_ = ++next_operation_;
    pending_command_ = command;
    const auto r = camera_.request(command, pending_);
    if (r != Result::Ok) {
        fail(r);
        return false;
    }
    return true;
}
void Controller::camera_event(CameraEvent e, Millis now) {
    if (!camera_.protocol_available())
        return;
    if (e.kind == CameraEventKind::Connected) {
        if (view_.connection != ConnectionState::Pairing && view_.connection != ConnectionState::Waiting &&
            view_.connection != ConnectionState::Reconnecting)
            return;
        session_ = e.session;
        view_.connection = ConnectionState::Preparing;
        return;
    }
    if (e.kind == CameraEventKind::Reconnecting) {
        if (view_.connection == ConnectionState::Disconnected || view_.connection == ConnectionState::Error)
            return;
        reset_capture();
        session_ = e.session;
        view_.connection = ConnectionState::Reconnecting;
        return;
    }
    if (e.session != session_ && e.kind != CameraEventKind::Fault && e.kind != CameraEventKind::PairingFailed)
        return;
    switch (e.kind) {
    case CameraEventKind::Ready:
        if (view_.connection != ConnectionState::Preparing)
            return;
        reset_capture();
        view_.connection = ConnectionState::Ready;
        view_.notice = Notice::None;
        activity(now);
        break;
    case CameraEventKind::Disconnected:
        reset_capture();
        if (view_.connection != ConnectionState::Error)
            view_.connection = ConnectionState::Disconnected;
        break;
    case CameraEventKind::Fault:
    case CameraEventKind::PairingFailed:
        if (view_.connection != ConnectionState::Disconnected)
            fail(e.result == Result::Ok ? Result::TransportError : e.result);
        break;
    case CameraEventKind::CommandComplete:
        if (!pending_ || e.operation != pending_ || e.command != pending_command_ ||
            view_.connection != ConnectionState::Ready)
            return;
        pending_ = 0;
        if (e.result != Result::Ok) {
            fail(e.result);
            return;
        }
        if (e.command == CameraCommand::Focus) {
            focus_done_ = true;
            due_at_ = now + timing_.focus_ms;
        } else if (e.command == CameraCommand::Shutter) {
            due_at_ = now + timing_.pulse_ms;
        } else {
            held_ = false;
            view_.capture = CapturePhase::Idle;
            if (report_sent_)
                view_.notice = Notice::CommandSent;
            report_sent_ = false;
        }
        break;
    default:
        break;
    }
}
void Controller::photo_down(InputSource source, Millis now) {
    activity(now);
    if (owner_ && owner_ != source) {
        cancel(now);
        view_.notice = Notice::Busy;
        return;
    }
    if (view_.connection != ConnectionState::Ready) {
        view_.notice = Notice::NotConnected;
        return;
    }
    if (view_.capture != CapturePhase::Idle || pending_) {
        view_.notice = Notice::Busy;
        return;
    }
    if (!camera_.capabilities().shutter)
        return;
    owner_ = source;
    pressed_at_ = now;
    held_ = true;
    view_.notice = Notice::None;
    view_.capture = CapturePhase::Focusing;
    focus_done_ = !camera_.capabilities().focus;
    due_at_ = now;
    if (!focus_done_)
        send(CameraCommand::Focus);
}
void Controller::photo_up(InputSource source, Millis now) {
    if (!owner_ || *owner_ != source)
        return;
    owner_.reset();
    activity(now);
    if (view_.capture == CapturePhase::Holding) {
        cancel(now);
        return;
    }
    if (view_.capture == CapturePhase::Focusing)
        view_.capture = CapturePhase::ShotPending;
}
void Controller::cancel(Millis now) {
    activity(now);
    owner_.reset();
    report_sent_ = false;
    if (held_ && view_.connection == ConnectionState::Ready && view_.capture != CapturePhase::Releasing) {
        view_.capture = CapturePhase::Releasing;
        send(CameraCommand::Release);
    }
}
bool Controller::touch_wake(Millis now) {
    const bool consumed = view_.power == DisplayPower::Off;
    activity(now);
    return consumed;
}
void Controller::activity(Millis now) {
    last_activity_ = now;
    view_.power = DisplayPower::Active;
}
void Controller::tick(Millis now) {
    if (view_.connection == ConnectionState::Ready && !pending_ && focus_done_) {
        if (view_.capture == CapturePhase::Focusing && owner_ && camera_.capabilities().hold_shutter &&
            now >= pressed_at_ + timing_.long_press_ms) {
            view_.capture = CapturePhase::Holding;
            send(CameraCommand::Shutter);
        } else if (view_.capture == CapturePhase::ShotPending && now >= due_at_) {
            view_.capture = CapturePhase::PhotoPulse;
            send(CameraCommand::Shutter);
        } else if (view_.capture == CapturePhase::PhotoPulse && now >= due_at_) {
            view_.capture = CapturePhase::Releasing;
            report_sent_ = true;
            send(CameraCommand::Release);
        }
    }
    if (view_.capture != CapturePhase::Idle) {
        last_activity_ = now;
        return;
    }
    const auto idle = now - last_activity_;
    view_.power = idle >= timing_.blank_ms ? DisplayPower::Off
                  : idle >= timing_.dim_ms ? DisplayPower::Dim
                                           : DisplayPower::Active;
}
ViewModel Controller::view() const {
    auto v = view_;
    v.can_photo = v.connection == ConnectionState::Ready && camera_.capabilities().shutter;
    return v;
}
std::optional<int> battery_percent(int mv) {
    // Approximate rested single-cell Li-ion curve. Not a fuel gauge or presence detector.
    constexpr std::pair<int, int> curve[] = {{3000, 0},  {3300, 3},  {3500, 8},  {3600, 15},
                                             {3700, 30}, {3750, 40}, {3800, 50}, {3900, 70},
                                             {4000, 85}, {4100, 95}, {4200, 100}};
    if (mv < 2500 || mv > 4350)
        return std::nullopt;
    if (mv <= curve[0].first)
        return 0;
    for (unsigned i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
        const auto [lo, lp] = curve[i - 1];
        const auto [hi, hp] = curve[i];
        if (mv <= hi)
            return lp + (mv - lo) * (hp - lp) / (hi - lo);
    }
    return 100;
}
} // namespace rc
