"""Bounded serial boot capture for the SD integration; requires pyserial/esptool."""
import argparse
from pathlib import Path
import re
import time
import serial
from esptool.reset import HardReset

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM13')
parser.add_argument('--seconds', type=float, default=55)
parser.add_argument('--output', default='../documents/logs/sd_boot.log')
parser.add_argument('--reset', action='store_true')
parser.add_argument('--expect-all', action='store_true')
parser.add_argument('--expect-assets', nargs='+', default=[])
args = parser.parse_args()
path = Path(args.output)
path.parent.mkdir(parents=True, exist_ok=True)
seen = set()
verified_assets = set()
port = serial.Serial(baudrate=115200, timeout=0.2)
port.dtr = False
port.rts = False
port.port = args.port
with port, path.open('w', encoding='utf-8') as log:
    if args.reset:
        port.reset_input_buffer()
        HardReset(port, uses_usb=True).reset()
    end = time.monotonic() + min(args.seconds, 600)
    while time.monotonic() < end:
        line = port.readline().decode('utf-8', errors='replace')
        if line:
            print(line, end='', flush=True)
            log.write(line)
            log.flush()
            match = re.search(r'SD_MOSTRANDO (\d+)/6', line)
            if match:
                seen.add(int(match[1]))
            asset = re.search(r'SD_VERSION_ACTUAL: (\S+)', line)
            if asset:
                verified_assets.add(asset[1])
            if args.expect_assets and set(args.expect_assets) <= verified_assets:
                break
            if args.expect_all and len(seen) == 6:
                break
            if 'Dibujo cargado desde flash' in line:
                break
print(f'\nSD_VERIFY: {len(seen)}/6 imagenes leidas y mostradas. Log: {path}', flush=True)
if args.expect_all and len(seen) != 6:
    raise SystemExit(2)
if not set(args.expect_assets) <= verified_assets:
    raise SystemExit(2)

