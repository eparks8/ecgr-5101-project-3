"""In-memory stand-in for the Lab 3 bootloader, for testing without a board.

Behaves like the receiver described in PROTOCOL.md and exposes the small part
of the pyserial API that firmware_sender.py uses (write, read, flush,
reset_input_buffer, close). Fault options let the tests exercise error paths.
"""

import struct

START_MARKER = b"BOOT"
ACK, NAK, DONE = 0x06, 0x15, 0x04
CHUNK_SIZE = 256
MAX_IMAGE_SIZE = 983040


class FakeBootloader:
    def __init__(self, *, update_window_open=True, nak_on_chunk=None,
                 stop_after_bytes=None, wrong_done_count=False, garbage_on_chunk=None):
        self.update_window_open = update_window_open  # False = ignores BOOT (window expired)
        self.nak_on_chunk = nak_on_chunk              # 1-based chunk index to NAK
        self.stop_after_bytes = stop_after_bytes      # go silent after this many payload bytes
        self.wrong_done_count = wrong_done_count
        self.garbage_on_chunk = garbage_on_chunk      # send 0x99 instead of ACK on this chunk

        self.flash = bytearray()      # what a real bootloader would program at 0x08010000
        self._out = bytearray()       # board -> host bytes waiting to be read
        self._state = "wait_marker"
        self._marker_pos = 0
        self._len_bytes = bytearray()
        self._length = 0
        self._in_chunk = 0
        self._chunk_index = 0
        self.completed = False
        self.aborted = False
        self.is_open = True

    # ---- pyserial-like API used by firmware_sender.py -------------------------
    def write(self, data):
        for b in bytes(data):
            self._feed(b)
        return len(data)

    def read(self, size=1):
        out = bytes(self._out[:size])
        del self._out[:size]
        return out

    def flush(self):
        pass

    def reset_input_buffer(self):
        self._out.clear()

    def close(self):
        self.is_open = False

    # ---- Receiver state machine ------------------------------------------------
    def _feed(self, b):
        if self._state == "wait_marker":
            if not self.update_window_open:
                return
            if b == START_MARKER[self._marker_pos]:
                self._marker_pos += 1
                if self._marker_pos == len(START_MARKER):
                    self._state = "length"
            else:   # keep searching; a 'B' may start a new match
                self._marker_pos = 1 if b == START_MARKER[0] else 0

        elif self._state == "length":
            self._len_bytes.append(b)
            if len(self._len_bytes) == 4:
                (self._length,) = struct.unpack("<I", self._len_bytes)
                if not 1 <= self._length <= MAX_IMAGE_SIZE:
                    self._abort()
                else:
                    self._out.append(ACK)
                    self._state = "data"

        elif self._state == "data":
            if self.stop_after_bytes is not None and len(self.flash) >= self.stop_after_bytes:
                self._state = "silent"
                return
            self.flash.append(b)
            self._in_chunk += 1
            last = len(self.flash) == self._length
            if self._in_chunk == CHUNK_SIZE or last:
                self._chunk_index += 1
                self._in_chunk = 0
                if self._chunk_index == self.nak_on_chunk:
                    self._abort()
                    return
                self._out.append(0x99 if self._chunk_index == self.garbage_on_chunk else ACK)
                if last:
                    count = len(self.flash) + (1 if self.wrong_done_count else 0)
                    self._out += bytes([DONE]) + struct.pack("<I", count)
                    self._state = "done"
                    self.completed = True

        # "done", "aborted", "silent": ignore further bytes (one transfer per reset)

    def _abort(self):
        self._out.append(NAK)
        self._state = "aborted"
        self.aborted = True
