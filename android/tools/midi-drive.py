#!/usr/bin/env python3
"""Drive BiteDJ as if a Pioneer DDJ-FLX4 were plugged in.

The app can expose its MIDI input on loopback for testing without hardware.
Enable it on the device, forward the port, then send FLX4 messages from here:

    # 1. Allow the app to create the virtual device (127.0.0.1 only):
    #    [Controller] in /storage/emulated/0/bitedj/mixxx.cfg
    #        VirtualMidiPort 7700
    # 2. adb forward tcp:7700 tcp:7700
    # 3. python midi-drive.py play:1
    #      cue:1 play:1 tempo:1,0,0 tempo:1,2,0
    #      jog:1,300 filter:96 pause:1

Addresses come from res/controllers/Pioneer-DDJ-FLX4.midi.xml:
  deck play    90+ch 0B 7F          deck cue    90+ch 0C 7F
  tempo fader  B0+ch 00 (MSB), 20 (LSB)     filter  B6 17 (14-bit MSB)
  jog wheel    B0+ch 22, value 64 = still, <64 back, >64 forward

Note that the FLX4 script enables soft takeover on "rate": the fader is
ignored until it first matches the current value (send tempo ...,0,0 before
moving it), which is the same pickup behaviour as the real hardware.
"""
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = 7700


def connect():
    sock = socket.create_connection((HOST, PORT), timeout=5)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return sock


def note(sock, deck, control, value=0x7F):
    sock.sendall(bytes([0x90 | (deck - 1), control, value]))


def cc(sock, deck, control, value):
    sock.sendall(bytes([0xB0 | (deck - 1), control, value]))


def send_tempo(sock, deck, msb, lsb=0):
    cc(sock, deck, 0x00, msb)
    cc(sock, deck, 0x20, lsb)


def send_jog(sock, deck, ticks):
    step = 1 if ticks > 0 else -1
    for _ in range(abs(ticks)):
        cc(sock, deck, 0x22, 64 + step)
        time.sleep(0.01)
    cc(sock, deck, 0x22, 64)


def send_filter(sock, msb):
    sock.sendall(bytes([0xB6, 0x17, msb]))


def run(steps):
    sock = connect()
    try:
        for step in steps:
            name, _, args = step.partition(":")
            parts = [int(p) for p in args.split(",") if p]
            if name == "play":
                note(sock, parts[0], 0x0B)
            elif name == "pause":
                note(sock, parts[0], 0x0B, 0x00)
            elif name == "cue":
                note(sock, parts[0], 0x0C)
            elif name == "tempo":
                send_tempo(sock, parts[0], parts[1], parts[2] if len(parts) > 2 else 0)
            elif name == "filter":
                send_filter(sock, parts[0])
            elif name == "jog":
                send_jog(sock, parts[0], parts[1])
            elif name == "wait":
                time.sleep(parts[0] / 1000.0)
            else:
                raise SystemExit(f"unknown step {step}")
            print(step)
            time.sleep(0.05)
    finally:
        sock.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    run(sys.argv[1:])
