#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace rc {
using Millis = uint64_t;
enum class ConnectionState {
    Unpaired,
    Pairing,
    Waiting,
    Preparing,
    Ready,
    Reconnecting,
    Disconnected,
    Error,
    ProtocolBlocked
};
enum class Result { Ok, Unsupported, NotReady, Busy, TransportError, StorageError, InvalidPeer };
enum class InputSource { Touch, Boot };
enum class CameraCommand { Focus, Shutter, Release };
enum class CameraEventKind {
    Connected,
    Ready,
    Disconnected,
    Reconnecting,
    PairingFailed,
    Fault,
    CommandComplete
};
enum class LinkStage { Idle, Scanning, Connecting, Securing, Confirming, Handshake, Ready, Retry, Failed };
enum class LinkError { None, Timeout, Rejected, Authentication, Gatt, Storage, Overflow, Unavailable };
struct CameraCapabilities {
    bool focus{false}, shutter{false}, hold_shutter{false};
};
struct CameraIdentity {
    std::string model{"X-E5"}, peer;
    uint8_t address_type{0};
};
struct CameraCandidate {
    uint32_t id{0};
    char name[64]{};
    char suffix[6]{};
    int rssi{0};
};
struct LinkSnapshot {
    LinkStage stage{LinkStage::Idle};
    LinkError error{LinkError::None};
    uint32_t scan_generation{0}, session{0}, passkey{0};
    uint8_t count{0};
    bool saved{false}, numeric_comparison{false};
    char name[64]{"X-E5"};
    std::array<CameraCandidate, 16> candidates{};
};
struct CameraEvent {
    CameraEventKind kind;
    uint32_t session{0}, operation{0};
    CameraCommand command{CameraCommand::Release};
    Result result{Result::Ok};
};
// Owner-task port. Ok means accepted; capture requests complete by event.
// Ready guarantees acknowledged neutral release and durable pairing metadata.
class ICameraAdapter {
  public:
    virtual ~ICameraAdapter() = default;
    virtual bool protocol_available() const = 0;
    virtual bool has_saved_peer() const = 0;
    virtual CameraCapabilities capabilities() const = 0;
    virtual Result start_pairing() = 0;
    virtual Result select_candidate(uint32_t generation, uint32_t id) = 0;
    virtual Result confirm_pairing(uint32_t session, bool accept) = 0;
    virtual Result connect_saved() = 0;
    virtual Result disconnect() = 0;
    virtual Result forget() = 0;
    virtual Result request(CameraCommand command, uint32_t operation) = 0;
};
} // namespace rc
