# SPDX-License-Identifier: MIT
"""Align hvcc 0.17.2 LightPipe payloads for its 64-bit message structures.

The upstream queue prefixes payloads with four bytes, but ReceiverMessagePair
and HvMessage require eight-byte alignment on 64-bit hosts. Keep the original
length in the header; align both the header slot and the next record boundary.
Original generated runtime copyright/license notices are preserved.
"""
from pathlib import Path


def patch_light_pipe(path: Path):
    text = path.read_text()
    if 'HLP_ALIGN_BYTES' in text:
        return
    anchor = '#define HLP_STOP 0'
    if text.count(anchor) != 1 or text.count('sizeof(hv_uint32_t)') != 9:
        raise RuntimeError(f'Unexpected hvcc LightPipe layout: {path}')
    text = text.replace(anchor, '''// heavyverb compatibility fix: align messages containing doubles/pointers.
#define HLP_ALIGN_BYTES 8U
#define HLP_PAD(n) (((n) + HLP_ALIGN_BYTES - 1U) & ~(HLP_ALIGN_BYTES - 1U))
#define HLP_STOP 0''')
    text = text.replace('sizeof(hv_uint32_t)', 'HLP_ALIGN_BYTES')
    text = text.replace('bytesToWrite + 2*HLP_ALIGN_BYTES', 'HLP_PAD(bytesToWrite) + 2*HLP_ALIGN_BYTES')
    text = text.replace('HLP_ALIGN_BYTES + bytesToWrite', 'HLP_ALIGN_BYTES + HLP_PAD(bytesToWrite)')
    text = text.replace('numBytes + 2*HLP_ALIGN_BYTES', 'HLP_PAD(numBytes) + 2*HLP_ALIGN_BYTES')
    text = text.replace('HLP_ALIGN_BYTES + numBytes', 'HLP_ALIGN_BYTES + HLP_PAD(numBytes)')
    text = text.replace('HLP_ALIGN_BYTES + HLP_GET_UINT32_AT_BUFFER(q->readHead)',
                        'HLP_ALIGN_BYTES + HLP_PAD(HLP_GET_UINT32_AT_BUFFER(q->readHead))')
    path.write_text(text)
