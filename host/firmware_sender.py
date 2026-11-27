#!/usr/bin/env python3
"""Send an application .bin to the Lab 3 USB bootloader (Requirement 3).

Implements the host side of PROTOCOL.md (version 1):

    Host -> Board : 'BOOT' + uint32 little-endian length      (8-byte header)
    Board -> Host : ACK 0x06
    Host -> Board : payload in 256-byte chunks, waiting for ACK after each
    Board -> Host : DONE 0x04 + uint32 little-endian received count
    Any time      : NAK 0x15 means the board rejected or aborted the transfer

Usage:
    python firmware_sender.py Lab3_Application.bin            # auto-detect port
    python firmware_sender.py Lab3_Application.bin -p COM5    # explicit port
    python firmware_sender.py Lab3_Application.bin --simulate # no board needed
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

# ---- Protocol constants (keep in sync with PROTOCOL.md) ----------------------
START_MARKER = b"BOOT"
ACK = 0x06
NAK = 0x15
DONE = 0x04
CHUNK_SIZE = 256

APP_BASE = 0x08010000
APP_END = 0x08100000                    # exclusive
MAX_IMAGE_SIZE = APP_END - APP_BASE     # 983040 bytes (960 KiB)

# SRAM range used to sanity-check the image's initial stack pointer
SRAM_START = 0x20000000
SRAM_END = 0x20050000

RESPONSE_TIMEOUT_S = 30.0               # per ACK / DONE, per PROTOCOL.md

# USB IDs the bootloader enumerates with (Lab3_BootLoader usbd_desc.c)
BOOTLOADER_VID = 0x0483                 # 1155, STMicroelectronics
BOOTLOADER_PID = 0x5740                 # 22336, STM32 Virtual ComPort


class TransferError(Exception):
    """Any failure that should stop the transfer and be reported to the user."""


# ---- Image checks -------------------------------------------------------------
def load_image(path: Path, force: bool = False) -> bytes:
    """Read the .bin in binary mode and check it looks like an app at 0x08010000."""
    if not path.is_file():
        raise TransferError(f"File not found: {path}")
    if path.suffix.lower() == ".elf":
        raise TransferError(
            f"{path.name} is an ELF file. Send the raw .bin "
            "(arm-none-eabi-objcopy -O binary Lab3_Application.elf Lab3_Application.bin).")

    data = path.read_bytes()
    if len(data) == 0:
        raise TransferError(f"{path.name} is empty.")
    if len(data) > MAX_IMAGE_SIZE:
        raise TransferError(
            f"{path.name} is {len(data)} bytes; the application region holds "
            f"at most {MAX_IMAGE_SIZE} bytes (960 KiB).")

    if len(data) >= 8 and not force:
        initial_sp, reset_handler = struct.unpack_from("<II", data, 0)
        if not (SRAM_START < initial_sp <= SRAM_END):
            raise TransferError(
                f"First word 0x{initial_sp:08X} is not a stack pointer in SRAM. "
                "This does not look like an application .bin (use --force to send anyway).")
        if not (APP_BASE <= (reset_handler & ~1) < APP_BASE + len(data)):
            raise TransferError(
                f"Reset handler 0x{reset_handler:08X} is outside the application region "
                f"starting at 0x{APP_BASE:08X}. Was this .bin linked for 0x08000000 "
                "(e.g. the bootloader)? Use --force to send anyway.")
    return data


# ---- Port handling ------------------------------------------------------------
def find_bootloader_port() -> str | None:
    """Return the COM port of the bootloader's CDC device, or None if absent."""
    from serial.tools import list_ports

    matches = [p.device for p in list_ports.comports()
               if p.vid == BOOTLOADER_VID and p.pid == BOOTLOADER_PID]
    if len(matches) > 1:
        raise TransferError(
            f"Several bootloader ports found ({', '.join(matches)}); pick one with -p.")
    return matches[0] if matches else None


def wait_for_port(wait_s: float) -> str:
    """Poll for the bootloader port so the user can run the script, then reset the board."""
    deadline = time.monotonic() + wait_s
    announced = False
    while True:
        port = find_bootloader_port()
        if port:
            return port
        if time.monotonic() >= deadline:
            from serial.tools import list_ports
            seen = [f"{p.device} ({p.description})" for p in list_ports.comports()]
            raise TransferError(
                "Bootloader USB port not found. Connect the board's USB OTG (user USB) "
                "port, not only the ST-LINK port, and reset the board.\n"
                "  Ports seen: " + (", ".join(seen) if seen else "none"))
        if not announced:
            print(f"Waiting up to {wait_s:.0f} s for the bootloader port "
                  "(is the board connected and reset?)...", file=sys.stderr)
            announced = True
        time.sleep(0.2)


def open_port(port: str, baud: int):
    import serial
    try:
        # Baud rate is ignored by USB CDC but pyserial requires a value.
        return serial.Serial(port, baudrate=baud, bytesize=8, parity="N",
                             stopbits=1, timeout=0.1, write_timeout=5)
    except serial.SerialException as exc:
        raise TransferError(f"Could not open {port}: {exc}") from exc


# ---- Protocol -----------------------------------------------------------------
def read_exact(ser, count: int, timeout_s: float, what: str) -> bytes:
    """Read exactly `count` bytes or raise after `timeout_s` seconds of waiting."""
    buf = bytearray()
    deadline = time.monotonic() + timeout_s
    while len(buf) < count:
        chunk = ser.read(count - len(buf))
        if chunk:
            buf += chunk
        elif time.monotonic() >= deadline:
            raise TransferError(
                f"Timed out after {timeout_s:.0f} s waiting for {what} "
                f"(got {len(buf)} of {count} bytes).")
    return bytes(buf)


def expect_ack(ser, timeout_s: float, what: str) -> None:
    resp = read_exact(ser, 1, timeout_s, what)[0]
    if resp == ACK:
        return
    if resp == NAK:
        raise TransferError(f"Board sent NAK for {what}: transfer rejected or aborted.")
    raise TransferError(f"Unexpected response 0x{resp:02X} while waiting for {what}.")


def send_firmware(ser, data: bytes, timeout_s: float = RESPONSE_TIMEOUT_S,
                  progress=None) -> int:
    """Run one complete transfer. Returns the byte count the board reported."""
    ser.reset_input_buffer()            # protocol has no banners; drop stale bytes

    # Header
    ser.write(START_MARKER + struct.pack("<I", len(data)))
    ser.flush()
    try:
        expect_ack(ser, timeout_s, "header ACK")
    except TransferError as exc:
        if "Timed out" in str(exc):
            raise TransferError(
                f"{exc}\n  The bootloader only accepts a transfer for 10 s after USB "
                "connects (LED blinking). Reset the board and run the script again.") from exc
        if "NAK" in str(exc):
            raise TransferError(f"{exc}\n  The board rejected the image length "
                                f"({len(data)} bytes).") from exc
        raise

    # Payload
    sent = 0
    total_chunks = (len(data) + CHUNK_SIZE - 1) // CHUNK_SIZE
    for index in range(total_chunks):
        chunk = data[index * CHUNK_SIZE:(index + 1) * CHUNK_SIZE]
        ser.write(chunk)
        ser.flush()
        expect_ack(ser, timeout_s, f"ACK for chunk {index + 1}/{total_chunks}")
        sent += len(chunk)
        if progress:
            progress(sent, len(data))

    # Completion
    marker = read_exact(ser, 1, timeout_s, "DONE")[0]
    if marker == NAK:
        raise TransferError("Board sent NAK instead of DONE (storage error?).")
    if marker != DONE:
        raise TransferError(f"Expected DONE (0x04) but got 0x{marker:02X}.")
    (count,) = struct.unpack("<I", read_exact(ser, 4, timeout_s, "DONE byte count"))
    if count != len(data):
        raise TransferError(f"Board reports {count} bytes received, expected {len(data)}.")
    return count


# ---- CLI ----------------------------------------------------------------------
def progress_bar(done: int, total: int, width: int = 40) -> None:
    filled = width * done // total
    bar = "#" * filled + "-" * (width - filled)
    print(f"\r[{bar}] {100 * done // total:3d}%  {done}/{total} bytes",
          end="" if done < total else "\n", file=sys.stderr, flush=True)


def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Send an application .bin to the Lab 3 USB bootloader.")
    p.add_argument("binary", type=Path, help="application .bin file (linked at 0x08010000)")
    p.add_argument("-p", "--port", help="serial port, e.g. COM5 or /dev/ttyACM0 (default: auto-detect)")
    p.add_argument("-b", "--baud", type=int, default=115200, help="ignored by USB CDC (default: 115200)")
    p.add_argument("-t", "--timeout", type=float, default=RESPONSE_TIMEOUT_S,
                   help=f"seconds to wait for each ACK/DONE (default: {RESPONSE_TIMEOUT_S:.0f})")
    p.add_argument("-w", "--wait", type=float, default=30.0,
                   help="seconds to wait for the bootloader port to appear when auto-detecting (default: 30)")
    p.add_argument("--force", action="store_true", help="skip the vector-table sanity check")
    p.add_argument("--simulate", action="store_true",
                   help="talk to a built-in fake bootloader instead of a board")
    return p.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    ser = None
    try:
        data = load_image(args.binary, force=args.force)

        if args.simulate:
            from fake_bootloader import FakeBootloader
            ser, label = FakeBootloader(), "simulated bootloader"
        else:
            port = args.port or wait_for_port(args.wait)
            ser, label = open_port(port, args.baud), port

        print(f"Sending {args.binary.name} ({len(data)} bytes) to {label}", file=sys.stderr)
        start = time.monotonic()
        count = send_firmware(ser, data, args.timeout, progress_bar)
        elapsed = time.monotonic() - start
        print(f"Success: board received {count} bytes in {elapsed:.1f} s. "
              "Press the User button (B1) to start the application.")
        return 0
    except TransferError as exc:
        print(f"\nError: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nCancelled. Reset the board before trying again.", file=sys.stderr)
        return 130
    except Exception as exc:                # serial disconnects, etc.
        print(f"\nError: {type(exc).__name__}: {exc}\n"
              "  Was the board reset or unplugged? Reset it and try again.", file=sys.stderr)
        return 1
    finally:
        if ser is not None:
            ser.close()


if __name__ == "__main__":
    sys.exit(main())
