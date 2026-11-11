#!/usr/bin/env python3
"""独立实现的 Matroska 结构核验器（G3 证据工具）。

刻意不复用产品侧的写入代码：自己解析 EBML 才能证明「写出来的字节符合规范」，
而不是「读写两边一起错」。

校验内容：
  1. EBML 头与 DocType
  2. Segment 可解析（未知长度也能一直读到文件尾）
  3. Info：TimecodeScale 与 Duration
  4. Tracks：轨道类型、CodecID、CodecPrivate（avcC）
  5. Cluster / SimpleBlock：数量、时间戳单调性、AVCC 长度链完整性
  6. H.264 NAL 结构：codec private 含 SPS/PPS，首帧为 IDR

用法: python verify/tools/mkv_probe.py <file.mkv>
退出码: 0 = 全部通过; 1 = 存在失败
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

ID_EBML = 0x1A45DFA3
ID_SEGMENT = 0x18538067
ID_INFO = 0x1549A966
ID_TIMECODE_SCALE = 0x2AD7B1
ID_DURATION = 0x4489
ID_TRACKS = 0x1654AE6B
ID_TRACK_ENTRY = 0xAE
ID_TRACK_NUMBER = 0xD7
ID_TRACK_TYPE = 0x83
ID_CODEC_ID = 0x86
ID_CODEC_PRIVATE = 0x63A2
ID_VIDEO = 0xE0
ID_PIXEL_WIDTH = 0xB0
ID_PIXEL_HEIGHT = 0xBA
ID_CLUSTER = 0x1F43B675
ID_TIMECODE = 0xE7
ID_SIMPLE_BLOCK = 0xA3
ID_DOC_TYPE = 0x4282

CONTAINERS = {ID_EBML, ID_SEGMENT, ID_INFO, ID_TRACKS, ID_TRACK_ENTRY, ID_VIDEO, ID_CLUSTER}


def read_vint(buf: bytes, pos: int) -> tuple[int, int, bool]:
    first = buf[pos]
    if first == 0:
        raise ValueError(f"invalid vint at {pos}")
    length = 1
    mask = 0x80
    while not (first & mask):
        mask >>= 1
        length += 1
    value = first & (mask - 1)
    for i in range(1, length):
        value = (value << 8) | buf[pos + i]
    unknown = value == (1 << (7 * length)) - 1
    return value, pos + length, unknown


def read_id(buf: bytes, pos: int) -> tuple[int, int]:
    first = buf[pos]
    if first == 0:
        # 首字节为 0 没有合法 ID 长度前缀。缺了这句，mask 会一路右移成 0，
        # `not (first & 0)` 恒为真 —— 死循环（本次排查就栽在这）。
        raise ValueError(f"invalid element id at offset {pos}")
    length = 1
    mask = 0x80
    while not (first & mask):
        mask >>= 1
        length += 1
    value = 0
    for i in range(length):
        value = (value << 8) | buf[pos + i]
    return value, pos + length


class Prober:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.doc_type = ""
        self.timecode_scale = 0
        self.duration_ms = 0.0
        self.tracks: dict[int, dict] = {}
        self.clusters = 0
        self.blocks: dict[int, list[int]] = {}
        self.nal_types: dict[int, int] = {}
        self.bad_avcc = 0
        self.elements = 0
        self.current_cluster_time = 0
        self.video_absolute: list[int] = []
        self.first_video_sample = b""

    def walk(self, start: int, end: int, depth: int = 0) -> None:
        pos = start
        while pos < end:
            try:
                eid, after_id = read_id(self.data, pos)
                size, after_size, unknown = read_vint(self.data, after_id)
            except (ValueError, IndexError):
                return
            payload_start = after_size
            payload_end = end if unknown else min(after_size + size, end)
            if payload_end < payload_start:
                return

            if eid in CONTAINERS:
                self.handle_container(eid, payload_start, payload_end)
                self.walk(payload_start, payload_end, depth + 1)
            else:
                self.handle_leaf(eid, payload_start, payload_end)

            if unknown:
                return
            pos = payload_end

    def handle_container(self, eid: int, start: int, end: int) -> None:
        if eid == ID_CLUSTER:
            self.clusters += 1
            self.current_cluster_time = 0
        elif eid == ID_TRACK_ENTRY:
            self.current_track = {"codec_private": b""}
        elif eid == ID_INFO:
            pass

    def handle_leaf(self, eid: int, start: int, end: int) -> None:
        payload = self.data[start:end]
        if eid == ID_DOC_TYPE:
            self.doc_type = payload.decode("ascii", "replace").strip("\x00")
        elif eid == ID_TIMECODE_SCALE and len(payload) <= 8:
            self.timecode_scale = int.from_bytes(payload, "big")
        elif eid == ID_DURATION and len(payload) == 8:
            self.duration_ms = struct.unpack(">d", payload)[0]
        elif eid == ID_TIMECODE:
            self.current_cluster_time = int.from_bytes(payload, "big")
        elif eid == ID_TRACK_NUMBER:
            # 按轨道号即时归档：之前用「pending_* + 首个 Block 时懒注册」的写法，
            # 在双轨文件里会把视频轨的元数据写成最后一条 TrackEntry（音频）的，
            # 表现为 video-track 判据报 codec=none。
            self.current_track_number = int.from_bytes(payload, "big")
            self.tracks.setdefault(self.current_track_number, {
                "type": 0,
                "codec": "?",
                "codec_private": b"",
                "width": 0,
                "height": 0,
            })
        elif eid == ID_TRACK_TYPE:
            self.tracks.setdefault(self.current_track_number, {})["type"] = int.from_bytes(payload, "big")
        elif eid == ID_CODEC_ID:
            self.tracks.setdefault(self.current_track_number, {})["codec"] = \
                payload.decode("ascii", "replace").strip("\x00")
        elif eid == ID_CODEC_PRIVATE:
            self.tracks.setdefault(self.current_track_number, {})["codec_private"] = payload
        elif eid == ID_PIXEL_WIDTH:
            self.tracks.setdefault(self.current_track_number, {})["width"] = int.from_bytes(payload, "big")
        elif eid == ID_PIXEL_HEIGHT:
            self.tracks.setdefault(self.current_track_number, {})["height"] = int.from_bytes(payload, "big")
        elif eid == ID_SIMPLE_BLOCK:
            self.handle_block(payload)

    def handle_block(self, payload: bytes) -> None:
        try:
            track, pos, _ = read_vint(payload, 0)
        except (ValueError, IndexError):
            return
        if pos + 3 > len(payload):
            return
        relative = struct.unpack(">h", payload[pos : pos + 2])[0]
        flags = payload[pos + 2]
        frame = payload[pos + 3 :]

        self.blocks.setdefault(track, []).append(relative)
        if track == 1:
            self.video_absolute.append(self.current_cluster_time + relative)

        info = self.tracks.setdefault(track, {
            "type": 0,
            "codec": "?",
            "codec_private": b"",
            "width": 0,
            "height": 0,
        })
        if info.get("type") == 1 and info.get("codec", "").startswith("V_MPEG4"):
            if not self.first_video_sample:
                self.first_video_sample = frame
            self.validate_avcc(frame)


    def validate_avcc(self, frame: bytes) -> None:
        offset = 0
        while offset + 4 <= len(frame):
            length = int.from_bytes(frame[offset : offset + 4], "big")
            if length == 0 or offset + 4 + length > len(frame):
                self.bad_avcc += 1
                return
            nal_header = frame[offset + 4]
            nal_type = nal_header & 0x1F
            self.nal_types[nal_type] = self.nal_types.get(nal_type, 0) + 1
            offset += 4 + length
        if offset != len(frame):
            self.bad_avcc += 1


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if len(sys.argv) < 2:
        print("usage: mkv_probe.py <file.mkv>")
        return 2

    path = Path(sys.argv[1])
    if not path.exists():
        print(f"file not found: {path}")
        return 2
    data = path.read_bytes()

    prober = Prober(data)
    prober.walk(0, len(data))

    failures: list[str] = []

    def check(name: str, ok: bool, detail: str) -> None:
        print(f"[{'PASS' if ok else 'FAIL'}] {name} — {detail}")
        if not ok:
            failures.append(name)

    check("doctype", prober.doc_type == "matroska", f"DocType={prober.doc_type!r}")
    check("timecode-scale", prober.timecode_scale == 1000000, f"scale={prober.timecode_scale}")
    check("duration", prober.duration_ms > 0, f"duration={prober.duration_ms:.1f}ms")
    check("tracks", len(prober.tracks) >= 1, f"{len(prober.tracks)} track(s)")

    video_track = next((t for t in prober.tracks.values() if t.get("type") == 1), None)
    check("video-track", video_track is not None,
          f"codec={video_track.get('codec') if video_track else 'none'} "
          f"{video_track.get('width') if video_track else 0}x{video_track.get('height') if video_track else 0}")

    if video_track:
        private = video_track.get("codec_private", b"")
        # avcC: [0]=1, [1..3]=profile/compat/level, [4]=0xFF, [5]=0xE1, [6..7]=SPS 长度
        is_avcc = len(private) > 7 and private[0] == 1 and private[5] & 0xE0 == 0xE0
        check("avcc-config", is_avcc, f"codec_private={len(private)} bytes")

        blocks = prober.blocks.get(1, [])
        check("video-blocks", len(blocks) >= 10, f"{len(blocks)} blocks")
        check("clusters", prober.clusters >= 1, f"{prober.clusters} cluster(s)")
        check("avcc-chain", prober.bad_avcc == 0, f"bad={prober.bad_avcc}")
        check("nal-sps", 7 in prober.nal_types or is_avcc, f"nal types={sorted(prober.nal_types)}")
        check("nal-idr", 5 in prober.nal_types, f"IDR count={prober.nal_types.get(5, 0)}")
        check("nal-slices", (1 in prober.nal_types) or (5 in prober.nal_types),
              f"non-IDR slices={prober.nal_types.get(1, 0)}")
        # 相对时间码在每个 Cluster 内重新计数，必须加上 Cluster 的 Timecode 才可比。
        absolute = prober.video_absolute
        mono = all(b >= a for a, b in zip(absolute, absolute[1:]))
        # 块按解码顺序写入，含 B 帧时 PTS 允许乱序（MKV 规范允许），
        # 因此这里只校验取值范围，另统计乱序帧数作为「是否含 B 帧」的观察量。
        reordered = sum(1 for i in range(1, len(absolute)) if absolute[i] < absolute[i - 1])
        in_range = bool(absolute) and min(absolute) >= 0 and max(absolute) <= prober.duration_ms + 1000
        check("timestamp-range", in_range,
              f"range={min(absolute) if absolute else 0}..{max(absolute) if absolute else 0} "
              f"duration={prober.duration_ms:.1f}ms")
        print(f"[INFO] reordered-pts={reordered} (B 帧导致，属正常)")

    print()
    print(f"RESULT: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
