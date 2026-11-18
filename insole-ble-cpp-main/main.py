#!/usr/bin/env python3
"""
Python equivalent of main.cpp from insole_ble_cpp.
Requires: bleak (for BLE) and Python 3.8+

Functionality:
- Start TCP servers for right and left insoles
- Scan BLE for devices exposing INSOLE_SERVICE_UUID
- Connect, read type, subscribe to voltage notifications
- Unpack 12-bit ADC values from packets and forward binary frames to TCP clients
- Periodically print PPS and moving averages
"""

import asyncio
import struct
import signal
import argparse
import sys
from collections import deque
from typing import Dict, Set

try:
    from bleak import BleakScanner, BleakClient
except Exception as e:
    print("Please install bleak: pip install bleak")
    raise

INSOLE_SERVICE_UUID = "12345678-aaaa-aaaa-aaaa-1234567890ab"
VOLTAGE_CHAR_UUID = "87654321-aaaa-aaaa-aaaa-abcdefabcdef"
TYPE_CHAR_UUID = "abcdefab-1234-5678-90ab-abcdefabcdef"
PACKET_SIZE = 17
PPS_WINDOW_SIZE = 10

class TcpServer:
    def __init__(self):
        self.clients: Set[asyncio.StreamWriter] = set()
        self.lock = asyncio.Lock()
        self.server = None

    async def start(self, port: int):
        self.server = await asyncio.start_server(self._handle_client, host='0.0.0.0', port=port)
        addr = self.server.sockets[0].getsockname()
        print(f"TCP server listening on port {addr[1]}")

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        peer = writer.get_extra_info('peername')
        print(f"Client connected from {peer}")
        async with self.lock:
            self.clients.add(writer)
        try:
            # Keep the connection open until client closes
            while not reader.at_eof():
                await asyncio.sleep(1)
        finally:
            async with self.lock:
                if writer in self.clients:
                    self.clients.remove(writer)
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass
            print(f"Client disconnected {peer}")

    async def send_data(self, timestamp: int, values: list):
        # Pack little-endian uint64 timestamp and 8 little-endian int16 values
        payload = struct.pack('<Q', timestamp) + struct.pack('<8h', *values)
        async with self.lock:
            clients = list(self.clients)
        for w in clients:
            try:
                w.write(payload)
                await w.drain()
            except Exception:
                async with self.lock:
                    if w in self.clients:
                        self.clients.remove(w)
                try:
                    w.close()
                    await w.wait_closed()
                except Exception:
                    pass

async def unpack12bit(packed: bytes):
    # Unpack 8 12-bit signed-with-offset values from 12 bytes
    out = []
    bit_buffer = 0
    bit_count = 0
    in_index = 0
    while len(out) < 8:
        while bit_count < 12:
            bit_buffer |= packed[in_index] << bit_count
            in_index += 1
            bit_count += 8
        val = (bit_buffer & 0x0FFF) - 2048
        # convert to signed 16-bit like C++ int16_t
        if val >= 32768:
            val -= 65536
        out.append(int(val))
        bit_buffer >>= 12
        bit_count -= 12
    return out

class InsoleManager:
    def __init__(self, right_port: int, left_port: int):
        self.tcp_servers: Dict[int, TcpServer] = {1: TcpServer(), 2: TcpServer()}
        self.right_port = right_port
        self.left_port = left_port
        self.cnt = {1: 0, 2: 0}
        self.pps_history = {1: deque(maxlen=PPS_WINDOW_SIZE), 2: deque(maxlen=PPS_WINDOW_SIZE)}
        self.running = True
        self.connected_clients = []
        self.devices_connected = asyncio.Event()

    async def start_servers(self):
        await asyncio.gather(
            self.tcp_servers[1].start(self.right_port),
            self.tcp_servers[2].start(self.left_port),
        )

    async def handle_notification(self, insole_type: int, data: bytes):
        if not self.running:
            return
        pkt_count = len(data) // PACKET_SIZE
        self.cnt[insole_type] += pkt_count
        for i in range(pkt_count):
            pkt = data[i*PACKET_SIZE:(i+1)*PACKET_SIZE]
            timestamp5 = pkt[:5]
            timestamp = int.from_bytes(timestamp5, byteorder='little', signed=False)
            packed_adc = pkt[5:5+12]
            values = await unpack12bit(packed_adc)
            # values are already adjusted by -2048
            await self.tcp_servers[insole_type].send_data(timestamp, values)

    async def connect_to_insole(self, device):
        try:
            async with BleakClient(device) as client:
                # Read type
                try:
                    type_b = await client.read_gatt_char(TYPE_CHAR_UUID)
                    insole_type = int(type_b[0])
                    print(f"Insole type: {insole_type}")
                    if insole_type not in (1, 2):
                        print(f"Unknown insole type {insole_type} on device {device.address}")
                        return
                except Exception as e:
                    print(f"Failed reading type from {device.address}: {e}")
                    return

                # Ensure voltage characteristic exists
                services = client.services
                if VOLTAGE_CHAR_UUID.replace('-', '').lower() not in ''.join([s.uuid.replace('-', '').lower() for s in services]):
                    # fallback: try to subscribe anyway and let it fail
                    pass

                async def callback(sender, data):
                    # data is bytearray
                    await self.handle_notification(insole_type, bytes(data))

                await client.start_notify(VOLTAGE_CHAR_UUID, callback)
                print(f"Connected to insole: {device.address}")
                # Signal that at least one device is connected
                self.devices_connected.set()

                # Keep connection open until stop
                while self.running:
                    await asyncio.sleep(1)
                try:
                    await client.stop_notify(VOLTAGE_CHAR_UUID)
                except Exception:
                    pass
        except Exception as e:
            print(f"Failed to connect to {device.address}: {e}")

    async def scan_and_connect(self, scan_duration_ms: int):
        print(f"Scanning for {scan_duration_ms} ms...")
        devices = await BleakScanner.discover(return_adv=True)
        insole_found = False

        # Connect sequentially to avoid parallel BLE connections
        for device_and_data in devices.values():
            d, adv_data = device_and_data
            uuids = adv_data.service_uuids
            uuids_lc = [u.lower() for u in uuids]
            if INSOLE_SERVICE_UUID.lower() in uuids_lc:
                insole_found = True
                print(f"Insole found! Device: {d.name} ({d.address})")
                # Await connection before attempting the next one
                await self.connect_to_insole(d)
                # If manager stopped during connection, break out
                if not self.running:
                    break

        if not insole_found:
            print("No insoles found during scan period")
            self.running = False
            return

    async def pps_counter(self):
        # Wait until at least one device is connected
        await self.devices_connected.wait()
        while self.running:
            await asyncio.sleep(1)
            current1 = self.cnt[1]
            current2 = self.cnt[2]
            self.cnt[1] = 0
            self.cnt[2] = 0
            self.pps_history[1].append(current1)
            self.pps_history[2].append(current2)
            avg1 = sum(self.pps_history[1]) / len(self.pps_history[1]) if self.pps_history[1] else 0
            avg2 = sum(self.pps_history[2]) / len(self.pps_history[2]) if self.pps_history[2] else 0
            print(f"PPS1: {current1} (avg: {avg1:.2f}) PPS2: {current2} (avg: {avg2:.2f})")

    async def stop(self):
        self.running = False
        # close servers
        for s in self.tcp_servers.values():
            if s.server:
                s.server.close()
                try:
                    await s.server.wait_closed()
                except Exception:
                    pass
        # close client connections implicitly by stopping loop

async def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument('--scan-duration', type=int, default=5000)
    parser.add_argument('--right-port', type=int, default=8001)
    parser.add_argument('--left-port', type=int, default=8002)
    args = parser.parse_args(argv)

    mgr = InsoleManager(args.right_port, args.left_port)

    await mgr.start_servers()

    loop = asyncio.get_running_loop()
    stop_event = asyncio.Event()

    def _signal_handler():
        print("Stopping...")
        stop_event.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, _signal_handler)
        except NotImplementedError:
            # Windows or unsupported
            pass

    # Start pps counter
    pps_task = asyncio.create_task(mgr.pps_counter())

    # Scan and connect
    scan_task = asyncio.create_task(mgr.scan_and_connect(args.scan_duration))

    await stop_event.wait()

    await mgr.stop()
    pps_task.cancel()
    try:
        await pps_task
    except asyncio.CancelledError:
        pass

if __name__ == '__main__':
    try:
        asyncio.run(main(sys.argv[1:]))
    except KeyboardInterrupt:
        pass
