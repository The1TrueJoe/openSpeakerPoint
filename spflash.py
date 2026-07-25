#!/usr/bin/env python3
"""spflash - flash or netboot the Control4 SpeakerPoint over its RS232 console.

This automates the RedBoot bootloader entirely over the serial cable, so no
TFTP server or network setup is needed. It drives the console, transfers the
built images with YMODEM, and either boots them from RAM (for testing) or
writes them to NOR flash so they persist across power cycles.

Serial header (near the EP9301, next to the MA3221C): pin 1 = RX, pin 2 = GND,
pin 3 = TX, 57600 baud.

Examples:
    # List candidate serial ports
    ./spflash.py --list-ports

    # Boot the freshly built image from RAM (no flash write), like the old
    # manual TFTP dance but automatic:
    ./spflash.py netboot

    # Permanently flash kernel + rootfs to NOR flash:
    ./spflash.py flash

Requires: pyserial  (pip install pyserial)
"""
import argparse
import os
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("error: pyserial is required.  Install it with:  pip install pyserial")

# --- Board / RedBoot constants ---------------------------------------------
BAUD = 57600
RAM_KERNEL = 0x00800000        # scratch RAM for the kernel image
RAM_ROOTFS = 0x01000000        # scratch RAM for the rootfs image
CONSOLE = "console=ttyAM0,57600"
KERNEL_IMG = "zImage.ep93xx-speakerpoint"
INITRD_IMG = "rootfs.cpio.gz"  # netboot uses the gzipped initramfs
ROOTFS_IMG = "rootfs.squashfs" # flash uses the squashfs (mounted from flash)

# YMODEM control bytes
SOH, STX, EOT, ACK, NAK, CAN, CRCCHAR = 0x01, 0x02, 0x04, 0x06, 0x15, 0x18, 0x43


# --------------------------------------------------------------------------
# YMODEM sender
# --------------------------------------------------------------------------
def _crc16(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def _send_block(ser, blkno: int, payload: bytes, stx: bool) -> None:
    start = STX if stx else SOH
    head = bytes([start, blkno & 0xFF, (~blkno) & 0xFF])
    crc = _crc16(payload)
    ser.write(head + payload + bytes([(crc >> 8) & 0xFF, crc & 0xFF]))
    ser.flush()


def _wait_byte(ser, wanted: set, timeout: float):
    deadline = time.time() + timeout
    while time.time() < deadline:
        c = ser.read(1)
        if c and c[0] in wanted:
            return c[0]
    return None


def ymodem_send(ser, filename: str, data: bytes, progress=None) -> None:
    """Send one file to a RedBoot `load -m ymodem` receiver."""
    if _wait_byte(ser, {CRCCHAR, NAK}, 20) is None:
        raise IOError("receiver never requested the transfer (no 'C')")

    # Block 0: filename + length, 128-byte SOH block.
    meta = filename.encode() + b"\x00" + str(len(data)).encode() + b"\x00"
    _send_block(ser, 0, meta.ljust(128, b"\x00"), stx=False)
    if _wait_byte(ser, {ACK}, 10) is None:
        raise IOError("header block was not acknowledged")
    if _wait_byte(ser, {CRCCHAR}, 10) is None:
        raise IOError("receiver did not continue after header")

    # Data blocks: 1024-byte STX blocks, CTRL-Z padded.
    blkno, off, total = 1, 0, len(data)
    while off < total:
        chunk = data[off:off + 1024].ljust(1024, b"\x1a")
        for _ in range(10):
            _send_block(ser, blkno, chunk, stx=True)
            r = _wait_byte(ser, {ACK, NAK, CAN}, 10)
            if r == ACK:
                break
            if r == CAN:
                raise IOError("transfer cancelled by receiver")
        else:
            raise IOError(f"block {blkno} was not acknowledged")
        off = min(off + 1024, total)
        blkno += 1
        if progress:
            progress(off, total)

    # End of file, then end of batch.
    ser.write(bytes([EOT])); _wait_byte(ser, {NAK}, 5)
    ser.write(bytes([EOT])); _wait_byte(ser, {ACK}, 5)
    if _wait_byte(ser, {CRCCHAR}, 5) is not None:
        _send_block(ser, 0, b"\x00" * 128, stx=False)
        _wait_byte(ser, {ACK}, 5)


# --------------------------------------------------------------------------
# RedBoot console driver
# --------------------------------------------------------------------------
class RedBoot:
    PROMPT = b"RedBoot>"

    def __init__(self, ser):
        self.ser = ser

    def read_until(self, token: bytes, timeout: float) -> bytes:
        deadline = time.time() + timeout
        buf = bytearray()
        while time.time() < deadline:
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if chunk:
                buf += chunk
                if token in buf:
                    break
        return bytes(buf)

    def interrupt(self, timeout: float = 60) -> bool:
        """Spam Ctrl-C until the RedBoot prompt appears (catches the ~1s
        interrupt window on boot)."""
        print("\n>>> Power-cycle the SpeakerPoint now (unplug/replug power).")
        print("    Waiting for RedBoot", end="", flush=True)
        deadline = time.time() + timeout
        buf = bytearray()
        last_dot = 0.0
        while time.time() < deadline:
            self.ser.write(b"\x03")
            time.sleep(0.03)
            buf += self.ser.read(self.ser.in_waiting or 1)
            if self.PROMPT in buf:
                self.read_until(self.PROMPT, 0.5)
                print(" OK")
                return True
            if time.time() - last_dot > 1.0:
                print(".", end="", flush=True)
                last_dot = time.time()
        print(" timed out")
        return False

    def cmd(self, command: str, timeout: float = 15) -> str:
        self.ser.reset_input_buffer()
        self.ser.write(command.encode() + b"\r\n")
        out = self.read_until(self.PROMPT, timeout)
        # Handle "...Continue (y/n)?" style confirmations (e.g. fis create).
        if b"(y/n)" in out and self.PROMPT not in out:
            self.ser.write(b"y\r\n")
            out += self.read_until(self.PROMPT, timeout)
        return out.decode("latin1", "replace")

    def begin_ymodem_load(self, addr: int) -> None:
        self.ser.reset_input_buffer()
        self.ser.write(f"load -r -v -b 0x{addr:08x} -m ymodem\r\n".encode())
        time.sleep(0.4)   # let RedBoot echo the command and start YMODEM


# --------------------------------------------------------------------------
# High-level operations
# --------------------------------------------------------------------------
def _bar(done: int, total: int) -> None:
    pct = done * 100 // total if total else 100
    width = 32
    filled = pct * width // 100
    sys.stdout.write(f"\r    [{'#' * filled}{'.' * (width - filled)}] {pct:3d}%  "
                     f"{done // 1024}/{total // 1024} KiB")
    sys.stdout.flush()


def _load(rb: RedBoot, addr: int, path: str) -> int:
    data = open(path, "rb").read()
    name = os.path.basename(path)
    est = len(data) / (BAUD / 10) + 5      # ~10 bits/byte on the wire
    print(f"  Transferring {name} ({len(data) // 1024} KiB, ~{est / 60:.1f} min at {BAUD} baud)")
    rb.begin_ymodem_load(addr)
    ymodem_send(rb.ser, name, data, progress=_bar)
    print()
    rb.read_until(rb.PROMPT, 15)
    return len(data)


def do_netboot(rb: RedBoot, images: str) -> None:
    kpath = os.path.join(images, KERNEL_IMG)
    rpath = os.path.join(images, INITRD_IMG)
    for p in (kpath, rpath):
        if not os.path.isfile(p):
            sys.exit(f"error: missing image: {p}")

    print("\n== Netboot (RAM only, nothing written to flash) ==")
    klen = _load(rb, RAM_KERNEL, kpath)
    rlen = _load(rb, RAM_ROOTFS, rpath)

    execcmd = (f'exec -b 0x{RAM_KERNEL:08x} -l 0x{klen:x} '
               f'-c "{CONSOLE}" -r 0x{RAM_ROOTFS:08x} -s 0x{rlen:x}')
    print(f"\n  Booting:\n    {execcmd}\n")
    rb.ser.write(execcmd.encode() + b"\r\n")
    print("  Kernel handed off. Switch your terminal to the console to watch it boot.")


def do_flash(rb: RedBoot, images: str) -> None:
    kpath = os.path.join(images, KERNEL_IMG)
    rpath = os.path.join(images, ROOTFS_IMG)
    for p in (kpath, rpath):
        if not os.path.isfile(p):
            sys.exit(f"error: missing image: {p}")

    print("\n== Flash (writes NOR flash - persists across power cycles) ==")

    klen = _load(rb, RAM_KERNEL, kpath)
    print("  Writing kernel to flash (fis create zImage)...")
    print(rb.cmd(f"fis create zImage -b 0x{RAM_KERNEL:08x} -l 0x{klen:x} "
                 f"-r 0x{RAM_KERNEL:08x} -e 0x{RAM_KERNEL:08x}", timeout=180).strip())

    rlen = _load(rb, RAM_ROOTFS, rpath)
    print("  Writing rootfs to flash (fis create rootfs)...")
    print(rb.cmd(f"fis create rootfs -b 0x{RAM_ROOTFS:08x} -l 0x{rlen:x}", timeout=240).strip())

    print("\n  Images written. Verify the FIS layout with:  fis list")
    print("  Then set RedBoot's boot script (run `fconfig`, enable the boot script,")
    print("  and enter these two lines when prompted):\n")
    print("      fis load zImage")
    print(f'      exec -c "{CONSOLE} root=/dev/mtdblock3 rootfstype=squashfs"\n')
    print("  NOTE: confirm the rootfs mtdblock index from `fis list` ordering; if the")
    print("  rootfs partition isn't mtdblock3, change the number in the exec line.")
    print("  (Boot-script setup is left manual on purpose - it's the one step that can")
    print("  brick auto-boot if written blind on an untested unit.)")


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def candidate_ports():
    ports = list(list_ports.comports())
    # Prefer obvious USB-serial adapters.
    likely = [p for p in ports if any(k in (p.description or "").lower()
              for k in ("usb", "uart", "serial", "ftdi", "cp210", "ch340"))]
    return likely or ports


def pick_port() -> str:
    ports = candidate_ports()
    if not ports:
        sys.exit("error: no serial ports found. Plug in the RS232 adapter, or pass --port.")
    if len(ports) == 1:
        print(f"Using serial port: {ports[0].device} ({ports[0].description})")
        return ports[0].device
    print("Multiple serial ports found; choose one with --port:")
    for p in ports:
        print(f"  {p.device}  -  {p.description}")
    sys.exit(1)


def main() -> None:
    ap = argparse.ArgumentParser(description="Flash or netboot the SpeakerPoint over RS232.")
    ap.add_argument("mode", nargs="?", choices=["netboot", "flash"], default="netboot",
                    help="netboot = run from RAM (default); flash = write NOR flash")
    ap.add_argument("--images", default="output/images", help="directory with built images")
    ap.add_argument("--port", help="serial device (auto-detected if omitted)")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--list-ports", action="store_true", help="list serial ports and exit")
    args = ap.parse_args()

    if args.list_ports:
        for p in list_ports.comports():
            print(f"{p.device}  -  {p.description}")
        return

    port = args.port or pick_port()
    with serial.Serial(port, args.baud, timeout=0.1) as ser:
        rb = RedBoot(ser)
        if not rb.interrupt():
            sys.exit("error: never reached the RedBoot prompt. Check the cable/baud and retry.")
        if args.mode == "flash":
            do_flash(rb, args.images)
        else:
            do_netboot(rb, args.images)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\naborted")
    except IOError as e:
        sys.exit(f"\nserial/transfer error: {e}")
