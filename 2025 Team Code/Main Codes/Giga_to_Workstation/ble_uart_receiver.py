import asyncio
from bleak import BleakClient, BleakScanner

UART_SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
TX_CHAR_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"

def handle_rx(_, data):
    print("Received from GIGA:", data.decode("utf-8").strip())

async def main():
    print("Scanning for GIGA_BLE_UART...")
    device = await BleakScanner.find_device_by_name("GIGA_BLE_UART", timeout=10)

    if not device:
        print("Device not found. Make sure GIGA is powered and advertising.")
        return

    print("Found GIGA! Connecting...")

    client = BleakClient(device)

    try:
        await client.connect()
        await asyncio.sleep(2)  # Delay to help GATT settle
        await client.start_notify(TX_CHAR_UUID, handle_rx)
        print("Connected. Listening for messages (press Ctrl+C to stop)...")

        while True:
            await asyncio.sleep(1)

    except Exception as e:
        import traceback
        print("Connection failed:")
        traceback.print_exc()

    finally:
        if client.is_connected:
            await client.stop_notify(TX_CHAR_UUID)
            await client.disconnect()
            print("Disconnected from GIGA.")

if __name__ == "__main__":
    asyncio.run(main())
