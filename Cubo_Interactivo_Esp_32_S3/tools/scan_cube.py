import asyncio
from bleak import BleakScanner
async def main():
    devices = await BleakScanner.discover(timeout=8, return_adv=True)
    for device, adv in devices.values():
        if '72a10000-6c75-426f-9000-6375626f0001' in adv.service_uuids:
            print(device.address, device.name, adv.service_uuids)
asyncio.run(main())
