#!/usr/bin/env python3
"""
Decode raw capture file produced by xnet-bmd-capture.

File format:
    [4 bytes: packet length (little-endian)]
    [N bytes: raw MoldUDP64 packet]
    ... repeats ...

MoldUDP64 packet layout:
    Bytes  0-9:   Session ID (10 bytes ASCII)
    Bytes 10-17:  Sequence Number (8 bytes big-endian)
    Bytes 18-19:  Message Count (2 bytes big-endian)
    Bytes 20+:    Message blocks (2-byte length prefix + ITCH body)

Usage:
    python3 tools/decode_raw.py data/XNET_BMD_2026-04-23.raw
    python3 tools/decode_raw.py data/XNET_BMD_2026-04-23.raw --limit 10
    python3 tools/decode_raw.py data/XNET_BMD_2026-04-23.raw --hex
"""

import struct
import sys
import os

def decode_file(filepath, limit=0, show_hex=False):
    file_size = os.path.getsize(filepath)
    print(f"File: {filepath}")
    print(f"Size: {file_size} bytes")
    print()

    with open(filepath, "rb") as f:
        packet_num = 0

        while True:
            # Read 4-byte length prefix
            header = f.read(4)
            if len(header) < 4:
                break

            packet_length = struct.unpack("<I", header)[0]

            # Read raw packet
            packet = f.read(packet_length)
            if len(packet) < packet_length:
                break

            packet_num += 1

            # Parse MoldUDP64 header
            if packet_length < 20:
                print(f"[{packet_num}] too short: {packet_length} bytes")
                continue

            session   = packet[0:10].decode("ascii", errors="replace").rstrip()
            seq_num   = struct.unpack(">Q", packet[10:18])[0]
            msg_count = struct.unpack(">H", packet[18:20])[0]

            print(f"[{packet_num}] session={session} seq={seq_num} msgs={msg_count} pkt_len={packet_length}")

            # Parse each ITCH message inside the packet
            offset = 20
            for msg_idx in range(msg_count):
                if offset + 2 > packet_length:
                    print(f"  msg {msg_idx}: truncated (no length field)")
                    break

                msg_len = struct.unpack(">H", packet[offset:offset+2])[0]
                offset += 2

                if offset + msg_len > packet_length:
                    print(f"  msg {msg_idx}: truncated (need {msg_len}, have {packet_length - offset})")
                    break

                msg_body = packet[offset:offset+msg_len]
                offset += msg_len

                # First byte of ITCH message is the message type
                msg_type = chr(msg_body[0]) if msg_len > 0 else "?"

                print(f"  msg {msg_idx}: type='{msg_type}' len={msg_len}")

                if show_hex and msg_len > 0:
                    hex_str = " ".join(f"{b:02X}" for b in msg_body)
                    print(f"    hex: {hex_str}")

            if limit > 0 and packet_num >= limit:
                print(f"\n... stopped at {limit} packets")
                break

    print(f"\nTotal: {packet_num} packets")

def main():
    if len(sys.argv) < 2:
        print("Usage: decode_raw.py <file.raw> [--limit N] [--hex]")
        sys.exit(1)

    filepath = sys.argv[1]
    limit = 0
    show_hex = False

    i = 2
    while i < len(sys.argv):
        if sys.argv[i] == "--limit" and i + 1 < len(sys.argv):
            limit = int(sys.argv[i + 1])
            i += 2
        elif sys.argv[i] == "--hex":
            show_hex = True
            i += 1
        else:
            i += 1

    if not os.path.exists(filepath):
        print(f"File not found: {filepath}")
        sys.exit(1)

    decode_file(filepath, limit, show_hex)

if __name__ == "__main__":
    main()
