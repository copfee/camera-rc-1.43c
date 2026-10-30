#include "fake_camera.hpp"
#include "rc/controller.hpp"
#include <cstdlib>
#include <iostream>
using namespace rc;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            std::cerr << __LINE__ << ": " << #x << '\n';                                                     \
            std::exit(1);                                                                                    \
        }                                                                                                    \
    } while (false)
void ready(Controller& c) {
    c.start(0);
    c.camera_event({CameraEventKind::Connected, 1}, 1);
    c.camera_event({CameraEventKind::Ready, 1}, 2);
    CHECK(c.view().can_photo);
}
int main() {
    {
        FakeCamera f;
        f.available = false;
        Controller c(f);
        c.start(0);
        c.photo_down(InputSource::Boot, 10);
        CHECK(c.view().connection == ConnectionState::ProtocolBlocked);
        CHECK(f.requests.empty());
    }
    {
        FakeCamera f;
        f.saved = false;
        Controller c(f);
        c.start(0);
        CHECK(f.scans == 0);
        CHECK(c.view().connection == ConnectionState::Unpaired);
        c.connect(10);
        CHECK(f.scans == 1);
        CHECK(c.view().connection == ConnectionState::Pairing);
        c.forget(20);
        CHECK(c.view().connection == ConnectionState::Unpaired);
    }
    {
        FakeCamera f;
        Controller c(f);
        ready(c);
        CHECK(f.connects == 1);
        CHECK(f.requests.empty());
        c.photo_down(InputSource::Touch, 10);
        c.photo_up(InputSource::Touch, 30);
        c.tick(1000);
        CHECK(f.requests.size() == 1); // No shutter until focus acknowledgement.
        c.camera_event(f.completion(), 1000);
        c.tick(1299);
        CHECK(f.requests.size() == 1);
        c.tick(1300);
        CHECK(f.requests.back().command == CameraCommand::Shutter);
        c.tick(5000);
        CHECK(f.requests.size() == 2); // Pulse starts at ack, not submission.
        c.camera_event(f.completion(), 5000);
        c.tick(5099);
        CHECK(f.requests.size() == 2);
        c.tick(5100);
        CHECK(f.requests.back().command == CameraCommand::Release);
        c.camera_event(f.completion(), 5200);
        CHECK(c.view().notice == Notice::CommandSent);
        CHECK(c.view().capture == CapturePhase::Idle);
    }
    {
        FakeCamera f;
        Controller c(f);
        ready(c);
        c.photo_down(InputSource::Boot, 10);
        c.camera_event(f.completion(), 20);
        c.tick(509);
        CHECK(f.requests.size() == 1);
        c.tick(510);
        CHECK(f.requests.back().command == CameraCommand::Shutter);
        c.camera_event(f.completion(), 550);
        c.tick(2000);
        CHECK(f.requests.size() == 2);
        c.photo_up(InputSource::Boot, 2001);
        CHECK(f.requests.back().command == CameraCommand::Release);
        c.camera_event(f.completion(), 2010);
        CHECK(c.view().capture == CapturePhase::Idle);
    }
    {
        FakeCamera f;
        Controller c(f);
        ready(c);
        c.photo_down(InputSource::Touch, 10);
        const auto stale = f.completion();
        c.cancel(20);
        CHECK(f.requests.back().command == CameraCommand::Release);
        c.camera_event(stale, 30);
        c.photo_up(InputSource::Touch, 31);
        c.tick(1000);
        CHECK(f.requests.size() == 2);
        c.camera_event(f.completion(), 1001);
        CHECK(c.view().capture == CapturePhase::Idle);
        c.photo_down(InputSource::Touch, 1100);
        c.photo_down(InputSource::Boot, 1101);
        CHECK(f.requests.back().command == CameraCommand::Release);
        c.tick(2000);
        CHECK(f.requests.size() == 4);
    }
    {
        FakeCamera f;
        Controller c(f);
        ready(c);
        c.photo_down(InputSource::Boot, 10);
        c.cancel(20);
        c.camera_event(f.completion(1, Result::TransportError), 30);
        CHECK(c.view().connection == ConnectionState::Error);
        CHECK(!c.view().can_photo);
    }
    {
        FakeCamera f;
        Controller c(f);
        ready(c);
        c.photo_down(InputSource::Boot, 10);
        const auto old = f.completion();
        c.camera_event({CameraEventKind::Reconnecting, 2}, 20);
        c.camera_event({CameraEventKind::Connected, 3}, 30);
        c.camera_event({CameraEventKind::Ready, 3}, 40);
        c.camera_event(old, 50);
        c.tick(1000);
        CHECK(f.requests.size() == 1);
        CHECK(c.view().capture == CapturePhase::Idle);
        c.disconnect(1100);
        c.camera_event({CameraEventKind::Ready, 3}, 1101);
        CHECK(!c.view().can_photo);
    }
    {
        FakeCamera f;
        Controller c(f);
        ready(c);
        c.tick(60000);
        CHECK(c.view().power == DisplayPower::Active);
        c.tick(60002);
        CHECK(c.view().power == DisplayPower::Dim);
        c.tick(120000);
        CHECK(c.view().power == DisplayPower::Dim);
        c.tick(120002);
        CHECK(c.touch_wake(120003));
        CHECK(!c.touch_wake(120004));
        c.tick(240004);
        c.photo_down(InputSource::Boot, 240005);
        CHECK(c.view().power == DisplayPower::Active);
    }
    {
        FakeCamera f;
        f.supports_focus = false;
        Controller c(f);
        ready(c);
        c.photo_down(InputSource::Boot, 10);
        c.photo_up(InputSource::Boot, 20);
        c.tick(20);
        CHECK(f.requests.back().command == CameraCommand::Shutter);
    }
    CHECK(!battery_percent(0));
    CHECK(!battery_percent(5000));
    CHECK(battery_percent(4200) == 100);
    CHECK(battery_percent(3850) == 60);
    std::cout << "core: async capture scenarios passed\n";
}
