#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/types/CaptureRequest.h"

namespace sr::recorder {

// G3 的端到端录制会话：采集 → BGRA→NV12 → H.264 → MKV。
//
// 关于「帧率」：DXGI Desktop Duplication 是变化驱动的，桌面静止时不产帧。
// 录制必须有固定帧率，因此这里用「时间轴补帧」：每个 1/fps 的时刻必须有一帧输出，
// 没有新帧就重复上一帧（对应 02 文档 §6.3 的负漂移分支与 M3-15 的定点丢帧）。
class RecordingSession {
public:
    struct Options {
        uint32_t seconds = 10;
        uint32_t fps = 30;
        uint32_t display_index = 0;
        // M1-07/M1-08/M1-09：按窗口采集（0 = 不按窗口，录显示器/区域）。
        uint64_t window_handle = 0;
        // M1-08#2：源尺寸与输出不一致时的适配策略（pad = 等比+黑边 / stretch = 拉伸铺满）。
        std::string fit_mode = "stretch";
        uint32_t bitrate_kbps = 6000;
        // M3-04：cbr（默认，ICodecAPI + filler 补齐）/ vbr（峰值约束 VBR，无 filler）
        std::string rate_mode = "cbr";
        // M3-04#2 / D-53：画质档位 0~100（0 = 按模式取默认：CBR 70 / VBR 80）与 VBR 峰值上限。
        uint32_t quality = 0;
        uint32_t max_bitrate_kbps = 0;
        std::string output_path = "out.mkv";
        // 调试用：把 Annex-B 码流另存一份，便于用系统解码器验证可解码性。
        std::string dump_elementary_stream;
        // 是否录制系统声音轨（M3-12）；设备不可用时自动降级为纯视频。
        bool audio = true;
        // 磁盘水位阈值（MB，M8-03/M8-04）
        uint32_t disk_warn_mb = 2048;
        uint32_t disk_stop_mb = 128;
        // 控制闭环（M2-02/M2-04）：倒计时秒数、暂停起点与暂停时长（后者为验收驱动）
        uint32_t countdown_seconds = 0;
        uint32_t pause_at_seconds = 0;
        uint32_t pause_duration_seconds = 0;
        // 多段暂停（M2-02）：与 pause_at_seconds 二选一，列表优先
        std::vector<uint32_t> pause_at_list;
        std::vector<uint32_t> pause_duration_list;
        // 倒计时内取消（M2-04）：到点即退出，且不得留下目录或 .part
        uint32_t cancel_at_seconds = 0;
        // 注入：让恢复失败，用于验证错误传播（D-28）
        bool fail_resume = false;
        // 注入：模拟所有编码器不可用（M8-08 全失败分支）
        bool fail_encoder = false;
        // 轮 47：定向注入（--fail-encoder hardware|software），只让一类 MFT 创建失败。
        bool fail_hardware_encoder = false;
        bool fail_software_encoder = false;
        // M3-05：强制指定编码器 id（见 sr-recorder --list-encoders）；空 = 自动选择。
        std::string encoder_id;
        // M6-08#2：令既有 CPU 中转路径可达（仅验收/开发构建暴露 --force-cpu）。
        bool force_cpu = false;
        // M1-24：是否把鼠标指针写入成片（CaptureRequest::include_cursor）。
        bool include_cursor = true;
        // 注入：第 N 次写盘后失败（P0-1）
        uint64_t fail_write_after = 0;
        // 注入：恢复时强制当前后端不可用，验证真实降级（P0-3 / C11）
        bool fail_resume_backend = false;
        // 写入失败时的兜底目录：修复后提交到此，退出码 16（P0-2）
        std::string fallback_dir;
        // 控制通道（T-17a）：启用后录制期间每帧轮询，断开不中断录制
        std::string control_pipe;
        // §7 握手令牌；与 SessionServer 默认值一致
        std::string control_token = "dev";
        uint32_t control_wait_ms = 5000;
        // 暂停保护（D-36）：**手动**暂停累计超过该秒数即收尾，0 表示不设上限。
        // 默认 900s，对应「UI 崩溃后不能无限挂起」的兜底；
        // 计时暂停（--pause-duration）有明确终点，不受此限制（F3）。
        uint32_t pause_timeout_seconds = 900;
        // 强制采集后端（用于 C6 双后端一致性验收）
        std::string backend;
        // M1-04：区域录制（宽高 >0 时按 Region 采集；UI 已做偶数对齐）
        bool region_enabled = false;
        sr::Rect region{};
        // 轮 30（M6-03）：人为丢帧注入（调试开关，SR_TEST_HOOKS）。
        // >0 时每 N 帧跳过编码提交并计入 frames_dropped，用于验证「丢帧计数可见 + 日志有 dropped=」。
        uint32_t inject_drop_every = 0;
    };

    struct Stats {
        uint64_t frames_encoded = 0;
        uint64_t frames_duplicated = 0;
        // 轮 30（M6-03）：三项实时指标，全部来自录制循环的真实计数。
        double capture_fps = 0.0;   // 采集侧每秒滑窗到达帧率
        double encode_fps = 0.0;    // 编码侧每秒滑窗提交帧率
        uint64_t frames_captured = 0;
        uint64_t frames_dropped = 0;
        uint64_t blocks_muxed = 0;
        uint64_t bytes_written = 0;
        uint32_t clusters = 0;
        double duration_seconds = 0.0;
        const char* encoder = "none";
        // M3-04：cbr / vbr（ICodecAPI 生效）/ avg_bitrate（仅 MF_MT_AVG_BITRATE）
        const char* rate_control = "none";
        uint32_t rate_control_quality = 0;     // 实际生效的画质档位
        uint32_t rate_control_max_kbps = 0;    // VBR 峰值上限（CBR 等于标称）
        uint64_t filler_bytes = 0;  // CBR 补齐字节数
        uint32_t stream_changes = 0;  // 编码器中途变更输出类型的次数（码控是否被重置）
        const char* converter = "none";
        uint64_t disk_free_bytes = 0;
        double estimated_seconds = 0.0;
        bool disk_warn = false;
        bool disk_stop = false;
        uint64_t paused_ms = 0;
        uint64_t countdown_ms = 0;
        uint32_t resume_fills = 0;
        uint64_t part_bytes_at_pause = 0;
        uint64_t part_bytes_at_resume = 0;
        std::string backend = "auto";
        bool backend_changed = false;
        // M1-08：源的尺寸变化次数与最后一次的尺寸（窗口缩放跟随的观测量）。
        uint32_t source_size_changes = 0;
        uint32_t source_width = 0;
        uint32_t source_height = 0;
        // 控制闭环相关
        bool cancelled = false;
        uint32_t pause_count = 0;
        bool pause_after_stop_rejected = false;
        std::string state_sequence;
        uint64_t write_errors = 0;
        std::string fallback_path;
        int64_t fallback_source_pts_ms = 0;
        std::string control = "absent";  // connected / absent / lost / rejected
        uint32_t commands_applied = 0;
        uint32_t commands_rejected = 0;
        // D-36：手动暂停期间控制通道断开，等待重连宽限期后自动恢复
        bool pause_auto_resumed = false;
        // 暂停超过 --pause-timeout：主动收尾，避免无 UI 时永久挂起
        bool pause_timeout_hit = false;
        // 重连成功次数（诊断用）
        uint32_t control_reconnects = 0;
        int64_t video_last_pts_ms = 0;
        int64_t audio_last_pts_ms = 0;
        // R1（轮 36）：收尾补齐写入的音频帧数 / 其中补静音的帧数。
        // 验收用例据此复算「音轨是否被补到不短于视频」，不靠目测。
        uint64_t audio_tail_fill_frames = 0;
        uint64_t audio_tail_silence_frames = 0;
        // D-39：媒体时间（暂停不计入），与成片时长对齐
        uint64_t media_elapsed_ms = 0;
        std::string output_path;  // 实际落地路径（可能因同名/占用而改名）
    };

    // 返回 0 表示成功。统计写入 stats（可为 nullptr）。
    int Run(const Options& options, Stats& stats, std::string& error);
};

}  // namespace sr::recorder
