#!/usr/bin/env python3
"""契约级自动核验工具（对应 docs/06-子智能体执行规程 §6.2）。

只做**静态结构核验**：需求编号引用、文件域归属、core 层平台依赖、接口齐备性、
状态机转移表与契约一致性。它不能替代编译与运行验收（那是 E4 的职责）。

用法:
    python verify/tools/contract_check.py [--verbose]

退出码: 0 = 全部 PASS; 1 = 存在 FAIL
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

import coverage_model as cm

ROOT = Path(__file__).resolve().parents[2]

CONTRACT = ROOT / "CONTRACT.md"
REQ_DOC = ROOT / "docs" / "01-功能需求清单.md"
TASKS_DIR = ROOT / "tasks"
SRC_DIR = ROOT / "src"
LEDGER = ROOT / "docs" / "07-执行台账.md"

ROLE_ID = re.compile(r"^(S\d|V1|O0)$")
REQ_ID = re.compile(r"\bM\d-\d{2}\b")
TABLE_ROW = re.compile(r"^\|(.+)\|\s*$")
INCLUDE = re.compile(r"#include\s*[<\"]([^>\"]+)[>\"]")
TRANSITION_ENTRY = re.compile(
    r"\{\s*RecorderState::\w+\s*,\s*RecorderCommand::\w+\s*,\s*RecorderState::\w+\s*\}"
)


class Result:
    def __init__(self) -> None:
        self.items: list[tuple[str, str, bool, str]] = []

    def add(self, cid: str, name: str, ok: bool, detail: str) -> None:
        self.items.append((cid, name, ok, detail))

    def report(self, verbose: bool) -> int:
        failed = 0
        for cid, name, ok, detail in self.items:
            tag = "PASS" if ok else "FAIL"
            if not ok:
                failed += 1
            line = f"[{tag}] {cid} {name}"
            if detail and (verbose or not ok):
                line += f" — {detail}"
            print(line)
        print()
        print(f"汇总: {len(self.items)} 项检查, {len(self.items) - failed} PASS, {failed} FAIL")
        return 1 if failed else 0


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def section(text: str, start_marker: str, end_marker: str) -> str:
    start = text.find(start_marker)
    if start < 0:
        return ""
    end = text.find(end_marker, start + len(start_marker))
    return text[start:end if end >= 0 else len(text)]


def table_rows(block: str) -> list[list[str]]:
    rows = []
    for line in block.splitlines():
        match = TABLE_ROW.match(line.strip())
        if not match:
            continue
        cells = [c.strip() for c in match.group(1).split("|")]
        if all(set(c) <= {"-", ":"} and c for c in cells):
            continue  # 分隔行
        rows.append(cells)
    return rows


def backticked(text: str) -> list[str]:
    return re.findall(r"`([^`]+)`", text)


def load_requirements() -> dict[str, str]:
    """返回 {需求编号: 版本标签}。"""
    reqs: dict[str, str] = {}
    for cells in table_rows(read_text(REQ_DOC)):
        if not cells:
            continue
        rid = cells[0].strip()
        if not re.fullmatch(r"M\d-\d{2}", rid):
            continue
        version = ""
        for cell in reversed(cells):
            if re.fullmatch(r"V\d\.\d", cell):
                version = cell
                break
        reqs[rid] = version
    return reqs


def check_requirement_refs(result: Result, reqs: dict[str, str]) -> set[str]:
    """C1: 任务卡（仅 TASK.md）引用的需求编号必须存在于 docs/01。

    注意：EVIDENCE/HANDOFF 不是任务卡，不能算「已切片」。轮 26 的 35/31 与评审的
    34/32 之差，正是三处只出现在证据/交接文档里的引用（M1-24/M6-03/M8-01）。
    """
    referenced: set[str] = set()
    unknown: list[str] = []
    cards = sorted(TASKS_DIR.rglob("TASK.md")) if TASKS_DIR.exists() else []
    for card in cards:
        for rid in REQ_ID.findall(read_text(card)):
            referenced.add(rid)
            if rid not in reqs:
                unknown.append(f"{card.relative_to(ROOT).as_posix()}:{rid}")
    detail = f"{len(cards)} 张 TASK.md, 引用 {len(referenced)} 个需求编号"
    if unknown:
        detail += f", 无效引用: {', '.join(sorted(set(unknown)))}"
    result.add("C1", "任务卡需求编号可检索", not unknown and len(cards) > 0, detail)
    return referenced


def check_file_ownership(result: Result) -> None:
    """C2: src/ 下每个文件都必须归属某个角色的文件域。"""
    block = section(read_text(CONTRACT), "## 2.", "## 3.")
    domains: dict[str, str] = {}
    for cells in table_rows(block):
        if len(cells) < 2:
            continue
        role = cells[0].strip().strip("*")
        if not ROLE_ID.match(role):
            continue
        for path in backticked(cells[1]):
            if path.startswith("src/"):
                domains[path.rstrip("/") + "/"] = role

    orphans: list[str] = []
    if SRC_DIR.exists():
        for file in sorted(SRC_DIR.rglob("*")):
            if not file.is_file():
                continue
            rel = file.relative_to(ROOT).as_posix()
            if file.name == "CMakeLists.txt":
                continue  # 构建脚本随所属目录
            if not any(rel.startswith(prefix) for prefix in domains):
                orphans.append(rel)

    detail = f"{len(domains)} 个文件域, 孤儿文件 {len(orphans)} 个"
    if orphans:
        detail += ": " + ", ".join(orphans[:5])
    result.add("C2", "src 文件归属无越界", not orphans, detail)


def check_core_platform_free(result: Result) -> None:
    """C3: core 层不得依赖平台头（M6-13）。"""
    contract_text = read_text(CONTRACT)
    banned: list[str] = []
    lines = contract_text.splitlines()
    for index, line in enumerate(lines):
        if "禁止 include 关键字" in line:
            for follow in lines[index + 1 :]:
                if not follow.strip():
                    break
                banned.extend(backticked(follow))
            break
    banned = [b.lower() for b in banned]

    violations: list[str] = []
    scanned = 0
    core_dir = SRC_DIR / "core"
    if core_dir.exists():
        for file in sorted(core_dir.rglob("*")):
            if not file.is_file() or file.suffix not in {".h", ".hpp", ".cpp"}:
                continue
            scanned += 1
            for include in INCLUDE.findall(read_text(file)):
                lowered = include.lower()
                if any(token in lowered for token in banned):
                    violations.append(f"{file.relative_to(ROOT).as_posix()} -> {include}")

    detail = f"扫描 {scanned} 个 core 文件, 违规 {len(violations)} 处"
    if violations:
        detail += ": " + "; ".join(violations[:5])
    result.add("C3", "core 层无平台依赖 (M6-13)", not violations, detail)


def check_interfaces(result: Result) -> set[str]:
    """C4: CONTRACT §4 声明的接口文件必须存在且无未声明接口。"""
    block = section(read_text(CONTRACT), "## 4.", "## 5.")
    declared: list[str] = []
    for cells in table_rows(block):
        if not cells:
            continue
        names = backticked(cells[0])
        if names and names[0].startswith("I"):
            declared.append(names[0])

    interfaces_dir = SRC_DIR / "core" / "interfaces"
    actual = sorted(p.stem for p in interfaces_dir.glob("*.h")) if interfaces_dir.exists() else []

    missing = [name for name in declared if name not in actual]
    extra = [name for name in actual if name not in declared]

    # 每个接口头必须声明同名类且有 include guard。
    shape_problems: list[str] = []
    for name in actual:
        text = read_text(interfaces_dir / f"{name}.h")
        if "#pragma once" not in text:
            shape_problems.append(f"{name}.h 缺少 #pragma once")
        if not re.search(rf"class\s+{re.escape(name)}\b", text):
            shape_problems.append(f"{name}.h 未声明 class {name}")

    detail = f"声明 {len(declared)} 个, 实际 {len(actual)} 个"
    if missing:
        detail += f", 缺失: {', '.join(missing)}"
    if extra:
        detail += f", 未声明: {', '.join(extra)}"
    if shape_problems:
        detail += f", 形状问题: {'; '.join(shape_problems)}"
    result.add("C4", "core 接口齐备 (CONTRACT §4)", not (missing or extra or shape_problems), detail)
    return set(declared)


def check_state_machine(result: Result) -> None:
    """C5: 状态机转移表条目数与 CONTRACT §5 一致。"""
    block = section(read_text(CONTRACT), "## 5.", "## 6.")
    contract_rows = [cells for cells in table_rows(block) if cells and re.fullmatch(r"T\d{2}", cells[0].strip())]

    cpp = SRC_DIR / "core" / "state" / "RecorderState.cpp"
    code_count = len(TRANSITION_ENTRY.findall(read_text(cpp))) if cpp.exists() else 0

    ok = cpp.exists() and code_count == len(contract_rows) and code_count > 0
    detail = f"契约 {len(contract_rows)} 条, 代码 {code_count} 条"
    if not ok:
        detail += " — 转移表与契约不一致"
    result.add("C5", "状态机转移表与契约一致", ok, detail)


def check_ledger(result: Result) -> None:
    """C6: 执行台账存在且为 7 列结构。"""
    if not LEDGER.exists():
        result.add("C6", "执行台账存在且结构完整", False, f"缺少 {LEDGER.relative_to(ROOT).as_posix()}")
        return
    header = None
    for cells in table_rows(read_text(LEDGER)):
        if cells and cells[0] == "轮次":
            header = cells
            break
    ok = header is not None and len(header) == 7
    detail = f"表头 {len(header)} 列" if header else "未找到以「轮次」起始的表头"
    result.add("C6", "执行台账存在且结构完整", ok, detail)


def check_coverage(result: Result) -> None:
    """C7: 覆盖非空（子句级）；C8: 未切片子句逐条挂账。"""
    reqs = cm.load_requirements()
    cards, clause_cards, delivery = cm.load_cards(reqs)
    clause_reasons, req_reasons = cm.load_existing_reasons()
    rows = cm.build_rows(reqs, cards, clause_cards, clause_reasons, req_reasons, delivery)
    stats = cm.summarize(rows)

    ok = stats["requirements"] > 0 and stats["implemented"] > 0
    result.add(
        "C7",
        "V1.0 覆盖非空（完整性见 C8）",
        ok,
        f"V1.0 {stats['requirements']} 条 / {stats['clauses']} 子句, "
        f"已实现 {stats['implemented']}, 有卡未实现 {stats['unimplemented']}, "
        f"无卡 {stats['uncovered']}",
    )

    # F1：三态收口——「有卡未实现」与「无卡」一样必须挂账，
    # 否则骨架卡可以继续把空洞藏在「有卡」里。
    missing = [
        row["clause_id"] for row in rows
        if row["state"] != "已实现" and not cm.is_waived(row["reason"])
    ]
    detail = (
        f"V1.0 {stats['clauses']} 子句, 已实现 {stats['implemented']}, "
        f"有卡未实现 {stats['unimplemented']}, 无卡 {stats['uncovered']}, "
        f"已挂账 {stats['waived']}, 缺理由 {len(missing)}"
    )
    if missing:
        detail += ": " + ", ".join(missing[:8])
    result.add("C8", "未实现子句逐条挂账（含「有卡未实现」）", not missing, detail)


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    verbose = "--verbose" in sys.argv

    for required in (CONTRACT, REQ_DOC):
        if not required.exists():
            print(f"缺少必需文件: {required}")
            return 1

    reqs = load_requirements()
    result = Result()
    referenced = check_requirement_refs(result, reqs)
    check_file_ownership(result)
    check_core_platform_free(result)
    check_interfaces(result)
    check_state_machine(result)
    check_ledger(result)
    check_coverage(result)

    print(f"需求库: {len(reqs)} 条")
    print()
    return result.report(verbose)


if __name__ == "__main__":
    sys.exit(main())
