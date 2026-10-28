#pragma once
#include "rc/camera.hpp"
#include <vector>
struct FakeCamera final : rc::ICameraAdapter {
    bool available{true}, saved{true}, supports_focus{true};
    rc::Result request_result{rc::Result::Ok};
    struct Request {
        rc::CameraCommand command;
        uint32_t operation;
    };
    std::vector<Request> requests;
    unsigned connects{0}, scans{0}, closes{0};
    bool protocol_available() const override { return available; }
    bool has_saved_peer() const override { return saved; }
    rc::CameraCapabilities capabilities() const override { return {supports_focus, true, true}; }
    rc::Result start_pairing() override {
        ++scans;
        return rc::Result::Ok;
    }
    rc::Result select_candidate(uint32_t, uint32_t) override { return rc::Result::Ok; }
    rc::Result confirm_pairing(uint32_t, bool) override { return rc::Result::Ok; }
    rc::Result connect_saved() override {
        ++connects;
        return rc::Result::Ok;
    }
    rc::Result disconnect() override {
        ++closes;
        return rc::Result::Ok;
    }
    rc::Result forget() override {
        saved = false;
        return rc::Result::Ok;
    }
    rc::Result request(rc::CameraCommand c, uint32_t op) override {
        requests.push_back({c, op});
        return request_result;
    }
    rc::CameraEvent completion(uint32_t session = 1, rc::Result result = rc::Result::Ok) const {
        const auto& r = requests.back();
        return {rc::CameraEventKind::CommandComplete, session, r.operation, r.command, result};
    }
};
