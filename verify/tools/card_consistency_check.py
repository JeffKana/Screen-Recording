#!/usr/bin/env python3
"""任务卡一致性检查（轮 38 · R1）。

检查目标：卡片内部「状态行」与「正文」自相矛盾；以及「待裁定项先翻绿」。

- 状态行：`## 交付状态` 段里形如 `- M6-02: 已实现（…）` 的声明。
- 正文：卡里其它位置提到同一需求编号、且出现「有卡未实现 / 未实现 / 不能标已实现 /
  状态保持」等**否定实现**措辞的句子。
- 历史标注：句子里（或它所在小节的标题里）出现「轮 N / 变更：/ 当时 / 此前 / 历史 /
  回填 / 回扫 / 本卡自身 / 已由」等**时间标记**。

判定 A：**状态行说已实现 + 正文说未实现 + 没有历史标注** = 冲突（退出码 1）。
带历史标注的过期说明不算冲突（那是「事实保留」，不是「状态声明」）。

判定 B（轮 42 · F2）：正文引用**待裁定的** `docs/1x-*裁定申请` 文书，
而该卡的 `## 交付状态` 已声明「已实现」 → 冲突。
依据：验收报告-轮41 §5 F2「待裁定项先翻绿」——裁定前一律挂「有卡未实现」，
裁定文书落地后再翻，否则「裁定」变成「追认」。
「已裁定 / 裁定记录 / 裁定结果」等已归档措辞不算待裁定（历史事实保留）。

用法:
    python verify/tools/card_consistency_check.py                    # 打印冲突明细，退出码 0/1
    python verify/tools/card_consistency_check.py --quiet            # 只在有冲突时输出
    python verify/tools/card_consistency_check.py --tasks-dir <dir>  # 指定卡目录（假卡自检用）
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TASKS = ROOT / "tasks"

# 正文里的「否定实现」措辞
STALE = re.compile(r"有卡未实现|未实现|不能标|状态保持|不能标已实现|尚未交付")
# 历史/时间标注：出现其一即认为该句是「历史说明」而不是「当前状态声明」
HISTORY = re.compile(
    r"轮\s*\d+|变更[:：]|当时|此前|历史|回填|回扫|本卡自身|已由|过去|原来|旧卡"
)

STATUS_LINE = re.compile(r"^\s*[-*]\s*([A-Z]\d+-\d+(?:#\d+)?)\s*[:：]\s*(\S+)")
SECTION = re.compile(r"^#{1,6}\s+")

# 轮 42 · F2：待裁定的裁定申请文书引用（只认 docs/1x-…裁定申请 形式的具体路径）。
PENDING_ADJUDICATION = re.compile(r"docs/1\d-[^\s`)\]|]*裁定申请")
# 已裁定 / 已归档的措辞：出现即视为历史事实，不报警。
ADJUDICATED = re.compile(r"已裁定|裁定记录|裁定结果|裁定已落地|归档|已回文|维持已实现")


def parse_status_section(lines: list[str]) -> tuple[dict[str, str], set[int]]:
    """返回 (需求编号 → 状态词, 状态声明行行号集合)。

    只有「状态声明行」本身不算正文；交付状态段里的其余内容（说明、留痕行）仍算正文——
    否则放在卡片末尾的交付状态段会把整张卡的正文都跳过。
    """
    start = None
    for index, line in enumerate(lines):
        if line.strip().startswith("## ") and "交付状态" in line:
            start = index
            break
    if start is None:
        return {}, set()
    end = len(lines)
    for index in range(start + 1, len(lines)):
        if SECTION.match(lines[index]):
            end = index
            break
    declared: dict[str, str] = {}
    declaration_lines: set[int] = set()
    for index in range(start + 1, end):
        match = STATUS_LINE.match(lines[index])
        if match:
            declared[match.group(1)] = match.group(2)
            declaration_lines.add(index)
    return declared, declaration_lines


def check_card(path: Path) -> list[dict]:
    lines = path.read_text(encoding="utf-8").splitlines()
    declared, declaration_lines = parse_status_section(lines)
    conflicts: list[dict] = []
    implemented = {req for req, state in declared.items() if state.startswith("已实现")}
    if not implemented:
        return conflicts
    try:
        rel = str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        rel = path.as_posix()  # 临时目录里造的假卡

    # 轮 42 · F2：正文引用待裁定申请 + 卡已声明已实现 -> 报警。
    # 状态声明行本身不算正文；已裁定/已归档措辞视为历史事实。
    for index, line in enumerate(lines):
        if index in declaration_lines:
            continue
        if not PENDING_ADJUDICATION.search(line):
            continue
        if ADJUDICATED.search(line):
            continue
        conflicts.append(
            {
                "card": rel,
                "line_no": index + 1,
                "requirement": ",".join(sorted(implemented)),
                "text": "[F2_PENDING_REF][F2 待裁定项先翻绿] " + line.strip()[:140],
            }
        )

    section = ""
    for index, line in enumerate(lines):
        if SECTION.match(line):
            section = line
        if index in declaration_lines:
            continue  # 状态声明行本身不算正文
        if not STALE.search(line):
            continue
        for req in implemented:
            if req not in line:
                continue
            context = f"{section}\n{line}"
            if HISTORY.search(context):
                continue  # 已标注为历史说明
            conflicts.append(
                {
                    "card": rel,
                    "line_no": index + 1,
                    "requirement": req,
                    "text": line.strip()[:160],
                }
            )
            break
    return conflicts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--tasks-dir", default=None,
                        help="指定卡目录（默认 tasks/；假卡自检传临时目录）")
    args = parser.parse_args()

    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    tasks_dir = Path(args.tasks_dir) if args.tasks_dir else TASKS
    cards = sorted(tasks_dir.rglob("TASK.md"))
    conflicts: list[dict] = []
    for card in cards:
        conflicts.extend(check_card(card))

    if conflicts:
        print(f"card_consistency: FAIL —— {len(conflicts)} 处冲突")
        for item in conflicts:
            print(
                f"  {item['card']}:{item['line_no']} [{item['requirement']}] {item['text']}"
            )
        print("处理办法：① 该句补历史标注（轮 N / 当时 / 已裁定…），或 ② 改写/删除该过期说明；")
        print("          F2 冲突：待裁定项先挂「有卡未实现」，裁定文书落地并归档后再翻「已实现」。")
        return 1
    if not args.quiet:
        print(f"card_consistency: OK —— 扫描 {len(cards)} 张卡，0 处冲突")
    return 0


if __name__ == "__main__":
    sys.exit(main())
