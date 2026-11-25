# USB firmware transfer protocol, version 1

This is the shared contract for Part 2 (bootloader receiver) and Part 4
(Python sender). It specifies the implementation to build; the current
firmware does not implement it yet.

## Transport and update entry

- Use the bootloader's USB CDC virtual COM port, not the ST-LINK UART port.
- Open the port as binary data, with no text encoding or newline conversion.
  The sender may select 115200 baud, 8 data bits, no parity, and one stop bit;
  this protocol has no UART baud-rate timing dependency.
- After USB becomes configured, the bootloader accepts a start marker for
  10 seconds. A blinking LED indicates this update window. If the window
  expires, it returns to idle, where the existing user-button jump remains
  available. Reset the board to open another update window.
- Once a transfer starts, suppress the user-button jump until it completes
  or fails. On failure remain in the bootloader; do not automatically jump.

## Header: host to board

Send this 8-byte header once:

1. Four literal ASCII bytes `BOOT`: hexadecimal `42 4F 4F 54`.
2. An unsigned 32-bit firmware length in little-endian byte order.

The length counts only the raw `.bin` payload, not the header or responses.
Valid lengths are 1 through 983040 bytes (960 KiB), inclusive. Reject invalid
lengths before any application flash erase. The application starts at
`0x08010000`; its exclusive upper bound is `0x08100000`.

For example, a 5-byte image has this header:

```text
42 4F 4F 54 05 00 00 00
```

While waiting for the marker, ignore nonmatching bytes and keep searching
for the complete `BOOT` sequence. Once the marker is recognized, the next
four bytes are always the length. The sender must wait for the header ACK
before sending payload data.

## Payload and pacing

- Send the image in chunks of 256 bytes. The last chunk contains exactly
  the remaining bytes and may be shorter.
- Chunks have no extra header, delimiter, newline, checksum, or padding.
  All byte values, including the marker and response values, are valid data.
- After each chunk, wait for one ACK before sending the next chunk.
- USB callback boundaries are not protocol boundaries. A header or chunk
  may arrive across several callbacks, and the parser must count bytes.
- The receiver ACKs a chunk only after accepting all its bytes. In Part 2,
  acceptance means consuming/counting the bytes, not saving the whole image
  in RAM. Later integration must delay ACK until the flash writer accepts
  that chunk. Any flash alignment padding is internal and is not transmitted
  or included in the reported length.
- Do not send another header or extra bytes during a transfer.

## Responses: board to host

All responses are binary bytes, without a newline:

- `ACK = 0x06`: header accepted and ready for data, or one complete data
  chunk accepted. Exactly one ACK is sent at each of those points.
- `NAK = 0x15`: transfer rejected or aborted (invalid length, receiver
  overflow, inactivity timeout, or a storage error after flash integration).
  There is no chunk retry in version 1.
- `DONE = 0x04` followed by a 4-byte little-endian received byte count:
  transfer complete. Send this immediately after the last chunk's ACK.
  The count must equal the advertised image length.

There are no unsolicited banners or human-readable debug messages on this
port. The host must parse a byte stream, so the final ACK and DONE response
may arrive in the same host read. A successful header ACK does not mean the
image has been transferred.

## Completion and timeouts

- The transfer ends after exactly the advertised number of payload bytes.
  There is no host end marker.
- After recognizing `BOOT`, abort if no further header or payload byte
  arrives for 5 seconds while waiting for host input. Refresh this inactivity
  timer on each received byte. Do not count receiver-side processing or
  pending response transmission as host inactivity.
- The host waits up to 30 seconds for each ACK and for the DONE response,
  allowing for later flash erase/program operations.
- If a response transmission is busy, retain its bytes and retry from the
  main loop; do not drop it or reuse its buffer before transmission finishes.
- On a NAK, timeout, disconnect, or malformed response, the host stops sending
  and reports failure. Reset the board and restart from the header for a new
  attempt. Do not resend an individual chunk: without sequence numbers this
  could duplicate data.
- On success, the receiver stays in the bootloader for Part 2 and permits
  the existing user-button jump again. Automatic launch after a successful
  flash update is Part 5 integration, not Part 2.

## Example exchange

For the five payload bytes `11 22 33 44 55`:

```text
Host -> Board: 42 4F 4F 54 05 00 00 00  (header)
Board -> Host: 06                       (header ACK)
Host -> Board: 11 22 33 44 55           (last/only chunk)
Board -> Host: 06                       (chunk ACK)
Board -> Host: 04 05 00 00 00           (DONE, received count = 5)
```

## Part 2 acceptance check

Use a serial tool that supports raw hexadecimal/binary transmission and
reception. Send the example above and check every response. Also test a
257-byte payload (256-byte chunk, ACK, then one-byte chunk), split header
writes, invalid lengths, and a transfer that stops partway through.

Part 2 passes when the reported count is exact, ACK pacing works, and
incomplete transfers time out. DONE in Part 2 confirms reception only;
it does not claim flash programming, image validity, or a bootable update.
CRC verification and sequence-number/retry support are optional later
extensions and would require updating this contract and the Python sender.

## Hardware test results (2026-11-25)

Tested on a NUCLEO-L496ZG using the user USB CDC port (COM4 on the test PC).
The Debug build was programmed and verified through ST-LINK. These are
reception-only tests; no payload was programmed into application flash.

- Five-byte example: PASS. Header ACK `06`; final response
  `06 04 05 00 00 00` (data ACK, DONE, count 5).
- 257-byte transfer: PASS. Header ACK `06`, ACK `06` after the first 256
  bytes, then final response `06 04 01 01 00 00` after the last byte
  (data ACK, DONE, count 257). The full chunk contained byte values 0-255.
- Incomplete-transfer timeout: PASS. Advertised 10 bytes, sent only three,
  and received NAK `15` after 5.00 seconds without further data.
- Invalid lengths and split-header tests: not yet verified in this log.

Reset the board before each test; it accepts one transfer per reset.
