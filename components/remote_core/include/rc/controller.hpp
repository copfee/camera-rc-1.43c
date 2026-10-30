#pragma once
#include "rc/camera.hpp"
#include <optional>
namespace rc {
struct Timing {
    Millis focus_ms{300}, pulse_ms{100}, long_press_ms{500}, pairing_ms{120000}, dim_ms{60000},
        blank_ms{120000};
};
enum class CapturePhase { Idle, Focusing, ShotPending, Holding, PhotoPulse, Releasing };
enum class Notice {
    None,
    NotConnected,
    Busy,
    CommandSent,
    TransportFailure,
    PairingTimeout,
    ProtocolMissing,
    StorageFailure
};
enum class DisplayPower { Active, Dim, Off };
struct ViewModel {
    ConnectionState connection{ConnectionState::Unpaired};
    CapturePhase capture{CapturePhase::Idle};
    Notice notice{Notice::None};
    DisplayPower power{DisplayPower::Active};
    bool can_photo{false};
};
class Controller {
  public:
    explicit Controller(ICameraAdapter& camera, Timing timing = {});
    void start(Millis now);
    void pair(Millis now);
    void connect(Millis now);
    void disconnect(Millis now);
    void forget(Millis now);
    void camera_event(CameraEvent event, Millis now);
    void photo_down(InputSource source, Millis now);
    void photo_up(InputSource source, Millis now);
    void cancel(Millis now);
    bool touch_wake(Millis now);
    void activity(Millis now);
    void tick(Millis now);
    ViewModel view() const;

  private:
    bool send(CameraCommand command);
    void fail(Result result);
    void reset_capture();
    ICameraAdapter& camera_;
    Timing timing_;
    ViewModel view_;
    std::optional<InputSource> owner_;
    Millis pressed_at_{0}, due_at_{0}, last_activity_{0};
    uint32_t session_{0}, next_operation_{0}, pending_{0};
    CameraCommand pending_command_{CameraCommand::Release};
    bool focus_done_{false}, held_{false}, report_sent_{false};
};
std::optional<int> battery_percent(int millivolts);
} // namespace rc
