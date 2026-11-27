"""Tests for firmware_sender.py against the fake bootloader.

Run from the host/ folder:  python -m unittest -v
"""

import struct
import tempfile
import unittest
from pathlib import Path

import firmware_sender as fs
from fake_bootloader import FakeBootloader


def make_image(size: int) -> bytes:
    """A fake app image with a valid vector table at the start."""
    header = struct.pack("<II", 0x20050000, 0x08010000 + 0x1C1)  # MSP, Thumb reset handler
    body = bytes((i * 7) & 0xFF for i in range(max(0, size - 8)))
    return (header + body)[:size]


class SuccessfulTransfers(unittest.TestCase):
    def check(self, size):
        data = make_image(size)
        board = FakeBootloader()
        count = fs.send_firmware(board, data, timeout_s=0.5)
        self.assertEqual(count, size)
        self.assertEqual(bytes(board.flash), data)
        self.assertTrue(board.completed)

    def test_protocol_example_5_bytes(self):
        board = FakeBootloader()
        fs.send_firmware(board, bytes([0x11, 0x22, 0x33, 0x44, 0x55]), timeout_s=0.5)
        self.assertEqual(bytes(board.flash), bytes([0x11, 0x22, 0x33, 0x44, 0x55]))

    def test_chunk_boundaries(self):
        for size in (1, 8, 255, 256, 257, 512, 513, 1000):
            with self.subTest(size=size):
                self.check(size)

    def test_typical_app_size(self):
        self.check(5 * 1024 + 3)

    def test_max_size(self):
        self.check(fs.MAX_IMAGE_SIZE)

    def test_payload_containing_protocol_bytes(self):
        data = make_image(8) + b"BOOT" + bytes([0x06, 0x15, 0x04]) * 100
        board = FakeBootloader()
        fs.send_firmware(board, data, timeout_s=0.5)
        self.assertEqual(bytes(board.flash), data)

    def test_stale_input_is_discarded(self):
        board = FakeBootloader()
        board._out += b"\x06\x06"          # leftovers from an earlier session
        fs.send_firmware(board, make_image(300), timeout_s=0.5)


class FailedTransfers(unittest.TestCase):
    def assertFails(self, board, data, message):
        with self.assertRaises(fs.TransferError) as ctx:
            fs.send_firmware(board, data, timeout_s=0.3)
        self.assertIn(message, str(ctx.exception))

    def test_update_window_closed(self):
        self.assertFails(FakeBootloader(update_window_open=False), make_image(100),
                         "Reset the board")

    def test_nak_mid_transfer(self):
        self.assertFails(FakeBootloader(nak_on_chunk=3), make_image(2000), "NAK")

    def test_board_goes_silent(self):
        self.assertFails(FakeBootloader(stop_after_bytes=600), make_image(2000), "Timed out")

    def test_wrong_done_count(self):
        self.assertFails(FakeBootloader(wrong_done_count=True), make_image(300), "expected 300")

    def test_garbage_response(self):
        self.assertFails(FakeBootloader(garbage_on_chunk=2), make_image(1000), "0x99")


class FakeBoardMatchesProtocol(unittest.TestCase):
    """Check the fake itself against PROTOCOL.md, so the tests mean something."""

    def test_example_exchange_bytes(self):
        board = FakeBootloader()
        board.write(bytes.fromhex("424F4F5405000000"))
        self.assertEqual(board.read(10), b"\x06")
        board.write(bytes.fromhex("1122334455"))
        self.assertEqual(board.read(10), bytes.fromhex("060405000000"))

    def test_invalid_lengths_are_nakked(self):
        for length in (0, fs.MAX_IMAGE_SIZE + 1):
            board = FakeBootloader()
            board.write(b"BOOT" + struct.pack("<I", length))
            self.assertEqual(board.read(10), b"\x15")

    def test_noise_before_marker_is_ignored(self):
        board = FakeBootloader()
        board.write(b"xxBOBOOT" + struct.pack("<I", 1))
        self.assertEqual(board.read(10), b"\x06")


class ImageChecks(unittest.TestCase):
    def write_tmp(self, data, suffix=".bin"):
        f = tempfile.NamedTemporaryFile(suffix=suffix, delete=False)
        f.write(data)
        f.close()
        self.addCleanup(Path(f.name).unlink)
        return Path(f.name)

    def test_valid_image_loads(self):
        data = make_image(1024)
        self.assertEqual(fs.load_image(self.write_tmp(data)), data)

    def test_rejects_empty_missing_too_big_and_elf(self):
        for path, msg in [
            (self.write_tmp(b""), "empty"),
            (Path("does_not_exist.bin"), "not found"),
            (self.write_tmp(make_image(fs.MAX_IMAGE_SIZE + 1)), "at most"),
            (self.write_tmp(make_image(64), ".elf"), "ELF"),
        ]:
            with self.subTest(msg=msg), self.assertRaises(fs.TransferError) as ctx:
                fs.load_image(path)
            self.assertIn(msg, str(ctx.exception))

    def test_rejects_bootloader_linked_image(self):
        data = struct.pack("<II", 0x20040000, 0x08000201) + bytes(100)
        with self.assertRaises(fs.TransferError) as ctx:
            fs.load_image(self.write_tmp(data))
        self.assertIn("0x08000000", str(ctx.exception))
        self.assertEqual(fs.load_image(self.write_tmp(data), force=True), data)

    def test_rejects_non_firmware(self):
        with self.assertRaises(fs.TransferError):
            fs.load_image(self.write_tmp(b"hello world, not firmware"))


class CommandLine(unittest.TestCase):
    def test_simulate_end_to_end(self):
        f = tempfile.NamedTemporaryFile(suffix=".bin", delete=False)
        f.write(make_image(3000))
        f.close()
        self.addCleanup(Path(f.name).unlink)
        self.assertEqual(fs.main([f.name, "--simulate"]), 0)

    def test_bad_file_returns_error_code(self):
        self.assertEqual(fs.main(["nope.bin", "--simulate"]), 1)


if __name__ == "__main__":
    unittest.main()
