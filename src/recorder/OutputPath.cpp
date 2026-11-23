#include "recorder/OutputPath.h"

#include "core/util/FreePath.h"
#include "core/util/PathProbe.h"

#include <fcntl.h>
#include <io.h>
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace sr::recorder {
namespace {

constexpr int kMaxIndex = 999;
// `.part` 变体上限：同名 `.part`（崩溃残留 / 另一个 recorder 正在写）最多避让 10 次，
// 再往上就让成品名进一位 —— 保证循环一定有界，不会在极端情况下转圈。
constexpr int kMaxPartVariants = 10;

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

// `.part` 的第 n 个候选名：n=0 → `x.mkv.part`，n≥1 → `x.mkv.<n>.part`（沿用 D-32 的命名）。
std::filesystem::path PartCandidate(const std::filesystem::path& final_path, int variant) {
    if (variant == 0) {
        return std::filesystem::path(final_path.string() + ".part");
    }
    return std::filesystem::path(final_path.string() + "." + std::to_string(variant) + ".part");
}

// CREATE_NEW 独占创建：文件已存在则失败，成功即「选名 + 占用」一步完成（R1-52）。
// 共享模式只给 FILE_SHARE_READ：录制中其它进程可以读（--list 的实时快照、ffprobe），
// 但不能再打开一份写句柄覆盖正在写的内容。
std::FILE* CreateReservedFile(const std::filesystem::path& path, DWORD& win_error) {
    HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        win_error = ::GetLastError();
        return nullptr;
    }
    const int fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_BINARY);
    if (fd < 0) {
        win_error = ERROR_OPEN_FAILED;
        ::CloseHandle(handle);
        return nullptr;
    }
    // 句柄已经打开且为空文件，这里只取流：不截断、不重开（重开就会把原子性丢了）。
    std::FILE* file = ::_fdopen(fd, "r+b");
    if (file == nullptr) {
        win_error = ERROR_OPEN_FAILED;
        ::_close(fd);
        return nullptr;
    }
    win_error = ERROR_SUCCESS;
    return file;
}

}  // namespace

void CloseReservedPart(OutputPlan& plan) {
    if (plan.part_file != nullptr) {
        std::fclose(plan.part_file);
        plan.part_file = nullptr;
    }
}

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

    // 同名不覆盖 + 原子占位（M5-15 / R1-52）：
    // 逐个候选名去 CREATE_NEW 它的 `.part`，成功即完成「选名 + 占用」一步。
    // 不能先 exists() 再 fopen：两个 recorder 会在同一毫秒选中同一个 `.part`，
    // 互相截断/覆盖，产物 EBML 损坏而两边都 exit 0 —— 这正是 R1-52。
    std::error_code ec;
    for (int index = 0; index <= kMaxIndex; ++index) {
        const std::filesystem::path candidate = index == 0 ? target : WithIndex(target, index);
        // 成品已存在 → 换下一个名字（成品绝不覆盖，D-24）。
        if (std::filesystem::exists(candidate, ec)) {
            ec.clear();
            continue;
        }
        for (int variant = 0; variant < kMaxPartVariants; ++variant) {
            const std::filesystem::path part = PartCandidate(candidate, variant);
            DWORD win_error = ERROR_SUCCESS;
            std::FILE* file = CreateReservedFile(part, win_error);
            if (file != nullptr) {
                plan.final_path = candidate.string();
                plan.part_path = part.string();
                plan.renamed_for_conflict = index != 0;
                plan.part_file = file;
                error.clear();
                return true;
            }
            if (win_error != ERROR_FILE_EXISTS && win_error != ERROR_ALREADY_EXISTS) {
                // 权限 / 磁盘满 / 路径非法：不是并发，再试也没用，直接拒绝（M8-06）。
                error = "cannot create temp file (win error " + std::to_string(win_error) +
                        "): " + part.string();
                return false;
            }
            // 同名 `.part` 已存在：崩溃残留（D-32）或另一个 recorder 正在写（R1-52）→ 顺延。
        }
    }

    error = "too many files with the same name (limit " + std::to_string(kMaxIndex) +
            "): " + target.string();
    return false;
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
