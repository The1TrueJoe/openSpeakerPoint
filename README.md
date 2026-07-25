# Control4 Speakerpoint Firmware Rewrite

Custom Linux firmware for the Control4 SpeakerPoint (Cirrus EP9301), 
replacing the stock 2.4.21 kernel + cramfs userland while
keeping the stock RedBoot bootloader untouched. 

This is a work in progress. All features may not be fully functional.

## Current Implemented Features

- SSH access
- React Web UI
- Speaker and RCA Audio test tones
- USB Playback
- USB Media Detection and Hotplug

## Planned Features

(If I can get around to it)

- Airplay 1
- Spotify Connect

## Building

```sh
git clone <this repo>
cd control4-speakerpoint-modern
docker build --target artifacts --output type=local,dest=./output .
```

This builds everything inside Docker and exports final images to `output/images`

The image build path compiles custom apps (including `speakerpoint-control`)
as part of normal Buildroot package compilation.

## Debug Connection

The main console connection to the speakerpoint is an RS232 (not standard UART) header near the Cirrus EP9301 and right next to the MA3221C. Pin 1 is RX, pin 2 is GND, and pin 3 is TX. The baudrate is 57600.

![Header Location](docs/rs232.png)

## Testing a build (recommended: TFTP netboot, no flash writes)

After building the image, run this command on your machine to open a TFTP server for the image.

```sh
python3 -m pip install -r requirements.txt
sudo ./tftp-serve.py output/images
```

Connect the RS232 adapter as shown above and set baud to 57600. If the device is powered on already, you can press enter to start the login prompt. The credentials for Control4's stock firmware are:

```sh
root
t0talc0ntr0l4!
```

Then, once you are into the console, enter the ```reboot``` command. You can also do a standard power-cycle.

RedBoot will send a ```+``` to the console and you will have a 1 second to press ```Control-C``` to interrupt the bootloader. Once you see the ```RedBoot>``` prompt, you will then enter the following commands:

```sh
ip_address -l 10.0.0.250 -h 10.0.0.105
load -r -v -b 0x00800000 zImage.ep93xx-speakerpoint
load -r -v -b 0x01000000 rootfs.cpio.gz
exec -b 0x00800000 -l 0x1c8e60 -c "console=ttyAM0,57600" -r 0x01000000 -s 0x36a7df
```

Where ```10.0.0.250``` is an unused IP on your subnet and ```10.0.0.105``` is the IP of the TFTP server. 

After running each ```load``` command you will get an output like: ```Raw file loaded 0x01000000-0x0135893a, assumed entry at 0x01000000```. 
You need to save the second number as that is the end address of the loaded file. For the ```exec``` command you need to input the size of each image
which you can calulate by subtracting the start addresses which is ```0x00800000``` or ```0x01000000``` from the end address of the image and add one.
For example:

```0x009c8e59 - 0x00800000 + 1 = 0x1c8e60``` and ```0x0136a7de - 0x01000000 + 1 = 0x36a7df```

Then you fill those numbers after ```-l``` and ```-s``` in the ```exec``` command respectively.

Run the command and it will boot.

The credentials for this image are:

```sh
root
speakerpoint
```

## Automated flashing / netboot over RS232 (`spflash.py`)

The manual RedBoot + TFTP dance above works, but `spflash.py` automates the
whole thing over just the serial cable — no TFTP server or network setup. It
drives the RedBoot console, transfers the images with YMODEM, and either boots
from RAM or writes NOR flash.

```sh
python3 -m pip install -r requirements.txt   # pyserial

# Boot the freshly built image from RAM (nothing written to flash):
./spflash.py netboot

# Permanently write kernel + rootfs to NOR flash:
./spflash.py flash
```

It auto-detects the serial port (or pass `--port /dev/tty...`; use
`--list-ports` to see candidates) and prompts you to power-cycle the unit so it
can catch RedBoot's interrupt window automatically. YMODEM over 57600 baud is
slow (~10 KiB/s), so a full flash of the rootfs takes a while — a progress bar
shows the transfer.

`netboot` uses `rootfs.cpio.gz` (RAM initramfs); `flash` uses
`rootfs.squashfs` (mounted from flash). After `flash`, the tool prints the
one remaining manual step: setting RedBoot's boot script via `fconfig` (left
manual by design, since a bad boot script written blind can break auto-boot).

## Disclaimer

This is not endorsed or authroized by Control4 Corporation. Control4 is a trademark of the Control4 Corporation. 