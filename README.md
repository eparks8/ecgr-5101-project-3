# ecgr-5101-project-3
Bootloader overwriting over USB for the NUCLEO-L496ZG-P with updates written via Python.

## Sending firmware with Python (Requirement 3)

`host/firmware_sender.py` sends an application `.bin` to the bootloader using
the protocol in [PROTOCOL.md](PROTOCOL.md).

### Setup

```
pip install pyserial
```

Building `Lab3_Application` now also writes `build/Debug/Lab3_Application.bin`
next to the `.elf`.

### Usage

1. Connect **both** USB cables: ST-LINK (power/programming) and the USB OTG
   (user USB) port, which the bootloader uses.
2. Press **RESET** on the board. The LED blinks for 10 s while the bootloader
   accepts an update.
3. Within those 10 s, run the script. It finds the bootloader's COM port
   (USB ID `0483:5740`, "STM32 Virtual ComPort") and waits up to 30 s for it
   to appear if the board is still enumerating:

   ```
   cd host
   python firmware_sender.py ../Lab3_Application/build/Debug/Lab3_Application.bin
   ```
4. When the script prints `Success`, press the **User button (B1)** to start
   the new application.

Options:

| Option | Meaning |
|---|---|
| `-p COM5` | Use this port instead of auto-detecting |
| `-t 30` | Seconds to wait for each ACK/DONE (default 30) |
| `-w 30` | Seconds to wait for the port to appear (default 30) |
| `--force` | Skip the check that the `.bin` is linked at `0x08010000` |
| `--simulate` | Send to a built-in fake bootloader; no board needed |

Before sending, the script checks that the file is a raw `.bin` (not `.elf`),
fits in 960 KiB, and starts with a vector table for `0x08010000` (stack pointer
in SRAM, reset handler inside the app region). This catches sending the
bootloader's image or the wrong file by mistake.

Exit code is 0 on success and 1 on any error (NAK, timeout, wrong DONE count,
port problems). After a failure, reset the board before trying again; the
bootloader accepts one transfer per reset.

### Testing without a board

```
cd host
python -m unittest -v
```

`fake_bootloader.py` implements the receiver side of PROTOCOL.md in memory.
The tests cover chunk-boundary sizes (1, 255, 256, 257 ... 960 KiB), payloads
containing protocol bytes, a closed update window, NAK mid-transfer, the board
going silent, a wrong DONE count, garbage responses, and bad input files.
