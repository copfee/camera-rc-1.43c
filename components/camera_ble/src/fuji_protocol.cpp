#include "rc/fuji_protocol.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace rc::fuji {
std::optional<Peer> match(const uint8_t* m, size_t n, const std::vector<std::string>& services,
                          const char* address, uint8_t type, const char* name, bool saved_scan) {
    if (!m || n < 3 || m[0] != 0xd8 || m[1] != 0x04)
        return {};
    const auto has = [&](const char* uuid) {
        return std::find(services.begin(), services.end(), uuid) != services.end();
    };
    Peer p;
    if (n == 7 && m[2] == 2 && (has(basic_adv) || has(xapp_adv))) {
        p.protocol = Protocol::Basic;
        std::copy(m + 3, m + 7, p.credential.begin());
    } else if (n == 8 && has(saved_scan ? secure_pair : secure_adv)) {
        p.protocol = Protocol::Secure;
        std::copy(m + 3, m + 8, p.credential.begin());
    } else
        return {};
    if (!address || std::strlen(address) != 17)
        return {};
    std::snprintf(p.address, sizeof(p.address), "%s", address);
    p.type = type;
    std::snprintf(p.name, sizeof(p.name), "%s", name && *name ? name : "Fujifilm");
    return p;
}
bool valid_record(const Peer& p, size_t length) {
    const auto address = [](const char* text) {
        for (size_t i = 0; i < 17; ++i) {
            const char c = text[i];
            if (i % 3 == 2) {
                if (c != ':')
                    return false;
            } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return false;
        }
        return text[17] == 0;
    };
    return length == sizeof(Peer) && p.schema == 1 &&
           (p.protocol == Protocol::Basic || p.protocol == Protocol::Secure) && p.name[63] == 0 &&
           address(p.address) && p.type <= 3 && p.identity_type <= 3 &&
           (p.identity[0] == 0 || address(p.identity));
}
bool same_camera(const Peer& a, const Peer& b) {
    if (a.protocol != b.protocol)
        return false;
    return a.protocol == Protocol::Secure ? a.credential == b.credential
                                          : a.type == b.type && std::strcmp(a.address, b.address) == 0;
}
std::vector<Step> handshake(const Peer& p, const std::string& id) {
    std::vector<Step> v;
    const auto sub = [&](const char* s, const char* c, bool notification, bool required = true) {
        v.push_back({GattKind::Subscribe, s, c, {uint8_t(notification ? 1 : 2), 0}, required});
    };
    if (p.protocol == Protocol::Basic) {
        v.push_back(
            {GattKind::Write, basic_pair, token_chr, {p.credential.begin(), p.credential.begin() + 4}});
        v.push_back({GattKind::Write, basic_pair, ident_chr, {id.begin(), id.end()}});
        sub(conf_svc, ind1, false);
        sub(conf_svc, ind2, false);
        sub(conf_svc, not1, true);
        sub(conf_svc, geo_update, true);
        sub(conf_svc, ind3, false);
    } else {
        v.push_back({GattKind::ReadStatus, secure_pair, status_chr, {}});
        // The status acknowledgement is filled from the four-byte read result.
        v.push_back({GattKind::Write, secure_pair, status_chr, {}});
        v.push_back({GattKind::Write, secure_pair, ident_chr, {id.begin(), id.end()}});
        sub(conf_svc, ind1, false);
        sub(conf_svc, ind2, false);
        sub(conf_svc, not1, true);
        sub(conf_svc, geo_update, true);
        sub(notx, "bf6dc9cf-3606-4ec9-a4c8-d77576e93ea4", true);
        sub(notx, "75823784-fbb7-4b71-abae-cd9a34072e3c", true);
        sub(conf_svc, "e6692c5c-b7cd-44f4-95fc-eda07ce32560", true, false);
        sub(notx, "aab609c4-94dd-4d89-bc60-665d5090b828", true, false);
        sub(notx, "2a125640-706d-4dd1-b420-c0f4ab93c361", true, false);
        sub(notx, "82a9f452-c5ce-4ef5-8203-3fc9a47f8171", true, false);
        sub(notx, "deef7187-3f43-4364-9e22-11a8c8a15951", true, false);
        sub(notx, sync_chr, true);
        v.push_back({GattKind::Write, notx, sync_chr, {10, 0}});
    }
    // Verify write permission and neutralise before exposing Ready or saving.
    v.push_back({GattKind::Write, shutter_svc, shutter_chr, {1, 0}});
    v.push_back({GattKind::Write, shutter_svc, shutter_chr, {0, 0}});
    return v;
}
Session::Session(ILink& l, IStore& s, std::string id)
    : link_(l), store_(s), identifier_(std::move(id)), saved_(s.load()) {
    if (saved_)
        std::snprintf(view_.name, sizeof(view_.name), "%s", saved_->name);
}
void Session::emit(CameraEventKind k, Result r, uint32_t op, CameraCommand cmd) {
    output_.push_back({k, view_.session, op, cmd, r});
}
std::optional<CameraEvent> Session::poll() {
    if (output_.empty())
        return {};
    const auto e = output_.front();
    output_.pop_front();
    return e;
}
LinkSnapshot Session::snapshot() const {
    LinkSnapshot output;
    snapshot(output);
    return output;
}
void Session::snapshot(LinkSnapshot& output) const {
    output = view_;
    output.saved = saved_.has_value();
}
CameraIdentity Session::identity() const {
    CameraIdentity i;
    const auto* p = target_ ? &*target_ : saved_ ? &*saved_ : nullptr;
    if (p) {
        i.model = p->name;
        i.peer = p->identity[0] ? p->identity : p->address;
        i.address_type = p->identity[0] ? p->identity_type : p->type;
    }
    return i;
}
Result Session::disconnect() {
    active_ = pairing_ = control_ = release_pending_ = false;
    waiting_ = 0;
    steps_.clear();
    target_.reset();
    ++view_.session;
    ++view_.scan_generation;
    view_.count = 0;
    view_.numeric_comparison = false;
    view_.passkey = 0;
    view_.stage = LinkStage::Idle;
    link_.close();
    output_.clear();
    return Result::Ok;
}
Result Session::forget() {
    disconnect();
    if (!store_.erase()) {
        view_.error = LinkError::Storage;
        return Result::StorageError;
    }
    saved_.reset();
    return Result::Ok;
}
Result Session::start_pairing() {
    if (saved_)
        return Result::Busy;
    const auto old_deadline = pairing_ ? pair_deadline_ : now_ + 120000;
    disconnect();
    pairing_ = active_ = true;
    pair_deadline_ = old_deadline;
    view_.error = LinkError::None;
    view_.stage = LinkStage::Scanning;
    deadline_ = pair_deadline_;
    if (!link_.scan(view_.session, deadline_ > now_ ? deadline_ - now_ : 1)) {
        fail(LinkError::Unavailable, false);
        return Result::TransportError;
    }
    return Result::Ok;
}
Result Session::select_candidate(uint32_t generation, uint32_t id) {
    if (!pairing_ || view_.stage != LinkStage::Scanning || generation != view_.scan_generation || id == 0 ||
        id > view_.count)
        return Result::InvalidPeer;
    link_.stop_scan();
    begin_connect(peers_[id - 1]);
    return Result::Ok;
}
Result Session::connect_saved() {
    if (!saved_)
        return Result::InvalidPeer;
    disconnect();
    active_ = true;
    attempt_ = 0;
    view_.error = LinkError::None;
    begin_attempt();
    return Result::Ok;
}
void Session::begin_attempt() {
    ++attempt_;
    ++view_.session;
    waiting_ = 0;
    target_ = saved_;
    if (target_->protocol == Protocol::Basic) {
        begin_connect(*target_);
        return;
    }
    view_.stage = LinkStage::Scanning;
    deadline_ = now_ + 60000;
    if (!link_.scan(view_.session, 60000))
        fail(LinkError::Unavailable);
}
void Session::begin_connect(const Peer& p) {
    target_ = p;
    std::snprintf(view_.name, sizeof(view_.name), "%s", p.name);
    view_.stage = LinkStage::Connecting;
    deadline_ = now_ + 10000;
    if (!link_.connect(p, view_.session))
        fail(LinkError::Unavailable);
}
void Session::begin_handshake() {
    view_.stage = LinkStage::Handshake;
    steps_ = handshake(*target_, identifier_);
    step_ = 0;
    advance();
}
void Session::advance() {
    waiting_ = 0;
    if (step_ == steps_.size()) {
        if (pairing_ && !store_.save(*target_)) {
            fail(LinkError::Storage, false);
            return;
        }
        if (pairing_)
            saved_ = target_;
        pairing_ = false;
        view_.stage = LinkStage::Ready;
        view_.error = LinkError::None;
        attempt_ = 0;
        emit(CameraEventKind::Ready);
        return;
    }
    waiting_ = ++ticket_;
    if (!link_.gatt(steps_[step_], view_.session, waiting_)) {
        if (steps_[step_].required)
            fail(LinkError::Gatt);
        else {
            ++step_;
            advance();
        }
    }
}
void Session::fail(LinkError reason, bool retryable) {
    link_.close();
    waiting_ = 0;
    control_ = release_pending_ = false;
    steps_.clear();
    view_.error = reason;
    view_.numeric_comparison = false;
    view_.passkey = 0;
    if (active_ && !pairing_ && saved_ && retryable && attempt_ < 2) {
        ++view_.session;
        view_.stage = LinkStage::Retry;
        deadline_ = now_ + 1000;
        emit(CameraEventKind::Reconnecting);
        return;
    }
    active_ = false;
    view_.stage = LinkStage::Failed;
    emit(pairing_ ? CameraEventKind::PairingFailed : CameraEventKind::Fault,
         reason == LinkError::Storage ? Result::StorageError : Result::TransportError);
    pairing_ = false;
}
Result Session::confirm_pairing(uint32_t s, bool accept) {
    if (s != view_.session || view_.stage != LinkStage::Confirming)
        return Result::InvalidPeer;
    if (!link_.confirm(accept)) {
        fail(LinkError::Authentication, false);
        return Result::TransportError;
    }
    if (!accept) {
        fail(LinkError::Rejected, false);
        return Result::Ok;
    }
    view_.numeric_comparison = false;
    view_.passkey = 0;
    view_.stage = LinkStage::Securing;
    return Result::Ok;
}
void Session::tick(Millis now) {
    now_ = now;
    if (!active_)
        return;
    if (pairing_ && now >= pair_deadline_) {
        fail(LinkError::Timeout, false);
        return;
    }
    if (view_.stage == LinkStage::Retry && now >= deadline_) {
        begin_attempt();
        return;
    }
    if (view_.stage != LinkStage::Ready && now >= deadline_) {
        fail(LinkError::Timeout);
        return;
    }
    if (control_ && now >= deadline_)
        fail(LinkError::Timeout, false);
}
void Session::receive(const WireEvent& e, Millis now) {
    now_ = now;
    if (!active_ || e.session != view_.session)
        return;
    if (e.kind == WireKind::Fault) {
        fail(LinkError::Overflow, false);
        return;
    }
    if (e.kind == WireKind::Advertisement && view_.stage == LinkStage::Scanning) {
        if (!pairing_) {
            if (saved_ && same_camera(*saved_, e.peer)) {
                link_.stop_scan();
                auto p = e.peer;
                std::memcpy(p.identity, saved_->identity, sizeof(p.identity));
                p.identity_type = saved_->identity_type;
                begin_connect(p);
            }
            return;
        }
        size_t i = 0;
        while (i < view_.count && !same_camera(peers_[i], e.peer))
            ++i;
        if (i == peers_.size())
            return;
        peers_[i] = e.peer;
        auto& c = view_.candidates[i];
        c.id = uint32_t(i + 1);
        c.rssi = e.rssi;
        std::snprintf(c.name, sizeof(c.name), "%s", e.peer.name);
        std::snprintf(c.suffix, sizeof(c.suffix), "%s", e.peer.address + 12);
        if (i == view_.count)
            ++view_.count;
        return;
    }
    if (e.kind == WireKind::ScanEnd) {
        if (view_.stage == LinkStage::Scanning)
            fail(LinkError::Timeout);
        return;
    }
    if (e.kind == WireKind::Disconnected) {
        if (view_.stage == LinkStage::Ready)
            attempt_ = 0;
        fail(LinkError::Unavailable);
        return;
    }
    if (e.kind == WireKind::Connected && view_.stage == LinkStage::Connecting) {
        if (e.status) {
            fail(LinkError::Unavailable);
            return;
        }
        emit(CameraEventKind::Connected);
        deadline_ = now + 30000;
        if (target_->protocol == Protocol::Secure) {
            view_.stage = LinkStage::Securing;
            if (!link_.secure(pairing_))
                fail(LinkError::Authentication);
        } else
            begin_handshake();
        return;
    }
    if (e.kind == WireKind::Identity && target_) {
        std::memcpy(target_->identity, e.peer.identity, sizeof(target_->identity));
        target_->identity_type = e.peer.identity_type;
        return;
    }
    if (e.kind == WireKind::Compare && view_.stage == LinkStage::Securing) {
        // A saved connection must restore its bond, never silently re-pair.
        if (!pairing_) {
            fail(LinkError::Authentication, false);
            return;
        }
        view_.stage = LinkStage::Confirming;
        view_.passkey = e.passkey;
        view_.numeric_comparison = true;
        return;
    }
    if (e.kind == WireKind::Secure && view_.stage == LinkStage::Securing) {
        // Like upstream secureConnection(), accept the negotiated security level.
        // A camera with no numeric-comparison IO may use encrypted Just Works.
        // New numeric comparisons require explicit owner/user confirmation.
        if (e.status || !e.encrypted || !e.bonded) {
            fail(LinkError::Authentication, false);
            return;
        }
        begin_handshake();
        return;
    }
    if (e.kind != WireKind::Gatt || e.ticket != waiting_)
        return;
    waiting_ = 0;
    if (e.status) {
        if (!control_ && step_ < steps_.size() && !steps_[step_].required) {
            ++step_;
            advance();
        } else
            fail(LinkError::Gatt, !control_);
        return;
    }
    if (control_) {
        if (release_pending_) {
            command_ = CameraCommand::Release;
            operation_ = release_operation_;
            release_pending_ = false;
            control_part_ = 0;
            control_next();
            return;
        }
        if (++control_part_ < 2)
            control_next();
        else {
            control_ = false;
            emit(CameraEventKind::CommandComplete, Result::Ok, operation_, command_);
        }
    } else if (view_.stage == LinkStage::Handshake) {
        if (steps_[step_].kind == GattKind::ReadStatus) {
            if (e.length != 4) {
                fail(LinkError::Gatt);
                return;
            }
            steps_[step_ + 1].value = {e.data[0], e.data[1], e.data[2], 0x20};
        }
        ++step_;
        advance();
    }
}
Result Session::request(CameraCommand cmd, uint32_t operation) {
    if (view_.stage != LinkStage::Ready || !active_)
        return Result::NotReady;
    if (control_) {
        if (cmd != CameraCommand::Release)
            return Result::Busy;
        release_pending_ = true;
        release_operation_ = operation;
        return Result::Ok;
    }
    command_ = cmd;
    operation_ = operation;
    control_part_ = 0;
    control_ = true;
    control_next();
    return Result::Ok;
}
void Session::control_next() {
    const uint8_t param = command_ == CameraCommand::Focus ? 3 : command_ == CameraCommand::Shutter ? 2 : 0;
    Step s{GattKind::Write, shutter_svc, shutter_chr, {uint8_t(control_part_ == 0 ? 1 : param), 0}};
    waiting_ = ++ticket_;
    deadline_ = now_ + 5000;
    if (!link_.gatt(s, view_.session, waiting_))
        fail(LinkError::Gatt, false);
}
} // namespace rc::fuji
