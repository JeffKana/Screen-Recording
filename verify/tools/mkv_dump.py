#!/usr/bin/env python3
"""最小 EBML 结构 dump（调试用）：只打印前 N 个元素，逐行 flush，便于定位解析卡点。"""

import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)


def read_id(buf, pos):
    first = buf[pos]
    if first == 0:
        raise ValueError("zero id byte")
    length = 1
    mask = 0x80
    while not (first & mask):
        mask >>= 1
        length += 1
    value = 0
    for i in range(length):
        value = (value << 8) | buf[pos + i]
    return value, pos + length


def read_vint(buf, pos):
    first = buf[pos]
    if first == 0:
        raise ValueError("zero vint byte")
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


def main():
    path = sys.argv[1]
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 12
    data = open(path, "rb").read()
    print(f"size={len(data)}")
    pos = 0
    for index in range(limit):
        if pos + 2 > len(data):
            print("eof")
            break
        try:
            eid, after_id = read_id(data, pos)
            size, after_size, unknown = read_vint(data, after_id)
        except Exception as exc:  # noqa: BLE001
            print(f"[{index}] pos={pos} parse-error={exc}")
            break
        print(f"[{index}] pos={pos} id=0x{eid:X} size={size} unknown={unknown} header={after_size - pos}B")
        if unknown:
            pos = after_size  # 继续按「下一层」看
            continue
        pos = after_size + size


if __name__ == "__main__":
    main()
