import socket
import threading
import time
import struct
import random

HOST = '0.0.0.0'
PORTS = [8001, 8002]
SEND_RATE_HZ = 200
INTERVAL = 1.0 / SEND_RATE_HZ


def client_handler(conn, addr):
    print(f"Client connected: {addr}")
    try:
        while True:
            start_time = time.time()

            # время в микросекундах
            timestamp_us = int(time.time() * 1_000_000)

            # 8 значений int16
            values = [random.randint(-1300, 1300) for _ in range(8)]

            # упаковка: Q + 8h
            packet = struct.pack('<Q8h', timestamp_us, *values)

            conn.sendall(packet)

            # держим 200 Гц
            elapsed = time.time() - start_time
            sleep_time = INTERVAL - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    except Exception as e:
        print(f"Client {addr} disconnected: {e}")
    finally:
        conn.close()


def start_server(port):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind((HOST, port))
    server.listen()

    print(f"Server listening on port {port}")

    while True:
        conn, addr = server.accept()
        threading.Thread(target=client_handler, args=(conn, addr), daemon=True).start()


if __name__ == "__main__":
    for port in PORTS:
        threading.Thread(target=start_server, args=(port,), daemon=True).start()

    print("Servers started on ports 8001 and 8002")

    while True:
        time.sleep(1)