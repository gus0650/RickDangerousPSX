#!/usr/bin/env python3
"""
Convert xrick's data.zip into PlayStation assets.

Outputs (in OUTDIR):
  romfs.bin    - read-only file table with all game data files. Sound files
                 are stored as their 44-byte WAV header only (xrick reads just
                 the header to learn each sound's length).
  sndbank.bin  - all sound effects encoded as SPU ADPCM, one entry per file.
  trackN.wav   - music tunes as 44.1 kHz 16-bit stereo WAV for CD-DA tracks.
  tracks.txt   - mapping "name track" for the music tunes.

Usage: python3 -I mkassets.py data.zip OUTDIR
"""
import io
import os
import struct
import sys
import wave
import zipfile

import numpy as np

# Music tunes go to CD-DA tracks (track 1 is the data track).
TUNES = ["sounds/tune0.wav", "sounds/tune1.wav", "sounds/tune2.wav",
         "sounds/tune3.wav", "sounds/tune4.wav", "sounds/tune5.wav"]
NAME_LEN = 48

# ---------------------------------------------------------------- ADPCM ----
FILTERS = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]
# all (filter, shift) combinations, evaluated in parallel per block
_COMBOS = [(f, s) for f in range(5) for s in range(13)]
_C1 = np.array([FILTERS[f][0] for f, _ in _COMBOS], dtype=np.int64)
_C2 = np.array([FILTERS[f][1] for f, _ in _COMBOS], dtype=np.int64)
_SH = np.array([s for _, s in _COMBOS], dtype=np.int64)


def adpcm_encode(pcm, loop=False):
    """Encode int16 PCM to PS1 SPU ADPCM (16-byte blocks of 28 samples)."""
    pcm = np.asarray(pcm, dtype=np.int64)
    nblocks = (len(pcm) + 27) // 28
    pcm = np.concatenate([pcm, np.zeros(nblocks * 28 - len(pcm), np.int64)])
    out = bytearray()
    s1 = s2 = 0  # decoder history (shared, chosen combo's)
    for b in range(nblocks):
        blk = pcm[b * 28:(b + 1) * 28]
        h1 = np.full(len(_COMBOS), s1, np.int64)
        h2 = np.full(len(_COMBOS), s2, np.int64)
        err = np.zeros(len(_COMBOS), np.int64)
        nib = np.zeros((28, len(_COMBOS)), np.int64)
        for i in range(28):
            pred = (h1 * _C1 + h2 * _C2 + 32) >> 6
            diff = blk[i] - pred
            # decoded = ((n << 12) >> shift) + pred  =>  n = diff * 2^shift / 4096
            q = np.rint(diff * (2.0 ** _SH) / 4096.0).astype(np.int64)
            q = np.clip(q, -8, 7)
            dec = ((q << 12) >> _SH) + pred
            dec = np.clip(dec, -32768, 32767)
            err += (blk[i] - dec) ** 2
            nib[i] = q
            h2 = h1
            h1 = dec
        best = int(np.argmin(err))
        f, s = _COMBOS[best]
        s1, s2 = int(h1[best]), int(h2[best])
        flags = 0
        if b == nblocks - 1:
            flags = 3 if loop else 1          # loop end (+repeat) / end
        if loop and b == 0:
            flags |= 4                        # loop start
        out += bytes([(f << 4) | s, flags])
        n = nib[:, best] & 0xF
        for i in range(0, 28, 2):
            out.append(int(n[i]) | (int(n[i + 1]) << 4))
    return bytes(out)


def read_wav_u8(data):
    with wave.open(io.BytesIO(data)) as w:
        assert w.getsampwidth() == 1 and w.getnchannels() == 1, "expected 8-bit mono"
        rate = w.getframerate()
        raw = np.frombuffer(w.readframes(w.getnframes()), dtype=np.uint8)
    return rate, (raw.astype(np.int64) - 128) << 8


def write_cdda_wav(path, rate, pcm):
    n_out = int(len(pcm) * 44100 / rate)
    x = np.interp(np.arange(n_out) * rate / 44100.0, np.arange(len(pcm)), pcm)
    x = np.clip(np.rint(x * 0.9), -32768, 32767).astype("<i2")
    # pad to a whole number of CD sectors (588 stereo frames)
    pad = (-len(x)) % 588
    x = np.concatenate([x, np.zeros(pad, "<i2")])
    stereo = np.repeat(x, 2)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(stereo.tobytes())


def pack_name(name):
    b = name.encode()
    assert len(b) < NAME_LEN, name
    return b + b"\0" * (NAME_LEN - len(b))


def main():
    zpath, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    z = zipfile.ZipFile(zpath)
    names = sorted(n for n in z.namelist() if not n.endswith("/"))

    # ---- romfs
    entries, blob = [], bytearray()
    for n in names:
        data = z.read(n)
        if n.startswith("sounds/") and n.endswith(".wav"):
            data = data[:44]
        entries.append((n, len(blob), len(data)))
        blob += data
        blob += b"\0" * ((-len(blob)) % 4)
    hdr = struct.pack("<4sI", b"XRFS", len(entries))
    table_size = len(hdr) + len(entries) * (NAME_LEN + 8)
    out = bytearray(hdr)
    for n, off, size in entries:
        out += pack_name(n) + struct.pack("<II", table_size + off, size)
    out += blob
    open(os.path.join(outdir, "romfs.bin"), "wb").write(out)
    print(f"romfs: {len(entries)} files, {len(out)} bytes")

    # ---- sound bank + CD tracks
    sfx = [n for n in names if n.startswith("sounds/") and n.endswith(".wav")
           and n not in TUNES]
    bank_entries, bank = [], bytearray()
    for n in sfx:
        rate, pcm = read_wav_u8(z.read(n))
        enc = adpcm_encode(pcm)
        bank_entries.append((n, len(bank), len(enc), rate))
        bank += enc
        bank += b"\0" * ((-len(bank)) % 64)
    hdr = struct.pack("<4sI", b"XRSB", len(bank_entries))
    out = bytearray(hdr)
    for n, off, size, rate in bank_entries:
        out += pack_name(n) + struct.pack("<III", off, size, rate)
    out += b"\0" * ((-len(out)) % 64)
    out += bank
    open(os.path.join(outdir, "sndbank.bin"), "wb").write(out)
    print(f"sndbank: {len(bank_entries)} effects, {len(bank)} bytes of ADPCM")
    if len(bank) > 512 * 1024 - 0x1010:
        sys.exit("sound bank does not fit in SPU RAM")

    with open(os.path.join(outdir, "tracks.txt"), "w") as f:
        for i, n in enumerate(TUNES):
            rate, pcm = read_wav_u8(z.read(n))
            write_cdda_wav(os.path.join(outdir, f"track{i + 2}.wav"), rate, pcm)
            f.write(f"{n} {i + 2}\n")
    print(f"cdda: {len(TUNES)} tracks")


if __name__ == "__main__":
    main()
