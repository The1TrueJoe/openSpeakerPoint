#!/usr/bin/env python3
"""spflash - install, netboot, back up or restore the Control4 SpeakerPoint.

Serial (RS232 console, 57600) drives RedBoot; SSH carries the data once a
Linux is up. Nothing here ever writes stock's RedBoot, kernel, cramfs or FIS
directory.

    ./spflash.py netboot                 boot the built image from RAM (writes nothing)
    ./spflash.py install                 netboot, back up the whole NOR, then install
                                         openSpeakerPoint persistently (see below)
    ./spflash.py backup  --host IP       dump the whole 16 MiB NOR from a running
                                         openSpeakerPoint, verified
    ./spflash.py restore --host IP       return an installed box to stock Control4
                                         (runs the box's own osp-restore)

How the install lays the flash out (board .dts): stock keeps RedBoot, its kernel
and cramfs, byte for byte. openSpeakerPoint goes into stock's 9 MiB jffs2.img
region (kernel, squashfs rootfs) plus the 256 KiB after it (settings), and
stock's jffs2 *files* are kept compressed in osp-restore - that is what lets
the box put itself back to stock with nothing but its own flash. RedBoot's FIS
directory is not edited: the new boot script is `fis load jffs2.img` and an
exec of the kernel at the start of that region. It is test-booted by hand from
the RedBoot prompt before it is saved.

Requires: pyserial, paramiko  (pip install -r requirements.txt)
"""
import argparse
import hashlib
import io
import lzma
import os
import re
import sys
import tarfile
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
ROOTFS_IMG = "rootfs.squashfs" # the install writes the squashfs to osp-rootfs

# Flash layout (keep in step with the board .dts).
NOR_SIZE = 0x1000000
STOCK_JFFS2_OFF, STOCK_JFFS2_LEN = 0x680000, 0x900000
REDBOOT_CFG_OFF = 0xFC0000
KERNEL_SLOT = 0x200000          # osp-kernel
RESTORE_SLOT = 0x220000         # osp-restore
JFFS2_IMG_RAM = 0x01500000      # where `fis load jffs2.img` puts the region (its FIS mem_base)
# Regions an install must leave exactly as stock had them.
STOCK_FIXED = {"RedBoot": (0x000000, 0x040000), "zImage": (0x040000, 0x100000),
               "cramfs": (0x140000, 0x540000), "FIS directory": (0xFE0000, 0x020000)}
BOOT_CMDLINE = f"{CONSOLE} root=/dev/mtdblock5 rootfstype=squashfs ro panic=5"
BOOT_SCRIPT = ["fis load jffs2.img",
               f'exec -b 0x{JFFS2_IMG_RAM:08x} -l 0x{KERNEL_SLOT:x} -c "{BOOT_CMDLINE}"']
OSP_PASSWORD = "speakerpoint"
# xz settings for the stock payload: a 4 MiB dictionary keeps decompression
# on the 32 MiB box to ~5 MiB of RAM (xz's default 64 MiB would not fit) and the
# payload clear of its 17-block partition.
XZ_FILTERS = [{"id": lzma.FILTER_LZMA2, "preset": 9 | lzma.PRESET_EXTREME, "dict_size": 4 << 20}]

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
                # Let the ^C storm drain and confirm a clean prompt: leftover
                # input here is what makes the next YMODEM header go
                # unacknowledged.
                time.sleep(1.0)
                self.ser.reset_input_buffer()
                self.ser.write(b"\r\n")
                self.read_until(self.PROMPT, 3)
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
    for attempt in (1, 2):
        rb.begin_ymodem_load(addr)
        try:
            ymodem_send(rb.ser, name, data, progress=_bar)
            break
        except IOError as e:
            if attempt == 2 or "header" not in str(e):
                raise
            # RedBoot sometimes misses the very first header after a reset;
            # abort its receive and go again.
            print(f"\n    ({e}; retrying)")
            rb.ser.write(bytes([CAN, CAN, CAN]))
            time.sleep(2)
            rb.ser.reset_input_buffer()
            rb.ser.write(b"\r\n")
            rb.read_until(rb.PROMPT, 5)
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


# --------------------------------------------------------------------------
# Linux side: the serial console, then SSH
# --------------------------------------------------------------------------
def md5(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


class Console:
    """The Linux shell on the serial console (openSpeakerPoint: root/speakerpoint)."""

    def __init__(self, ser):
        self.ser = ser

    def _read_until(self, tokens, timeout):
        buf = bytearray()
        deadline = time.time() + timeout
        while time.time() < deadline:
            buf += self.ser.read(self.ser.in_waiting or 1)
            if any(t in buf for t in tokens):
                time.sleep(0.3)
                buf += self.ser.read(self.ser.in_waiting or 1)
                break
        return bytes(buf)

    def wait_login(self, timeout=600):
        out = self._read_until([b"login:"], timeout)
        if b"login:" not in out:
            raise IOError("no login prompt on the console")

    def login(self, attempts=4):
        # Boot messages that land after the getty prompt can swallow the
        # first exchange, so try a few times.
        for _ in range(attempts):
            self.ser.write(b"\r")
            out = self._read_until([b"login:", b"# "], 5)
            if b"# " in out and b"login:" not in out:
                return
            if b"login:" in out:
                self.ser.write(b"root\r")
                self._read_until([b"assword"], 5)
                self.ser.write(OSP_PASSWORD.encode() + b"\r")
                if b"# " in self._read_until([b"# "], 10):
                    return
            time.sleep(3)
        raise IOError("console login failed")

    def run(self, cmd, timeout=20) -> str:
        mark = f"__spflash_{int(time.time() * 1000)}__"
        self.ser.reset_input_buffer()
        self.ser.write(f"{cmd}; echo {mark}\r".encode())
        out = self._read_until([f"\n{mark}".encode()], timeout).decode("latin1")
        lines = out.replace("\r", "").split("\n")[1:]
        return "\n".join(l for l in lines if mark not in l).strip()

    def ip(self) -> str:
        for _ in range(30):
            m = re.search(r"inet (\d+\.\d+\.\d+\.\d+)", self.run("ip -4 -o addr show eth0"))
            if m:
                return m.group(1)
            time.sleep(2)
        raise IOError("no IPv4 address on eth0")

    def hw_reset(self):
        """Reset through the EP93xx watchdog (S01z-ep93xx-reset's own path)."""
        self.ser.write(b"sync; devmem 0x80940000 32 0xaaaa\r")
        time.sleep(1.5)


class Box:
    """A running openSpeakerPoint, over SSH (paramiko)."""

    def __init__(self, host):
        import paramiko
        self.host = host
        self.c = paramiko.SSHClient()
        self.c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self.c.connect(host, username="root", password=OSP_PASSWORD, timeout=15,
                       look_for_keys=False, allow_agent=False)

    def run(self, cmd, check=True) -> str:
        _, out, err = self.c.exec_command(cmd)
        data = out.read().decode("utf-8", "replace")
        rc = out.channel.recv_exit_status()
        if check and rc != 0:
            raise IOError(f"`{cmd}` failed ({rc}): {err.read().decode('utf-8', 'replace').strip()}")
        return data.strip()

    def read(self, cmd) -> bytes:
        _, out, _ = self.c.exec_command(cmd)
        data = out.read()
        if out.channel.recv_exit_status() != 0:
            raise IOError(f"`{cmd}` failed")
        return data

    def put(self, data: bytes, path: str):
        stdin, out, _ = self.c.exec_command(f"cat > {path}")
        stdin.write(data)
        stdin.channel.shutdown_write()
        if out.channel.recv_exit_status() != 0:
            raise IOError(f"upload to {path} failed")
        if self.run(f"md5sum {path}").split()[0] != md5(data):
            raise IOError(f"{path} arrived corrupted")

    def booted_from_flash(self) -> bool:
        # What is mounted at / - not the command line, which carries the
        # kernel's built-in root= even on a netboot.
        return self.run("awk '$2 == \"/\" { print $3 }' /proc/mounts | tail -n 1") == "squashfs"


def backup_nor(box: Box, dest_dir: str) -> bytes:
    """Dump the whole NOR, checked against the box's own md5sum."""
    print("  Reading the whole 16 MiB NOR...")
    nor = box.read("cat /dev/mtd0")
    want = box.run("md5sum /dev/mtd0").split()[0]
    if len(nor) != NOR_SIZE or md5(nor) != want:
        raise IOError(f"NOR dump is {len(nor)} bytes, md5 {md5(nor)}; the box says {want}")
    os.makedirs(dest_dir, exist_ok=True)
    path = os.path.join(dest_dir, f"nor-full-16M-{time.strftime('%Y-%m-%d')}.bin")
    open(path, "wb").write(nor)
    # The two pieces a raw restore writes back, ready to copy to a USB stick
    # (osp-restore stock --yes --raw /media/usb/<dir>).
    parts = {"jffs2.bin": nor[STOCK_JFFS2_OFF:STOCK_JFFS2_OFF + STOCK_JFFS2_LEN],
             "redboot-config.bin": nor[REDBOOT_CFG_OFF:REDBOOT_CFG_OFF + 4096]}
    for name, data in parts.items():
        open(os.path.join(dest_dir, name), "wb").write(data)
    sums = {os.path.basename(path): want, **{k: md5(v) for k, v in parts.items()}}
    open(os.path.join(dest_dir, "MD5SUMS"), "w").write("".join(f"{v}  {k}\n" for k, v in sums.items()))
    print(f"  Backed up to {path} (md5 {want}), with jffs2.bin + redboot-config.bin for a USB restore")
    return nor


def stock_payload(box: Box, nor: bytes) -> bytes:
    """osp-restore's contents, from this unit's own stock jffs2 (mounted
    read-only through the kernel's jffs2 driver, so modes and links are
    exact) and its own RedBoot config block."""
    print("  Packing stock's jffs2 files (read-only mount)...")
    box.run("mtdpart add /dev/mtd0 stock-jffs2 0x%x 0x%x" % (STOCK_JFFS2_OFF, STOCK_JFFS2_LEN))
    mtd = box.run("""sed -n 's/^mtd\\([0-9]*\\): .* "stock-jffs2"$/\\1/p' /proc/mtd""")
    try:
        if not mtd.isdigit():
            raise IOError("mtdpart did not create stock-jffs2")
        box.run(f"mkdir -p /tmp/stock && mount -t jffs2 -o ro /dev/mtdblock{mtd} /tmp/stock")
        try:
            tar = box.read("tar -c -C /tmp/stock .")
        finally:
            box.run("umount /tmp/stock", check=False)
    finally:
        if mtd.isdigit():
            box.run(f"mtdpart del /dev/mtd0 {mtd}", check=False)

    # Per-file md5s, checked by osp-restore after it unpacks onto the flash.
    sums = []
    with tarfile.open(fileobj=io.BytesIO(tar)) as t:
        for m in t.getmembers():
            if m.isfile():
                name = m.name if m.name.startswith("./") else "./" + m.name
                sums.append(f"{md5(t.extractfile(m).read())}  {name}")
    files = {
        "stock-jffs2.tar.xz": lzma.compress(tar, format=lzma.FORMAT_XZ, filters=XZ_FILTERS),
        "stock-jffs2.md5": ("\n".join(sums) + "\n").encode(),
        "redboot-config.bin": nor[REDBOOT_CFG_OFF:REDBOOT_CFG_OFF + 4096],
    }
    files["MANIFEST"] = (
        f"openSpeakerPoint stock payload\ncreated {time.strftime('%Y-%m-%dT%H:%M:%S')}\n"
        f"nor-md5 {md5(nor)}\nstock-files {len(sums)}\n"
        f"stock-jffs2.tar {len(tar)} bytes\n").encode()
    files["PAYLOAD.md5"] = "".join(f"{md5(v)}  {k}\n" for k, v in files.items()).encode()

    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode="w", format=tarfile.USTAR_FORMAT) as t:
        for name in ("MANIFEST", "PAYLOAD.md5", "redboot-config.bin", "stock-jffs2.md5", "stock-jffs2.tar.xz"):
            ti = tarfile.TarInfo(name)
            ti.size, ti.mtime, ti.mode = len(files[name]), int(time.time()), 0o644
            t.addfile(ti, io.BytesIO(files[name]))
    data = out.getvalue()
    print(f"  Payload: {len(sums)} files, {len(tar) // 1024} KiB -> {len(data) // 1024} KiB")
    if len(data) > RESTORE_SLOT:
        raise IOError(f"payload is {len(data)} bytes; osp-restore holds {RESTORE_SLOT}")
    return data


def write_part(box: Box, label: str, data: bytes):
    """Stream an image straight onto an openSpeakerPoint partition and verify it.

    Nothing is staged in /tmp: the netbooted RAM system keeps its whole root
    filesystem in RAM and has ~5 MiB free, so a 2-4 MiB upload there gets the
    OOM killer going. Erase what the image needs, write it from the SSH
    stream, then read it back against the local md5.
    """
    if label not in ("osp-kernel", "osp-rootfs", "osp-restore"):
        raise IOError(f"refusing to write {label}")
    mtd = box.run(f"""sed -n 's/^mtd\\([0-9]*\\): .* "{label}"$/\\1/p' /proc/mtd""")
    if not mtd.isdigit():
        raise IOError(f"no {label} partition")
    size = int(box.run(f"cat /sys/class/mtd/mtd{mtd}/size"))
    erase = int(box.run(f"cat /sys/class/mtd/mtd{mtd}/erasesize"))
    if len(data) > size:
        raise IOError(f"{label}: image is {len(data)} bytes, partition {size}")
    blocks = -(-len(data) // erase)
    box.run(f"flash_erase -q /dev/mtd{mtd} 0 {blocks}")
    stdin, out, err = box.c.exec_command(f"dd of=/dev/mtd{mtd} bs=4096 2>/dev/null")
    for i in range(0, len(data), 65536):
        stdin.write(data[i:i + 65536])
    stdin.channel.shutdown_write()
    if out.channel.recv_exit_status() != 0:
        raise IOError(f"{label}: write failed: {err.read().decode('utf-8', 'replace').strip()}")
    got = box.run(f"head -c {len(data)} /dev/mtd{mtd} | md5sum").split()[0]
    if got != md5(data):
        raise IOError(f"{label}: reads back {got}, expected {md5(data)}")
    print(f"    {label}: {len(data)} bytes written and verified ({got})")


def set_boot_script(rb: RedBoot):
    rb.ser.reset_input_buffer()
    rb.ser.write(b"fconfig boot_script_data\r\n")
    out = rb.read_until(b">>", 10)
    if b">>" not in out:
        raise IOError("RedBoot did not offer to edit the boot script:\n" + out.decode("latin1"))
    for line in BOOT_SCRIPT:
        rb.ser.write(line.encode() + b"\r\n")
        rb.read_until(b">>", 5)
    rb.ser.write(b"\r\n")
    out = rb.read_until(b"(y/n)?", 10)
    rb.ser.write(b"y\r\n")
    out += rb.read_until(rb.PROMPT, 60)
    if b"rror" in out:
        raise IOError("RedBoot config update failed:\n" + out.decode("latin1"))


def do_install(ser, images: str, backups: str, host: str = None):
    kernel = open(os.path.join(images, KERNEL_IMG), "rb").read()
    rootfs = open(os.path.join(images, ROOTFS_IMG), "rb").read()
    if len(kernel) > KERNEL_SLOT:
        sys.exit(f"error: kernel is {len(kernel)} bytes; osp-kernel holds {KERNEL_SLOT}")

    rb, con = RedBoot(ser), Console(ser)
    if host:
        con.login()     # already netbooted (--host): carry on from there
    else:
        if not rb.interrupt():
            sys.exit("error: never reached the RedBoot prompt")
        do_netboot(rb, images)
        print("  Waiting for the RAM system to boot...")
        con.wait_login()
        con.login()
    ip = host or con.ip()
    print(f"  Up at {ip}")
    box = Box(ip)
    if box.booted_from_flash():
        sys.exit("error: this system booted from flash; an install must run from the netbooted RAM system")
    # The RAM system keeps its whole root filesystem in RAM and has ~5 MiB to
    # spare; with its audio and MQTT services running, the OOM killer takes
    # SSH sessions mid-install. Nothing they do is needed for an install.
    box.run("for s in S99shairport-sync S61speakerpoint-control S58mpd S50mosquitto S50crond S90httpd; do "
            "[ -x /etc/init.d/$s ] && /etc/init.d/$s stop; done >/dev/null 2>&1; sync; "
            "echo 3 > /proc/sys/vm/drop_caches; rm -f /tmp/*.img", check=False)

    unit = box.run("hostname")
    nor = backup_nor(box, os.path.join(backups, unit))

    # Re-install over openSpeakerPoint keeps the payload already on the box;
    # a first install builds it from stock's jffs2, which is still intact.
    has_payload = box.run("osp-restore status 2>/dev/null | grep -q '^state: ready' && echo yes || true") == "yes"
    for name, (off, ln) in STOCK_FIXED.items():
        print(f"  stock {name}: md5 {md5(nor[off:off + ln])}")
    payload = None if has_payload else stock_payload(box, nor)

    print("\n== Writing openSpeakerPoint to flash ==")
    if payload:
        write_part(box, "osp-restore", payload)   # first: the way back exists before anything else goes
    write_part(box, "osp-kernel", kernel)
    write_part(box, "osp-rootfs", rootfs)
    data = box.run("""sed -n 's/^mtd\\([0-9]*\\): .* "osp-data"$/\\1/p' /proc/mtd""")
    box.run(f"flash_erase -q -j /dev/mtd{data} 0 0")
    print("    osp-data: empty jffs2")

    print("\n== Test boot from flash (nothing saved yet) ==")
    con.hw_reset()
    if not rb.interrupt(timeout=60):
        sys.exit("error: lost RedBoot after the reset")
    print("   ", BOOT_SCRIPT[0])
    print("   ", rb.cmd(BOOT_SCRIPT[0], timeout=60).strip())   # copies the 9 MiB region to RAM
    print("   ", BOOT_SCRIPT[1])
    rb.ser.write(BOOT_SCRIPT[1].encode() + b"\r\n")
    con.wait_login(timeout=600)    # a first boot also generates SSH host keys
    for _ in range(60):            # sshd comes up after the getty prompt
        try:
            if Box(ip).booted_from_flash():
                break
            sys.exit("error: the test boot did not come up from flash")
        except Exception as e:     # connection refused, or paramiko's SSHException mid key-generation
            if isinstance(e, SystemExit):
                raise
            time.sleep(10)
    else:
        sys.exit("error: no SSH after the test boot")
    print("  Booted from flash.")

    print("\n== Saving RedBoot's boot script ==")
    con.hw_reset()
    if not rb.interrupt(timeout=60):
        sys.exit("error: lost RedBoot after the reset")
    set_boot_script(rb)
    print(rb.cmd("fconfig -l -n").strip())
    rb.ser.write(b"reset\r\n")
    con.wait_login(timeout=600)
    print("\n  Installed. The SpeakerPoint now boots openSpeakerPoint from flash; settings persist in /data.")
    print("  Return to stock any time: the dashboard's Settings, MQTT cmd/system/restore, or")
    print(f"  ./spflash.py restore --host <ip>. Full NOR backup: {os.path.join(backups, unit)}")


def do_backup(host: str, backups: str):
    box = Box(host)
    backup_nor(box, os.path.join(backups, box.run("hostname")))


def do_restore(host: str):
    box = Box(host)
    print(box.run("osp-restore status --verify"))
    print("  Starting the return to stock; the box reboots into Control4 (~2 min).")
    box.run("setsid osp-restore stock --yes </dev/null >/dev/null 2>&1 &", check=False)


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
    ap = argparse.ArgumentParser(description="Install, netboot, back up or restore the SpeakerPoint.")
    ap.add_argument("mode", nargs="?", choices=["netboot", "install", "backup", "restore"], default="netboot",
                    help="netboot = run from RAM (default); install = persistent install; "
                         "backup/restore = against a running openSpeakerPoint (--host)")
    ap.add_argument("--images", default="output/images", help="directory with built images")
    ap.add_argument("--backups", default="backups", help="where NOR backups are kept (never committed)")
    ap.add_argument("--host", help="IP of a running openSpeakerPoint (backup, restore; install: "
                                   "an already-netbooted one, skipping the netboot)")
    ap.add_argument("--port", help="serial device (auto-detected if omitted)")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--list-ports", action="store_true", help="list serial ports and exit")
    args = ap.parse_args()

    if args.list_ports:
        for p in list_ports.comports():
            print(f"{p.device}  -  {p.description}")
        return
    if args.mode in ("backup", "restore"):
        if not args.host:
            sys.exit(f"error: {args.mode} needs --host <ip of the running openSpeakerPoint>")
        return do_backup(args.host, args.backups) if args.mode == "backup" else do_restore(args.host)

    port = args.port or pick_port()
    with serial.Serial(port, args.baud, timeout=0.1) as ser:
        if args.mode == "install":
            do_install(ser, args.images, args.backups, args.host)
            return
        rb = RedBoot(ser)
        if not rb.interrupt():
            sys.exit("error: never reached the RedBoot prompt. Check the cable/baud and retry.")
        do_netboot(rb, args.images)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\naborted")
    except IOError as e:
        sys.exit(f"\nserial/transfer error: {e}")
