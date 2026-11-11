#pragma once

#include <cstdint>
#include <string>

namespace sr::recorder {

// 磁盘空间守护（M8-03 / M8-04 / M8-05）。
//
// 三级阈值：
//   warn     —— 非阻塞提示，录制继续
//   stop     —— 安全停止线，主动收尾落盘，避免写满后产生半截文件
//   critical —— warn 与 stop 之间的强提示区（由 UI 决定呈现方式，这里只给状态）
//
// 为什么要有独立的 stop 线而不是「写失败再处理」：写满时最后一次写入可能只落了一半，
// 封装层来不及改写 Duration/Cluster，产物就是不可播的。提前停才有机会正常收尾。
class StorageGuard {
public:
    StorageGuard(std::string path_on_target_volume, uint64_t warn_bytes, uint64_t stop_bytes);

    // 重新读取可用空间。失败时返回 false 并保留上次的值。
    bool Refresh(std::string& error);

    struct Status {
        uint64_t free_bytes = 0;
        bool warn = false;       // 低于 warn 线
        bool critical = false;   // 低于 warn 线的一半，属强提示区
        bool must_stop = false;  // 低于 stop 线
        double estimated_seconds = 0.0;  // 按当前码率估算还能录多久
    };

    // total_bitrate_kbps = 视频码率 + 音频码率，用于估算剩余时长。
    Status Evaluate(uint32_t total_bitrate_kbps) const;

    uint64_t FreeBytes() const { return free_bytes_; }
    bool Valid() const { return valid_; }

private:
    std::string path_;
    uint64_t free_bytes_ = 0;
    uint64_t warn_bytes_ = 0;
    uint64_t stop_bytes_ = 0;
    bool valid_ = false;
};

}  // namespace sr::recorder
