#pragma once
#include "rc/board.hpp"
#include "rc/controller.hpp"

namespace rc {
enum class Action {
    PhotoDown,
    PhotoUp,
    Activity,
    Cancel,
    Pair,
    Connect,
    Disconnect,
    Forget,
    Select,
    Confirm,
    Reject
};
struct Snapshot {
    ViewModel view;
    LinkSnapshot link;
    BatterySample battery;
    int percentage{-1};
    bool rssi_valid{false};
    int rssi{0};
    char peer[18]{};
};
void runtime_start(Board& board);
void post(Action action, uint32_t argument = 0, uint32_t context = 0);
Snapshot snapshot();
} // namespace rc
