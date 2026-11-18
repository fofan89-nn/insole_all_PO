# Insole BLE TCP Server

Программа для сбора данных с BLE стелек и отправки их через TCP сервер.

## Возможности

- Сканирование и подключение к BLE стелькам (левая и правая)
- TCP сервер для каждой стельки с конфигурируемыми портами
- Передача данных в бинарном формате: `uint64_t timestamp` + `int16_t values[8]`
- Кроссплатформенность: Linux и Windows

## Параметры запуска

```bash
./simpleble_test [OPTIONS]
```

### Опции:
- `--scan-duration=<ms>` - время сканирования BLE в миллисекундах (по умолчанию 5000)
- `--right-port=<port>` - TCP порт для правой стельки (по умолчанию 8001)
- `--left-port=<port>` - TCP порт для левой стельки (по умолчанию 8002)

## Примеры использования

### Linux

```bash
# Сканирование 10 секунд с портами по умолчанию
./simpleble_test --scan-duration=10000

# Пользовательские порты
./simpleble_test --scan-duration=5000 --right-port=9001 --left-port=9002

# Все параметры
./simpleble_test --scan-duration=10000 --right-port=8001 --left-port=8002
```

### Windows

```bash
# Сканирование 10 секунд с портами по умолчанию
simpleble_test.exe --scan-duration=10000

# Пользовательские порты
simpleble_test.exe --scan-duration=5000 --right-port=9001 --left-port=9002
```

## Формат данных TCP

Каждый пакет содержит 18 байт:
- **Байты 0-7**: `uint64_t timestamp` (8 байт) - время в миллисекундах
- **Байты 8-25**: `int16_t values[8]` (16 байт) - 8 значений сенсоров по 2 байта каждое

Пример парсинга в Python:

```python
import socket
import struct

def read_sensor_data(host, port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))
    
    while True:
        data = sock.recv(18)  # 8 + 2*8 = 18 байт
        if not data:
            break
        
        timestamp, *values = struct.unpack('<Q8h', data)
        print(f"Timestamp: {timestamp}, Values: {values}")
    
    sock.close()

# Подключение к левой стельке (порт 8002)
read_sensor_data('localhost', 8002)
```

## Компиляция

### Linux

```bash
mkdir build && cd build
cmake ..
make
```

### Windows (MSVC)

```bash
mkdir build
cd build
cmake -G "Visual Studio 17 2022" ..
cmake --build . --config Release
```

### Windows (MinGW)

```bash
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

## Зависимости

- SimpleBLE для работы с Bluetooth LE
- CMake 3.15 или выше
- C++20 компилятор

## Архитектура

### TcpServer класс
Управляет TCP сервером для одной стельки:
- `init(port)` - инициализация сервера на указанном порту
- `accept_clients()` - принятие входящих соединений (работает в отдельном потоке)
- `send_data(SensorData)` - отправка данных всем подключенным клиентам
- `close_socket()` - корректное завершение работы сервера

### Главный поток
- Сканирует BLE устройства
- Подключается к найденным стелькам
- Запускает потоки приема клиентов для каждого сервера
- Получает данные с BLE и отправляет их через TCP

## Диагностика

При запуске программа выводит:
- Информацию об адаптере Bluetooth
- Время сканирования
- Используемые порты TCP
- Информацию о найденных стельках
- Статистику (пакетов в секунду для каждой стельки)
- Информацию о подключении/отключении клиентов
