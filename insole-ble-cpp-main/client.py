#!/usr/bin/env python3
"""
TCP клиент для получения данных со стелек через TCP сервер.

Примеры использования:
    python3 client.py --port 8001 --verbose
    python3 client.py --host 192.168.1.100 --port 8001
    python3 client.py --output data.bin  # сохранить в файл
"""

import socket
import struct
import argparse
import sys
from pathlib import Path


def read_sensor_packet(sock):
    """Прочитать один пакет сенсора (18 байт)."""
    data = sock.recv(24)
    if len(data) < 24:
        return None
    
    # Распаковать: uint64_t timestamp + 8 int16_t values
    timestamp, *values = struct.unpack('<Q8h', data)
    return timestamp, values


def main():
    parser = argparse.ArgumentParser(description='TCP клиент для стелек')
    parser.add_argument('--host', default='localhost', help='IP адрес сервера')
    parser.add_argument('--port', type=int, default=8002, help='TCP порт')
    parser.add_argument('--count', type=int, default=0, help='Количество пакетов (0 = бесконечно)')
    parser.add_argument('--verbose', '-v', action='store_true', help='Подробный вывод')
    parser.add_argument('--output', '-o', help='Сохранить данные в файл')
    
    args = parser.parse_args()
    
    try:
        print(f"Подключение к {args.host}:{args.port}...", file=sys.stderr)
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((args.host, args.port))
        print(f"Подключено!", file=sys.stderr)
        
        output_file = None
        if args.output:
            output_file = open(args.output, 'wb')
        
        packet_count = 0
        try:
            while True:
                result = read_sensor_packet(sock)
                if result is None:
                    break
                
                timestamp, values = result
                packet_count += 1
                
                if args.verbose:
                    print(f"[{packet_count}] Timestamp: {timestamp:>12} | Values: {values}")
                else:
                    print(f"{timestamp}," + ",".join(str(v) for v in values))
                
                if output_file:
                    output_file.write(struct.pack('<Q8h', timestamp, *values))
                
                if args.count > 0 and packet_count >= args.count:
                    break
        
        except KeyboardInterrupt:
            print(f"\nПрервано пользователем", file=sys.stderr)
        
        finally:
            if output_file:
                output_file.close()
                print(f"Данные сохранены в {args.output}", file=sys.stderr)
            
            sock.close()
            print(f"Получено {packet_count} пакетов", file=sys.stderr)
    
    except ConnectionRefusedError:
        print(f"Ошибка: не удается подключиться к {args.host}:{args.port}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Ошибка: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
