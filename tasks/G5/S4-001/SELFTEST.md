# SELFTEST: S4-001

## 1. 兜底分支误伤正常提交（回归时暴露）

加入兜底后 `g4_control` 有 3 项失败：`--fail-resume`（采集恢复失败，但数据完好）也被
当成「数据完整性受损」送进兜底分支，导致没有成品、只剩 `.part`。

修法：用 `integrity_lost = write_errors > 0 || close_failed` 区分两类致命错误。
**教训**：新增一条错误分支后，必须回跑既有错误路径用例，确认没有把「数据好好的失败」也吞掉。

## 2. 句柄未关导致清理静默失败（D-33）

`std::filesystem::remove(part_path, ec)` 用 error_code 重载，失败不抛异常也不打印。
第一次验证时原目录残留文件，看起来像「删除逻辑没写」，实际是 `MkvWriter::Close` 在
flush 失败时提前 return，句柄还在。

**教训**：错误路径上的资源释放要和成功路径一样显式；带 `error_code` 的 API 必须检查返回值，
否则失败是静默的。

## 3. 注入点下沉后要重新确认断言

`--fail-encoder` 从调用方短路下沉到 `CreateTransform` 后，原来的断言串
（`no usable video encoder`）再也不出现。若沿用旧断言，用例会一直红；
若沿用旧实现，用例会一直绿但什么也没测。现在断言改成「错误里同时含 hardware 与 software」，
直接对应两条真实分支。
