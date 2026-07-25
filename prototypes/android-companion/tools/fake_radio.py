#!/usr/bin/env python3
"""Send synthetic SmartSDR discovery datagrams into the emulator.

Harness wiring (see ../README.md):
  adb emu 'redir add udp:14992:4992'   # host 14992 -> guest 4992
Host port is 14992 because a running desktop AetherSDR owns UDP 4992.
The advertised ip=127.0.0.1 port=4993 pairs with fake_radio_tcp.py
through an `adb reverse tcp:4993 tcp:4993` tunnel. Field set mirrors
what src/core/RadioDiscovery.cpp parses.
"""
import socket
import time

PAYLOAD = (
    "discovery_protocol_version=3.0.0.1 model=FLEX-6600 serial=1234-5678-9012-3456 "
    "version=3.8.23 nickname=EmuFlex callsign=KK7GWY ip=127.0.0.1 port=4993 "
    "status=Available inuse_ip= inuse_host= max_licensed_version=v3 "
    "radio_license_id=00-1C-2D-00-00-00 requires_additional_license=0 "
    "fpc_mac= wan_connected=1 licensed_clients=2 available_clients=2 "
    "max_panadapters=4 available_panadapters=4 max_slices=4 available_slices=4 "
    "gui_client_ips= gui_client_hosts= gui_client_programs= gui_client_stations= "
    "gui_client_handles= mf_enable=1"
).encode()

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
for i in range(30):
    sock.sendto(PAYLOAD, ("127.0.0.1", 14992))
    print(f"sent datagram {i + 1}")
    time.sleep(1)
