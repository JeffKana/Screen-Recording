#include "recorder/RecordingSession.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

#include "audio/AudioTimeline.h"
#include "audio/WasapiSource.h"
#include "capture/CaptureChain.h"
#include "capture/CaptureDeviceAccess.h"
#include "capture/CaptureFactory.h"
#include "capture/DxgiCaptureSource.h"
#include "compositor/Nv12Converter.h"
#include "encode/MfAudioEncoder.h"
#include "encode/MfVideoEncoder.h"
#include "mux/MkvWriter.h"
#include "platform/win/QpcClock.h"
#include "core/util/FreePath.h"
#include "ipc/PipeChannel.h"
#include "recorder/OutputPath.h"
#include "resilience/MkvRecovery.h"
#include "core/state/RecorderState.h"
#include "diag/Logger.h"
#include "recorder/StorageGuard.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sr::recorder {
namespace {

// 轮 30（P0 日志分级）：录制侧日志统一走 sr::diag，tag=record 保持 stdout 前缀
// `[record] ` 不变（既有脚本按这个前缀抓行）；级别由 --log-level 控制。
void Log(const std::string& text) {
    sr::diag::Info(text);
}

void LogDebug(const std::string& text) {
    sr::diag::Debug(text);
}

void LogWarn(const std::string& text) {
    sr::diag::Warn(text);
}

bool AppendAnnexB(std::FILE* file, const std::vector<uint8_t>& avcc) {
    // AVCC（4 字节长度前缀）转 Annex-B（00 00 00 01）。
    // 先整体校验长度链是否完整，再落盘：否则半截写入会产出非法码流。
    size_t offset = 0;
    while (offset + 4 <= avcc.size()) {
        const size_t length = (static_cast<size_t>(avcc[offset]) << 24) |
                              (static_cast<size_t>(avcc[offset + 1]) << 16) |
                              (static_cast<size_t>(avcc[offset + 2]) << 8) |
                              static_cast<size_t>(avcc[offset + 3]);
        if (length == 0 || offset + 4 + length > avcc.size()) {
            return false;
        }
        offset += 4 + length;
    }
    if (offset != avcc.size()) {
        return false;
    }

    static const uint8_t start_code[4] = {0, 0, 0, 1};
    offset = 0;
    while (offset + 4 <= avcc.size()) {
        const size_t length = (static_cast<size_t>(avcc[offset]) << 24) |
                              (static_cast<size_t>(avcc[offset + 1]) << 16) |
                              (static_cast<size_t>(avcc[offset + 2]) << 8) |
                              static_cast<size_t>(avcc[offset + 3]);
        offset += 4;
        std::fwrite(start_code, 1, 4, file);
        std::fwrite(avcc.data() + offset, 1, length, file);
        offset += length;
    }
    return true;
}
// 写盘失败后的兜底：把 .part 截断修复后提交到指定目录（P0-2）。
// 注意 copy 不算——必须经过 MkvRecovery 的「最后一个完整 Cluster」截断，
// 否则拷过去的仍是半截文件。
bool RepairIntoFallback(const std::string& part_path,
                        const std::string& fallback_dir,
                        std::string& out_path,
                        int64_t& source_pts_ms,
                        std::string& error) {
    sr::resilience::MkvRecovery recovery;
    const sr::RecoveryItem item = recovery.Probe(part_path);
    if (!item.repairable) {
        error = "fallback: " + item.probe_reason;
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(fallback_dir, ec);
    if (ec) {
        error = "fallback: cannot create directory " + fallback_dir;
        return false;
    }

    // rec.mkv.part -> rec.mkv.recovered.mkv
    const std::filesystem::path source(part_path);
    std::string stem = source.stem().string();
    if (stem.empty()) {
        stem = "recovered";
    }
    const std::string desired = (std::filesystem::path(fallback_dir) / (stem + ".recovered.mkv")).string();
    const std::string resolved = sr::util::FindFreePath(desired);
    if (resolved.empty()) {
        error = "fallback: no free name in " + fallback_dir;
        return false;
    }
    const std::filesystem::path target(resolved);

    if (!recovery.Repair(item, target.string(), error)) {
        return false;
    }
    out_path = target.string();
    source_pts_ms = item.last_pts_ms;

    // Repair 会把源 .part 改名成 .part.repaired（D-32）。既然已提交到兜底目录，
    // 原目录不必再留一份，否则下次启动还会提示可修复。
    std::error_code clean_ec;
    std::filesystem::remove(part_path, clean_ec);
    clean_ec.clear();
    std::filesystem::remove(part_path + ".repaired", clean_ec);
    return true;
}
}  // namespace

int RecordingSession::Run(const Options& options, Stats& stats, std::string& error) {
    sr::win::QpcClock clock;

    // ---- 状态机（M2-02）----
    // 产品路径自此拥有唯一状态源：此前 G1 的状态机只活在单元测试里，录制流程用的是本地 bool。
    sr::RecorderStateMachine state;
    auto apply = [&](sr::RecorderCommand command) {
        const auto status = state.Apply(command);
        if (!stats.state_sequence.empty()) {
            stats.state_sequence += ",";
        }
        stats.state_sequence += sr::ToString(state.State());
        return status;
    };
    stats.state_sequence = sr::ToString(state.State());  // 记录初始态 Idle
    apply(sr::RecorderCommand::Start);        // Idle -> Preflight
    // 说明：路径可写性检查放在倒计时之后（PrepareOutputPath），
    // 否则取消时会留下一个空目录，违反 M2-04「取消后不产生任何文件」。
    apply(sr::RecorderCommand::PreflightOk);  // Preflight -> Countdown

    // ---- 倒计时与取消（M2-04）----
    if (options.countdown_seconds > 0) {
        Log("countdown " + std::to_string(options.countdown_seconds) + "s");
        const sr::Tick countdown_begin = clock.Now();
        for (;;) {
            const int64_t elapsed = clock.ElapsedMs(countdown_begin, clock.Now());
            if (options.cancel_at_seconds > 0 &&
                elapsed >= static_cast<int64_t>(options.cancel_at_seconds) * 1000) {
                apply(sr::RecorderCommand::Cancel);  // Countdown -> Idle
                stats.cancelled = true;
                stats.countdown_ms = static_cast<uint64_t>(elapsed);
                Log("cancelled during countdown; no directory and no .part created");
                return 14;
            }
            if (elapsed >= static_cast<int64_t>(options.countdown_seconds) * 1000) {
                break;
            }
            Sleep(10);
        }
        stats.countdown_ms = static_cast<uint64_t>(clock.ElapsedMs(countdown_begin, clock.Now()));
    }
    apply(sr::RecorderCommand::CountdownElapsed);  // Countdown -> Recording

    sr::CaptureRequest request;
    // M1-07/M1-08/M1-09：--window 优先于 --region。
    if (options.window_handle != 0) {
        request.kind = sr::CaptureKind::Window;
        request.window_handle = options.window_handle;
    } else {
        request.kind = options.region_enabled ? sr::CaptureKind::Region : sr::CaptureKind::Display;
    }
    request.display_index = options.display_index;
    request.region = options.region;
    request.include_cursor = options.include_cursor;  // M1-24：WGC 桌面源的指针采集开关
    request.target_fps = options.fps;
    request.preferred_backend = options.backend.empty() ? nullptr : options.backend.c_str();

    std::unique_ptr<sr::ICaptureSource> source = sr::capture::CaptureFactory::Create(clock, request, error);
    if (!source) {
        return 2;
    }
    if (!source->Start(request, error)) {
        Log(std::string("capture start failed: ") + error);
        return 3;
    }

    if (auto* chain = dynamic_cast<sr::capture::CaptureChain*>(source.get())) {
        stats.backend = chain->ActiveBackend();
    }

    const sr::SourceCaps caps = source->Caps();
    sr::capture::CaptureDevice capture_device;
    if (!sr::capture::TryGetCaptureDevice(source.get(), capture_device)) {
        Log("capture source did not expose a D3D11 context");
        return 4;
    }

    sr::compositor::Nv12Converter converter;
    // M6-08#2：让既有 CPU 中转路径可达；只置运行期降级用的同一标志，不改转换算法。
    if (options.force_cpu) {
        converter.ForceCpu();
    }
    if (!converter.Initialize(capture_device.device, capture_device.context, caps.max_size.width, caps.max_size.height, error)) {
        Log(std::string("nv12 converter init failed: ") + error);
        return 5;
    }
    stats.converter = converter.UsingGpu() ? "gpu-videoprocessor" : "cpu";
    // M1-08#2：适配策略（pad = 等比+黑边 / stretch = 拉伸铺满）。
    converter.SetFitMode(options.fit_mode == "pad" ? sr::compositor::FitMode::Pad
                                                   : sr::compositor::FitMode::Stretch);

    sr::VideoEncoderConfig config;
    config.width = converter.Width();
    config.height = converter.Height();
    config.fps = options.fps;
    config.bitrate_kbps = options.bitrate_kbps;
    // M3-04：默认 CBR；vbr 走峰值约束 VBR（均值 + 上限），未知值按 CBR 处理。
    config.rate_control =
        options.rate_mode == "vbr" ? sr::RateControlMode::Vbr : sr::RateControlMode::Cbr;
    config.quality = options.quality;
    config.max_bitrate_kbps = options.max_bitrate_kbps;

    // M8-08 全失败分支：注入点下沉到 MfVideoEncoder 内部，
    // 让 硬编创建失败 → 软编创建失败 → 报错 这条真实路径被走到。
    config.fail_all_encoders = options.fail_encoder;
    sr::encode::MfVideoEncoder encoder;
    // M3-05：显式指定编码器时不做回退（不可用 → 下面这条路径退出码 6）。
    encoder.SetRequestedId(options.encoder_id);
    encoder.SetFailureInjection(options.fail_hardware_encoder, options.fail_software_encoder);
    if (!encoder.Open(config, error)) {
        Log(std::string("encoder open failed: ") + error);
        return 6;
    }
    stats.encoder = encoder.BackendName();
    stats.rate_control = encoder.RateControlName();
    stats.rate_control_quality = encoder.Quality();
    stats.rate_control_max_kbps = encoder.MaxBitrateKbps();
    Log(std::string("encoder=") + stats.encoder + " rate_control=" + stats.rate_control +
        " quality=" + std::to_string(stats.rate_control_quality) +
        " max_kbps=" + std::to_string(stats.rate_control_max_kbps) +
        (encoder.RateControlError().empty() ? "" : " rate_control_error=" + encoder.RateControlError()));

    sr::mux::MkvWriter muxer;
    // 录制中写临时文件，正常收尾才改名为成品（M5-04）；崩溃后残留的 .part 供恢复流程处理。
    // 建目录、可写探测、同名避让统一在 PrepareOutputPath 里做（M5-15 / M8-06）。
    sr::recorder::OutputPlan output_plan;
    if (!sr::recorder::PrepareOutputPath(options.output_path, output_plan, error)) {
        Log(std::string("output path rejected: ") + error);
        return 13;
    }
    if (output_plan.renamed_for_conflict) {
        Log("target already exists, writing to: " + output_plan.final_path);
    }
    const std::string part_path = output_plan.part_path;
    muxer.SetFailWriteAfter(options.fail_write_after);
    if (!muxer.Open(part_path, error)) {
        Log(std::string("muxer open failed: ") + error);
        return 7;
    }

    // ---- 存储守护（M8-03/04/05）----
    // 放在 muxer 打开之后：此时输出目录已确认可写，探针路径一定存在。
    sr::recorder::StorageGuard storage(
        part_path,
        static_cast<uint64_t>(options.disk_warn_mb) * 1024ull * 1024ull,
        static_cast<uint64_t>(options.disk_stop_mb) * 1024ull * 1024ull);
    const uint32_t total_bitrate_kbps = options.bitrate_kbps + 192;  // 视频 + 音频

    std::string disk_error;
    if (storage.Refresh(disk_error)) {
        const auto initial = storage.Evaluate(total_bitrate_kbps);
        stats.disk_free_bytes = initial.free_bytes;
        stats.estimated_seconds = initial.estimated_seconds;
        if (initial.must_stop) {
            // 开录前就低于停止线：直接拒绝，避免生成一个必然不可播的碎片文件。
            error = "insufficient disk space to start recording";
            Log(error);
            std::string close_error;
            muxer.Close(close_error);
            std::error_code remove_ec;
            std::filesystem::remove(part_path, remove_ec);
            return 12;
        }
    } else {
        Log(std::string("disk probe failed, continuing without guard: ") + disk_error);
    }
    sr::mux::MkvWriter::VideoTrackInfo track;
    track.width = converter.Width();
    track.height = converter.Height();
    track.fps = options.fps;
    track.codec_private = encoder.SequenceHeader();
    if (!muxer.SetVideoTrack(track, error)) {
        Log(std::string("set video track failed: ") + error);
        return 8;
    }

    // ---- 音频链路：WASAPI 回环 → 时间轴 → AAC ----
    std::unique_ptr<sr::audio::WasapiSource> audio_source;
    std::unique_ptr<sr::audio::AudioTimeline> audio_timeline;
    sr::encode::MfAudioEncoder audio_encoder;
    std::vector<float> audio_buffer;
    int64_t audio_cursor = 0;
    constexpr uint32_t kAudioBlockFrames = 960;
    // 收尾补齐的静音上限（R1，轮 36）：最多补 200ms，避免异常情况下无限写静音。
    constexpr uint64_t kAudioTailFillCapMs = 200;
    // AAC-LC 的帧长（样本数）。AAC 编码器会丢掉末尾不足一帧的部分，
    // 所以「音轨末尾 PTS」比「已提交样本数」少 1~2 帧——补齐目标必须把这部分算进去。
    constexpr uint64_t kAacFrameFrames = 1024;
    bool audio_ok = options.audio;

    if (audio_ok) {
        audio_source = std::make_unique<sr::audio::WasapiSource>(clock, sr::audio::AudioRole::SystemLoopback);
        std::string audio_error;
        if (!audio_source->Start(audio_error)) {
            Log(std::string("audio source unavailable, recording video only: ") + audio_error);
            audio_source.reset();
            audio_ok = false;
        } else {
            const uint32_t rate = audio_source->SampleRate();
            const uint16_t channels = audio_source->Channels();
            audio_timeline = std::make_unique<sr::audio::AudioTimeline>(clock, rate, channels);
            sr::AudioEncoderConfig audio_config;
            audio_config.sample_rate = rate;
            audio_config.channels = channels;
            audio_config.bitrate_kbps = 192;
            if (!audio_encoder.Open(audio_config, audio_error)) {
                Log(std::string("AAC encoder unavailable: ") + audio_error);
                audio_source->Stop();
                audio_source.reset();
                audio_ok = false;
            } else {
                sr::mux::MkvWriter::AudioTrackInfo audio_track;
                audio_track.sample_rate = rate;
                audio_track.channels = channels;
                audio_track.codec_private = audio_encoder.CodecPrivate();
                muxer.SetAudioTrack(audio_track, audio_error);
                audio_buffer.assign(static_cast<size_t>(kAudioBlockFrames) * channels, 0.0f);
            }
        }
    }

    // 音频提交 + 写盘的唯一入口（R1，轮 36）。
    // 主循环与「收尾补齐」都走这一条，audio_cursor 的记账口径只有一处。
    auto submit_audio_block = [&](uint32_t frames) {
        if (frames == 0) {
            return;
        }
        sr::AudioBlock block;
        block.samples = audio_buffer.data();
        block.desc.first_sample = audio_cursor;
        block.desc.sample_rate = audio_source->SampleRate();
        block.desc.channels = audio_source->Channels();
        block.desc.frame_count = frames;
        audio_cursor += frames;

        if (!audio_encoder.Submit(block)) {
            return;
        }
        sr::EncodedPacket packet;
        while (audio_encoder.TryReceive(packet)) {
            if (!muxer.WriteAudio(packet.data, packet.size, packet.pts.value)) {
                ++stats.write_errors;
            } else {
                stats.audio_last_pts_ms = packet.pts.value / 10000;
            }
        }
    };

    std::FILE* es_dump = nullptr;
    if (!options.dump_elementary_stream.empty()) {
        es_dump = std::fopen(options.dump_elementary_stream.c_str(), "wb");
    }

    std::vector<uint8_t> nv12;
    sr::VideoFrame latest{};
    bool have_frame = false;
    // 轮 39（M1-08 · 崩溃修复）：上一帧的纹理必须由会话**持有引用**。
    // 采集侧在窗口尺寸变化时会重建自己的拷贝纹理（旧的被 Release），
    // 会话手里的裸指针随即悬空 → 下一帧转换时访问违规（0xC0000005，实测 5 次里 2 次）。
    // 用 ComPtr 持有即可：旧纹理在会话释放引用前一直有效。
    win::ComPtr<ID3D11Texture2D> latest_texture;

    const int64_t frame_duration_100ns = 10000000LL / static_cast<int64_t>(options.fps == 0 ? 30 : options.fps);
    const uint64_t total_frames = static_cast<uint64_t>(options.seconds) * (options.fps == 0 ? 30 : options.fps);

    const sr::Tick begin = clock.Now();

    // M6-03：三项实时指标（采集 fps / 编码 fps / 丢帧数）全部在录制循环里计数，
    // 每秒滑窗刷新一次并通过 StateSnapshot 扩展块推给 UI。
    // UI 侧不做「帧数 ÷ 时间」倒推——那等于让 UI 自证。
    ULONGLONG metrics_window_start = GetTickCount64();
    ULONGLONG next_metrics_ms = metrics_window_start + 1000;
    uint64_t window_captured = 0;
    uint64_t window_encoded = 0;
    uint64_t window_dropped = 0;
    auto refresh_metrics = [&]() {
        const ULONGLONG now = GetTickCount64();
        // 固定 1Hz：指标每秒刷新一次（M6-03 判据），不随帧率抖动。
        if (now < next_metrics_ms) {
            return;
        }
        const ULONGLONG window_ms = now - metrics_window_start;
        if (window_ms < 200) {
            return;
        }
        const double seconds = static_cast<double>(window_ms) / 1000.0;
        stats.capture_fps = static_cast<double>(window_captured) / seconds;
        stats.encode_fps = static_cast<double>(window_encoded) / seconds;
        char line[192] = {};
        std::snprintf(line, sizeof(line),
                      "metrics capture_fps=%.1f encode_fps=%.1f dropped=%llu captured=%llu window_ms=%llu",
                      stats.capture_fps, stats.encode_fps,
                      static_cast<unsigned long long>(stats.frames_dropped),
                      static_cast<unsigned long long>(stats.frames_captured),
                      static_cast<unsigned long long>(window_ms));
        Log(line);
        metrics_window_start = now;
        next_metrics_ms = now + 1000;
        window_captured = 0;
        window_encoded = 0;
        window_dropped = 0;
    };

    // 暂停计划（M2-02）。--pause-at / --pause-duration 是验收用的确定性驱动；
    // 支持多段（"2,5" 配 "1,1"），也兼容只给单值的旧写法。
    // G4 接入 IPC 后，UI 的暂停按钮复用同一套状态与恢复流程。
    const uint32_t fps_value = options.fps == 0 ? 30 : options.fps;
    std::vector<uint32_t> pause_at_seconds = options.pause_at_list;
    std::vector<uint32_t> pause_duration_seconds = options.pause_duration_list;
    if (pause_at_seconds.empty() && options.pause_at_seconds > 0) {
        pause_at_seconds.push_back(options.pause_at_seconds);
        pause_duration_seconds.push_back(options.pause_duration_seconds);
    }
    if (pause_duration_seconds.size() < pause_at_seconds.size()) {
        const uint32_t fill = pause_duration_seconds.empty() ? options.pause_duration_seconds
                                                             : pause_duration_seconds.back();
        pause_duration_seconds.resize(pause_at_seconds.size(), fill);
    }
    std::vector<uint64_t> pause_at_frames;
    std::vector<int64_t> pause_durations_ms;
    for (size_t i = 0; i < pause_at_seconds.size(); ++i) {
        const uint64_t frame = static_cast<uint64_t>(pause_at_seconds[i]) * fps_value;
        const int64_t duration = static_cast<int64_t>(pause_duration_seconds[i]) * 1000;
        if (frame == 0 || duration <= 0) {
            continue;
        }
        pause_at_frames.push_back(frame);
        pause_durations_ms.push_back(duration);
    }

    size_t pause_index = 0;
    bool paused = false;
    int fatal_code = 0;  // 非 0 表示发生了必须向上传播的错误（D-28）
    sr::Tick pause_start{};
    // 累计暂停时长：墙钟节拍整体顺延，输出时间轴（index）不推进，
    // 这样「暂停 4 秒」不会让成片多出 4 秒，也不会在恢复瞬间突发补帧。
    int64_t timeline_offset_100ns = 0;
    std::vector<uint8_t> cached_nv12;  // 暂停前最后一帧，恢复时用于补齐
    int resume_fill_left = 0;

    // D-39：对 UI 暴露的 session_elapsed_ms 必须是「媒体时间」，不是墙钟。
    // 基准取 begin（已在倒计时与采集/编码初始化之后），暂停期间冻结，
    // 恢复后扣除累计暂停时长 —— 与成片时间轴一致（M2-09）。
    sr::Tick media_start = begin;
    int64_t media_paused_ms = 0;

    // D-36：手动暂停期间控制通道断开，先给 1.5s 重连宽限；过期即自动恢复。
    // 验收判据是「≤2s」，宽限 1.5s + 握手候选追加 300ms，留足余量。
    // 暂停超时（默认 900s）是最后一道保险：UI 永久消失时不能无限挂起，
    // 到点主动收尾，产物时长等于已录内容时长（暂停期不计入）。
    constexpr int64_t kControlLostGraceMs = 1500;
    const int64_t pause_timeout_ms =
        static_cast<int64_t>(options.pause_timeout_seconds) * 1000LL;  // 0 = 不设上限

    // ---- 控制通道（T-17a / D-36）----
    // 契约 §7：客户端必须先发 Hello{token}，服务端校验后回 HelloAck。
    // 握手必须非阻塞：静默客户端（连上不发 Hello）不得拖慢录制主循环。
    // 这里用「候选连接 + 截止时间」状态机每帧推进，不引线程、不改 I/O 模型。
    sr::ipc::PipeServer control_server;
    std::unique_ptr<sr::ipc::PipeConnection> control_connection;    // 已完成握手
    std::unique_ptr<sr::ipc::PipeConnection> handshake_connection;  // 等待 Hello
    ULONGLONG handshake_deadline_ms = 0;
    bool control_pause_pending = false;
    bool control_resume_pending = false;
    bool stop_requested = false;
    bool manual_pause = false;  // 由控制通道触发、等待 Resume 的暂停
    ULONGLONG next_accept_ms = 0;
    ULONGLONG next_snapshot_ms = 0;
    bool control_ever_connected = false;

    auto media_elapsed_ms = [&]() -> uint64_t {
        // 暂停中锚定在 pause_start：3 秒暂停内 UI 查询多少次，返回值都不变。
        const sr::Tick anchor = paused ? pause_start : clock.Now();
        const int64_t value = clock.ElapsedMs(media_start, anchor) - media_paused_ms;
        return value > 0 ? static_cast<uint64_t>(value) : 0ull;
    };

    // 暂停被 Stop/超时终止（不走 Resume）时，把这一段暂停也计入扣除，
    // 否则最终 media_elapsed 会把暂停时长算进成片时长。
    auto freeze_pause_for_media = [&]() {
        if (paused) {
            media_paused_ms += clock.ElapsedMs(pause_start, clock.Now());
        }
    };

    auto make_snapshot = [&](uint8_t snapshot_state,
                             sr::ipc::SnapshotKind kind = sr::ipc::SnapshotKind::Periodic) {
        sr::ipc::StateSnapshotPayload snapshot;
        snapshot.state = snapshot_state;
        snapshot.parameter_locked = state.IsParameterLocked();
        snapshot.session_elapsed_ms = media_elapsed_ms();
        snapshot.kind = static_cast<uint8_t>(kind);
        // M6-03：值取自 Stats（录制循环计数），不在协议层做任何推算。
        snapshot.metrics.capture_fps_x100 =
            static_cast<uint32_t>(stats.capture_fps * 100.0 + 0.5);
        snapshot.metrics.encode_fps_x100 =
            static_cast<uint32_t>(stats.encode_fps * 100.0 + 0.5);
        snapshot.metrics.frames_dropped = stats.frames_dropped;
        snapshot.metrics.frames_captured = stats.frames_captured;
        snapshot.has_metrics = true;
        return snapshot;
    };

    auto start_handshake = [&](std::unique_ptr<sr::ipc::PipeConnection> candidate) {
        handshake_connection = std::move(candidate);
        handshake_deadline_ms = GetTickCount64() + 1000;  // 1s 不完成握手即拒绝
    };

    auto drop_handshake = [&](const char* result) {
        if (handshake_connection) {
            handshake_connection->Close();
            handshake_connection.reset();
        }
        stats.control = result;
    };

    auto poll_handshake = [&]() {
        if (!handshake_connection) {
            return;
        }
        sr::ipc::Message message;
        if (handshake_connection->TryReceive(message)) {
            if (message.type != sr::ipc::MessageType::Hello) {
                drop_handshake("rejected");
                Log("control handshake rejected: first message must be Hello");
                return;
            }
            sr::ipc::HelloPayload hello;
            if (!sr::ipc::Decode(message.payload, hello) || hello.token != options.control_token) {
                drop_handshake("rejected");
                Log("control handshake rejected: bad token");
                return;
            }
            sr::ipc::HelloAckPayload ack;
            ack.protocol_version = sr::ipc::kProtocolVersion;
            ack.process_id = GetCurrentProcessId();
            ack.state = static_cast<uint8_t>(state.State());
            if (!handshake_connection->Send(
                    sr::ipc::Message{sr::ipc::MessageType::HelloAck, sr::ipc::Encode(ack)})) {
                drop_handshake("rejected");
                return;
            }

            // 契约 §7 在 Ack 之后紧跟一条状态快照：srctl（以及未来的 UI）会阻塞等它。
            // P1：重连补的必须是「当前」快照。暂停中重连时 state 已是 Paused，
            // 与 state_sequence 的末态一致，不能沿用握手排队时的旧值。
            const sr::ipc::StateSnapshotPayload snapshot =
                make_snapshot(static_cast<uint8_t>(state.State()));
            if (!handshake_connection->Send(
                    sr::ipc::Message{sr::ipc::MessageType::StateSnapshot, sr::ipc::Encode(snapshot)})) {
                drop_handshake("rejected");
                return;
            }
            control_connection = std::move(handshake_connection);
            if (control_ever_connected) {
                ++stats.control_reconnects;
                Log("control client reconnected");
            } else {
                Log("control client connected");
            }
            control_ever_connected = true;
            stats.control = "connected";
            return;
        }
        if (!handshake_connection->IsOpen()) {
            drop_handshake("lost");  // 连上又立刻断开
            return;
        }
        if (GetTickCount64() >= handshake_deadline_ms) {
            drop_handshake("rejected");
            Log("control handshake timeout (silent client rejected), recording continues");
        }
    };

    if (!options.control_pipe.empty()) {
        std::string pipe_error;
        const std::wstring control_name(options.control_pipe.begin(), options.control_pipe.end());
        if (!control_server.Open(control_name, pipe_error)) {
            Log("control pipe open failed: " + pipe_error);
            stats.control = "absent";
        } else {
            auto candidate = control_server.WaitForClient(options.control_wait_ms, pipe_error);
            if (candidate) {
                // 只登记候选，握手交给主循环推进：连接本身不构成启动延迟。
                start_handshake(std::move(candidate));
                stats.control = "absent";
            } else {
                stats.control = control_server.ClientSeen() ? "lost" : "absent";
                Log("control channel not established (" + pipe_error + "), recording continues");
            }
        }
        next_accept_ms = GetTickCount64() + 100;
    }

    // 每帧调用一次：只读「已到齐的整帧」，绝不在主循环里阻塞。
    auto poll_control = [&]() {
        if (options.control_pipe.empty()) {
            return;
        }
        if (handshake_connection) {
            poll_handshake();
            return;
        }
        if (!control_connection) {
            // 每 100ms 尝试接受一次：srctl 是一次性客户端，UI 重启也要能接上。
            if (GetTickCount64() >= next_accept_ms) {
                next_accept_ms = GetTickCount64() + 100;
                std::string accept_error;
                auto candidate = control_server.TryAcceptClient(accept_error);
                if (candidate) {
                    start_handshake(std::move(candidate));
                }
            }
            if (!control_connection) {
                return;
            }
        }
        for (int guard = 0; guard < 16; ++guard) {
            sr::ipc::Message message;
            if (!control_connection->TryReceive(message)) {
                if (!control_connection->IsOpen()) {
                    stats.control = "lost";
                    control_connection.reset();
                    Log("control channel lost, recording continues");
                }
                return;
            }
            if (message.type != sr::ipc::MessageType::Command) {
                continue;
            }
            sr::ipc::CommandPayload payload;
            if (!sr::ipc::Decode(message.payload, payload)) {
                ++stats.commands_rejected;
                continue;
            }
            const auto command = static_cast<sr::RecorderCommand>(payload.command);
            bool applied = true;
            if (command == sr::RecorderCommand::Pause && !paused) {
                control_pause_pending = true;  // 真正的暂停动作在主循环里做
            } else if (command == sr::RecorderCommand::Resume && paused) {
                control_resume_pending = true;
            } else if (command == sr::RecorderCommand::Stop) {
                stop_requested = true;
            } else {
                applied = state.Apply(command) == sr::TransitionStatus::Accepted;
            }
            if (applied) {
                ++stats.commands_applied;
            } else {
                ++stats.commands_rejected;
            }

            // 契约 §7：命令处理完回一条 StateSnapshot，srctl 会阻塞等它。
            // Pause/Resume/Stop 的真实状态切换发生在主循环里（要先停采集再登记状态），
            // 这里回报「命令生效后的目标状态」，否则客户端拿到的是切换前的旧状态。
            uint8_t reported_state = static_cast<uint8_t>(state.State());
            if (command == sr::RecorderCommand::Pause) {
                reported_state = static_cast<uint8_t>(sr::RecorderState::Paused);
            } else if (command == sr::RecorderCommand::Resume) {
                reported_state = static_cast<uint8_t>(sr::RecorderState::Recording);
            } else if (command == sr::RecorderCommand::Stop) {
                reported_state = static_cast<uint8_t>(sr::RecorderState::Stopping);
            }
            control_connection->Send(sr::ipc::Message{
                sr::ipc::MessageType::StateSnapshot,
                sr::ipc::Encode(make_snapshot(reported_state,
                                              sr::ipc::SnapshotKind::CommandReceipt))});
        }
    };
    // 编码器不回报「是否关键帧」，按 GOP 假设（每 fps*2 帧一个关键帧）标记，
    // 只影响 Cluster 切分与 seek 精度，不影响解码正确性。
    uint64_t packet_index = 0;
    const uint64_t gop = static_cast<uint64_t>(options.fps == 0 ? 30 : options.fps) * 2;
    // 视频轨末尾 PTS（R1 收尾补齐的参照）：取所有写进容器的视频包里的最大值，
    // 而不是「最后一个写出的包」——有 B 帧时两者不是同一个值。
    int64_t max_video_pts_ms = 0;

    auto drain_encoder = [&]() {
        sr::EncodedPacket packet;
        while (encoder.TryReceive(packet)) {
            const bool keyframe = (packet_index % gop) == 0;
            ++packet_index;
            if (!muxer.WriteVideo(packet.data, packet.size, packet.pts.value, keyframe)) {
                ++stats.write_errors;
            } else {
                // 记录"真正写进去的末尾 PTS"，而不是用帧数推算（F1：推算会少算一帧）。
                stats.video_last_pts_ms = packet.pts.value / 10000;
                if (stats.video_last_pts_ms > max_video_pts_ms) {
                    max_video_pts_ms = stats.video_last_pts_ms;
                }
            }
            if (es_dump != nullptr) {
                std::vector<uint8_t> avcc(packet.data, packet.data + packet.size);
                AppendAnnexB(es_dump, avcc);
            }
        }
    };

    for (uint64_t index = 0; index < total_frames; ++index) {
        // ---- 暂停 / 恢复（M2-02）----
        poll_control();
        if (stop_requested) {
            break;
        }
        // 录制中至少每秒推送一条快照：UI 的计时/剩余时长不能只在命令回执时更新。
        // 状态仍是唯一源（媒体时间），不引入 UI 侧的乐观计时。
        if (control_connection && GetTickCount64() >= next_snapshot_ms) {
            next_snapshot_ms = GetTickCount64() + 1000;
            control_connection->Send(sr::ipc::Message{
                sr::ipc::MessageType::StateSnapshot,
                sr::ipc::Encode(make_snapshot(static_cast<uint8_t>(state.State())))});
        }

        const bool timer_pause = !paused && pause_index < pause_at_frames.size() &&
                                 index >= pause_at_frames[pause_index];
        if (!paused && (timer_pause || control_pause_pending)) {
            manual_pause = control_pause_pending;
            control_pause_pending = false;
            paused = true;
            pause_start = clock.Now();
            // 按 02 §5 规则 4 的裁定：暂停即真正停掉采集与音频，避免白占 GPU 与磁盘带宽。
            source->Stop();
            if (audio_ok) {
                audio_source->Stop();
            }
            // 暂停前先把当前 Cluster 落盘：暂停期间被杀时，这段内容才不会丢。
            if (!muxer.FlushPending()) {
                ++stats.write_errors;
            }
            stats.part_bytes_at_pause = muxer.BytesWritten();
            apply(sr::RecorderCommand::Pause);  // Recording -> Paused
            ++stats.pause_count;
            Log("paused at frame " + std::to_string(index));
            refresh_metrics();
        }
        if (paused) {
            // 暂停窗口内不采集、不写盘：这是 C2「.part 不增长」的依据。
            if (stop_requested) {
                // 暂停中收到 Stop：直接进入收尾，不要再走 Resume（否则状态序列会多一次 Recording）。
                freeze_pause_for_media();
                paused = false;
                break;
            }

            bool pause_timed_out = false;
            if (manual_pause) {
                // 手动暂停：等到 Resume / Stop，或控制通道断开的宽限期结束。
                // 断线后 UI 已经无法发出 Resume；无限等待会让用户以为在录、
                // 磁盘上却什么都没有（05 §7.1 Blocker，D-36）。
                ULONGLONG control_lost_at = 0;
                if (!control_connection) {
                    control_lost_at = GetTickCount64();
                }
                while (!control_resume_pending && !stop_requested) {
                    poll_control();
                    const ULONGLONG now = GetTickCount64();
                    if (!control_connection && control_lost_at == 0) {
                        control_lost_at = now;
                        Log("control lost during manual pause; auto-resume in 1500ms if not reconnected");
                    } else if (control_connection) {
                        control_lost_at = 0;  // 重连成功，取消宽限
                    }
                    // 握手中的候选连接也算「正在重连」：再多给 300ms，上限仍 < 2s。
                    const ULONGLONG resume_deadline =
                        control_lost_at == 0 ? 0
                                             : control_lost_at + kControlLostGraceMs +
                                                   (handshake_connection ? 300ull : 0ull);
                    if (resume_deadline != 0 && now >= resume_deadline) {
                        stats.pause_auto_resumed = true;
                        control_resume_pending = true;
                        Log("pause auto-resumed: control channel lost, recording continues");
                        break;
                    }
                    if (pause_timeout_ms > 0 &&
                        clock.ElapsedMs(pause_start, clock.Now()) >= pause_timeout_ms) {
                        stats.pause_timeout_hit = true;
                        pause_timed_out = true;
                        Log("pause timeout (" + std::to_string(options.pause_timeout_seconds) +
                            "s), finalizing recording");
                        break;
                    }
                    Sleep(5);
                }
            } else {
                const int64_t want_ms = pause_durations_ms[pause_index];
                while (clock.ElapsedMs(pause_start, clock.Now()) < want_ms) {
                    poll_control();
                    // 计时暂停有明确的计划终点，不受 --pause-timeout 截断（F3）。
                    // 超时是给「UI 消失、无人恢复」的手动暂停兜底的；
                    // 若把 1200s 的计划暂停截到 900s，反而制造了不可预期行为。
                    Sleep(5);
                }
            }
            if (pause_timed_out) {
                freeze_pause_for_media();
                paused = false;
                break;  // 直接收尾：不写 Resume，状态序列保持 ...Paused,Stopping,Idle
            }
            if (stop_requested) {
                // Stop 是等待期间到达的：直接收尾，不要 Resume（否则状态序列多一次 Recording）。
                freeze_pause_for_media();
                paused = false;
                break;
            }

            const int64_t paused_ms = clock.ElapsedMs(pause_start, clock.Now());
            // 无论后面是正常恢复还是恢复失败，这一段暂停都不属于媒体时间。
            media_paused_ms += paused_ms;
            stats.part_bytes_at_resume = muxer.BytesWritten();

            if (options.fail_resume) {
                // 注入：恢复失败必须以非零码退出，而不是静默收尾（D-28）。
                error = "resume failed (injected by --fail-resume)";
                Log(error);
                paused = false;  // 已把 pause 计入 media_paused，收尾时不能再按暂停锚定
                fatal_code = 15;
                break;
            }

            // 注入：让恢复时的原后端不可用，验证降级链真实生效（C11）。
            // 造不出真实降级时该项必须判 BLOCKED，不接受硬编码 backend_changed=1。
            sr::CaptureRequest resume_request = request;
            if (options.fail_resume_backend && !stats.backend.empty()) {
                resume_request.excluded_backend = stats.backend.c_str();
            }
            std::string resume_error;
            if (!source->Start(resume_request, resume_error)) {
                error = "resume failed: " + resume_error;
                Log(error);
                paused = false;
                fatal_code = 15;
                break;
            }

            // 恢复后采集源是新建的，D3D11 设备很可能已经变了。
            // 转换器仍绑在旧设备上会导致 VideoProcessorBlt 失败，进而 CPU 路径跨设备拷贝崩溃。
            if (auto* chain = dynamic_cast<sr::capture::CaptureChain*>(source.get())) {
                const std::string resumed_backend = chain->ActiveBackend();
                if (resumed_backend != stats.backend) {
                    Log("WARNING: backend changed after resume: " + stats.backend + " -> " + resumed_backend);
                    stats.backend = resumed_backend;
                    stats.backend_changed = true;
                }
            }

            sr::capture::CaptureDevice resumed_device;
            if (!sr::capture::TryGetCaptureDevice(source.get(), resumed_device)) {
                error = "resume: capture source exposed no D3D11 device";
                Log(error);
                paused = false;
                fatal_code = 15;
                break;
            }
            // 无条件重建：旧设备在 Stop() 时已释放，指针可能被新设备复用，
            // 用指针比较判断「是否变了」会漏判，进而让转换器继续绑在已释放的设备上。
            {
                const bool same_pointer = resumed_device.device == capture_device.device;
                const uint32_t previous_width = converter.Width();
                const uint32_t previous_height = converter.Height();
                std::string reinit_error;
                if (!converter.Initialize(resumed_device.device, resumed_device.context,
                                          caps.max_size.width, caps.max_size.height, reinit_error)) {
                    error = "resume: converter re-init failed: " + reinit_error;
                    Log(error);
                    paused = false;
                    fatal_code = 15;
                    break;
                }
                capture_device = resumed_device;
                // 只有尺寸变了才丢缓存：缓存帧是恢复期补齐的唯一数据源，
                // 清空它会导致恢复后既无新帧又无可补帧，直接跳过若干帧形成空洞。
                if (converter.Width() != previous_width || converter.Height() != previous_height) {
                    cached_nv12.clear();
                }
                Log(std::string("converter rebuilt on resume") + (same_pointer ? " (same address)" : ""));
            }
            if (audio_ok) {
                std::string audio_resume_error;
                if (audio_source->Start(audio_resume_error)) {
                    // 时间轴重置：否则恢复后会按「已过时间」一次性补出 4 秒静音，音轨比视频长。
                    audio_timeline->Reset();
                } else {
                    Log("audio resume failed, continuing video only: " + audio_resume_error);
                    audio_ok = false;
                }
            }

            timeline_offset_100ns += paused_ms * 10000LL;
            stats.paused_ms += static_cast<uint64_t>(paused_ms);
            paused = false;
            if (!manual_pause) {
                ++pause_index;
            }
            control_resume_pending = false;
            apply(sr::RecorderCommand::Resume);  // Paused -> Recording
            resume_fill_left = 3;  // 规范允许用复制上一帧补齐 ≤3 帧
            have_frame = false;    // 旧帧的纹理随采集源停止已失效
            latest = sr::VideoFrame{};
            latest_texture.Reset();
            Log("resumed after " + std::to_string(paused_ms) + "ms");
            // 暂停段的空洞不能算进 fps 滑窗，恢复时重新起算。
            metrics_window_start = GetTickCount64();
            next_metrics_ms = metrics_window_start + 1000;
            window_captured = 0;
            window_encoded = 0;
            window_dropped = 0;
        }

        const int64_t deadline_100ns =
            static_cast<int64_t>(index) * frame_duration_100ns + timeline_offset_100ns;
        // 等到达该帧的时刻（用 QPC 换算成 100ns）
        for (;;) {
            const int64_t elapsed_100ns = clock.ElapsedMs(begin, clock.Now()) * 10000LL;
            if (elapsed_100ns >= deadline_100ns) {
                break;
            }
            sr::VideoFrame probe;
            if (source->TryAcquire(probe)) {
                latest = probe;
                latest_texture.Attach(static_cast<ID3D11Texture2D*>(probe.native_texture));
                have_frame = true;
                // 采集到达计数：这是「采集侧 fps」的唯一来源。
                ++stats.frames_captured;
                ++window_captured;
            } else if (source->NeedsRebuild()) {
                std::string rebuild_error;
                source->Stop();
                if (!source->Start(request, rebuild_error)) {
                    Log(std::string("capture rebuild failed: ") + rebuild_error);
                    error = rebuild_error;
                    return 9;
                }
                have_frame = false;
            }
            Sleep(1);
        }

        // 再取一次，尽量用最新帧
        sr::VideoFrame fresh;
        if (source->TryAcquire(fresh)) {
            latest = fresh;
            latest_texture.Attach(static_cast<ID3D11Texture2D*>(fresh.native_texture));
            have_frame = true;
            ++stats.frames_captured;
            ++window_captured;
        } else {
            ++stats.frames_duplicated;
        }

        // 优先用新帧；恢复后若采集尚未出帧，用暂停前最后一帧补齐（≤3 帧）。
        const uint8_t* submit_data = nullptr;
        size_t submit_size = 0;

        auto* texture = have_frame ? latest_texture.Get() : nullptr;
        // M1-08：窗口缩放时源的尺寸会变（输出尺寸固定，内容按策略缩放）。
        // 这里记下尺寸变化次数与最后一次尺寸——验收方要求「先量再调」：
        // 重建次数与丢帧一起看，才知道要不要限流。
        if (have_frame) {
            if (stats.source_width == 0) {
                stats.source_width = latest.desc.size.width;
                stats.source_height = latest.desc.size.height;
            } else if (latest.desc.size.width != stats.source_width ||
                       latest.desc.size.height != stats.source_height) {
                stats.source_width = latest.desc.size.width;
                stats.source_height = latest.desc.size.height;
                ++stats.source_size_changes;
            }
        }
        if (texture != nullptr && converter.Convert(texture, nv12, error)) {
            cached_nv12 = nv12;
            submit_data = cached_nv12.data();
            submit_size = cached_nv12.size();
            resume_fill_left = 0;  // 已拿到新帧，不再需要补齐
        } else if (resume_fill_left > 0 && !cached_nv12.empty()) {
            submit_data = cached_nv12.data();
            submit_size = cached_nv12.size();
            --resume_fill_left;
            ++stats.resume_fills;
        }

        if (submit_data == nullptr) {
            continue;
        }

        // 人为丢帧注入（M6-03 验收）：跳过编码提交并计入 dropped。
        // 最后一帧除外，避免把首帧关键帧丢掉导致成片缺参考帧。
        if (options.inject_drop_every > 0 && index + 1 < total_frames &&
            (index % static_cast<uint64_t>(options.inject_drop_every)) ==
                static_cast<uint64_t>(options.inject_drop_every) - 1) {
            ++stats.frames_dropped;
            ++window_dropped;
            LogDebug("inject_drop frame=" + std::to_string(index) + " dropped=" +
                     std::to_string(stats.frames_dropped));
            refresh_metrics();
            continue;
        }

        const int64_t pts = static_cast<int64_t>(index) * frame_duration_100ns;
        if (!encoder.SubmitBytes(submit_data, submit_size, pts)) {
            ++stats.frames_dropped;
            ++window_dropped;
            LogWarn("encoder submit failed, frame dropped");
            refresh_metrics();
            continue;
        }
        ++stats.frames_encoded;
        ++window_encoded;
        LogDebug("submit frame=" + std::to_string(index) + " bytes=" +
                 std::to_string(submit_size));
        drain_encoder();
        refresh_metrics();

        if (stats.write_errors > 0 && fatal_code == 0) {
            error = "muxer write failed";
            Log(error);
            fatal_code = 15;
            break;
        }

        // 每秒查一次水位：太频繁会拖慢主循环，太稀疏则可能在两次检查之间写满。
        if (index % static_cast<uint64_t>(options.fps == 0 ? 30 : options.fps) == 0) {
            std::string refresh_error;
            if (storage.Refresh(refresh_error)) {
                const auto status = storage.Evaluate(total_bitrate_kbps);
                stats.disk_free_bytes = status.free_bytes;
                stats.estimated_seconds = status.estimated_seconds;
                if (status.warn && !stats.disk_warn) {
                    stats.disk_warn = true;
                    LogWarn("low disk: " + std::to_string(status.free_bytes / (1024ull * 1024ull)) +
                            " MB free, ~" +
                            std::to_string(static_cast<long long>(status.estimated_seconds)) + "s left");
                }
                if (status.must_stop) {
                    // 提前收尾：让封装层正常写 Duration/Cluster，比「写失败才发现」可靠得多。
                    stats.disk_stop = true;
                    LogWarn("disk below stop line, finalizing early to keep the file playable");
                    break;
                }
            }
        }

        if (audio_ok) {
            // 一次迭代可能欠下多块音频，循环拉平，避免音轨落后于视频时间轴。
            for (int guard = 0; guard < 8; ++guard) {
                const uint32_t pulled = audio_timeline->Pull(*audio_source, audio_buffer.data(), kAudioBlockFrames);
                if (pulled == 0) {
                    break;
                }
                submit_audio_block(pulled);
            }
        }
    }

    // 收尾：无论正常结束还是错误退出，都必须把已录内容落成可播文件（D-28）。
    stats.media_elapsed_ms = media_elapsed_ms();  // 媒体时间（D-39），与成片时长对齐
    apply(sr::RecorderCommand::Stop);  // Recording / Paused -> Stopping

    // 收尾：冲刷编码器，把剩余包全部写盘。
    std::string flush_error;
    encoder.Flush(flush_error);
    for (int guard = 0; guard < 600; ++guard) {
        sr::EncodedPacket packet;
        if (!encoder.TryReceive(packet)) {
            break;
        }
        if (muxer.WriteVideo(packet.data, packet.size, packet.pts.value, false)) {
            stats.video_last_pts_ms = packet.pts.value / 10000;
            if (stats.video_last_pts_ms > max_video_pts_ms) {
                max_video_pts_ms = stats.video_last_pts_ms;
            }
        } else {
            ++stats.write_errors;
        }
        if (es_dump != nullptr) {
            std::vector<uint8_t> avcc(packet.data, packet.data + packet.size);
            AppendAnnexB(es_dump, avcc);
        }
    }
    if (es_dump != nullptr) {
        std::fclose(es_dump);
    }

    // ---- 收尾补齐（R1，轮 36）：音轨不得短于视频 ----
    //
    // 机制：主循环里的音频 pull 只在循环内发生，最后一次 pull 之后没有补拉；
    //       AAC 编码器的输出又按 1024 样本一帧对齐，末帧 PTS 落在帧边界上。
    //       于是收尾时音轨比视频短 0~186ms（8h 实测 99ms、60s 实测 92ms），
    //       判据 ≤100ms 只差 1ms 属「余量运气」，重跑完全可能超。
    //
    // 做法：目标不是「补够 media_elapsed」，而是「音轨末尾 PTS ≥ 视频轨末尾 PTS」——
    //       视频轨末尾是成片时长的唯一口径（容器 Duration 取最后一个视频块）。
    //       ① 先按时间轴再拉一次，把「最后一次 pull 之后流逝的时间」用真实音频补上；
    //       ② 仍不够的部分补静音（上限 200ms），保证音轨不短于视频。
    if (audio_ok) {
        const uint32_t rate = audio_source->SampleRate();
        const int64_t target_ms = max_video_pts_ms;
        // 目标样本数：把视频轨末尾换算成样本数，再向上取整到 AAC 帧边界并多留 1 帧。
        // AAC 编码器每 1024 样本出一帧，末帧不足一帧时补零输出，于是
        // 「最后一个音频包的 PTS」= (ceil(S/1024) − 1) × 1024（S = 已提交样本数）。
        // 要让音轨末尾 PTS ≥ 视频末尾 PTS，需要 ceil(S/1024) ≥ ceil(V/1024) + 1，
        // 即 S ≥ ceil(V/1024)×1024 + 1 —— 那个 +1 就是「必须多提交一帧」的那一帧。
        // 余量取 0 帧：边界即最紧的无残余位置，尾点偏差落在 0~23ms（半个 AAC 帧）内。
        const uint64_t video_end_frames =
            (static_cast<uint64_t>(target_ms > 0 ? target_ms : 0) * rate + 999ull) / 1000ull;
        const uint64_t target_frames =
            ((video_end_frames + kAacFrameFrames - 1ull) / kAacFrameFrames) * kAacFrameFrames + 1ull;

        // ① 先按时间轴再拉一次：把「最后一次 pull 之后流逝的时间」用真实音频补上。
        for (int guard = 0; guard < 8; ++guard) {
            if (static_cast<uint64_t>(audio_cursor) >= target_frames) {
                break;
            }
            const uint32_t pulled = audio_timeline->Pull(*audio_source, audio_buffer.data(), kAudioBlockFrames);
            if (pulled == 0) {
                break;
            }
            submit_audio_block(pulled);
            stats.audio_tail_fill_frames += pulled;
        }

        // ② 仍不够的部分补静音，直到音轨末尾不短于视频末尾。
        const uint64_t cap_frames = static_cast<uint64_t>(rate) * kAudioTailFillCapMs / 1000ull;
        uint64_t silence_frames = 0;
        while (static_cast<uint64_t>(audio_cursor) < target_frames && silence_frames < cap_frames) {
            const uint64_t remain = target_frames - static_cast<uint64_t>(audio_cursor);
            const uint64_t room = cap_frames - silence_frames;
            const uint32_t frames = static_cast<uint32_t>(
                std::min<uint64_t>(kAudioBlockFrames, std::min<uint64_t>(remain, room)));
            if (frames == 0) {
                break;
            }
            std::memset(audio_buffer.data(), 0, audio_buffer.size() * sizeof(float));
            submit_audio_block(frames);
            silence_frames += frames;
            stats.audio_tail_fill_frames += frames;
        }
        stats.audio_tail_silence_frames = silence_frames;
        Log("audio tail fill: video_end_pts_ms=" + std::to_string(target_ms) +
            " target_frames=" + std::to_string(target_frames) +
            " cursor=" + std::to_string(audio_cursor) +
            " fill_frames=" + std::to_string(stats.audio_tail_fill_frames) +
            " silence_frames=" + std::to_string(silence_frames) +
            " cap_frames=" + std::to_string(cap_frames));
    }

    if (audio_ok) {
        std::string audio_flush_error;
        audio_encoder.Flush(audio_flush_error);
        for (int guard = 0; guard < 600; ++guard) {
            sr::EncodedPacket packet;
            if (!audio_encoder.TryReceive(packet)) {
                break;
            }
            if (muxer.WriteAudio(packet.data, packet.size, packet.pts.value)) {
                stats.audio_last_pts_ms = packet.pts.value / 10000;
            } else {
                ++stats.write_errors;
            }
        }
        audio_encoder.Close();
        audio_source->Stop();
    }

    source->Stop();
    encoder.Close();

    bool close_failed = false;
    if (!muxer.Close(error)) {
        Log(std::string("muxer close failed: ") + error);
        close_failed = true;
        if (fatal_code == 0) {
            fatal_code = 10;
        }
    }

    // 只有「数据完整性受损」（写盘出错或封装收尾失败）才需要截断修复；
    // 像 --fail-resume 这种「运行期出错但数据完好」的情况，仍应正常提交成品。
    const bool integrity_lost = stats.write_errors > 0 || close_failed;
    if (fatal_code != 0 && integrity_lost) {
        if (!options.fallback_dir.empty()) {
            std::string fallback_path;
            std::string fallback_error;
            int64_t source_pts_ms = 0;
            if (RepairIntoFallback(part_path, options.fallback_dir, fallback_path, source_pts_ms, fallback_error)) {
                stats.fallback_source_pts_ms = source_pts_ms;
                stats.fallback_path = fallback_path;
                std::error_code remove_ec;
                std::filesystem::remove(part_path, remove_ec);  // 原目录不留半成品
                Log("recovered into fallback: " + fallback_path);
                apply(sr::RecorderCommand::FinalizeDone);
                return 16;
            }
            Log("fallback failed: " + fallback_error);
        }
        // 没有兜底目录或修复失败：保留 .part 供离线恢复，不覆盖成成品。
        apply(sr::RecorderCommand::FinalizeDone);
        return fatal_code;
    }

    // 收尾改名：目标若在录制期间被占用，CommitOutput 会自动换名而不是判失败（M8-07）。
    std::string actual_output;
    if (!sr::recorder::CommitOutput(output_plan, actual_output, error)) {
        Log(error);
        return 11;
    }
    stats.output_path = actual_output;

    apply(sr::RecorderCommand::FinalizeDone);  // Stopping -> Idle

    // 「Stop 之后 Pause 必须被拒」：状态机是产品路径的唯一状态源，这条要能自证。
    stats.pause_after_stop_rejected =
        state.Apply(sr::RecorderCommand::Pause) == sr::TransitionStatus::Rejected;

    // 末尾 PTS 已在写入点记录（见 drain 与收尾冲刷），此处不再用帧数推算。

    stats.blocks_muxed = muxer.BlocksWritten();
    stats.bytes_written = muxer.BytesWritten();
    stats.filler_bytes = encoder.FillerBytes();  // CBR 补齐量（M3-04，供证据核对）
    stats.stream_changes = encoder.StreamChanges();
    stats.clusters = muxer.ClusterCount();
    stats.duration_seconds = static_cast<double>(clock.ElapsedMs(begin, clock.Now())) / 1000.0;
    if (fatal_code != 0) {
        Log("recording ended with fatal error; already-recorded content was finalized");
    }
    return fatal_code;
}

}  // namespace sr::recorder
