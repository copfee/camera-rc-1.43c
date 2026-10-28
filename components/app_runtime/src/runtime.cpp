#include "rc/runtime.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rc/state_names.hpp"
#include "rc/xe5_camera.hpp"
#include <atomic>
#include <cstdio>
#include <type_traits>

namespace rc {
namespace {
constexpr const char* kTag = "runtime";
// Includes nested NimBLE security/GATT calls and their logging on the owner.
constexpr uint32_t kOwnerStackBytes = 12 * 1024;
const char* action_name(Action action) {
    switch (action) {
    case Action::PhotoDown:
        return "按下拍照";
    case Action::PhotoUp:
        return "松开拍照";
    case Action::Activity:
        return "用户活动";
    case Action::Cancel:
        return "取消";
    case Action::Pair:
        return "配对";
    case Action::Connect:
        return "连接";
    case Action::Disconnect:
        return "断开";
    case Action::Select:
        return "选择相机";
    case Action::Confirm:
        return "确认验证码";
    case Action::Reject:
        return "拒绝验证码";
    case Action::Forget:
        return "忘记相机";
    }
    return "未知";
}
void log_view(const ViewModel& view) {
    ESP_LOGI(kTag, "状态：连接=%s 拍摄=%s 提示=%s 可拍照=%d", diagnostic_name(view.connection),
             diagnostic_name(view.capture), diagnostic_name(view.notice), view.can_photo);
}
bool view_changed(const ViewModel& a, const ViewModel& b) {
    return a.connection != b.connection || a.capture != b.capture || a.notice != b.notice ||
           a.can_photo != b.can_photo;
}
struct InputEvent {
    Action action;
    uint32_t generation;
    uint32_t argument{0}, context{0};
};
QueueHandle_t inputs, views;
std::atomic<uint32_t> generation{0};
std::atomic<bool> stop_requested{false};
static_assert(std::is_trivially_copyable<Snapshot>::value, "FreeRTOS queue payload must be trivial");

void worker(void* argument) {
    ESP_LOGI(kTag, "相机主控任务已进入 CPU%d；初始化相机适配器", xPortGetCoreID());
    auto& board = *static_cast<Board*>(argument);
    Xe5Camera camera;
    Controller controller(camera);
    camera.poll(Board::milliseconds());
    controller.start(Board::milliseconds());
    ESP_LOGI(kTag, "控制器已启动：协议可用=%d 已保存相机=%d", camera.protocol_available(),
             camera.has_saved_peer());
    auto previous_view = controller.view();
    log_view(previous_view);
    uint32_t active_generation = generation.load();
    bool boot_armed = false, stable = board.boot_pressed(), candidate = stable;
    Millis changed_at = Board::milliseconds(), next_telemetry = 0;
    Snapshot state{};
    int filtered_mv = 0;
    Millis next_health = 0;
    auto previous_stage = LinkStage::Idle;
    auto previous_error = LinkError::None;
    for (;;) {
        const auto now = Board::milliseconds();
        if (stop_requested.exchange(false))
            controller.disconnect(now);
        const auto current_generation = generation.load();
        if (current_generation != active_generation) {
            controller.cancel(now);
            active_generation = current_generation;
        }
        InputEvent input{};
        for (unsigned n = 0; n < 24 && xQueueReceive(inputs, &input, 0) == pdTRUE; ++n) {
            if (input.generation != generation.load()) {
                ESP_LOGW(kTag, "丢弃已取消的输入：操作=%s 代次=%lu", action_name(input.action),
                         static_cast<unsigned long>(input.generation));
                continue;
            }
            if (input.generation != active_generation) {
                controller.cancel(now);
                active_generation = input.generation;
            }
            switch (input.action) {
            case Action::PhotoDown:
                controller.photo_down(InputSource::Touch, now);
                break;
            case Action::PhotoUp:
                controller.photo_up(InputSource::Touch, now);
                break;
            case Action::Activity:
                controller.activity(now);
                break;
            case Action::Pair:
                controller.pair(now);
                break;
            case Action::Connect:
                controller.connect(now);
                break;
            case Action::Disconnect:
                controller.disconnect(now);
                break;
            case Action::Forget:
                controller.forget(now);
                break;
            case Action::Select:
                if (camera.select_candidate(input.context, input.argument) != Result::Ok)
                    ESP_LOGW(kTag, "忽略已过期的相机选择");
                break;
            case Action::Confirm:
            case Action::Reject:
                camera.confirm_pairing(input.context, input.action == Action::Confirm);
                break;
            case Action::Cancel:
                controller.cancel(now);
                break;
            }
        }
        const bool down = board.boot_pressed();
        if (down != candidate) {
            candidate = down;
            changed_at = now;
        }
        if (candidate != stable && now - changed_at >= 25) {
            stable = candidate;
            ESP_LOGI(kTag, "BOOT 按键消抖完成：%s 已允许触发=%d", stable ? "按下" : "松开", boot_armed);
            if (!stable) {
                if (boot_armed)
                    controller.photo_up(InputSource::Boot, now);
                boot_armed = true;
            } else if (boot_armed) {
                board.shutter_sound(); // Local press feedback, including protocol-blocked mode.
                controller.photo_down(InputSource::Boot, now);
            }
        }
        if (!stable)
            boot_armed = true; // Ignore a BOOT already held when firmware starts.
        while (auto event = camera.poll(now)) {
            ESP_LOGI(kTag, "相机事件：%s", diagnostic_name(event->kind));
            controller.camera_event(*event, now);
        }
        controller.tick(now);
        camera.link_snapshot(state.link);
        if (state.link.stage != previous_stage || state.link.error != previous_error) {
            ESP_LOGI(kTag, "链路：阶段=%s(%u) 错误=%s(%u) 候选数=%u", diagnostic_name(state.link.stage),
                     static_cast<unsigned>(state.link.stage), diagnostic_name(state.link.error),
                     static_cast<unsigned>(state.link.error), state.link.count);
            previous_stage = state.link.stage;
            previous_error = state.link.error;
        }
        if (now >= next_telemetry) {
            state.battery = board.battery();
            if (state.battery.valid) {
                filtered_mv = filtered_mv == 0 ? state.battery.millivolts
                                               : (filtered_mv * 3 + state.battery.millivolts) / 4;
                state.battery.millivolts = filtered_mv;
                state.percentage = battery_percent(filtered_mv).value_or(-1);
            } else {
                state.percentage = -1;
                filtered_mv = 0;
            }
            const auto signal = camera.rssi();
            state.rssi_valid = signal.has_value();
            state.rssi = signal.value_or(0);
            const auto identity = camera.identity();
            std::snprintf(state.peer, sizeof(state.peer), "%s", identity.peer.c_str());
            next_telemetry = now + 2000;
        }
        state.view = controller.view();
        if (view_changed(previous_view, state.view)) {
            log_view(state.view);
            previous_view = state.view;
        }
        if (now >= next_health) {
            ESP_LOGI(kTag,
                     "相机主控心跳：队列待处理=%u 栈最小余量字节=%u 内部堆可用字节=%u 内部堆最小余量字节=%u "
                     "电池读数有效=%d 电压毫伏=%d 电量百分比=%d 充电中=%d",
                     static_cast<unsigned>(uxQueueMessagesWaiting(inputs)),
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                     state.battery.valid, state.battery.millivolts, state.percentage, state.battery.charging);
            next_health = now + 10000;
        }
        xQueueOverwrite(views, &state);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
} // namespace

void runtime_start(Board& board) {
    ESP_LOGI(kTag, "创建输入队列（容量24）与快照队列（容量1）");
    inputs = xQueueCreate(24, sizeof(InputEvent));
    views = xQueueCreate(1, sizeof(Snapshot));
    assert(inputs && views);
    const auto result =
        xTaskCreatePinnedToCore(worker, "camera_owner", kOwnerStackBytes, &board, 5, nullptr, 0);
    assert(result == pdPASS);
    ESP_LOGI(kTag, "相机主控任务已创建：CPU=0 优先级=5 栈字节数=%lu",
             static_cast<unsigned long>(kOwnerStackBytes));
}

void post(Action action, uint32_t argument, uint32_t context) {
    if (action == Action::Disconnect) {
        generation.fetch_add(1);
        stop_requested.store(true);
        return;
    }
    if (action == Action::Cancel) {
        generation.fetch_add(1); // Never dropped, even if the ordinary queue is full.
        return;
    }
    InputEvent event{action, generation.load(), argument, context};
    if (xQueueSend(inputs, &event, 0) != pdTRUE) {
        generation.fetch_add(1);
        ESP_LOGW(kTag, "输入队列已满：操作=%s；已投递取消", action_name(action));
    }
}

Snapshot snapshot() {
    Snapshot state{};
    // Show idle until the owner publishes its first runtime snapshot.
    state.view.connection = ConnectionState::Unpaired;
    state.view.notice = Notice::None;
    xQueuePeek(views, &state, 0);
    return state;
}
} // namespace rc
