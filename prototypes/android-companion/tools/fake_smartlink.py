#!/usr/bin/env python3
"""Fake SmartLink broker + TLS radio for the spike's WAN path.

Broker on TLS :14443 (self-signed, generated on first run):
  - accepts "application register ... token=..." (any token)
  - pushes a one-radio "radio list" whose public_ip/public_tls_port
    point at the TLS radio front below
  - answers "application connect serial=..." with
    "radio connect_ready handle=<h> serial=<s>"

TLS radio front on :14994: accepts the TLS session, expects
"C1|wan validate handle=..." first, then proxies the byte stream to a
plain fake_radio_tcp.py on 127.0.0.1:4993 (run it alongside).

Emulator wiring (the app sees everything on guest loopback):
  adb reverse tcp:14443 tcp:14443    # broker
  adb reverse tcp:14994 tcp:14994    # TLS radio front
  adb reverse tcp:4992  tcp:4993     # (manual-connect path, optional)
In the app, enter "127.0.0.1:14443" in the SmartLink email field and
press Login (harness mode — no Auth0).
"""
import socket
import ssl
import subprocess
import threading
from pathlib import Path

BROKER_PORT = 14443
RADIO_TLS_PORT = 14994
RADIO_PLAIN = ("127.0.0.1", 4993)
WAN_HANDLE = "00ABCDEF"
SERIAL = "1234-5678-9012-3456"

CERT_DIR = Path(__file__).parent / ".harness-certs"


def ensure_cert():
    cert, key = CERT_DIR / "cert.pem", CERT_DIR / "key.pem"
    if not cert.exists():
        CERT_DIR.mkdir(exist_ok=True)
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", str(key), "-out", str(cert), "-days", "30",
            "-subj", "/CN=fake-smartlink-harness",
        ], check=True, capture_output=True)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(str(cert), str(key))
    return ctx


def radio_list_line():
    entry = (f"serial={SERIAL}#model=FLEX-6600#nickname=WanFlex"
             f"#callsign=KK7GWY#status=Available"
             f"#public_ip=127.0.0.1#public_tls_port={RADIO_TLS_PORT}")
    return f"radio list {entry}\n"


def serve_broker(conn):
    print("broker: client connected", flush=True)
    buf = b""
    while True:
        data = conn.recv(4096)
        if not data:
            break
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode(errors="replace").strip()
            if not text or text == "ping":
                continue
            print(f"broker RX: {text}", flush=True)
            if text.startswith("application register"):
                conn.sendall(radio_list_line().encode())
            elif text.startswith("application connect"):
                conn.sendall(
                    f"radio connect_ready handle={WAN_HANDLE} "
                    f"serial={SERIAL}\n".encode())
    print("broker: client disconnected", flush=True)


def serve_radio_front(conn):
    """Expect wan validate, ack it, then splice to the plain fake radio."""
    print("radio-front: TLS client connected", flush=True)
    upstream = socket.create_connection(RADIO_PLAIN)
    # Swallow the upstream greeting (V/H/slice statuses) and replay it
    # to the client after wan validate, preserving the desktop ordering
    # where the radio talks only after the TLS session is up.
    def pump(src, dst, tag):
        try:
            while True:
                data = src.recv(4096)
                if not data:
                    break
                if tag == "c2r":
                    for rawline in data.split(b"\n"):
                        if rawline.strip():
                            print(f"radio-front RX: {rawline.decode(errors='replace')}",
                                  flush=True)
                dst.sendall(data)
        except OSError:
            pass
        finally:
            try:
                dst.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    threading.Thread(target=pump, args=(conn, upstream, "c2r"),
                     daemon=True).start()
    pump(upstream, conn, "r2c")
    print("radio-front: session ended", flush=True)


def listen(port, ctx, handler):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(2)
    print(f"listening (TLS) on {port}", flush=True)
    while True:
        raw, _ = srv.accept()
        try:
            conn = ctx.wrap_socket(raw, server_side=True)
        except ssl.SSLError as e:
            print(f"TLS handshake failed on {port}: {e}", flush=True)
            raw.close()
            continue
        threading.Thread(target=handler, args=(conn,), daemon=True).start()


ctx = ensure_cert()
threading.Thread(target=listen, args=(BROKER_PORT, ctx, serve_broker),
                 daemon=True).start()
listen(RADIO_TLS_PORT, ctx, serve_radio_front)
