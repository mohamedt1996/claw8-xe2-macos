#!/usr/bin/env python3
"""Host-side check of a GuC firmware blob using the same rules as ClawXe2::gucParseFw()
(Linux xe parse_css_header + guc_read_css_info). Run in CI so a bad blob never ships.

usage: check-guc-fw.py firmware/lnl_guc_70.bin
"""
import struct
import sys

CSS_FMT = "<32I"  # struct uc_css_header, 128 bytes


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    blob = open(sys.argv[1], "rb").read()
    if len(blob) < 128:
        print("FAIL: blob smaller than CSS header")
        return 1
    d = struct.unpack(CSS_FMT, blob[:128])
    header_size_dw, size_dw = d[1], d[6]
    key_dw, mod_dw, exp_dw = d[7], d[8], d[9]
    # guc_info starts at dword 10: time, reserved0[5], sw_version, submission_version,
    # reserved1[11], header_info, private_data_size, ukernel_info
    sw_version, sub_version = d[16], d[17]
    private_data_size, ukernel_info = d[30], d[31]

    hdr = (header_size_dw - key_dw - mod_dw - exp_dw) * 4
    ucode = (size_dw - header_size_dw) * 4
    rsa = key_dw * 4
    ver = ((sw_version >> 16) & 0xFF, (sw_version >> 8) & 0xFF, sw_version & 0xFF)
    sub = ((sub_version >> 16) & 0xFF, (sub_version >> 8) & 0xFF, sub_version & 0xFF)
    build = ("prod", "preprod", "debug", "?")[(ukernel_info >> 2) & 3]

    print(f"file          {sys.argv[1]} ({len(blob)} bytes)")
    print(f"version       {ver[0]}.{ver[1]}.{ver[2]}  (submission {sub[0]}.{sub[1]}.{sub[2]})")
    print(f"build         {build}")
    print(f"header        {hdr} bytes, ucode {ucode} bytes, rsa {rsa} bytes")
    print(f"private data  {private_data_size} bytes")
    print(f"rsa delivery  {'GGTT address' if rsa > 256 else 'UOS_RSA_SCRATCH registers'}")

    errors = []
    if hdr != 128:
        errors.append(f"unexpected CSS header size {hdr}")
    if len(blob) < 128 + ucode + rsa:
        errors.append("blob truncated (header + ucode + rsa)")
    if (ver[0] << 16 | ver[1] << 8 | ver[2]) < (70 << 16 | 29 << 8 | 2):
        errors.append("GuC older than 70.29.2")
    if ver[0] != 70:
        errors.append("major version is not 70 (lnl_guc_70.bin expected)")
    if rsa <= 256 and rsa < 256:
        errors.append("RSA key smaller than the 256-byte scratch copy")
    upload = 128 + ucode
    wopcm = ((8 << 20) - (36 << 10) - (16 << 10)) & 0xFFFFF000
    if upload + (16 << 10) + (8 << 10) > wopcm:
        errors.append("firmware does not fit the GuC WOPCM partition")

    for e in errors:
        print(f"FAIL: {e}")
    print("OK" if not errors else "FAILED")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
