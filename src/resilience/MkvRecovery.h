#pragma once

#include <cstdint>
#include <string>

#include "core/interfaces/IRecoveryService.h"

namespace sr::resilience {

// MKV 崩溃恢复（M8-01 / M8-02）。
//
// 原理来自 ADR-02 的写入策略：Segment 用未知长度、Cluster 用已知长度且约 1.5 秒落盘一次。
// 因此崩溃后文件最多损坏「正在写的那个 Cluster」，把文件截断到最后一个完整 Cluster
// 的末尾即可得到一个结构合法、可解码的文件，无需重编码。
class MkvRecovery final : public IRecoveryService {
public:
    RecoveryReport Scan(const std::string& directory) override;

    RecoveryItem Probe(const std::string& segment_path) override;

    bool Repair(const RecoveryItem& item, const std::string& out_path, std::string& error) override;

    // M5-06：成品文件只读头部（≤1MB）取时长/分辨率，不把整文件读进内存。
    RecoveryItem InspectHeader(const std::string& file_path);
};

}  // namespace sr::resilience
