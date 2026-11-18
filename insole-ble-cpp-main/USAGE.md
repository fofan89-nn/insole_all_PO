# Примеры использования TCP серверов стелек

## Быстрый старт

### 1. Запуск сервера на Linux

```bash
cd build
./simpleble_test --scan-duration=10000 --right-port=8001 --left-port=8002
```

Вывод:
```
Using adapter: hci0
Scanning for 10000 ms...
Right insole port: 8001
Left insole port: 8002
TCP server listening on port 8001
TCP server listening on port 8002
Insole found! Device: ...
Insole type: Right
Connected to insole: ... (address)
...
```

### 2. Запуск на Windows

```bash
cd build
simpleble_test.exe --scan-duration=10000 --right-port=8001 --left-port=8002
```

## Подключение к серверу

### Python клиент (получение данных в реальном времени)

```bash
# Подключение к левой стельке и вывод первых 10 пакетов
python3 client.py --port 8002 --count 10 --verbose

# Подключение к правой стельке с подробным выводом
python3 client.py --port 8001 --verbose

# Сохранение данных в бинарный файл
python3 client.py --port 8002 --output left_insole.bin

#远 хост
python3 client.py --host 192.168.1.100 --port 8002
```

### C++ клиент

```bash
# Компиляция (добавить в CMakeLists.txt или скомпилировать отдельно)
g++ -std=c++20 client.cpp -o client_cpp

# Запуск (левая стелька)
./client_cpp --port 8002 --count 100

# Запуск (правая стелька)
./client_cpp --port 8001
```

### netcat (быстрая проверка)

```bash
# Linux
nc -l localhost 8002

# Проверка подключения
(echo "test"; sleep 1) | nc localhost 8002
```

### Telnet

```bash
telnet localhost 8002
```

## Обработка бинарных данных

### Python - парсинг сохраненного файла

```python
import struct

def read_sensor_file(filename):
    with open(filename, 'rb') as f:
        while True:
            data = f.read(18)  # 8 + 2*8 = 18 байт
            if len(data) < 18:
                break
            
            timestamp, *values = struct.unpack('<Q8h', data)
            print(f"Timestamp: {timestamp}, Values: {values}")

read_sensor_file('left_insole.bin')
```

### Множественные клиенты

Несколько приложений могут одновременно подключаться к одному серверу:

```bash
# Терминал 1 - запуск сервера
./simpleble_test --scan-duration=30000

# Терминал 2 - клиент 1 логирует данные
python3 client.py --port 8001 --output left_data.bin

# Терминал 3 - клиент 2 просматривает в реальном времени
python3 client.py --port 8001 --verbose

# Терминал 4 - клиент 3 для другой стельки
python3 client.py --port 8002 --verbose
```

## Форматы параметров

### Параметры главного приложения

```bash
./simpleble_test [--scan-duration=MS] [--right-port=PORT] [--left-port=PORT]

--scan-duration=MS      Время сканирования в миллисекундах (по умолчанию 5000)
--right-port=PORT       TCP порт для правой стельки (по умолчанию 8001)
--left-port=PORT        TCP порт для левой стельки (по умолчанию 8002)
```

Примеры:
```bash
# Стандартные порты, быстрое сканирование
./simpleble_test --scan-duration=3000

# Пользовательские порты
./simpleble_test --right-port=9000 --left-port=9001

# Длительное сканирование, пользовательские порты
./simpleble_test --scan-duration=20000 --right-port=10001 --left-port=10002
```

## Диагностика

### Проверка открытых портов

```bash
# Linux
netstat -tuln | grep 800

lsof -i :8001

# Windows
netstat -ano | findstr :8001

Get-NetTCPConnection -LocalPort 8001 -ErrorAction SilentlyContinue
```

### Отладка подключения

```bash
# Linux - мониторинг трафика на порту 8002
tcpdump -i lo -n port 8002

# Linux - просмотр размера принятых пакетов
nc -l localhost 8002 | od -x
```

## Интеграция в собственное приложение

### Минимальный код на Python

```python
import socket
import struct

def connect_to_insole(port):
    sock = socket.socket()
    sock.connect(('localhost', port))
    return sock

def read_packet(sock):
    data = sock.recv(18)
    timestamp, *values = struct.unpack('<Q8h', data)
    return timestamp, values

sock = connect_to_insole(8002)
for _ in range(100):
    ts, vals = read_packet(sock)
    print(f"Time: {ts}, Sensors: {vals}")
sock.close()
```

### Минимальный код на C++

```cpp
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

int sock = socket(AF_INET, SOCK_STREAM, 0);
sockaddr_in addr = {};
addr.sin_family = AF_INET;
addr.sin_port = htons(8002);
inet_pton(AF_INET, "localhost", &addr.sin_addr);
connect(sock, (sockaddr*)&addr, sizeof(addr));

uint64_t timestamp;
int16_t values[8];
while(read(sock, &timestamp, 8) && read(sock, values, 16)) {
    // обработка данных
}
close(sock);
```

## Производительность

Каждая стелька отправляет данные со своей частотой:
- Один пакет = 18 байт
- Типичная частота отправки: 100+ пакетов в секунду
- Пропускная способность на стельку: ~1.8 кБ/с

Программа поддерживает одновременное подключение нескольких клиентов к одному TCP серверу.
