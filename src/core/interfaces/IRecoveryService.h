#pragma once

#include <string>

#include "core/types/StorageConfig.h"

namespace sr {

// 中断恢复抽象。实现：libavformat 探测 + remux（不重编码）。
class IRecoveryService {
public:
    virtual ~IRecoveryService() = default;

    // 扫描目录下的遗留分片，填充可修复清单（M8-02）。
    virtual RecoveryReport Scan(const std::string& directory) = 0;

    // 探测单个分片是否可播放。
    virtual RecoveryItem Probe(const std::string& segment_path) = 0;

    // 修复（remux）到 out_path。失败不得删除或覆盖原分片（M8-02 验收要求）。
    virtual bool Repair(const RecoveryItem& item, const std::string& out_path, std::string& error) = 0;
};

}  // namespace sr
