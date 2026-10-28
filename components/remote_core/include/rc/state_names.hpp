#pragma once
#include "rc/controller.hpp"

namespace rc {
// Pure value formatting shared by firmware diagnostics; no platform dependency.
constexpr const char* diagnostic_name(ConnectionState value) {
    switch (value) {
    case ConnectionState::Unpaired:
        return "未配对";
    case ConnectionState::Pairing:
        return "配对中";
    case ConnectionState::Waiting:
        return "寻找相机";
    case ConnectionState::Preparing:
        return "准备连接";
    case ConnectionState::Ready:
        return "已就绪";
    case ConnectionState::Reconnecting:
        return "重连中";
    case ConnectionState::Disconnected:
        return "已断开";
    case ConnectionState::Error:
        return "异常";
    case ConnectionState::ProtocolBlocked:
        return "协议不可用";
    }
    return "未知";
}
constexpr const char* diagnostic_name(LinkStage value) {
    switch (value) {
    case LinkStage::Idle:
        return "空闲";
    case LinkStage::Scanning:
        return "扫描相机";
    case LinkStage::Connecting:
        return "建立连接";
    case LinkStage::Securing:
        return "安全认证";
    case LinkStage::Confirming:
        return "确认验证码";
    case LinkStage::Handshake:
        return "相机握手";
    case LinkStage::Ready:
        return "已就绪";
    case LinkStage::Retry:
        return "等待重试";
    case LinkStage::Failed:
        return "失败";
    }
    return "未知";
}
constexpr const char* diagnostic_name(LinkError value) {
    switch (value) {
    case LinkError::None:
        return "无";
    case LinkError::Timeout:
        return "超时";
    case LinkError::Rejected:
        return "配对被拒绝";
    case LinkError::Authentication:
        return "安全认证失败";
    case LinkError::Gatt:
        return "GATT操作失败";
    case LinkError::Storage:
        return "存储失败";
    case LinkError::Overflow:
        return "事件队列溢出";
    case LinkError::Unavailable:
        return "相机不可连接";
    }
    return "未知";
}
constexpr const char* diagnostic_name(CapturePhase value) {
    switch (value) {
    case CapturePhase::Idle:
        return "空闲";
    case CapturePhase::Focusing:
        return "对焦中";
    case CapturePhase::ShotPending:
        return "等待拍摄";
    case CapturePhase::Holding:
        return "持续按住";
    case CapturePhase::PhotoPulse:
        return "快门脉冲";
    case CapturePhase::Releasing:
        return "释放中";
    }
    return "未知";
}
constexpr const char* diagnostic_name(Notice value) {
    switch (value) {
    case Notice::None:
        return "无";
    case Notice::NotConnected:
        return "未连接";
    case Notice::Busy:
        return "忙碌";
    case Notice::CommandSent:
        return "指令已发送";
    case Notice::TransportFailure:
        return "传输失败";
    case Notice::PairingTimeout:
        return "配对超时";
    case Notice::ProtocolMissing:
        return "协议不可用";
    case Notice::StorageFailure:
        return "存储失败";
    }
    return "未知";
}
constexpr const char* diagnostic_name(DisplayPower value) {
    switch (value) {
    case DisplayPower::Active:
        return "亮屏";
    case DisplayPower::Dim:
        return "调暗";
    case DisplayPower::Off:
        return "熄屏";
    }
    return "未知";
}
constexpr const char* diagnostic_name(Result value) {
    switch (value) {
    case Result::Ok:
        return "成功";
    case Result::Unsupported:
        return "不支持";
    case Result::NotReady:
        return "未就绪";
    case Result::Busy:
        return "忙碌";
    case Result::TransportError:
        return "传输错误";
    case Result::StorageError:
        return "存储错误";
    case Result::InvalidPeer:
        return "对端无效";
    }
    return "未知";
}
constexpr const char* diagnostic_name(CameraEventKind value) {
    switch (value) {
    case CameraEventKind::CommandComplete:
        return "指令完成";
    case CameraEventKind::Reconnecting:
        return "重连中";
    case CameraEventKind::Connected:
        return "已连接";
    case CameraEventKind::Ready:
        return "已就绪";
    case CameraEventKind::Disconnected:
        return "已断开";
    case CameraEventKind::PairingFailed:
        return "配对失败";
    case CameraEventKind::Fault:
        return "故障";
    }
    return "未知";
}
} // namespace rc
