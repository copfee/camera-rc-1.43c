#include "rc/xe5_camera.hpp"
#include "NimBLEDevice.h"
#include "NimBLEUUID.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
#include "nvs.h"
#include "os/os_mbuf.h"
#include "rc/fuji_protocol.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
namespace rc {
namespace {
using namespace fuji;
constexpr const char* tag = "fujifilm";

const char* gatt_kind_name(GattKind kind) {
    switch (kind) {
    case GattKind::ReadStatus:
        return "读取状态";
    case GattKind::Write:
        return "写入";
    case GattKind::Subscribe:
        return "订阅";
    }
    return "未知";
}

// Query only through the host store API. Never log addresses or key material.
void log_bond_status(const char* phase, const ble_addr_t& address) {
    ble_store_key_sec key{};
    key.peer_addr = address;
    // Store keys use PUBLIC/RANDOM; scan reports may use PUBLIC_ID/RANDOM_ID.
    key.peer_addr.type &= 1;
    ble_store_value_sec value{};
    const int status = ble_store_read_peer_sec(&key, &value);
    ESP_LOGI(tag, "绑定检查：阶段=%s 绑定数=%d 地址类型=%u 查询返回码=%d LTK存在=%d IRK存在=%d", phase,
             NimBLEDevice::getNumBonds(), address.type, status, status == 0 && value.ltk_present,
             status == 0 && value.irk_present);
}

class PeerStore final : public IStore {
    nvs_handle_t handle_{};
    bool opened_{false};
    std::optional<Peer> peer_;

  public:
    PeerStore() {
        const auto opened = nvs_open("camera_rc", NVS_READWRITE, &handle_);
        opened_ = opened == ESP_OK;
        ESP_LOGI(tag, "打开相机记录存储：返回码=%s", esp_err_to_name(opened));
        if (!opened_)
            return;
        Peer p{};
        size_t size = sizeof(p);
        const auto loaded = nvs_get_blob(handle_, "fuji_peer_v1", &p, &size);
        const bool valid = loaded == ESP_OK && valid_record(p, size);
        ESP_LOGI(tag, "读取相机记录：返回码=%s 字节数=%u 记录有效=%d", esp_err_to_name(loaded),
                 static_cast<unsigned>(size), valid);
        if (valid)
            peer_ = p;
    }
    ~PeerStore() override {
        if (opened_)
            nvs_close(handle_);
    }
    std::optional<Peer> load() const override { return peer_; }
    bool save(const Peer& p) override {
        if (!opened_)
            return false;
        const auto written = nvs_set_blob(handle_, "fuji_peer_v1", &p, sizeof(p));
        const auto committed = written == ESP_OK ? nvs_commit(handle_) : ESP_ERR_INVALID_STATE;
        ESP_LOGI(tag, "保存相机记录：写入=%s 提交=%s", esp_err_to_name(written), esp_err_to_name(committed));
        if (written != ESP_OK || committed != ESP_OK)
            return false;
        peer_ = p;
        return true;
    }
    bool erase() override {
        if (!opened_)
            return false;
        if (peer_) {
            const auto& p = *peer_;
            const NimBLEAddress address(p.identity[0] ? p.identity : p.address,
                                        p.identity[0] ? p.identity_type : p.type);
            if (NimBLEDevice::isBonded(address) && !NimBLEDevice::deleteBond(address))
                return false;
        }
        for (const char* key : {"fuji_peer_v1", "peer"}) {
            const auto r = nvs_erase_key(handle_, key);
            if (r != ESP_OK && r != ESP_ERR_NVS_NOT_FOUND)
                return false;
        }
        if (nvs_commit(handle_) != ESP_OK)
            return false;
        peer_.reset();
        return true;
    }
};
enum class RadioKind {
    Adv,
    ScanEnd,
    Connected,
    Disconnected,
    Security,
    Passkey,
    Identity,
    Service,
    Characteristic,
    Descriptor,
    Io
};
struct RadioEvent {
    RadioKind kind{};
    uint32_t session{}, ticket{};
    int status{};
    uint16_t handle{}, first{}, last{};
    uint8_t properties{}, action{};
    uint32_t pin{};
    ble_addr_t address{};
    ble_uuid_any_t uuid{};
    int rssi{};
    uint16_t length{};
    uint8_t data[255]{};
};
struct Attribute {
    uint16_t definition{}, value{}, end{}, ccc{};
    uint8_t properties{};
};
struct Service {
    uint16_t first{}, last{};
    bool discovered{false};
    std::map<std::string, Attribute> attributes;
};
class Radio final : public ILink {
    struct Context {
        Radio* self;
        uint32_t session, ticket;
        ble_npl_event retirement{};
    };
    QueueHandle_t queue_{xQueueCreate(48, sizeof(RadioEvent))};
    std::atomic<bool> overflow_{false};
    // A one-slot callback mailbox survives a full queue, including a lost Connect.
    std::atomic<uint16_t> observed_handle_{BLE_HS_CONN_HANDLE_NONE};
    std::unique_ptr<Context> scan_context_;
    uint32_t session_{0}, ticket_{0};
    uint16_t connection_{BLE_HS_CONN_HANDLE_NONE};
    bool connecting_{false}, started_{false};
    Step job_{GattKind::Write, "", "", {}};
    std::map<std::string, Service> services_;
    struct Advertisement {
        ble_addr_t address{};
        bool used{false};
        std::vector<uint8_t> manufacturer;
        std::vector<std::string> services;
        std::string name;
    };
    std::array<Advertisement, 32> advertisements_{};
    size_t replace_{0};
    uint16_t descriptor_{0};
    void enqueue(const RadioEvent& e) {
        if (xQueueSend(queue_, &e, 0) != pdTRUE)
            overflow_.store(true);
    }
    static RadioEvent event(Context* c, RadioKind kind) {
        RadioEvent e{};
        e.kind = kind;
        e.session = c->session;
        e.ticket = c->ticket;
        return e;
    }
    static int gap(ble_gap_event* g, void* arg) {
        auto* c = static_cast<Context*>(arg);
        RadioEvent e = event(c, RadioKind::Io);
        bool terminal = false;
        switch (g->type) {
        case BLE_GAP_EVENT_DISC:
            e.kind = RadioKind::Adv;
            e.address = g->disc.addr;
            e.rssi = g->disc.rssi;
            e.length = g->disc.length_data;
            if (e.length > sizeof(e.data)) {
                c->self->overflow_.store(true);
                return 0;
            }
            std::memcpy(e.data, g->disc.data, e.length);
            break;
        case BLE_GAP_EVENT_DISC_COMPLETE:
            e.kind = RadioKind::ScanEnd;
            break;
        case BLE_GAP_EVENT_CONNECT:
            e.kind = RadioKind::Connected;
            e.handle = g->connect.conn_handle;
            e.status = g->connect.status;
            terminal = e.status != 0;
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            e.kind = RadioKind::Disconnected;
            e.handle = g->disconnect.conn.conn_handle;
            e.status = g->disconnect.reason;
            terminal = true;
            break;
        case BLE_GAP_EVENT_ENC_CHANGE:
            e.kind = RadioKind::Security;
            e.handle = g->enc_change.conn_handle;
            e.status = g->enc_change.status;
            break;
        case BLE_GAP_EVENT_IDENTITY_RESOLVED:
            e.kind = RadioKind::Identity;
            e.handle = g->identity_resolved.conn_handle;
            break;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            e.kind = RadioKind::Passkey;
            e.handle = g->passkey.conn_handle;
            e.action = g->passkey.params.action;
            e.pin = g->passkey.params.numcmp;
            break;
        default:
            return 0;
        }
        if (e.kind == RadioKind::Connected && !e.status)
            c->self->observed_handle_.store(e.handle);
        if (e.kind == RadioKind::Disconnected) {
            auto expected = e.handle;
            c->self->observed_handle_.compare_exchange_strong(expected, BLE_HS_CONN_HANDLE_NONE);
        }
        c->self->enqueue(e);
        if (terminal)
            delete c;
        return 0;
    }
    static int service_cb(uint16_t h, const ble_gatt_error* error, const ble_gatt_svc* s, void* arg) {
        auto* c = static_cast<Context*>(arg);
        auto e = event(c, RadioKind::Service);
        e.handle = h;
        e.status = error->status;
        if (!e.status && s) {
            e.first = s->start_handle;
            e.last = s->end_handle;
        }
        c->self->enqueue(e);
        if (e.status)
            delete c;
        return 0;
    }
    static int char_cb(uint16_t h, const ble_gatt_error* error, const ble_gatt_chr* a, void* arg) {
        auto* c = static_cast<Context*>(arg);
        auto e = event(c, RadioKind::Characteristic);
        e.handle = h;
        e.status = error->status;
        if (!e.status && a) {
            e.first = a->def_handle;
            e.last = a->val_handle;
            e.properties = a->properties;
            e.uuid = a->uuid;
        }
        c->self->enqueue(e);
        if (e.status)
            delete c;
        return 0;
    }
    static int desc_cb(uint16_t h, const ble_gatt_error* error, uint16_t, const ble_gatt_dsc* d, void* arg) {
        auto* c = static_cast<Context*>(arg);
        auto e = event(c, RadioKind::Descriptor);
        e.handle = h;
        e.status = error->status;
        if (!e.status && d) {
            e.first = d->handle;
            e.uuid = d->uuid;
        }
        c->self->enqueue(e);
        if (e.status)
            delete c;
        return 0;
    }
    static int io_cb(uint16_t h, const ble_gatt_error* error, ble_gatt_attr* a, void* arg) {
        auto* c = static_cast<Context*>(arg);
        auto e = event(c, RadioKind::Io);
        e.handle = h;
        e.status = error->status;
        if (!e.status && a && a->om) {
            e.length = OS_MBUF_PKTLEN(a->om);
            if (e.length > sizeof(e.data)) {
                c->self->overflow_.store(true);
                delete c;
                return 0;
            }
            os_mbuf_copydata(a->om, 0, e.length, e.data);
        }
        c->self->enqueue(e);
        delete c;
        return 0;
    }
    template <typename F> bool launch(F fn) {
        auto* c = new Context{this, session_, ticket_};
        const int r = fn(c);
        if (r) {
            delete c;
            return false;
        }
        return true;
    }
    void result(Session& s, int status, const RadioEvent* r = nullptr) {
        if (status)
            ESP_LOGW(tag, "GATT 操作失败：类型=%s(%u) 返回码=%d", gatt_kind_name(job_.kind),
                     static_cast<unsigned>(job_.kind), status);
        WireEvent e{};
        e.kind = WireKind::Gatt;
        e.session = session_;
        e.ticket = ticket_;
        e.status = status;
        if (r && r->length <= e.data.size()) {
            e.length = r->length;
            std::copy(r->data, r->data + r->length, e.data.begin());
        } else if (r && r->length)
            e.status = BLE_HS_EMSGSIZE;
        s.receive(e, now_);
    }
    Millis now_{0};
    bool resolve() {
        auto& service = services_[job_.service];
        if (!service.first) {
            const NimBLEUUID uuid(job_.service);
            return launch([&](Context* c) {
                return ble_gattc_disc_svc_by_uuid(connection_, uuid.getBase(), service_cb, c);
            });
        }
        if (!service.discovered)
            return launch([&](Context* c) {
                return ble_gattc_disc_all_chrs(connection_, service.first, service.last, char_cb, c);
            });
        const auto found = service.attributes.find(job_.characteristic);
        if (found == service.attributes.end())
            return false;
        auto& a = found->second;
        if (job_.kind == GattKind::Subscribe) {
            const uint8_t required =
                job_.value[0] == 1 ? BLE_GATT_CHR_PROP_NOTIFY : BLE_GATT_CHR_PROP_INDICATE;
            if (!(a.properties & required))
                return false;
            if (!a.ccc) {
                descriptor_ = 0;
                return launch([&](Context* c) {
                    return ble_gattc_disc_all_dscs(connection_, a.value, a.end, desc_cb, c);
                });
            }
        } else if (job_.kind == GattKind::Write && !(a.properties & BLE_GATT_CHR_PROP_WRITE))
            return false;
        else if (job_.kind == GattKind::ReadStatus && !(a.properties & BLE_GATT_CHR_PROP_READ))
            return false;
        if (job_.kind == GattKind::ReadStatus)
            return launch([&](Context* c) { return ble_gattc_read(connection_, a.value, io_cb, c); });
        const auto handle = job_.kind == GattKind::Subscribe ? a.ccc : a.value;
        return launch([&](Context* c) {
            return ble_gattc_write_flat(connection_, handle, job_.value.data(), job_.value.size(), io_cb, c);
        });
    }
    void advertisement(const RadioEvent& r, Session& session) {
        Advertisement* a = nullptr;
        for (auto& item : advertisements_)
            if (item.used && item.address.type == r.address.type &&
                !std::memcmp(item.address.val, r.address.val, 6)) {
                a = &item;
                break;
            }
        if (!a) {
            a = &advertisements_[replace_++ % advertisements_.size()];
            *a = {};
            a->used = true;
            a->address = r.address;
        }
        for (size_t i = 0; i < r.length;) {
            const size_t n = r.data[i];
            if (!n || i + n + 1 > r.length)
                break;
            const auto type = r.data[i + 1];
            const uint8_t* data = r.data + i + 2;
            const size_t len = n - 1;
            if (type == 0xff && len <= 24)
                a->manufacturer.assign(data, data + len);
            if (type == 8 || type == 9)
                a->name.assign(reinterpret_cast<const char*>(data), std::min<size_t>(len, 63));
            if (type == 6 || type == 7)
                for (size_t offset = 0; offset + 16 <= len; offset += 16) {
                    ble_uuid128_t uuid{};
                    uuid.u.type = BLE_UUID_TYPE_128;
                    std::memcpy(uuid.value, data + offset, 16);
                    const auto text = NimBLEUUID(&uuid.u).toString();
                    if (a->services.size() < 8 &&
                        std::find(a->services.begin(), a->services.end(), text) == a->services.end())
                        a->services.push_back(text);
                }
            i += n + 1;
        }
        const auto address = NimBLEAddress(a->address).toString();
        const auto p = match(a->manufacturer.data(), a->manufacturer.size(), a->services, address.c_str(),
                             a->address.type, a->name.c_str(), session.has_saved_peer());
        if (p) {
            WireEvent e{};
            e.kind = WireKind::Advertisement;
            e.session = r.session;
            e.peer = *p;
            e.rssi = r.rssi;
            session.receive(e, now_);
        }
    }

  public:
    Radio() { assert(queue_); }
    ~Radio() override {
        close();
        if (started_)
            NimBLEDevice::deinit(true);
        vQueueDelete(queue_);
    }
    bool init(const std::string& name) {
        started_ = NimBLEDevice::init(name);
        if (!started_)
            return false;
        NimBLEDevice::setSecurityAuth(true, true, true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
        NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
        NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
        NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT);
        return true;
    }
    bool scan(uint32_t epoch, Millis duration) override {
        stop_scan();
        session_ = epoch;
        for (auto& advertisement : advertisements_)
            advertisement = {};
        replace_ = 0;
        scan_context_ = std::make_unique<Context>(Context{this, epoch, 0});
        ble_gap_disc_params p{};
        p.itvl = p.window = 0x100;
        p.passive = 0;
        p.filter_duplicates = 0;
        return ble_gap_disc(BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT, int32_t(duration), &p, gap,
                            scan_context_.get()) == 0;
    }
    void stop_scan() override {
        ble_gap_disc_cancel();
        if (scan_context_) {
            // The host may have copied cb_arg before cancel acquired its lock.
            // Reclaim only after a host-queue barrier, after that callback returns.
            auto* context = scan_context_.release();
            ble_npl_event_init(
                &context->retirement,
                [](ble_npl_event* event) {
                    auto* retired = static_cast<Context*>(ble_npl_event_get_arg(event));
                    ble_npl_event_deinit(event);
                    delete retired;
                },
                context);
            ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &context->retirement);
        }
    }
    bool connect(const Peer& peer, uint32_t epoch) override {
        session_ = epoch;
        services_.clear();
        ticket_ = 0;
        const NimBLEAddress address(peer.address, peer.type);
        ble_gap_conn_params p{};
        p.scan_itvl = p.scan_window = 16;
        p.itvl_min = 24;
        p.itvl_max = 40;
        p.latency = 1;
        p.supervision_timeout = 400;
        auto* c = new Context{this, epoch, 0};
        const int r = ble_gap_connect(BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT, address.getBase(), 10000, &p, gap, c);
        connecting_ = r == 0;
        if (r)
            delete c;
        return r == 0;
    }
    bool secure() override {
        ble_gap_conn_desc description{};
        const int found = ble_gap_conn_find(connection_, &description);
        if (found) {
            ESP_LOGW(tag, "安全认证前查询连接失败：返回码=%d", found);
            return false;
        }
        log_bond_status("认证前", description.peer_id_addr);
        const int status = ble_gap_security_initiate(connection_);
        ESP_LOGI(tag, "发起安全认证：会话=%lu 返回码=%d", static_cast<unsigned long>(session_), status);
        return status == 0;
    }
    bool confirm(bool accept) override {
        ble_sm_io io{};
        io.action = BLE_SM_IOACT_NUMCMP;
        io.numcmp_accept = accept;
        return ble_sm_inject_io(connection_, &io) == 0;
    }
    bool gatt(const Step& s, uint32_t epoch, uint32_t ticket) override {
        if (connection_ == BLE_HS_CONN_HANDLE_NONE)
            return false;
        job_ = s;
        session_ = epoch;
        ticket_ = ticket;
        return resolve();
    }
    void close() override {
        stop_scan();
        if (connecting_)
            ble_gap_conn_cancel();
        connecting_ = false;
        const auto observed = observed_handle_.load();
        const auto handle = observed != BLE_HS_CONN_HANDLE_NONE ? observed : connection_;
        if (handle != BLE_HS_CONN_HANDLE_NONE)
            ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        connection_ = BLE_HS_CONN_HANDLE_NONE;
        ticket_ = 0;
    }
    std::optional<int> rssi() const {
        int8_t value{};
        if (connection_ != BLE_HS_CONN_HANDLE_NONE && !ble_gap_conn_rssi(connection_, &value))
            return value;
        return {};
    }
    void process(Session& s, Millis now) {
        now_ = now;
        if (overflow_.exchange(false)) {
            xQueueReset(queue_);
            WireEvent e{};
            e.kind = WireKind::Fault;
            e.session = s.session_id();
            s.receive(e, now);
            return;
        }
        RadioEvent r{};
        for (unsigned n = 0; n < 48 && xQueueReceive(queue_, &r, 0) == pdTRUE; ++n) {
            if (r.session != s.session_id()) {
                // A cancelled connection can still complete; close that link on the owner.
                if (r.kind == RadioKind::Connected && !r.status)
                    ble_gap_terminate(r.handle, BLE_ERR_REM_USER_CONN_TERM);
                continue;
            }
            WireEvent e{};
            e.session = r.session;
            e.status = r.status;
            switch (r.kind) {
            case RadioKind::Adv:
                advertisement(r, s);
                break;
            case RadioKind::ScanEnd:
                e.kind = WireKind::ScanEnd;
                s.receive(e, now);
                break;
            case RadioKind::Connected:
                connecting_ = false;
                if (!r.status)
                    connection_ = r.handle;
                e.kind = WireKind::Connected;
                s.receive(e, now);
                break;
            case RadioKind::Disconnected:
                connection_ = BLE_HS_CONN_HANDLE_NONE;
                e.kind = WireKind::Disconnected;
                s.receive(e, now);
                break;
            case RadioKind::Security:
            case RadioKind::Identity: {
                ble_gap_conn_desc d{};
                if (ble_gap_conn_find(r.handle, &d))
                    break;
                e.kind = WireKind::Identity;
                const auto address = NimBLEAddress(d.peer_id_addr).toString();
                std::snprintf(e.peer.identity, sizeof(e.peer.identity), "%s", address.c_str());
                e.peer.identity_type = d.peer_id_addr.type;
                s.receive(e, now);
                if (r.kind == RadioKind::Security) {
                    ESP_LOGI(tag, "安全认证完成：返回码=%d 已加密=%d 已绑定=%d 已认证=%d", r.status,
                             d.sec_state.encrypted, d.sec_state.bonded, d.sec_state.authenticated);
                    log_bond_status("认证后", d.peer_id_addr);
                    e.kind = WireKind::Secure;
                    e.encrypted = d.sec_state.encrypted;
                    e.bonded = d.sec_state.bonded;
                    e.authenticated = d.sec_state.authenticated;
                    s.receive(e, now);
                }
                break;
            }
            case RadioKind::Passkey:
                ESP_LOGI(tag, "安全交互请求：会话=%lu 操作码=%u 已保存相机=%d",
                         static_cast<unsigned long>(r.session), r.action, s.has_saved_peer());
                if (r.action == BLE_SM_IOACT_NUMCMP) {
                    e.kind = WireKind::Compare;
                    e.passkey = r.pin;
                    s.receive(e, now);
                } else {
                    e.kind = WireKind::Secure;
                    e.status = BLE_HS_ENOTSUP;
                    s.receive(e, now);
                }
                break;
            case RadioKind::Service:
            case RadioKind::Characteristic:
            case RadioKind::Descriptor:
            case RadioKind::Io: {
                if (r.ticket != ticket_ || !ticket_)
                    break;
                auto& service = services_[job_.service];
                if (r.kind == RadioKind::Io) {
                    result(s, r.status, &r);
                    break;
                }
                if (r.status && r.status != BLE_HS_EDONE) {
                    result(s, r.status);
                    break;
                }
                if (r.kind == RadioKind::Service && !r.status) {
                    service.first = r.first;
                    service.last = r.last;
                }
                if (r.kind == RadioKind::Characteristic && !r.status)
                    service.attributes[NimBLEUUID(&r.uuid.u).toString()] = {r.first, r.last, service.last, 0,
                                                                            r.properties};
                if (r.kind == RadioKind::Descriptor && !r.status && ble_uuid_u16(&r.uuid.u) == 0x2902)
                    descriptor_ = r.first;
                if (r.status == BLE_HS_EDONE) {
                    if (r.kind == RadioKind::Service && !service.first) {
                        result(s, BLE_HS_ENOENT);
                        break;
                    }
                    if (r.kind == RadioKind::Characteristic) {
                        service.discovered = true;
                        for (auto& a : service.attributes)
                            for (const auto& b : service.attributes)
                                if (b.second.definition > a.second.value)
                                    a.second.end = std::min<uint16_t>(a.second.end, b.second.definition - 1);
                    }
                    if (r.kind == RadioKind::Descriptor) {
                        if (!descriptor_) {
                            result(s, BLE_HS_ENOENT);
                            break;
                        }
                        service.attributes[job_.characteristic].ccc = descriptor_;
                    }
                    if (!resolve())
                        result(s, BLE_HS_ENOENT);
                }
                break;
            }
            }
        }
    }
};
std::string identifier() {
    uint8_t mac[6]{};
    esp_efuse_mac_get_default(mac);
    uint32_t id{};
    std::memcpy(&id, mac, 4);
    id ^= id << 13;
    id ^= id >> 17;
    id ^= id << 5;
    char text[24];
    std::snprintf(text, sizeof(text), "camera-rc-%05lx", static_cast<unsigned long>(id & 0xfffff));
    return text;
}
} // namespace
struct Xe5Camera::Impl {
    PeerStore store;
    Radio radio;
    fuji::Session session;
    bool available;
    Impl() : session(radio, store, identifier()), available(radio.init(identifier())) {
        ESP_LOGI(tag, "蓝牙主机初始化：已就绪=%d 已保存相机=%d", available, session.has_saved_peer());
        if (available) {
            ESP_LOGI(tag, "恢复 BLE 绑定：数量=%d", NimBLEDevice::getNumBonds());
            if (const auto saved = store.load()) {
                const NimBLEAddress address(saved->identity[0] ? saved->identity : saved->address,
                                            saved->identity[0] ? saved->identity_type : saved->type);
                log_bond_status("启动恢复相机", *address.getBase());
            }
        }
    }
};
Xe5Camera::Xe5Camera() : impl_(std::make_unique<Impl>()) {}
Xe5Camera::~Xe5Camera() = default;
bool Xe5Camera::protocol_available() const {
    return impl_->available;
}
bool Xe5Camera::has_saved_peer() const {
    return impl_->session.has_saved_peer();
}
CameraCapabilities Xe5Camera::capabilities() const {
    return impl_->session.capabilities();
}
Result Xe5Camera::start_pairing() {
    return impl_->session.start_pairing();
}
Result Xe5Camera::select_candidate(uint32_t g, uint32_t id) {
    return impl_->session.select_candidate(g, id);
}
Result Xe5Camera::confirm_pairing(uint32_t s, bool a) {
    return impl_->session.confirm_pairing(s, a);
}
Result Xe5Camera::connect_saved() {
    return impl_->session.connect_saved();
}
Result Xe5Camera::disconnect() {
    return impl_->session.disconnect();
}
Result Xe5Camera::forget() {
    return impl_->session.forget();
}
Result Xe5Camera::request(CameraCommand c, uint32_t o) {
    return impl_->session.request(c, o);
}
std::optional<CameraEvent> Xe5Camera::poll(Millis now) {
    impl_->session.tick(now);
    impl_->radio.process(impl_->session, now);
    return impl_->session.poll();
}
void Xe5Camera::link_snapshot(LinkSnapshot& output) const {
    impl_->session.snapshot(output);
}
std::optional<int> Xe5Camera::rssi() const {
    return impl_->radio.rssi();
}
CameraIdentity Xe5Camera::identity() const {
    return impl_->session.identity();
}
} // namespace rc
