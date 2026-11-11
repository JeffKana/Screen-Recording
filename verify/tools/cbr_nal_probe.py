#!/usr/bin/env python3
"""轮 43 F1：CBR 关键帧秒的字节分解（keyframe / filler / other）。

用法：
    python verify/tools/cbr_nal_probe.py <cbr.mkv> <second> [--ffprobe <path>] [--json <out>]

原理：
  ffprobe -show_packets 给出每个视频包在 MKV 里的 pos（文件偏移）与 size；
  MKV 的 H.264 数据是 AVCC（4 字节大端长度 + NAL），直接 seek(pos) 读 size 字节即可
  精确解析，不需要 -show_data 的 hex dump（seek 区间法实测只取到 5/30 个包，不可用）。
  NAL type = nal[0] & 0x1F：
      5  = IDR slice      -> keyframe_bytes
      12 = filler data    -> filler_bytes
      其它（1/6/7/8/9 等）-> other_bytes

输出：JSON 一行 + 人类可读摘要。exit 0 表示解析成功，非 0 表示取数失败。
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys


def run(cmd: list[str]) -> str:
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return proc.stdout or ""


def _parse_avcc(data: bytes) -> dict:
    total = keyframe = filler = other = nals = 0
    off = 0
    while off + 4 <= len(data):
        length = int.from_bytes(data[off:off + 4], "big")
        if length <= 0 or off + 4 + length > len(data):
            break
        nal = data[off + 4:off + 4 + length]
        size = 4 + length
        nal_type = (nal[0] & 0x1F) if nal else 0
        if nal_type == 5:
            keyframe += size
        elif nal_type == 12:
            filler += size
        else:
            other += size
        total += size
        nals += 1
        off += 4 + length
    return {"total": total, "keyframe": keyframe, "filler": filler,
            "other": other, "nals": nals}


def decode_packet(data: bytes) -> dict:
    # 调用方已按 pos+4 跳过 4 字节 MKV SimpleBlock 头，data 就是完整 AVCC 载荷。
    # 实测 NAL 链覆盖全部 size 字节（debug-pos.py 对关键帧/普通包均验证过）。
    return _parse_avcc(data)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("mkv")
    ap.add_argument("second", type=int)
    ap.add_argument("--ffprobe", default=None)
    ap.add_argument("--json", dest="json_out", default=None)
    args = ap.parse_args()

    ffprobe = args.ffprobe or shutil.which("ffprobe")
    if not ffprobe:
        print("FAIL: ffprobe not found", file=sys.stderr)
        return 2
    text = run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_packets",
                "-show_entries", "packet=pts_time,size,pos,flags", "-of", "json", args.mkv])
    try:
        packets = json.loads(text).get("packets", [])
    except json.JSONDecodeError as exc:
        print(f"FAIL: ffprobe json decode: {exc}", file=sys.stderr)
        return 3
    wanted = []
    for p in packets:
        try:
            if int(float(p.get("pts_time", ""))) != args.second:
                continue
            if not p.get("pos") or not p.get("size"):
                continue
            wanted.append(p)
        except (TypeError, ValueError):
            continue
    if not wanted:
        print(f"FAIL: no packets in second {args.second} (parsed {len(packets)} packets)",
              file=sys.stderr)
        return 4

    agg = {"total": 0, "keyframe": 0, "filler": 0, "other": 0, "nals": 0}
    probe_bytes = 0
    keyframes = 0
    with open(args.mkv, "rb") as fh:
        for p in wanted:
            size = int(p["size"])
            probe_bytes += size
            keyframes += 1 if "K" in str(p.get("flags", "")) else 0
            # pos 指向 MKV SimpleBlock 元素起始，跳过 4 字节 EBML ID+VINT 头才是 AVCC 载荷。
            fh.seek(int(p["pos"]) + 4)
            d = decode_packet(fh.read(size))
            for k in agg:
                agg[k] += d[k]
    result = {
        "mkv": args.mkv,
        "second": args.second,
        "packets": len(wanted),
        "keyframe_packets": keyframes,
        "probe_bytes": probe_bytes,
        "decoded_bytes": agg["total"],
        "keyframe_bytes": agg["keyframe"],
        "filler_bytes": agg["filler"],
        "other_bytes": agg["other"],
        "nal_count": agg["nals"],
        "parse_complete": agg["total"] > 0 and agg["total"] == probe_bytes,
    }
    out = json.dumps(result, ensure_ascii=False)
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            f.write(out + "\n")
    print("NAL_BREAKDOWN " + out)
    return 0 if result["parse_complete"] else 5


if __name__ == "__main__":
    sys.exit(main())
