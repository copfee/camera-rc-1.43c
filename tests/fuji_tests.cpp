#include "rc/fuji_protocol.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
using namespace rc;
using namespace rc::fuji;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            std::cerr << __LINE__ << ": " << #x << '\n';                                                     \
            std::exit(1);                                                                                    \
        }                                                                                                    \
    } while (false)
struct Store : IStore {
    std::optional<Peer> peer;
    bool failure{false};
    unsigned saves{0};
    std::optional<Peer> load() const override { return peer; }
    bool save(const Peer& p) override {
        ++saves;
        if (failure)
            return false;
        peer = p;
        return true;
    }
    bool erase() override {
        if (failure)
            return false;
        peer.reset();
        return true;
    }
};
struct Link : ILink {
    uint32_t epoch{}, ticket{};
    unsigned scans{0}, connects{0}, closes{0}, confirmations{0};
    bool ok{true}, accepted{false}, confirm_ok{true}, pairing_allowed{false};
    const char* reject_chr{nullptr};
    std::vector<Step> writes;
    Peer target{};
    bool scan(uint32_t e, Millis) override {
        epoch = e;
        ++scans;
        return ok;
    }
    void stop_scan() override {}
    bool connect(const Peer& p, uint32_t e) override {
        target = p;
        epoch = e;
        ++connects;
        return ok;
    }
    bool secure(bool allow_pairing) override {
        pairing_allowed = allow_pairing;
        return ok;
    }
    bool confirm(bool yes) override {
        ++confirmations;
        accepted = yes;
        return ok && confirm_ok;
    }
    bool gatt(const Step& s, uint32_t e, uint32_t t) override {
        epoch = e;
        ticket = t;
        writes.push_back(s);
        return ok && (!reject_chr || std::strcmp(reject_chr, s.characteristic));
    }
    void close() override { ++closes; }
    void ack(Session& s, int status = 0, size_t len = 4, Millis now = 10) {
        WireEvent e{};
        e.kind = WireKind::Gatt;
        e.session = epoch;
        e.ticket = ticket;
        e.status = status;
        e.length = len;
        e.data = {7, 0x96, 0, 0};
        s.receive(e, now);
    }
    void event(Session& s, WireKind k, Millis now = 1) {
        WireEvent e{};
        e.kind = k;
        e.session = epoch;
        s.receive(e, now);
    }
};
Peer peer(Protocol p = Protocol::Basic) {
    Peer v{};
    v.protocol = p;
    std::strcpy(v.address, "aa:bb:cc:dd:ee:01");
    std::strcpy(v.name, "X-E5");
    v.credential = {1, 2, 3, 4, 5};
    return v;
}
void choose(Session& s, Link& l, Peer p, bool authenticated = true) {
    CHECK(s.start_pairing() == Result::Ok);
    WireEvent e{};
    e.kind = WireKind::Advertisement;
    e.session = l.epoch;
    e.peer = p;
    e.rssi = -54;
    s.receive(e, 1);
    s.receive(e, 2);
    CHECK(s.snapshot().count == 1);
    CHECK(s.select_candidate(s.snapshot().scan_generation + 1, 1) == Result::InvalidPeer);
    CHECK(s.select_candidate(s.snapshot().scan_generation, 1) == Result::Ok);
    l.event(s, WireKind::Connected);
    if (p.protocol == Protocol::Secure) {
        WireEvent secure{};
        secure.kind = WireKind::Secure;
        secure.session = l.epoch;
        secure.encrypted = secure.bonded = true;
        secure.authenticated = authenticated;
        s.receive(secure, 2);
    }
}
void finish(Session& s, Link& l) {
    for (unsigned i = 0; i < 40 && s.snapshot().stage == LinkStage::Handshake; ++i)
        l.ack(s);
}
int main() {
    uint8_t basic[] = {0xd8, 4, 2, 1, 2, 3, 4};
    uint8_t secure[] = {0xd8, 4, 9, 1, 2, 3, 4, 5};
    CHECK(match(basic, 7, {basic_adv}, "aa:bb:cc:dd:ee:01", 0, "X-E5"));
    CHECK(match(basic, 7, {xapp_adv}, "aa:bb:cc:dd:ee:01", 0, "X-E5"));
    CHECK(!match(basic, 6, {basic_adv}, "aa:bb:cc:dd:ee:01", 0, "X-E5"));
    basic[2] = 3;
    CHECK(!match(basic, 7, {basic_adv}, "aa:bb:cc:dd:ee:01", 0, "X-E5"));
    basic[2] = 2;
    CHECK(!match(basic, 7, {secure_adv}, "aa:bb:cc:dd:ee:01", 0, "X-E5"));
    CHECK(match(secure, 8, {secure_adv}, "aa:bb:cc:dd:ee:01", 0, "X-E5"));
    CHECK(!match(secure, 8, {secure_pair}, "aa:bb:cc:dd:ee:01", 0, "X-E5"));
    CHECK(match(secure, 8, {secure_pair}, "aa:bb:cc:dd:ee:01", 0, "X-E5", true));
    secure[0] = 0;
    CHECK(!match(secure, 8, {secure_adv}, "aa:bb:cc:dd:ee:01", 0, "X-E5"));
    for (auto protocol : {Protocol::Basic, Protocol::Secure}) {
        Link l;
        Store st;
        Session s(l, st, "camera-rc-12345");
        choose(s, l, peer(protocol));
        CHECK(st.saves == 0);
        finish(s, l);
        CHECK(s.snapshot().stage == LinkStage::Ready);
        CHECK(st.saves == 1);
        CHECK(l.writes[l.writes.size() - 2].value == std::vector<uint8_t>({1, 0}));
        CHECK(l.writes.back().value == std::vector<uint8_t>({0, 0}));
        if (protocol == Protocol::Secure)
            CHECK(l.writes[1].value == std::vector<uint8_t>({7, 0x96, 0, 0x20}));
        while (s.poll()) {
        }
        CHECK(s.request(CameraCommand::Shutter, 11) == Result::Ok);
        const size_t before = l.writes.size();
        CHECK(s.request(CameraCommand::Release, 12) == Result::Ok);
        l.ack(s);
        CHECK(l.writes.size() == before + 1 && l.writes.back().value == std::vector<uint8_t>({1, 0}));
        l.ack(s);
        CHECK(l.writes.back().value == std::vector<uint8_t>({0, 0}));
        l.ack(s);
        auto completion = s.poll();
        CHECK(completion && completion->operation == 12 && completion->command == CameraCommand::Release);
        CHECK(!s.poll()); // Cancelled shutter is never completed or replayed.
        s.disconnect();
        const auto count = l.writes.size();
        l.ack(s);
        CHECK(l.writes.size() == count);
    }
    // Every required handshake write/subscription fails closed; optional subscriptions continue.
    for (auto protocol : {Protocol::Basic, Protocol::Secure}) {
        const auto plan = handshake(peer(protocol), "id");
        for (size_t i = 0; i < plan.size(); ++i) {
            Link l;
            Store st;
            Session s(l, st, "id");
            choose(s, l, peer(protocol));
            for (size_t j = 0; j < i; ++j)
                l.ack(s);
            l.ack(s, 1);
            if (plan[i].required) {
                CHECK(s.snapshot().stage == LinkStage::Failed);
                CHECK(st.saves == 0);
            } else {
                finish(s, l);
                CHECK(s.snapshot().stage == LinkStage::Ready);
            }
        }
    }
    {
        Link l;
        Store st;
        Session s(l, st, "id");
        choose(s, l, peer(Protocol::Secure));
        l.ack(s, 0, 3);
        CHECK(s.snapshot().stage == LinkStage::Failed);
        CHECK(st.saves == 0);
    }
    {
        Link l;
        Store st;
        st.failure = true;
        Session s(l, st, "id");
        choose(s, l, peer());
        finish(s, l);
        CHECK(s.snapshot().error == LinkError::Storage);
        CHECK(!s.has_saved_peer());
    }
    {
        Link l;
        Store st;
        Session s(l, st, "id");
        CHECK(s.start_pairing() == Result::Ok);
        s.tick(120000);
        CHECK(s.snapshot().error == LinkError::Timeout);
    }
    {
        Link l;
        Store st;
        st.peer = peer(Protocol::Secure);
        Session s(l, st, "id");
        s.connect_saved();
        CHECK(l.scans == 1);
        WireEvent e{};
        e.session = l.epoch;
        e.kind = WireKind::Advertisement;
        e.peer = *st.peer;
        std::strcpy(e.peer.address, "aa:bb:cc:dd:ee:99");
        s.receive(e, 1);
        CHECK(l.connects == 1);
        CHECK(std::strcmp(l.target.address, e.peer.address) == 0);
        l.event(s, WireKind::Disconnected, 2);
        CHECK(s.snapshot().stage == LinkStage::Retry);
        s.tick(1002);
        CHECK(l.scans == 2);
        s.tick(61002);
        CHECK(s.snapshot().stage == LinkStage::Failed);
        s.tick(999999);
        CHECK(l.scans == 2);
    }
    {
        Link l;
        Store st;
        Session s(l, st, "id");
        // New pairing displays the code and cannot confirm, save or start GATT
        // until the user approves; encryption and handshake must still succeed.
        CHECK(s.start_pairing() == Result::Ok);
        WireEvent e{};
        e.kind = WireKind::Advertisement;
        e.session = l.epoch;
        e.peer = peer(Protocol::Secure);
        s.receive(e, 1);
        LinkSnapshot view;
        s.snapshot(view);
        CHECK(view.session == s.session_id() && view.count == 1 && !view.saved);
        CHECK(s.select_candidate(view.scan_generation, 1) == Result::Ok);
        l.event(s, WireKind::Connected);
        e.kind = WireKind::Compare;
        e.passkey = 123456;
        s.receive(e, 2);
        CHECK(l.pairing_allowed && !l.accepted && l.confirmations == 0);
        CHECK(s.snapshot().stage == LinkStage::Confirming);
        CHECK(s.snapshot().numeric_comparison && s.snapshot().passkey == 123456);
        CHECK(st.saves == 0 && l.writes.empty());
        CHECK(s.confirm_pairing(e.session, true) == Result::Ok);
        CHECK(l.accepted && l.confirmations == 1 && s.snapshot().stage == LinkStage::Securing);
        e.kind = WireKind::Secure;
        e.encrypted = e.bonded = e.authenticated = true;
        s.receive(e, 3);
        s.snapshot(view);
        CHECK(view.stage == LinkStage::Handshake && !view.saved && st.saves == 0);
        finish(s, l);
        s.snapshot(view);
        CHECK(view.stage == LinkStage::Ready && view.saved && st.saves == 1);
        CHECK(s.disconnect() == Result::Ok);
        CHECK(s.session_id() != e.session);
        s.receive(e, 4); // A late security event cannot restart a cancelled handshake.
        s.snapshot(view);
        CHECK(view.stage == LinkStage::Idle && view.saved && st.saves == 1);
        CHECK(s.confirm_pairing(e.session, true) == Result::InvalidPeer);
        e.kind = WireKind::Compare;
        s.receive(e, 5); // A cancelled comparison cannot send an acceptance.
        CHECK(l.confirmations == 1);
    }
    {
        Store st;
        {
            Link first;
            Session initial(first, st, "id");
            choose(initial, first, peer(Protocol::Secure));
            WireEvent identity{};
            identity.kind = WireKind::Identity;
            identity.session = first.epoch;
            std::strcpy(identity.peer.identity, "aa:bb:cc:dd:ee:99");
            initial.receive(identity, 3);
            finish(initial, first);
            CHECK(st.saves == 1);
        }
        // A new owner/session restores the record, follows the camera's changed
        // scan address and completes encryption without a comparison request.
        Link restored;
        Session rebooted(restored, st, "id");
        CHECK(rebooted.has_saved_peer());
        CHECK(rebooted.connect_saved() == Result::Ok);
        CHECK(restored.scans == 1 && restored.connects == 0);
        WireEvent seen{};
        seen.kind = WireKind::Advertisement;
        seen.session = restored.epoch;
        seen.peer = peer(Protocol::Secure);
        std::strcpy(seen.peer.address, "aa:bb:cc:dd:ee:77");
        seen.peer.type = 2;
        rebooted.receive(seen, 1);
        CHECK(restored.connects == 1);
        CHECK(std::strcmp(restored.target.address, seen.peer.address) == 0);
        CHECK(std::strcmp(restored.target.identity, st.peer->identity) == 0);
        restored.event(rebooted, WireKind::Connected, 2);
        CHECK(rebooted.snapshot().stage == LinkStage::Securing);
        CHECK(!restored.pairing_allowed);
        seen.kind = WireKind::Secure;
        seen.encrypted = seen.bonded = seen.authenticated = true;
        rebooted.receive(seen, 3);
        finish(rebooted, restored);
        CHECK(rebooted.snapshot().stage == LinkStage::Ready);
        CHECK(rebooted.snapshot().saved && st.saves == 1 && !restored.accepted);
    }
    {
        Link l;
        Store st;
        Session s(l, st, "id");
        s.start_pairing();
        WireEvent e{};
        e.kind = WireKind::Advertisement;
        e.session = l.epoch;
        e.peer = peer(Protocol::Secure);
        s.receive(e, 1);
        s.select_candidate(s.snapshot().scan_generation, 1);
        l.event(s, WireKind::Connected);
        e.kind = WireKind::Compare;
        e.passkey = 482916;
        s.receive(e, 2);
        CHECK(s.snapshot().stage == LinkStage::Confirming);
        CHECK(!l.accepted && l.confirmations == 0);
        CHECK(s.confirm_pairing(e.session + 1, true) == Result::InvalidPeer);
        CHECK(s.confirm_pairing(e.session, false) == Result::Ok);
        CHECK(s.snapshot().error == LinkError::Rejected);
        s.receive(e, 4);
        CHECK(s.snapshot().stage == LinkStage::Failed);
        CHECK(l.confirmations == 1 && st.saves == 0);
    }
    {
        Link l;
        Store st;
        Session s(l, st, "id");
        s.start_pairing();
        for (unsigned i = 0; i < 20; ++i) {
            WireEvent e{};
            e.session = l.epoch;
            e.kind = WireKind::Advertisement;
            e.peer = peer();
            std::snprintf(e.peer.address, 18, "aa:bb:cc:dd:ee:%02x", i);
            s.receive(e, 1);
        }
        CHECK(s.snapshot().count == 16);
        const auto generation = s.snapshot().scan_generation;
        s.start_pairing();
        CHECK(s.select_candidate(generation, 1) == Result::InvalidPeer);
        l.event(s, WireKind::Fault);
        CHECK(s.snapshot().error == LinkError::Overflow);
    }
    {
        auto p = peer();
        CHECK(valid_record(p, sizeof(p)));
        CHECK(!valid_record(p, 20));
        p.schema = 2;
        CHECK(!valid_record(p, sizeof(p)));
        p.schema = 1;
        p.address[0] = 'z';
        CHECK(!valid_record(p, sizeof(p)));
        p = peer();
        p.name[63] = 'x';
        CHECK(!valid_record(p, sizeof(p)));
        p = peer();
        std::strcpy(p.identity, "aa:bb:cc:dd:ee:99");
        CHECK(valid_record(p, sizeof(p)));
        p.identity[17] = 'x';
        CHECK(!valid_record(p, sizeof(p)));
    }
    for (unsigned fail_part = 0; fail_part < 2; ++fail_part) {
        Link l;
        Store st;
        Session s(l, st, "id");
        choose(s, l, peer());
        finish(s, l);
        while (s.poll()) {
        }
        s.request(CameraCommand::Shutter, 41);
        if (fail_part)
            l.ack(s);
        l.ack(s, 1);
        CHECK(s.snapshot().stage == LinkStage::Failed);
        CHECK(s.request(CameraCommand::Release, 42) == Result::NotReady);
        auto e = s.poll();
        CHECK(e && e->kind == CameraEventKind::Fault);
        const auto n = l.writes.size();
        s.tick(999999);
        CHECK(l.writes.size() == n);
    }
    {
        Link l;
        Store st;
        Session s(l, st, "id");
        s.start_pairing();
        WireEvent e{};
        e.kind = WireKind::Advertisement;
        e.session = l.epoch;
        e.peer = peer(Protocol::Secure);
        s.receive(e, 1);
        s.select_candidate(s.snapshot().scan_generation, 1);
        l.event(s, WireKind::Connected);
        e.kind = WireKind::Compare;
        e.passkey = 123456;
        s.receive(e, 2);
        CHECK(s.confirm_pairing(e.session, true) == Result::Ok);
        CHECK(l.accepted);
        e.kind = WireKind::Secure;
        e.bonded = true;
        e.encrypted = false;
        s.receive(e, 3);
        CHECK(s.snapshot().error == LinkError::Authentication);
        CHECK(st.saves == 0);
    }
    for (const auto& step : handshake(peer(Protocol::Secure), "id")) {
        if (step.required)
            continue;
        Link l;
        l.reject_chr = step.characteristic;
        Store st;
        Session s(l, st, "id");
        choose(s, l, peer(Protocol::Secure));
        finish(s, l);
        CHECK(s.snapshot().stage == LinkStage::Ready);
    }
    {
        Link l;
        Store st;
        Session s(l, st, "id");
        s.start_pairing();
        WireEvent e{};
        e.kind = WireKind::Advertisement;
        e.session = l.epoch;
        e.peer = peer(Protocol::Secure);
        s.receive(e, 1);
        s.select_candidate(s.snapshot().scan_generation, 1);
        l.event(s, WireKind::Connected);
        e.kind = WireKind::Compare;
        l.confirm_ok = false;
        s.receive(e, 2);
        CHECK(l.confirmations == 0 && s.snapshot().stage == LinkStage::Confirming);
        CHECK(s.confirm_pairing(e.session, true) == Result::TransportError);
        CHECK(s.snapshot().error == LinkError::Authentication);
        CHECK(l.confirmations == 1 && l.writes.empty() && st.saves == 0);
        s.receive(e, 3);
        CHECK(l.confirmations == 1);
    }
    for (bool comparison : {false, true}) {
        Link l;
        Store st;
        st.peer = peer(Protocol::Secure);
        Session s(l, st, "id");
        CHECK(s.connect_saved() == Result::Ok);
        WireEvent e{};
        e.kind = WireKind::Advertisement;
        e.session = l.epoch;
        e.peer = *st.peer;
        s.receive(e, 1);
        l.event(s, WireKind::Connected);
        e.kind = WireKind::Compare;
        auto stale = e;
        ++stale.session;
        s.receive(stale, 2);
        CHECK(l.confirmations == 0);
        CHECK(!l.pairing_allowed);
        if (comparison)
            s.receive(e, 3); // Restoring a saved bond cannot silently start new pairing.
        else
            l.event(s, WireKind::Fault, 4); // Queue overflow closes before any later confirmation.
        s.receive(e, 5);
        CHECK(l.confirmations == 0);
        CHECK(s.snapshot().error == (comparison ? LinkError::Authentication : LinkError::Overflow));
        CHECK(l.writes.empty() && st.saves == 0);
    }
    {
        Link l;
        Store st;
        Session s(l, st, "id");
        choose(s, l, peer(Protocol::Secure), false);
        finish(s, l);
        CHECK(s.snapshot().stage == LinkStage::Ready);
    }
    {
        Link l;
        Store st;
        Session empty(l, st, "id");
        CHECK(empty.snapshot().name[0] == 0 && empty.snapshot().address[0] == 0);
        const auto unnamed = match(basic, 7, {basic_adv}, "aa:bb:cc:dd:ee:01", 0, "");
        CHECK(unnamed && std::strcmp(unnamed->name, "Fujifilm") == 0);
        st.peer = peer(Protocol::Secure);
        std::strcpy(st.peer->identity, "bb:bb:cc:dd:ee:55");
        Session s(l, st, "id");
        CHECK(s.snapshot().address_saved && std::strcmp(s.snapshot().name, "X-E5") == 0);
        CHECK(std::strcmp(s.snapshot().address, st.peer->address) == 0);
        CHECK(s.connect_saved() == Result::Ok && s.snapshot().address_saved);
        WireEvent e{};
        e.kind = WireKind::Advertisement;
        e.session = l.epoch;
        e.peer = *st.peer;
        std::strcpy(e.peer.address, "aa:bb:cc:dd:ee:99");
        std::strcpy(e.peer.name, "Fujifilm X-E5");
        s.receive(e, 1);
        CHECK(!s.snapshot().address_saved);
        CHECK(std::strcmp(s.snapshot().address, e.peer.address) == 0);
        CHECK(std::strcmp(s.snapshot().name, e.peer.name) == 0);
        CHECK(s.identity().peer == st.peer->identity); // Identity remains distinct from the radio address.
        l.event(s, WireKind::Disconnected, 2);
        CHECK(s.snapshot().stage == LinkStage::Retry && !s.snapshot().address_saved);
        CHECK(std::strcmp(s.snapshot().address, e.peer.address) == 0);
        CHECK(s.disconnect() == Result::Ok && s.snapshot().address_saved);
        CHECK(std::strcmp(s.snapshot().address, st.peer->address) == 0);
        CHECK(s.forget() == Result::Ok);
        CHECK(s.snapshot().name[0] == 0 && s.snapshot().address[0] == 0 && !s.snapshot().address_saved);
    }
    std::cout << "fujifilm: protocol, handshake, cancellation and retry scenarios passed\n";
}
