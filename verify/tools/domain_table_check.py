#!/usr/bin/env python3
"""文件域表一致性检查（轮 37 · docs/06 §2 与 CONTRACT.md §2 逐行对齐）。

两份表的列数不同（docs/06 是 5 列：ID/角色/需求/文件域/禁止；CONTRACT §2 是 3 列：角色/目录/说明），
所以按「第一个含反引号的单元格」取文件域目录。

用法:
    python verify/tools/domain_table_check.py          # 打印逐行比对，退出码 0=一致 / 1=不一致
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS06 = ROOT / "docs" / "06-子智能体执行规程.md"
CONTRACT = ROOT / "CONTRACT.md"
ROLES = ("S1", "S2", "S3", "S4", "S5", "S6", "V1")


def read_rows(path: Path) -> dict[str, list[str]]:
    out: dict[str, list[str]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("|") or "`" not in line:
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        role = cells[0].strip("* ")
        if role not in ROLES:
            continue
        for cell in cells[1:]:
            dirs = re.findall(r"`([^`]+)`", cell)
            if dirs:
                out[role] = dirs
                break
    return out


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    a = read_rows(DOCS06)
    b = read_rows(CONTRACT)
    mismatches = 0
    for role in ROLES:
        same = a.get(role) == b.get(role)
        mismatches += 0 if same else 1
        print(f"{role} docs06={a.get(role)} contract={b.get(role)} {'MATCH' if same else 'DIFF'}")
    print(f"MISMATCH_COUNT={mismatches}")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
