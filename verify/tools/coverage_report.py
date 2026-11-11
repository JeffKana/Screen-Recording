#!/usr/bin/env python3
"""V1.0 子句级覆盖表生成器（D-40 / F1）。

事实源：
  1. docs/01 的 V1.0 需求，验收要点按「；」「。」切成子句；
  2. tasks/**/TASK.md 的 `## 覆盖子句` 声明（没有该节的旧卡按全子句覆盖）。

生成的 docs/09-V1.0覆盖表.md 只允许人工填写最后一列「挂账理由」；
重跑本脚本会保留该列已有内容（兼容旧的需求级 5 列表格）。

用法:
    python verify/tools/coverage_report.py            # 打印统计
    python verify/tools/coverage_report.py --write    # 生成/更新覆盖表
    python verify/tools/coverage_report.py --check    # 与现有文件比对（CI 用）
"""

from __future__ import annotations

import sys
from pathlib import Path

import coverage_model as cm


def render() -> str:
    reqs = cm.load_requirements()
    cards, clause_cards, delivery = cm.load_cards(reqs)
    clause_reasons, req_reasons = cm.load_existing_reasons()
    rows = cm.build_rows(reqs, cards, clause_cards, clause_reasons, req_reasons, delivery)
    stats = cm.summarize(rows)

    covered_reqs = {row["requirement"] for row in rows if row["covered"]}
    lines: list[str] = []
    lines.append("# 09 · V1.0 覆盖表（子句级）")
    lines.append("")
    lines.append("> 本文件由 `python verify/tools/coverage_report.py --write` 生成。")
    lines.append("> **只手工填写最后一列「挂账理由」**；重跑生成器会保留该列已有内容。")
    lines.append("> 子句 = docs/01 验收要点按「；」「。」切分；任务卡用 `## 覆盖子句` 声明")
    lines.append("> `M2-09#1` / `M2-09#*`。没有该节的旧卡按全子句覆盖（历史卡）——")
    lines.append("> 有该节的卡若漏写某子句，则该子句必须挂账，不能再被整条需求的「有卡」掩盖。")
    lines.append("> 「状态」三态（轮 30 / F1）：`已实现` / `有卡未实现` / `无卡`；")
    lines.append("> 卡在 `## 交付状态` 里写 `- M8-14: 已实现` 才算交付；")
    lines.append("> **没有该节的卡默认「有卡未实现」**（轮 32 F2：漏写一节不再自动算已交付）。")
    lines.append("> 轮 33（R1）：`## 交付状态` 支持子句级 `- M3-04#1: 已实现`，")
    lines.append("> 取「子句声明 > 需求级兜底」——整条需求未实现不会再把已达标子句一起压成未实现。")
    lines.append("> 轮 37（R5）：**多卡合并改为「最保守」**——同一子句被多张卡声明时，")
    lines.append("> 只有**全部**归属卡都写「已实现」才算已实现；任一张不是 → 「有卡未实现」。")
    lines.append("> 这样「新卡纠正旧卡」才生效（旧规则是任一卡写已实现即已实现，覆盖表只涨不跌）。")
    lines.append("> 非「已实现」的声明之间若互相矛盾（状态词不同，或理由都非空且不同）→ **报冲突**，")
    lines.append("> 由 `coverage_report.py` 以非零退出码拦下，交人工改卡。")
    lines.append("")
    lines.append("> **2026-10-29 变更说明（轮 37 · R5 纠偏）**：M3-05 的交付状态由「已实现」纠偏为")
    lines.append("> 「有卡未实现」——G3/S4-001 卡写「已实现」但同卡 EVIDENCE 写 PARTIAL，且全仓无")
    lines.append("> 「编码器列表 + 不可用原因」结构（旧合并规则「任一卡写已实现即已实现」把纠正覆盖掉了）。")
    lines.append("> 该纠偏使**已实现 87→85、有卡未实现 4→6**：**属纠偏，不是回归**——G5 已于轮 36 关闭，")
    lines.append("> M6-02#1 / M6-09#1 的 8h 单文件证据不变；M3-05 落地后恢复为 87/4/25。")
    lines.append("")
    lines.append("> **轮 42：M8-05#1 因口径重定回退为「有卡未实现」，`g5_storage` 新口径 5 连绿后翻回**")
    lines.append("> （裁定 A：对照口径由 10s 实测速率改为配置码率，并新增独立 60s 长窗口护栏；")
    lines.append("> 依据验收报告-轮41 §2 / 轮42指令 §4.1）。**数字先降后升是计划内状态迁移，不是回归。**")
    lines.append("")
    lines.append(
        f"需求库 {len(reqs)} 条，V1.0 **{stats['requirements']}** 条 / **{stats['clauses']}** 个子句；"
        f"已实现 **{stats['implemented']}**，有卡未实现 **{stats['unimplemented']}**，"
        f"无卡 **{stats['uncovered']}**，未实现已挂账 **{stats['waived']}**。"
    )
    lines.append("")
    lines.append("`contract_check.py` C8 判据：**有卡未实现 + 无卡** 的子句都必须填写挂账理由，否则 FAIL。")
    lines.append("")
    lines.append("| 需求 | 子句 | 验收要点 | 任务卡 | 状态 | 挂账理由 |")
    lines.append("| --- | --- | --- | --- | --- | --- |")
    for row in rows:
        card_text = "<br>".join(f"`{c}`" for c in row["cards"]) if row["cards"] else "—"
        reason = row["reason"] if row["state"] != "已实现" else "—"
        text = row["text"].replace("|", "\\|")
        lines.append(
            f"| {row['clause_id']} | {row['index']} | {text} | {card_text} | {row['state']} | {reason} |"
        )
    lines.append("")
    lines.append("## 统计（按模块 / 子句）")
    lines.append("")
    modules = sorted({row["requirement"].split("-")[0] for row in rows})
    lines.append("| 模块 | 需求 | 子句 | 已实现 | 有卡未实现 | 无卡 | 已挂账 | 缺理由 |")
    lines.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for module in modules:
        module_rows = [row for row in rows if row["requirement"].startswith(module + "-")]
        module_reqs = {row["requirement"] for row in module_rows}
        module_stats = cm.summarize(module_rows)
        lines.append(
            f"| {module} | {len(module_reqs)} | {module_stats['clauses']} | "
            f"{module_stats['implemented']} | {module_stats['unimplemented']} | "
            f"{module_stats['uncovered']} | {module_stats['waived']} | "
            f"{module_stats['missing']} |"
        )
    lines.append(
        f"| **合计** | **{stats['requirements']}** | **{stats['clauses']}** | "
        f"**{stats['implemented']}** | **{stats['unimplemented']}** | "
        f"**{stats['uncovered']}** | **{stats['waived']}** | **{stats['missing']}** |"
    )
    lines.append("")
    lines.append("> 未实现子句未挂账会被 C8 点名；理由写清「已实现待补卡 / 未开工待排期 / 环境阻塞」。")
    lines.append("> 有卡需求："
                 + (", ".join(sorted(covered_reqs)) if covered_reqs else "无"))
    lines.append("")
    conflicts = [(row["clause_id"], row["conflicts"]) for row in rows if row["conflicts"]]
    if conflicts:
        lines.append("## 卡声明冲突（轮 37 R5：必须改卡后重生成）")
        lines.append("")
        for cid, items in conflicts:
            for item in items:
                lines.append(f"- {item}")
        lines.append("")
    return "\n".join(lines)


def self_test() -> int:
    """三态模型自检（g5_diag 调用）：确保「有卡未实现」不会被算成已实现。"""
    card = "## 覆盖子句\n- M8-14#*\n\n## 交付状态\n- M8-14: 有卡未实现\n"
    parsed = cm.card_delivery(card)
    if parsed != {"M8-14": "有卡未实现"}:
        print(f"self_test FAIL: card_delivery={parsed}")
        return 1

    reqs = cm.load_requirements()
    stub = ["tasks/STUB/TASK.md"]
    clause_cards = {"M8-14#1": stub, "M8-14#2": stub}
    delivery = {"tasks/STUB/TASK.md": {"M8-14": "有卡未实现"}}
    rows = cm.build_rows(reqs, {"M8-14": stub}, clause_cards, {}, {}, delivery)
    states = [row["state"] for row in rows if row["requirement"] == "M8-14"]
    if states != ["有卡未实现", "有卡未实现"]:
        print(f"self_test FAIL: states={states}")
        return 1

    # 轮 32（F2）：没有 ## 交付状态 的卡必须判「有卡未实现」——
    # 漏写一节 = 自动算已交付，正是 M8-14 那个洞的入口。
    rows = cm.build_rows(reqs, {"M8-14": stub}, clause_cards, {}, {}, {})
    states = [row["state"] for row in rows if row["requirement"] == "M8-14"]
    if states != ["有卡未实现", "有卡未实现"]:
        print(f"self_test FAIL: legacy states={states}")
        return 1

    # 轮 33（R1）：子句级声明优先于需求级兜底。
    # M3-04 整条因 VBR 未实现，但 #1（CBR）已达标——必须能表达成「#1 已实现、#2 未实现」。
    clause_card = "## 覆盖子句\n- M3-04#1\n\n## 交付状态\n- M3-04: 有卡未实现\n- M3-04#1: 已实现\n"
    parsed = cm.card_delivery(clause_card)
    if parsed != {"M3-04": "有卡未实现", "M3-04#1": "已实现"}:
        print(f"self_test FAIL: clause delivery={parsed}")
        return 1
    stub2 = ["tasks/STUB2/TASK.md"]
    both_clauses = {"M3-04#1": stub2, "M3-04#2": stub2}
    rows = cm.build_rows(reqs, {"M3-04": stub2}, both_clauses,
                         {}, {}, {"tasks/STUB2/TASK.md": parsed})
    states = [row["state"] for row in rows if row["requirement"] == "M3-04"]
    if states != ["已实现", "有卡未实现"]:
        print(f"self_test FAIL: clause states={states}")
        return 1

    # 反向：子句级写「有卡未实现」时不能被需求级「已实现」拉绿。
    reverse = {"tasks/STUB2/TASK.md": {"M3-04": "已实现", "M3-04#1": "有卡未实现"}}
    rows = cm.build_rows(reqs, {"M3-04": stub2}, both_clauses, {}, {}, reverse)
    states = [row["state"] for row in rows if row["requirement"] == "M3-04"]
    if states != ["有卡未实现", "已实现"]:
        print(f"self_test FAIL: clause override states={states}")
        return 1
    # 轮 37（R5）：**对抗用例**——合并规则必须能拦住「新卡纠正旧卡」。
    # 场景 1：旧卡说已实现、新卡说未实现 → 终态必须是「有卡未实现」（旧规则会误判已实现）。
    # 场景 2：两张卡都未实现但理由不同 → 必须报冲突。
    # 场景 3：两张卡都未实现且理由一致 → 不算冲突（只有一条理由的补充不算矛盾）。
    # 场景 4：两张卡都已实现 → 仍为已实现，且不报冲突。
    import tempfile

    def _scenario(first: str, second: str) -> tuple[list[str], list[str]]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "tasks" / "OLD" / "T1").mkdir(parents=True)
            (root / "tasks" / "NEW" / "T2").mkdir(parents=True)
            (root / "tasks" / "OLD" / "T1" / "TASK.md").write_text(
                "## 覆盖子句\n- M9-99#1\n\n## 交付状态\n- M9-99#1: " + first + "\n",
                encoding="utf-8")
            (root / "tasks" / "NEW" / "T2" / "TASK.md").write_text(
                "## 覆盖子句\n- M9-99#1\n\n## 交付状态\n- M9-99#1: " + second + "\n",
                encoding="utf-8")
            fake_reqs = {"M9-99": {"name": "stub", "acceptance": "占位；占位",
                                   "version": "V1.0", "clauses": ["占位；占位"]}}
            _, clause_cards, delivery = cm.load_cards(fake_reqs, tasks_dir=root)
            rows = cm.build_rows(fake_reqs, {"M9-99": clause_cards.get("M9-99#1", [])},
                                 clause_cards, {}, {}, delivery)
            state = rows[0]["state"] if rows else "?"
            return [state], rows[0]["conflicts"] if rows else []

    states, conflicts = _scenario("已实现", "有卡未实现")
    if states != ["有卡未实现"] or conflicts:
        print(f"self_test FAIL: 场景1（一已实现 + 一未实现）state={states} conflicts={conflicts}")
        return 1
    states, conflicts = _scenario("有卡未实现（甲理由）", "有卡未实现（乙理由）")
    if states != ["有卡未实现"] or not conflicts:
        print(f"self_test FAIL: 场景2（都未实现但理由不同）state={states} conflicts={conflicts}")
        return 1
    states, conflicts = _scenario("有卡未实现（同一理由）", "有卡未实现（同一理由）")
    if states != ["有卡未实现"] or conflicts:
        print(f"self_test FAIL: 场景3（都未实现且理由一致）state={states} conflicts={conflicts}")
        return 1
    states, conflicts = _scenario("已实现", "已实现")
    if states != ["已实现"] or conflicts:
        print(f"self_test FAIL: 场景4（两张都已实现）state={states} conflicts={conflicts}")
        return 1
    print("self_test ok: 三态 + 子句级优先判定正确（缺节默认未实现）")
    print("self_test ok: 轮 37 R5 对抗用例——最保守合并 + 冲突上报")
    return 0


def collect_conflicts() -> list[str]:
    reqs = cm.load_requirements()
    cards, clause_cards, delivery = cm.load_cards(reqs)
    clause_reasons, req_reasons = cm.load_existing_reasons()
    rows = cm.build_rows(reqs, cards, clause_cards, clause_reasons, req_reasons, delivery)
    out: list[str] = []
    for row in rows:
        out.extend(row.get("conflicts", []))
    return out


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if "--self-test" in sys.argv:
        return self_test()
    text = render()
    conflicts = collect_conflicts()
    if "--write" in sys.argv:
        cm.COVERAGE.parent.mkdir(parents=True, exist_ok=True)
        cm.COVERAGE.write_text(text, encoding="utf-8")
        print(f"written: {cm.COVERAGE.relative_to(cm.ROOT).as_posix()}")
        if conflicts:
            print("卡声明冲突（轮 37 R5，必须改卡）:")
            for item in conflicts:
                print("  " + item)
            return 1
        return 0
    if "--check" in sys.argv:
        current = cm.COVERAGE.read_text(encoding="utf-8") if cm.COVERAGE.exists() else ""
        if current != text:
            print("coverage table is stale; run: python verify/tools/coverage_report.py --write")
            return 1
        if conflicts:
            print("卡声明冲突（轮 37 R5，必须改卡）:")
            for item in conflicts:
                print("  " + item)
            return 1
        print("coverage table is up to date")
        return 0

    reqs = cm.load_requirements()
    cards, clause_cards, delivery = cm.load_cards(reqs)
    clause_reasons, req_reasons = cm.load_existing_reasons()
    rows = cm.build_rows(reqs, cards, clause_cards, clause_reasons, req_reasons, delivery)
    stats = cm.summarize(rows)
    print(
        f"需求库 {len(reqs)} 条, V1.0 {stats['requirements']} 条 / {stats['clauses']} 子句, "
        f"已实现 {stats['implemented']}, 有卡未实现 {stats['unimplemented']}, "
        f"无卡 {stats['uncovered']}, 已挂账 {stats['waived']}, 缺理由 {stats['missing']}"
    )
    missing = [row["clause_id"] for row in rows
               if row["state"] != "已实现" and not cm.is_waived(row["reason"])]
    if missing:
        print("缺理由: " + ", ".join(missing))
    if conflicts:
        print("卡声明冲突（轮 37 R5，必须改卡）:")
        for item in conflicts:
            print("  " + item)
    print("生成: python verify/tools/coverage_report.py --write")
    return 1 if conflicts else 0


if __name__ == "__main__":
    sys.exit(main())
