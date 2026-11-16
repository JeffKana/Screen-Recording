#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mux/MuxFormat.h"

namespace sr::mux {

// 极简 ISO BMFF 写入器（与 MkvWriter 并列，接口形状保持一致）。
//
// 布局（轮 49 起 faststart）：ftyp + moov + mdat。
//   * 录制期间先写 ftyp + mdat(64 位 largesize)，边录边写，**不产生 moov**；
//     Close 时把 moov 生成到内存、整体落在 mdat 之前，再把 mdat 数据区后移 moov 的体积。
//   * 崩溃语义（与 MKV 不同，见 HANDOFF §2）：录制期的 .part 只有 ftyp + mdat、没有 moov，
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
    bool MoveMdatForward(uint64_t shift, std::string& error);
    // offset_shift：stco/co64 里每个样本偏移的额外平移量（= moov 的体积）。
    std::vector<uint8_t> BuildMoov(uint64_t offset_shift) const;
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
    uint64_t fail_after_ = 0;
    uint64_t writes_ = 0;
    uint64_t bytes_written_ = 0;
};

}  // namespace sr::mux
