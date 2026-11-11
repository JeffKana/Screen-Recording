#!/usr/bin/env python3
"""子句级 V1.0 覆盖模型（D-40 / F1）。

事实源：
  1. docs/01 的验收要点按「；」「。」切成子句（M2-09#1、M2-09#2 …）；
  2. tasks/**/TASK.md 的 `## 覆盖子句` 节：
       - 没有该节的旧卡：其引用的需求视为**全子句**覆盖（历史卡没有子句声明）；
       - 有该节的卡：只覆盖列出的子句（`#1`、`#2-3` 或 `#*`）。
    3. tasks/**/TASK.md 的 `## 交付状态` 节（轮 30 / F1，轮 32 收紧）：
       - `- M8-14: 有卡未实现` 表示「有卡但没交付」；
       - 没有该节的卡默认 **有卡未实现**：漏写一节 = 自动算已交付，正是
         M8-14 那个洞的入口（轮 32 F2）。要翻绿必须显式写「已实现」。

子句状态三态：已实现 / 有卡未实现 / 无卡。C8 要求后两态都逐条挂账，
「有卡」不再等同于「已覆盖」（这条规则来自轮 29 F1：M8-14 被骨架卡蹭成已覆盖）。
"""

from __future__ import annotations

import re
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REQ_DOC = ROOT / "docs" / "01-功能需求清单.md"
TASKS_DIR = ROOT / "tasks"
COVERAGE = ROOT / "docs" / "09-V1.0覆盖表.md"

REQ_ID = re.compile(r"\bM\d-\d{2}\b")
CLAUSE_TOKEN = re.compile(r"\b(M\d-\d{2})#(\d+|\*)")
CLAUSE_ID = re.compile(r"^M\d-\d{2}#\d+$")
PLACEHOLDERS = {"", "—", "-", "待填", "tbd", "todo", "n/a", "na"}


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def table_rows(block: str) -> list[list[str]]:
    rows: list[list[str]] = []
    for line in block.splitlines():
        line = line.strip()
        if not line.startswith("|") or not line.endswith("|"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if cells and all(set(c) <= {"-", ":"} and c for c in cells):
            continue
        rows.append(cells)
    return rows


def split_clauses(acceptance: str) -> list[str]:
    parts = [p.strip() for p in re.split(r"[；。]", acceptance) if p.strip()]
    return parts or ["（无验收要点）"]


def load_requirements() -> dict[str, dict]:
    reqs: dict[str, dict] = {}
    for cells in table_rows(read_text(REQ_DOC)):
        if len(cells) < 4 or not re.fullmatch(r"M\d-\d{2}", cells[0]):
            continue
        version = ""
        for cell in reversed(cells):
            if re.fullmatch(r"V\d\.\d", cell):
                version = cell
                break
        acceptance = cells[2]
        reqs[cells[0]] = {
            "name": cells[1],
            "acceptance": acceptance,
            "version": version,
            "clauses": split_clauses(acceptance),
        }
    return reqs


def clause_ids(rid: str, req: dict) -> list[str]:
    return [f"{rid}#{index}" for index in range(1, len(req["clauses"]) + 1)]


DELIVERY_SECTION = re.compile(r"(?ms)^##\s*(?:\d+[\.\)]\s*)?交付状态\s*$(.*?)(?=^##\s+|\Z)")
# 只认「已实现」为已交付；其它写法（有卡未实现 / 未开工 / 骨架 / 任意说明）
# 一律按未实现处理——否则写错一个词就会静默变成「已覆盖」。
# 轮 33（R1）：键可以是需求级 `M8-14`，也可以是子句级 `M3-04#1`。
DELIVERY_LINE = re.compile(r"^[-*]\s*(M\d-\d{2}(?:#\d+)?)\s*[:：]\s*(.+)$")


def card_delivery(text: str) -> dict[str, str]:
    """卡级交付状态：`## 交付状态` 里 `- M8-14: 有卡未实现` / `- M3-04#1: 已实现`。

    缺该节的卡返回空字典；调用方（build_rows）对缺失项按「有卡未实现」处理。
    这是轮 32 F2 的口径翻转：默认从严，翻绿必须显式声明「已实现」。

    轮 33（R1）：允许子句级声明。判据是「子句声明 > 需求级兜底」——
    需求整体未实现（如 M3-04 的 VBR 缺）不再把已达标的 #1 一起压成未实现；
    但子句级声明写「有卡未实现」时也不会被需求级的「已实现」反向拉绿。
    """
    match = DELIVERY_SECTION.search(text)
    if match is None:
        return {}
    out: dict[str, str] = {}
    for line in match.group(1).splitlines():
        stripped = line.strip()
        if not stripped.startswith(("-", "*")):
            continue
        found = DELIVERY_LINE.match(stripped)
        if found:
            out[found.group(1)] = found.group(2).strip()
    return out


def card_clause_section(text: str) -> dict[str, set[str]] | None:
    match = re.search(r"(?ms)^##\s*(?:\d+[\.\)]\s*)?覆盖子句\s*$(.*?)(?=^##\s+|\Z)", text)
    if match is None:
        return None
    section: dict[str, set[str]] = defaultdict(set)
    # 只认列表项（- M2-09#1）。正文/引用里的 `M2-09#2` 是在解释缺口，
    # 不能反过来把它算成覆盖声明。
    for line in match.group(1).splitlines():
        stripped = line.strip()
        if not stripped.startswith(("-", "*")):
            continue
        for rid, spec in CLAUSE_TOKEN.findall(stripped):
            section[rid].add(spec)
    return section


def load_cards(reqs: dict[str, dict], tasks_dir: Path | None = None) -> tuple[dict[str, list[str]], dict[str, list[str]], dict[str, dict[str, str]]]:
    """返回 (需求→卡, 子句→卡, 卡→需求→交付状态)。"""
    cards: dict[str, list[str]] = defaultdict(list)
    clause_cards: dict[str, list[str]] = defaultdict(list)
    delivery: dict[str, dict[str, str]] = defaultdict(dict)

    root = Path(tasks_dir) if tasks_dir is not None else TASKS_DIR
    for card in sorted(root.rglob("TASK.md")):
        # 卡片路径：生产用相对仓库根（表里显示 tasks/…）；自检用的临时目录不在仓库里，
        # 退化为相对 tasks 根，不影响任何判定。
        try:
            rel = card.relative_to(ROOT).as_posix()
        except ValueError:
            rel = card.relative_to(root).as_posix()
        text = read_text(card)
        referenced = set(REQ_ID.findall(text))
        section = card_clause_section(text)
        for rid, state in card_delivery(text).items():
            delivery[rel][rid] = state

        # 有 `## 覆盖子句` 的卡：**只认这一节**。正文里顺带提到别的需求号
        # （例如「依赖 M5-04 的索引」）不能算覆盖——否则一句引用就能把无卡变有卡，
        # 这正是轮 29 F1 那类「有卡 = 已覆盖」的洞。
        for rid in (section if section is not None else referenced):
            if rid not in reqs:
                continue
            if rel not in cards[rid]:
                cards[rid].append(rel)
            specs = section.get(rid, set()) if section is not None else None
            if specs is None:
                for cid in clause_ids(rid, reqs[rid]):
                    if rel not in clause_cards[cid]:
                        clause_cards[cid].append(rel)
            else:
                for cid in clause_ids(rid, reqs[rid]):
                    number = cid.rsplit("#", 1)[1]
                    if "*" in specs or number in specs:
                        if rel not in clause_cards[cid]:
                            clause_cards[cid].append(rel)
    return cards, clause_cards, dict(delivery)


def load_existing_reasons() -> tuple[dict[str, str], dict[str, str]]:
    """兼容旧表（需求级，5 列）与新表（子句级，6 列）。"""
    if not COVERAGE.exists():
        return {}, {}
    clause_reasons: dict[str, str] = {}
    req_reasons: dict[str, str] = {}
    for cells in table_rows(read_text(COVERAGE)):
        if not cells:
            continue
        key = cells[0]
        if CLAUSE_ID.fullmatch(key) and len(cells) >= 6:
            clause_reasons[key] = cells[5]
        elif re.fullmatch(r"M\d-\d{2}", key) and len(cells) >= 5:
            req_reasons[key] = cells[4]
    return clause_reasons, req_reasons


def is_waived(reason: str) -> bool:
    return reason.strip().lower() not in PLACEHOLDERS


DECLARATION_SPLIT = re.compile(r"^([^（(]*?)\s*[（(](.*)[）)]\s*$")


def split_declaration(text: str) -> tuple[str, str]:
    """把交付声明拆成 (状态词, 理由)。

    `有卡未实现` → ("有卡未实现", "")
    `有卡未实现（探测已完成；列表见 S4-002）` → ("有卡未实现", "探测已完成；列表见 S4-002")
    """
    stripped = text.strip()
    found = DECLARATION_SPLIT.match(stripped)
    if found:
        return found.group(1).strip(), found.group(2).strip()
    return stripped, ""


def merge_clause_state(cid: str, rid: str, owners: list[str],
                       delivery: dict[str, dict[str, str]]) -> tuple[str, list[str]]:
    """合并同一子句的多张卡声明（轮 37 R5：改为「最保守」）。

    规则：
      1. 每张归属卡对本子句的声明 = 子句级声明，没有才回落到需求级声明，
         都没有则按「有卡未实现」（轮 32 F2：漏写一节 = 未交付）。
      2. **所有**归属卡都声明「已实现」才算已实现；只要任一张不是 → 「有卡未实现」。
         旧规则是「任一卡声明已实现即已实现」，于是新卡对旧卡的纠正永远不生效，
         覆盖表只涨不跌（轮 35 R5 事实链）。
      3. 互相矛盾的非「已实现」声明 → 报冲突（状态词不同，或状态词相同但理由都非空且不同），
         交人工改卡。「已实现」与「未实现」并存不算冲突：那是规则 2 的正常收敛。

    返回 (终态, 冲突描述列表)。
    """
    declarations: list[tuple[str, str, str]] = []
    for owner in owners:
        card_states = delivery.get(owner, {})
        declared = card_states.get(cid, card_states.get(rid, "有卡未实现"))
        state_word, reason = split_declaration(declared)
        declarations.append((owner, state_word, reason))

    if not declarations:
        return "无卡", []

    implemented = all(state.startswith("已实现") for _, state, _ in declarations)
    state = "已实现" if implemented else "有卡未实现"

    pending = [(owner, state, reason) for owner, state, reason in declarations
               if not state.startswith("已实现")]
    conflicts: list[str] = []
    for i in range(len(pending)):
        for j in range(i + 1, len(pending)):
            a_owner, a_state, a_reason = pending[i]
            b_owner, b_state, b_reason = pending[j]
            if a_state != b_state:
                conflicts.append(
                    f"{cid}: 「{a_owner}」声明 {a_state}，「{b_owner}」声明 {b_state}（状态互相矛盾）")
            elif a_reason and b_reason and a_reason != b_reason:
                conflicts.append(
                    f"{cid}: 「{a_owner}」与「{b_owner}」都声明 {a_state}，但理由不同"
                    f"（{a_reason} / {b_reason}）")
    return state, conflicts


def build_rows(reqs: dict[str, dict] | None = None,
               cards: dict[str, list[str]] | None = None,
               clause_cards: dict[str, list[str]] | None = None,
               clause_reasons: dict[str, str] | None = None,
               req_reasons: dict[str, str] | None = None,
               delivery: dict[str, dict[str, str]] | None = None) -> list[dict]:
    reqs = reqs or load_requirements()
    if cards is None or clause_cards is None:
        cards, clause_cards, delivery = load_cards(reqs)
    if delivery is None:
        delivery = {}
    if clause_reasons is None or req_reasons is None:
        clause_reasons, req_reasons = load_existing_reasons()

    rows: list[dict] = []
    for rid in sorted(rid for rid, req in reqs.items() if req["version"] == "V1.0"):
        for index, text in enumerate(reqs[rid]["clauses"], start=1):
            cid = f"{rid}#{index}"
            owners = clause_cards.get(cid, [])
            covered = bool(owners)
            # F1：三态。有卡不等于已交付——只要没有任何归属卡声明「已实现」，
            # 该子句就是「有卡未实现」，C8 同样要求挂账。
            # 轮 37（R5）：多卡合并改为「最保守」——全部声明已实现才算已实现。
            if covered:
                state, conflicts = merge_clause_state(cid, rid, owners, delivery)
            else:
                state, conflicts = "无卡", []
            reason = "—" if state == "已实现" else (
                clause_reasons.get(cid, "") or req_reasons.get(rid, "")
            )
            rows.append({
                "clause_id": cid,
                "requirement": rid,
                "index": index,
                "text": text,
                "cards": owners,
                "covered": covered,
                "implemented": state == "已实现",
                "state": state,
                "reason": reason,
                "conflicts": conflicts,
            })
    return rows


def summarize(rows: list[dict]) -> dict[str, int]:
    implemented = sum(1 for row in rows if row["state"] == "已实现")
    unimplemented = sum(1 for row in rows if row["state"] == "有卡未实现")
    uncovered = sum(1 for row in rows if row["state"] == "无卡")
    waived = sum(1 for row in rows if row["state"] != "已实现" and is_waived(row["reason"]))
    conflicts = sum(len(row.get("conflicts", [])) for row in rows)
    return {
        "clauses": len(rows),
        "covered": implemented + unimplemented,   # 「有卡」（用于 C7）
        "implemented": implemented,
        "unimplemented": unimplemented,
        "uncovered": uncovered,
        "waived": waived,
        "missing": unimplemented + uncovered - waived,
        "conflicts": conflicts,
        "requirements": len({row["requirement"] for row in rows}),
    }
