#!/usr/bin/env python3
"""轮 48 · MP4 box / sample table 结构核验（不依赖 ffprobe，只读文件字节）。

验证「sample table 正确」这件事本身：stts/ctts/stsz/stsc/stco(co64)/stss 的计数、
时长、偏移必须彼此自洽，且每个 chunk 偏移都落在 mdat 数据区里。

用法:
    python verify/tools/mp4_box_check.py <file.mp4>
输出: 逐条 PASS/FAIL + `RESULT: N PASS / M FAIL`，退出码 0/1。
本卡（轮 48）moov 允许后置；faststart 留轮 49，不在这里判。
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

CONTAINERS = {"moov", "trak", "mdia", "minf", "stbl", "dinf", "edts", "mvex", "moof", "traf"}


def boxes(data: bytes, start: int, end: int):
    off = start
    while off + 8 <= end:
        size = struct.unpack_from(">I", data, off)[0]
        kind = data[off + 4:off + 8].decode("latin1")
        header = 8
        if size == 1:
            if off + 16 > end:
                return
            size = struct.unpack_from(">Q", data, off + 8)[0]
            header = 16
        elif size == 0:
            size = end - off
        if size < header or off + size > end:
            return
        yield kind, off, header, size
        off += size


def find(data: bytes, start: int, end: int, kind: str):
    for item in boxes(data, start, end):
        if item[0] == kind:
            return item
    return None


def all_of(data: bytes, start: int, end: int, kind: str):
    return [item for item in boxes(data, start, end) if item[0] == kind]


def payload(data: bytes, item) -> bytes:
    _, off, header, size = item
    return data[off + header:off + size]


def u32(buf: bytes, off: int) -> int:
    return struct.unpack_from(">I", buf, off)[0]


def fullbox_payload(data: bytes, item):
    """返回 (version, flags, body)。"""
    p = payload(data, item)
    version = p[0]
    flags = (p[1] << 16) | (p[2] << 8) | p[3]
    return version, flags, p[4:]


def stbl_boxes(data: bytes, stbl):
    _, off, header, size = stbl
    out = {}
    for kind, o, h, s in boxes(data, off + header, off + size):
        out.setdefault(kind, []).append((kind, o, h, s))
    return out


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: python verify/tools/mp4_box_check.py <file.mp4>")
        return 2
    path = Path(sys.argv[1])
    if not path.is_file():
        print(f"FAIL: file missing: {path}")
        return 1
    data = path.read_bytes()

    passed = 0
    failed = 0

    def check(name: str, ok: bool, detail: str = "") -> None:
        nonlocal passed, failed
        if ok:
            passed += 1
            print(f"  PASS {name} — {detail}")
        else:
            failed += 1
            print(f"  FAIL {name} — {detail}")

    top = list(boxes(data, 0, len(data)))
    kinds = [item[0] for item in top]
    check("top-ftyp", "ftyp" in kinds, f"top boxes={','.join(kinds)}")
    ftyp = find(data, 0, len(data), "ftyp")
    check("ftyp-isom", ftyp is not None and payload(data, ftyp)[:4] in (b"isom", b"mp42"),
          f"major={payload(data, ftyp)[:4]!r}" if ftyp else "ftyp 缺失")

    mdat = find(data, 0, len(data), "mdat")
    moov = find(data, 0, len(data), "moov")
    check("has-mdat-moov", mdat is not None and moov is not None,
          f"mdat_off={mdat[1] if mdat else '-'} moov_off={moov[1] if moov else '-'}")
    if mdat is None or moov is None:
        print(f"RESULT: {passed} PASS / {failed + 1} FAIL")
        return 1

    _, mdat_off, mdat_header, mdat_size = mdat
    largesize = struct.unpack_from(">Q", data, mdat_off + 8)[0] if mdat_header == 16 else mdat_size
    data_start = mdat_off + mdat_header
    data_end = mdat_off + largesize
    check("mdat-largesize", mdat_header == 16 and largesize == mdat_size,
          f"header={mdat_header} largesize={largesize} box_size={mdat_size}")
    check("mdat-payload-consistent", data_end == moov[1],
          f"mdat 数据区 [{data_start},{data_end}) / moov 起点={moov[1]}")
    # 本卡 moov 后置；faststart 是轮 49 的范围，这里只记录不判失败。
    check("moov-after-mdat", moov[1] > mdat_off,
          f"moov_off={moov[1]} > mdat_off={mdat_off}（轮 48 起步：moov 可后置）")

    mvhd = find(data, moov[1] + 8, moov[1] + moov[3], "mvhd")
    check("has-mvhd", mvhd is not None, "moov 内 mvhd")
    if mvhd:
        _, _, body = fullbox_payload(data, mvhd)
        timescale, duration = u32(body, 8), u32(body, 12)
        check("mvhd-timescale-1000", timescale == 1000, f"timescale={timescale} duration={duration}")
        check("mvhd-duration-positive", 0 < duration < 3600000, f"duration={duration}ms")

    traks = all_of(data, moov[1] + 8, moov[1] + moov[3], "trak")
    check("trak-count-2", len(traks) == 2, f"trak 数={len(traks)}（1 路 H.264 + 1 路 AAC）")

    total_sample_bytes = 0
    for index, trak in enumerate(traks):
        label = "video" if index == 0 else "audio"
        t_begin, t_end = trak[1] + trak[2], trak[1] + trak[3]
        mdia = find(data, t_begin, t_end, "mdia")
        minf = find(data, mdia[1] + mdia[2], mdia[1] + mdia[3], "minf") if mdia else None
        stbl = find(data, minf[1] + minf[2], minf[1] + minf[3], "stbl") if minf else None
        mdhd = find(data, mdia[1] + mdia[2], mdia[1] + mdia[3], "mdhd") if mdia else None
        if not (mdia and minf and stbl and mdhd):
            check(f"{label}-stbl-present", False, "mdia/minf/stbl/mdhd 缺失")
            continue
        check(f"{label}-stbl-present", True, "mdia/minf/stbl/mdhd 齐备")
        _, _, mdhd_body = fullbox_payload(data, mdhd)
        track_timescale, track_duration = u32(mdhd_body, 8), u32(mdhd_body, 12)
        expected_timescale = 90000 if label == "video" else None
        if expected_timescale:
            check(f"{label}-timescale", track_timescale == expected_timescale,
                  f"timescale={track_timescale}（视频 90kHz 避免 ±1 tick 漂移）")
        else:
            check(f"{label}-timescale", track_timescale > 0, f"timescale={track_timescale}")

        table = stbl_boxes(data, stbl)
        for needed in ("stsd", "stts", "stsc", "stsz"):
            check(f"{label}-has-{needed}", needed in table, f"{needed} 存在={needed in table}")
        check(f"{label}-has-offsets", ("stco" in table) or ("co64" in table),
              f"stco={('stco' in table)} co64={('co64' in table)}")
        if not all(k in table for k in ("stsd", "stts", "stsc", "stsz")):
            continue
        if not (("stco" in table) or ("co64" in table)):
            continue

        _, _, stts_body = fullbox_payload(data, table["stts"][0])
        stts_count = u32(stts_body, 0)
        stts_entries = [struct.unpack_from(">II", stts_body, 4 + 8 * i)
                        for i in range(stts_count)]
        stts_samples = sum(count for count, _ in stts_entries)
        stts_ticks = sum(count * delta for count, delta in stts_entries)
        check(f"{label}-stts-duration", stts_ticks == track_duration,
              f"stts 合计={stts_ticks} vs mdhd.duration={track_duration}")
        check(f"{label}-stts-deltas-positive", all(delta > 0 for _, delta in stts_entries),
              f"entries={stts_entries[:3]}{'...' if len(stts_entries) > 3 else ''}")

        _, _, stsz_body = fullbox_payload(data, table["stsz"][0])
        sample_size, stsz_count = u32(stsz_body, 0), u32(stsz_body, 4)
        sizes = [u32(stsz_body, 8 + 4 * i) for i in range(stsz_count)] if sample_size == 0 else []
        check(f"{label}-stsz-count", stsz_count == stts_samples,
              f"stsz.samples={stsz_count} vs stts.samples={stts_samples}")

        offset_kind = "co64" if "co64" in table else "stco"
        _, _, off_body = fullbox_payload(data, table[offset_kind][0])
        off_count = u32(off_body, 0)
        width = 8 if offset_kind == "co64" else 4
        fmt = ">Q" if width == 8 else ">I"
        offsets = [struct.unpack_from(fmt, off_body, 4 + width * i)[0] for i in range(off_count)]
        check(f"{label}-offset-count", off_count == stts_samples,
              f"{offset_kind}.chunks={off_count} vs samples={stts_samples}")
        check(f"{label}-offsets-in-mdat",
              bool(offsets) and all(data_start <= o < data_end for o in offsets),
              f"min={min(offsets) if offsets else '-'} max={max(offsets) if offsets else '-'} "
              f"mdat=[{data_start},{data_end})")
        if sizes:
            last_end = max(o + s for o, s in zip(offsets, sizes))
            check(f"{label}-samples-fit-mdat", last_end <= data_end,
                  f"末样本尾={last_end} <= mdat 尾={data_end}")
            total_sample_bytes += sum(sizes)
            if label == "video":
                check("video-stss-idr-only", "stss" in table, "stss 存在（只标 IDR）")
                if "stss" in table:
                    _, _, stss_body = fullbox_payload(data, table["stss"][0])
                    n = u32(stss_body, 0)
                    sync = [u32(stss_body, 4 + 4 * i) for i in range(n)]
                    check("video-stss-valid", bool(sync) and sync[0] == 1 and
                          all(1 <= s <= stts_samples for s in sync) and
                          all(a < b for a, b in zip(sync, sync[1:])),
                          f"同步样本={sync[:8]}{'...' if len(sync) > 8 else ''} / 总样本={stts_samples}")
            if "ctts" in table:
                version, _, ctts_body = fullbox_payload(data, table["ctts"][0])
                runs = [struct.unpack_from(">Ii", ctts_body, 4 + 8 * i)
                        if version == 1 else struct.unpack_from(">II", ctts_body, 4 + 8 * i)
                        for i in range(u32(ctts_body, 0))]
                ctts_samples = sum(count for count, _ in runs)
                check(f"{label}-ctts-count", ctts_samples == stts_samples,
                      f"version={version} ctts.samples={ctts_samples} vs {stts_samples}")

        _, _, stsc_body = fullbox_payload(data, table["stsc"][0])
        stsc_count = u32(stsc_body, 0)
        check(f"{label}-stsc-one-sample-per-chunk",
              stsc_count == 1 and u32(stsc_body, 8) == 1 and u32(stsc_body, 12) == 1,
              f"entries={stsc_count}（A/V 交错，每样本一个 chunk）")

    expected_data_bytes = data_end - data_start
    check("mdat-no-padding", total_sample_bytes == expected_data_bytes,
          f"样本字节合计={total_sample_bytes} vs mdat 数据区={expected_data_bytes}")

    print(f"RESULT: {passed} PASS / {failed} FAIL ; file={path.name}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())
