#include "recorder/OutputPath.h"

#include "core/util/FreePath.h"
#include "core/util/PathProbe.h"

#include <cstdio>
#include <filesystem>
#include <vector>

namespace sr::recorder {
namespace {

constexpr int kMaxIndex = 999;

std::filesystem::path WithIndex(const std::filesystem::path& base, int index) {
    const std::string stem = base.stem().string();
    const std::string ext = base.extension().string();
    char suffix[16] = {};
    std::snprintf(suffix, sizeof(suffix), "_%03d", index);
    return base.parent_path() / (stem + suffix + ext);
}

// 在 base 上找第一个不存在的路径：base 本身优先，其次 _001…_999。
// used_index = 0 表示 base 本身可用。
// 返回 false 表示全部被占用 —— 调用方必须拒绝，绝不能把一个已存在的路径当可用路径返回。
bool FindFreePath(const std::filesystem::path& base, std::filesystem::path& out, int& used_index) {
    // 收敛到 core/util/FreePath.h：全仓只保留一份避让实现（D-24/D-32/D-34 三次事故的教训）。
    const std::string resolved = sr::util::FindFreePath(base.string());
    used_index = 0;
    if (resolved.empty()) {
        out.clear();
        return false;
    }
    out = resolved;
    return true;
}

}  // namespace

bool PrepareOutputPath(const std::string& requested, OutputPlan& plan, std::string& error) {
    if (requested.empty()) {
        error = "output path is empty";
        return false;
    }

    std::filesystem::path target(requested);
    const std::filesystem::path directory =
        target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
    // 唯一实现：core/util/PathProbe（UI 的路径预检与 Recorder 的 M8-06 共用）。
    if (!sr::util::EnsureDirectoryWritable(directory.string(), error)) {
        return false;
    }

    // 同名不覆盖：取第一个空位（M5-15）。
    // 不能写成「存在就换下一个」的循环：那样 999 个都被占用时会把已存在的 _999
    // 当作可用路径返回，方向上是覆盖而不是拒绝（D-24 的 off-by-one 半边）。
    std::filesystem::path candidate;
    int conflict_index = 0;
    if (!FindFreePath(target, candidate, conflict_index)) {
        error = "too many files with the same name (limit " + std::to_string(kMaxIndex) +
                "): " + target.string();
        return false;
    }
    const bool renamed = conflict_index != 0;

    plan.final_path = candidate.string();
    plan.part_path = plan.final_path + ".part";
    plan.renamed_for_conflict = renamed;

    // .part 也可能残留（上次崩溃），同样避让，避免覆盖尚未恢复的数据。
    int part_index = 1;
    std::error_code ec;
    while (std::filesystem::exists(plan.part_path, ec)) {
        plan.part_path = plan.final_path + "." + std::to_string(part_index++) + ".part";
        ec.clear();
    }

    error.clear();
    return true;
}

bool CommitOutput(const OutputPlan& plan, std::string& actual_final, std::string& error) {
    // 收尾前必须重新查一次空位。Windows 上 rename 会**覆盖**已存在的目标
    // （MSVC 的 std::filesystem::rename 带 MOVEFILE_REPLACE_EXISTING，见
    // crt/src/stl/filesystem.cpp），所以「准备之后目标被别人创建」这件事
    // 不会以 rename 失败的形式出现，只能自己先查（D-24）。
    const std::filesystem::path requested(plan.final_path);
    std::filesystem::path target;
    int conflict_index = 0;
    if (!FindFreePath(requested, target, conflict_index)) {
        error = "no available output filename (limit " + std::to_string(kMaxIndex) +
                "): " + requested.string();
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(plan.part_path, target, ec);
    if (!ec) {
        actual_final = target.string();
        error.clear();
        return true;
    }

    // 改名仍失败：目标被独占打开或权限不足（M8-07）。继续换名重试，
    // 不把已经录好的内容判为失败。
    for (int index = 1; index <= kMaxIndex; ++index) {
        const std::filesystem::path alternative = WithIndex(requested, index);
        if (alternative == target || std::filesystem::exists(alternative, ec)) {
            ec.clear();
            continue;
        }
        std::error_code rename_ec;
        std::filesystem::rename(plan.part_path, alternative, rename_ec);
        if (!rename_ec) {
            actual_final = alternative.string();
            error.clear();
            return true;
        }
        ec.clear();
    }

    error = "cannot finalize output file: " + plan.part_path;
    return false;
}

}  // namespace sr::recorder
