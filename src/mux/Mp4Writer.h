#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mux/MuxFormat.h"

namespace sr::mux {

// 极简 ISO BMFF 写入器（与 MkvWriter 并列，接口形状保持一致）。
//
// 布局（轮 52 起 faststart「预留 + 回填」）：ftyp + free(预留区) + moov + free(余量) + mdat。
//   * 开录时按「预计时长 × 每样本 stsz+stco 开销」预留一块 free box（轮 52 R2）；
//     录制期间边录边写 mdat(64 位 largesize)，**不产生 moov**；
//   * Close 时把 moov 生成到内存、直接写进预留区，**mdat 一个字节都不搬**——
//     收尾耗时从 O(文件大小) 降到 O(moov)（轮 51 实测搬移 1.15GB 需 0.60s，HDD 8h 外推 ≈280s）；
//   * 预留区装不下 moov（超长录制 / 人为调小预留）时回退到旧路径：整体后移 mdat，
//     并置 FaststartFallback()，由上层打 `faststart_fallback=move`。
//   * 崩溃语义（与 MKV 不同，见 HANDOFF §2）：录制期的 .part 只有 ftyp + free + mdat、没有 moov，
//     恢复流程判「不可修复」并**保留**原文件；MP4 的崩溃恢复适配是 V1.1 的前置，不在 V1.0 冒充。
//
// 轨道：1 路 H.264（avcC / AVCC 长度前缀样本）+ 1 路 AAC（AudioSpecificConfig）。
// video timescale = 90000；audio timescale = sample_rate；movie timescale = 1000。
// stts 的 delta 一律由相邻真实 DTS 差换算，不用 1/fps 累加；stss 只标真实 IDR
// （扫样本里的 NAL type 5），不信任调用侧按 GOP 假设传进来的 keyframe。
// 每个样本自成一个 chunk（A/V 在 mdat 里交错），故 stsc 固定一条 (1,1,1)，
// stco/co64 逐样本给绝对文件偏移。
class Mp4Writer {
public:
    ~Mp4Writer();

    using VideoTrackInfo = sr::mux::VideoTrackInfo;
    using AudioTrackInfo = sr::mux::AudioTrackInfo;

    bool Open(const std::string& path, std::string& error);
    bool SetVideoTrack(const VideoTrackInfo& info, std::string& error);
    bool SetAudioTrack(const AudioTrackInfo& info, std::string& error);

    // pts 为 100ns 单位（与 MF 一致）。keyframe 仅为接口对齐保留，stss 用真实 IDR 判定。
    bool WriteVideo(const uint8_t* data, size_t size, int64_t pts_100ns, bool keyframe);
    bool WriteAudio(const uint8_t* data, size_t size, int64_t pts_100ns);

    bool Close(std::string& error);
    // MKV 用它落盘最后一个 Cluster；MP4 没有 Cluster，只做一次 fflush（不改恢复语义）。
    bool FlushPending();

    // 注入：第 N 次物理写之后让写入返回失败（与 MkvWriter 同口径）。
    void SetFailWriteAfter(uint64_t writes);

    // 轮 52（R2）：faststart 预留区的估算输入。seconds/fps 是**开录前已知的预计时长与帧率**
    // （--seconds / --fps），只用来估 moov 上界，不参与成片内容；务必在 Open 之前调用。
    void SetFaststartReserveHint(uint64_t seconds, uint32_t fps);
    // 调试/验收：直接指定预留字节数（0 = 按 hint 估算）。用于走回退路径（判据 #4）。
    void SetFaststartReserveOverride(uint64_t bytes);

    // 收尾观测量（供日志与用例核对，不写进文件）。
    uint64_t FaststartReserve() const { return faststart_reserve_; }
    uint64_t FaststartMoovBytes() const { return faststart_moov_bytes_; }
    // true = 预留区装不下 moov，走了「整体后移 mdat」的回退路径。
    bool FaststartFallback() const { return faststart_fallback_; }
    // 收尾 faststart 的实测耗时（微秒）：moov 生成 + 预留回填/搬移 + 落盘。
    uint64_t FaststartCloseMicros() const { return faststart_close_us_; }

    uint64_t BlocksWritten() const { return video_.sizes.size() + audio_.sizes.size(); }
    uint64_t VideoBlocks() const { return video_.sizes.size(); }
    uint64_t AudioBlocks() const { return audio_.sizes.size(); }
    uint64_t BytesWritten() const { return bytes_written_; }
    // MP4 没有 Cluster 概念；统计口径与 MKV 并列，0 表示「本容器不产生 Cluster」。
    uint32_t ClusterCount() const { return 0; }

    // 供结构自检/用例核验的只读观测量（不写进产品日志）。
    uint32_t VideoTimescale() const { return video_.timescale; }
    uint32_t AudioTimescale() const { return audio_.timescale; }

private:
    struct Track {
        std::vector<uint32_t> sizes;      // stsz
        std::vector<uint64_t> offsets;    // stco / co64
        std::vector<int64_t> pts_100ns;   // 编码器给的真实 PTS（B 帧时非单调）
        std::vector<uint32_t> deltas;     // stts：Close 时由真实 DTS 差生成
        std::vector<int32_t> ctts;        // ctts：PTS - DTS（轨道 timescale，带符号）
        std::vector<uint32_t> sync;       // stss：1-based sample number
        uint32_t timescale = 90000;
    };

    bool WriteRaw(const void* data, size_t size);
    bool AppendSample(Track& track, const uint8_t* data, size_t size,
                      int64_t pts_100ns, bool sync);
    static void FinalizeTrack(Track& track, uint32_t nominal_delta);
    static uint32_t TrackDuration(const Track& track);
    bool PatchMdatSize(std::string& error);
    // faststart：把 [mdat_offset_, EOF) 整体后移 shift 字节（从尾部往前分块搬，单文件原地完成）。
    // 轮 52 起只在「预留区装不下 moov」的回退路径上调用，shift == 0 直接返回 true。
    bool MoveMdatForward(uint64_t shift, std::string& error);
    // offset_shift：stco/co64 里每个样本偏移的额外平移量（预留命中时恒为 0）。
    // align_to_8：把 moov 总体积补到 8 的倍数（用 moov 内部的 free box 填），
    //             这样「预留区 - moov」才是 0 或 ≥8，剩余空间总能落一个合法 free box。
    std::vector<uint8_t> BuildMoov(uint64_t offset_shift, bool align_to_8) const;
    std::vector<uint8_t> BuildVideoTrak(uint32_t movie_duration_ms, uint64_t offset_shift) const;
    std::vector<uint8_t> BuildAudioTrak(uint32_t movie_duration_ms, uint64_t offset_shift) const;

    std::FILE* file_ = nullptr;
    std::string path_;
    VideoTrackInfo video_info_{};
    AudioTrackInfo audio_info_{};
    Track video_{};
    Track audio_{};
    bool has_video_ = false;
    bool has_audio_ = false;
    uint64_t mdat_offset_ = 0;
    uint64_t moov_offset_ = 0;              // 预留区起点（= ftyp 末尾），moov 回填到这里
    uint64_t reserve_hint_seconds_ = 0;
    uint32_t reserve_hint_fps_ = 30;
    uint64_t reserve_override_ = 0;
    uint64_t faststart_reserve_ = 0;        // 实际预留字节数（8 的倍数；0 = 未预留）
    uint64_t faststart_moov_bytes_ = 0;     // 收尾生成的 moov 体积
    uint64_t faststart_close_us_ = 0;       // 收尾 faststart 实测微秒
    bool faststart_fallback_ = false;       // 预留不足 → 走了搬移路径
    uint64_t fail_after_ = 0;
    uint64_t writes_ = 0;
    uint64_t bytes_written_ = 0;
};

}  // namespace sr::mux
