#pragma once
#include "rc/camera.hpp"
#include <memory>
#include <optional>
namespace rc {
class Xe5Camera final : public ICameraAdapter {
  public:
    Xe5Camera();
    ~Xe5Camera() override;
    bool protocol_available() const override;
    bool has_saved_peer() const override;
    CameraCapabilities capabilities() const override;
    Result start_pairing() override;
    Result select_candidate(uint32_t generation, uint32_t id) override;
    Result confirm_pairing(uint32_t session, bool accept) override;
    Result connect_saved() override;
    Result disconnect() override;
    Result forget() override;
    Result request(CameraCommand command, uint32_t operation) override;
    std::optional<CameraEvent> poll(Millis now);
    void link_snapshot(LinkSnapshot& output) const;
    std::optional<int> rssi() const;
    CameraIdentity identity() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rc
