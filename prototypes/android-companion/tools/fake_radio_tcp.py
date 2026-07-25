#!/usr/bin/env python3
"""Fake SmartSDR radio: TCP command channel on :4993 + VITA UDP out.

Speaks just enough wire protocol for spike phases 3-6: sends V + H on
accept, two slice statuses, replies R to every command, applies
"slice tune"/"slice set ... mode=" and broadcasts updated status. On
"stream create type=remote_audio_rx" it streams 600 Hz sine audio
(PCC 0x03E3); on "display panafall create" it emits pan status and
streams FFT frames (PCC 0x8003) whose peak tracks slice 0. Reach it
from the emulator guest via `adb reverse tcp:4993 tcp:4993`; VITA UDP
goes to host 24993, mapped into the guest's 14993 by
`adb emu 'redir add udp:24993:14993'` (see ../README.md). Logs
everything received.
"""
import math
import socket
import struct
import threading
import time

HOST, PORT = "0.0.0.0", 4993
HANDLE = "12345678"
AUDIO_STREAM_ID = 0x04000001
PAN_STREAM_ID = 0x40000001
# Emulator harness: guest binds VitaStream::kLocalPort (14993);
# `adb emu redir add udp:24993:14993` maps host 24993 onto it.
AUDIO_DEST = ("127.0.0.1", 24993)

pan = {"center": 14.100, "bandwidth": 0.200}


def audio_sender(stop_event, opus=False):
    """600 Hz sine at 24 kHz stereo as VITA ExtData packets.

    PCM mode: float32 big-endian, PCC 0x03E3, 256 samples/packet.
    Opus mode: one 240-sample (10 ms) Opus frame per packet, PCC 0x8005
    (requires `pip install opuslib` + a libopus on the loader path).
    """
    sample_rate = 24000
    samples_per_pkt = 240 if opus else 256
    encoder = None
    if opus:
        import opuslib  # noqa: deferred so PCM mode needs no libopus
        encoder = opuslib.Encoder(sample_rate, 2, "audio")
    phase = 0.0
    step = 2 * math.pi * 600 / sample_rate
    seq = 0
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    period = samples_per_pkt / sample_rate
    print(f"audio sender ({'opus' if opus else 'pcm'}) -> {AUDIO_DEST}", flush=True)
    while not stop_event.is_set():
        samples = []
        for _ in range(samples_per_pkt):
            v = 0.3 * math.sin(phase)
            phase += step
            samples += [v, v]  # stereo
        if opus:
            pcm16 = struct.pack(f"<{len(samples)}h",
                                *(int(s * 32767) for s in samples))
            payload = encoder.encode(pcm16, samples_per_pkt)
            pcc = 0x534C8005
        else:
            payload = struct.pack(f">{len(samples)}f", *samples)
            pcc = 0x534C03E3
        word0 = (0x3 << 28) | (0x1 << 24) | ((seq & 0xF) << 16) | ((28 + len(payload)) // 4)
        header = struct.pack(">IIIIIII", word0, AUDIO_STREAM_ID,
                             0x00001C2D, pcc, 0, 0, 0)
        sock.sendto(header + payload, AUDIO_DEST)
        seq += 1
        time.sleep(period)

slices = {
    0: {"in_use": 1, "RF_frequency": 14.074000, "mode": "USB"},
    1: {"in_use": 1, "RF_frequency": 7.155000, "mode": "LSB"},
}


METER_DEFS = {7: 0, 8: 1}  # meter index -> slice num (SLC/LEVEL, dBm)


def meter_def_status():
    parts = []
    for idx, num in METER_DEFS.items():
        parts.append(f"{idx}.src=SLC#{idx}.num={num}#{idx}.nam=LEVEL"
                     f"#{idx}.unit=dBm#{idx}.low=-150.0#{idx}.hi=20.0")
    return f"S{HANDLE}|meter {'#'.join(parts)}\n"


def meter_sender(stop_event):
    """Meter VITA packets (PCC 0x8002): wobbling S-meter per slice,
    dBm raw = value * 128."""
    t = 0
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(f"meter sender -> {AUDIO_DEST}", flush=True)
    while not stop_event.is_set():
        payload = b""
        for idx, num in METER_DEFS.items():
            dbm = -85 + 12 * math.sin(t * 0.7 + num * 2)
            payload += struct.pack(">Hh", idx, int(dbm * 128))
        word0 = (0x3 << 28) | (0x1 << 24) | ((t & 0xF) << 16) | ((28 + len(payload)) // 4)
        header = struct.pack(">IIIIIII", word0, 0x00000700,
                             0x00001C2D, 0x534C8002, 0, 0, 0)
        sock.sendto(header + payload, AUDIO_DEST)
        t += 1
        stop_event.wait(0.25)


def pan_status():
    return (f"S{HANDLE}|display pan 0x{PAN_STREAM_ID:08X} "
            f"center={pan['center']:.6f} bandwidth={pan['bandwidth']:.6f} "
            f"x_pixels=512 y_pixels=200\n")


def fft_sender(stop_event):
    """FFT frames: 512 u16 bins (y-pixel from top, ypixels=200), a noise
    floor with a peak that tracks slice 0's frequency, 2 packets/frame,
    ~20 fps."""
    total_bins, ypix = 512, 200
    seq = 0
    frame_index = 0
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(f"fft sender -> {AUDIO_DEST}", flush=True)
    while not stop_event.is_set():
        f0 = pan["center"] - pan["bandwidth"] / 2
        peak_bin = int((slices[0]["RF_frequency"] - f0) / pan["bandwidth"] * total_bins)
        bins = []
        for i in range(total_bins):
            floor = 170 + int(10 * math.sin(i * 0.37 + frame_index * 0.5))
            if 0 <= peak_bin < total_bins:
                d = abs(i - peak_bin)
                if d < 12:
                    floor = min(floor, 30 + d * 10)
            bins.append(max(0, min(ypix - 1, floor)))
        half = total_bins // 2
        for start in (0, half):
            chunk = bins[start:start + half]
            sub = struct.pack(">HHHHI", start, len(chunk), 2, total_bins, frame_index)
            payload = struct.pack(f">{len(chunk)}H", *chunk)
            word0 = (0x3 << 28) | (0x1 << 24) | ((seq & 0xF) << 16) | (
                (28 + len(sub) + len(payload)) // 4)
            header = struct.pack(">IIIIIII", word0, PAN_STREAM_ID,
                                 0x00001C2D, 0x534C8003, 0, 0, 0)
            sock.sendto(header + sub + payload, AUDIO_DEST)
            seq += 1
        frame_index += 1
        stop_event.wait(0.05)


def slice_status(sid):
    s = slices[sid]
    return (f"S{HANDLE}|slice {sid} in_use={s['in_use']} "
            f"RF_frequency={s['RF_frequency']:.6f} mode={s['mode']}\n")


def serve(conn, addr):
    print(f"client connected: {addr}", flush=True)
    audio_stop = threading.Event()
    conn.sendall(b"V1.4.0.0\n")
    conn.sendall(f"H{HANDLE}\n".encode())
    for sid in slices:
        conn.sendall(slice_status(sid).encode())
    buf = b""
    while True:
        data = conn.recv(4096)
        if not data:
            break
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode(errors="replace").strip()
            if not text:
                continue
            print(f"RX: {text}", flush=True)
            if not text.startswith("C"):
                continue
            seq, _, cmd = text[1:].partition("|")
            if cmd == "sub meter all":
                conn.sendall(f"R{seq}|0|\n".encode())
                conn.sendall(meter_def_status().encode())
                threading.Thread(target=meter_sender, args=(audio_stop,),
                                 daemon=True).start()
                continue
            if cmd.startswith("stream create type=remote_audio_rx"):
                use_opus = "compression=opus" in cmd
                conn.sendall(f"R{seq}|0|{AUDIO_STREAM_ID:X}\n".encode())
                threading.Thread(target=audio_sender,
                                 args=(audio_stop, use_opus),
                                 daemon=True).start()
                continue
            if cmd.startswith("display panafall create"):
                conn.sendall(
                    f"R{seq}|0|0x{PAN_STREAM_ID:08X},0x42000001\n".encode())
                conn.sendall(pan_status().encode())
                threading.Thread(target=fft_sender, args=(audio_stop,),
                                 daemon=True).start()
                continue
            if cmd.startswith("display pan set"):
                for kv in cmd.split()[3:]:
                    k, _, v = kv.partition("=")
                    if k in ("center", "bandwidth"):
                        pan[k] = float(v)
                conn.sendall(f"R{seq}|0|\n".encode())
                conn.sendall(pan_status().encode())
                continue
            if cmd.startswith("display pan remove"):
                audio_stop.set()
                conn.sendall(f"R{seq}|0|\n".encode())
                continue
            if cmd.startswith("stream remove"):
                audio_stop.set()
                conn.sendall(f"R{seq}|0|\n".encode())
                continue
            conn.sendall(f"R{seq}|0|\n".encode())
            parts = cmd.split()
            if parts[:2] == ["slice", "tune"] and len(parts) >= 4:
                sid = int(parts[2])
                slices[sid]["RF_frequency"] = float(parts[3])
                conn.sendall(slice_status(sid).encode())
            elif parts[:2] == ["slice", "set"] and len(parts) >= 4:
                sid = int(parts[2])
                for kv in parts[3:]:
                    k, _, v = kv.partition("=")
                    if k == "mode":
                        slices[sid]["mode"] = v
                conn.sendall(slice_status(sid).encode())
    audio_stop.set()
    print("client disconnected", flush=True)


srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((HOST, PORT))
srv.listen(1)
print(f"fake radio TCP on {PORT}", flush=True)
while True:
    c, a = srv.accept()
    threading.Thread(target=serve, args=(c, a), daemon=True).start()
