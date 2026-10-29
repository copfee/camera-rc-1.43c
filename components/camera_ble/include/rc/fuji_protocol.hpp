#pragma once
// Protocol definitions adapted from furble 0cac22a (MIT); see LICENSES/furble-MIT.txt.
#include "rc/camera.hpp"
#include <array>
#include <deque>
#include <optional>
#include <vector>
namespace rc::fuji {
enum class Protocol : uint8_t { Basic = 1, Secure = 2 };
struct Peer {
    uint8_t schema{1};
    Protocol protocol{Protocol::Basic};
    char name[64]{}, address[18]{}, identity[18]{};
    uint8_t type{0}, identity_type{0};
    std::array<uint8_t, 5> credential{};
};
inline constexpr const char* basic_adv = "af854c2e-b214-458e-97e2-912c4ecf2cb8";
inline constexpr const char* xapp_adv = "117c4142-edd4-4c77-8696-dd18eebb770a";
inline constexpr const char* secure_adv = "a9d2b304-e8d6-4902-8336-352b772d7597";
inline constexpr const char* secure_pair = "123d8f06-62a1-4935-9322-833c531ee225";
inline constexpr const char* basic_pair = "91f1de68-dff6-466e-8b65-ff13b0f16fb8";
inline constexpr const char* token_chr = "aba356eb-9633-4e60-b73f-f52516dbd671";
inline constexpr const char* ident_chr = "85b9163e-62d1-49ff-a6f5-054b4630d4a1";
inline constexpr const char* status_chr = "f557d96b-8284-4667-8793-b971c1deca2a";
inline constexpr const char* conf_svc = "4c0020fe-f3b6-40de-acc9-77d129067b14";
inline constexpr const char* ind1 = "a68e3f66-0fcc-4395-8d4c-aa980b5877fa";
inline constexpr const char* ind2 = "bd17ba04-b76b-4892-a545-b73ba1f74dae";
inline constexpr const char* not1 = "f9150137-5d40-4801-a8dc-f7fc5b01da50";
inline constexpr const char* geo_update = "ad06c7b7-f41a-46f4-a29a-712055319122";
inline constexpr const char* ind3 = "049ec406-ef75-4205-a390-08fe209c51f0";
inline constexpr const char* notx = "4e941240-d01d-46b9-a5ea-67636806830b";
inline constexpr const char* sync_chr = "c95d91ae-b247-4d6d-8661-7dd5d6a0f85b";
inline constexpr const char* shutter_svc = "6514eb81-4e8f-458d-aa2a-e691336cdfac";
inline constexpr const char* shutter_chr = "7fcf49c6-4ff0-4777-a03d-1a79166af7a8";
std::optional<Peer> match(const uint8_t* manufacturer, size_t length,
                          const std::vector<std::string>& services, const char* address, uint8_t type,
                          const char* name, bool saved_scan = false);
bool valid_record(const Peer& peer, size_t length);
bool same_camera(const Peer& saved, const Peer& seen);
enum class GattKind { ReadStatus, Write, Subscribe };
struct Step {
    GattKind kind;
    const char* service;
    const char* characteristic;
    std::vector<uint8_t> value;
    bool required{true};
};
std::vector<Step> handshake(const Peer& peer, const std::string& identifier);
enum class WireKind {
    Advertisement,
    Connected,
    Disconnected,
    Secure,
    Compare,
    Identity,
    Gatt,
    ScanEnd,
    Fault
};
struct WireEvent {
    WireKind kind{WireKind::Fault};
    uint32_t session{0}, ticket{0};
    Peer peer{};
    int status{0}, rssi{0};
    bool encrypted{false}, bonded{false}, authenticated{false};
    uint32_t passkey{0};
    std::array<uint8_t, 64> data{};
    size_t length{0};
};
// Async transport. Callbacks must only copy WireEvent; all methods run on owner.
class ILink {
  public:
    virtual ~ILink() = default;
    virtual bool scan(uint32_t session, Millis duration) = 0;
    virtual void stop_scan() = 0;
    virtual bool connect(const Peer& peer, uint32_t session) = 0;
    virtual bool secure(bool allow_pairing) = 0;
    virtual bool confirm(bool accept) = 0;
    virtual bool gatt(const Step& step, uint32_t session, uint32_t ticket) = 0;
    virtual void close() = 0;
};
class IStore {
  public:
    virtual ~IStore() = default;
    virtual std::optional<Peer> load() const = 0;
    virtual bool save(const Peer& peer) = 0;
    virtual bool erase() = 0;
};
class Session final : public ICameraAdapter {
  public:
    Session(ILink& link, IStore& store, std::string identifier);
    void tick(Millis now);
    void receive(const WireEvent& event, Millis now);
    std::optional<CameraEvent> poll();
    LinkSnapshot snapshot() const;
    void snapshot(LinkSnapshot& output) const;
    uint32_t session_id() const { return view_.session; }
    CameraIdentity identity() const;
    bool protocol_available() const override { return true; }
    bool has_saved_peer() const override { return saved_.has_value(); }
    CameraCapabilities capabilities() const override { return {true, true, true}; }
    Result start_pairing() override;
    Result select_candidate(uint32_t generation, uint32_t id) override;
    Result confirm_pairing(uint32_t session, bool accept) override;
    Result connect_saved() override;
    Result disconnect() override;
    Result forget() override;
    Result request(CameraCommand command, uint32_t operation) override;

  private:
    void begin_attempt();
    void begin_connect(const Peer& peer);
    void begin_handshake();
    void advance();
    void control_next();
    void fail(LinkError reason, bool retryable = true);
    void emit(CameraEventKind kind, Result result = Result::Ok, uint32_t op = 0,
              CameraCommand cmd = CameraCommand::Release);
    ILink& link_;
    IStore& store_;
    std::string identifier_;
    std::optional<Peer> saved_, target_;
    std::array<Peer, 16> peers_{};
    LinkSnapshot view_{};
    std::deque<CameraEvent> output_;
    std::vector<Step> steps_;
    size_t step_{0};
    uint32_t ticket_{0}, waiting_{0}, operation_{0}, release_operation_{0};
    Millis now_{0}, deadline_{0}, pair_deadline_{0};
    uint8_t attempt_{0}, control_part_{0};
    CameraCommand command_{CameraCommand::Release};
    bool pairing_{false}, active_{false}, control_{false}, release_pending_{false};
};
} // namespace rc::fuji
