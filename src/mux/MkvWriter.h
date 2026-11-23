#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mux/MuxFormat.h"

namespace sr::mux {

// 极简 Matroska 写入器（02 文档 ADR-02：录制中写分片 MKV）。
//
// 为什么自己写而不用 libavformat：本环境没有 FFmpeg 的开发包（只有 CLI 分发的可能），
// 而 MKV 的 EBML 结构足够简单，自己写反而能把「崩溃后仍可播放」这件事做到位：
//   - Segment 用未知长度，边录边写，不需要回头改大小；
//   - Cluster 用已知长度、每个约 1~2 秒，崩溃时已完成的 Cluster 全部有效；
//   - Duration 在 Info 里预留 8 字节，Close 时回填。
// 代价：不支持 B 帧重排（SimpleBlock 只写 PTS），也不写 SeekHead/Cues。
class MkvWriter {
public:
    ~MkvWriter();

    // 轮 48：结构提到 `mux/MuxFormat.h` 供 MP4 复用，保留类内别名，
    // 既有 `MkvWriter::VideoTrackInfo` 调用方无需改动、行为不变。
    using VideoTrackInfo = sr::mux::VideoTrackInfo;
    using AudioTrackInfo = sr::mux::AudioTrackInfo;

    bool Open(const std::string& path, std::string& error);
    // 轮 54（R1-52）：接管一个已经由调用方 **独占创建**（CREATE_NEW）的句柄。
    // 为什么需要它：先查存在再 fopen 是 TOCTOU —— 两个 recorder 会在同一毫秒选中同一个
    // `.part`，互相覆盖写坏。改成由 OutputPath 原子创建后把句柄交进来，选名与占用不可分割。
    // 所有权：无论返回 true/false，句柄都归本类（析构 / Close 关闭）。
    bool AdoptOpen(std::FILE* file, const std::string& path, std::string& error);
    bool SetVideoTrack(const VideoTrackInfo& info, std::string& error);
    // 轮 49（M3-09#2）：每调用一次追加一条音轨（第 1 次 = TrackNumber 2）。
    // 单音轨只调用一次 —— 输出的 EBML 与轮 48 逐字节一致（回归口径）。
    bool SetAudioTrack(const AudioTrackInfo& info, std::string& error);

    // pts 为 100ns 单位（与 MF 一致），内部换算成 ms。
    bool WriteVideo(const uint8_t* data, size_t size, int64_t pts_100ns, bool keyframe);
    // track_index 从 0 起（0 = TrackNumber 2）。越界返回 false，不静默丢样本。
    // 单音轨只需传 0：输出的 EBML 与轮 48 的单音轨实现逐字节一致。
    bool WriteAudio(size_t track_index, const uint8_t* data, size_t size, int64_t pts_100ns);

    bool Close(std::string& error);

    // 立即把当前缓冲的 Cluster 落盘。进入暂停时必须调用：
    // 否则暂停期间进程被杀，上一个 Cluster 之后的内容会全部丢失。
    bool FlushPending();

    // 注入：第 N 次物理写之后让写入返回失败，用于验证写失败传播（不得绕过真实代码路径）。
    void SetFailWriteAfter(uint64_t writes);

    uint64_t BlocksWritten() const { return video_blocks_ + audio_blocks_; }
    uint64_t VideoBlocks() const { return video_blocks_; }
    uint64_t AudioBlocks() const { return audio_blocks_; }
    uint64_t BytesWritten() const { return bytes_written_; }
    uint32_t ClusterCount() const { return cluster_count_; }

private:
    bool WriteRaw(const void* data, size_t size);
    bool WriteTracks();
    bool StartCluster(int64_t timestamp_ms);
    bool FlushCluster();
    bool AppendBlock(uint64_t track_number, const uint8_t* data, size_t size, int64_t pts_ms, bool keyframe);

    std::FILE* file_ = nullptr;
    std::string path_;
    VideoTrackInfo video_info_{};
    std::vector<AudioTrackInfo> audio_info_{};
    std::vector<uint8_t> cluster_;
    int64_t cluster_base_ms_ = 0;
    bool cluster_open_ = false;
    uint64_t fail_after_ = 0;  // 注入：第 N 次写之后失败
    uint64_t writes_ = 0;
    uint32_t cluster_count_ = 0;
    uint64_t video_blocks_ = 0;
    uint64_t audio_blocks_ = 0;
    uint64_t bytes_written_ = 0;
    uint64_t duration_offset_ = 0;
    bool has_video_ = false;
    bool tracks_written_ = false;
    int64_t last_video_ms_ = 0;
};

}  // namespace sr::mux
