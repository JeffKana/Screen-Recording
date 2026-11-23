#pragma once

#include <cstdio>
#include <string>

namespace sr::recorder {

// 输出路径规划（M5-15 / M8-06 / M8-07）。
//
// 三个约束合并成一处处理，避免散落在录制流程里：
//   1. 同名绝不覆盖 —— 已存在则追加 _001/_002（M5-15）
//   2. 目录不存在时创建，不可写时开录前就报错（M8-06 前半）
//   3. 收尾改名若撞上占用，自动换名重试而非失败（M8-07）
struct OutputPlan {
    std::string final_path;   // 期望的成品路径（可能被改过名）
    std::string part_path;    // 录制中的临时文件
    bool renamed_for_conflict = false;
    // 轮 54（R1-52）：`.part` 在选名的同一刻由 CREATE_NEW 独占创建，句柄放在这里。
    // 选名 + 占用必须是同一步：先 exists() 再 fopen 是 TOCTOU，两个 recorder 会写同一个文件。
    // 所有权：调用方必须把它交给 muxer.Open（写入器接管并关闭），
    //         或调用 CloseReservedPart 关闭（不要自己 fclose）。
    std::FILE* part_file = nullptr;
};

// 关闭尚未移交的占位句柄（PrepareOutputPath 成功但没走到 muxer.Open 时的兜底）。
void CloseReservedPart(OutputPlan& plan);

// 准备输出路径：建目录、探测可写、解决同名冲突，并**原子占位** `.part`。
// 失败（目录建不出来 / 不可写 / 999 个名字都被占）时 error 给出可直接展示给用户的原因。
bool PrepareOutputPath(const std::string& requested, OutputPlan& plan, std::string& error);

// 收尾：把 .part 改名为成品。目标被占用时自动换名，actual_final 返回实际落地路径。
bool CommitOutput(const OutputPlan& plan,
                  std::string& actual_final,
                  std::string& error);

}  // namespace sr::recorder
