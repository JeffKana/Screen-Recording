# TASK: V1-013 轮 44 · 反向护栏 ×2（self-test 17 → 19）

阶段：G6｜角色：V1｜轮次：44｜来源：`.codebuddy/plans/轮44指令.md` §2 P0

## 需求范围

在 `g5_format_selftest.ps1` 新增 2 条断言，各造一次异常并**必须让用例判 FAIL**：

| 断言 | 注入内容 | 期望 |
| --- | --- | --- |
| `r44-window-spread-fail` | 逐秒序列使 5 秒滑窗 spread >10%，但每个单秒都 ≤2× | `FAIL cbr-window5-spread`、退出码 1 |
| `r44-single-second-cap-fail` | 逐秒序列含一个 > 2× 标称的尖峰（如 16,699kbps），但滑窗仍 ≤10% | `FAIL cbr-single-second-cap`、退出码 1 |

## 文件域（白名单）

- `verify/cases/g5_format_selftest.ps1`（唯一）
- `tasks/G6/V1-013/*`（本卡四件套）

## 约束

1. **总数必须 19**（原 17 + 2），原 17 条一条不删；`RESULT` 行格式不变。
2. 原守卫 `f2-threshold-spread-10pct` 的模式因断言改名要同步更新，但
   **仍必须断言文本含 `-le 0.10`**（判据数字未被改软）；
   另加 2 条守卫检查 `-le 2` / 滑窗宽度 `5` 等新判据字面量。
3. 注入走 V1-012 的 `-SimulateCbrRates`，不启动 recorder；
   若模拟分支输出 SKIP 而不是 FAIL，自检必须判失败（不得藏成 SKIP）。
4. 不改 `src/`、不改 `docs/`、不改 `g5_format.ps1`。

## 完成定义（DoD）

- [ ] `g5_format_selftest.ps1` → **19 PASS / 0 FAIL**
- [ ] 两条护栏的红证据（用例输出片段）落在 artifacts
- [ ] 四件套齐全

## 覆盖子句

无（自检侧工作）。

## 交付状态

无（本卡不声明需求子句）。