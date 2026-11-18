# Инструкция по сборке на Windows

## Требования

- Visual Studio 2019+ или MinGW
- CMake 3.15+
- SimpleBLE с поддержкой Windows

## Установка зависимостей

### 1. Установить CMake

Скачать с https://cmake.org/download/ и установить, или через package manager:

```powershell
# Через chocolatey
choco install cmake

# Или скачать .msi файл и установить вручную
```

### 2. Установить SimpleBLE

```bash
# Через vcpkg (рекомендуется)
vcpkg install simpleble:x64-windows

# Или скачать из https://github.com/OpenBluetoothToolbox/SimpleBLE
```

## Сборка с Visual Studio

### Способ 1: Visual Studio 2022

```powershell
# Создать папку build
mkdir build
cd build

# Сгенерировать Visual Studio проект
cmake -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake" ..

# Собрать Release версию
cmake --build . --config Release

# Собранный файл будет в: Release\simpleble_test.exe
```

### Способ 2: Visual Studio 2019

```powershell
cmake -G "Visual Studio 16 2019" -DCMAKE_TOOLCHAIN_FILE="C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake" ..
cmake --build . --config Release
```

## Сборка с MinGW

```bash
mkdir build
cd build

# Сгенерировать Makefiles
cmake -G "MinGW Makefiles" ..

# Собрать
cmake --build . --config Release

# Или просто
mingw32-make
```

## Сборка с помощью скрипта

Создайте файл `build_windows.bat`:

```batch
@echo off
setlocal enabledelayedexpansion

REM Установить пути (измените если нужно)
set VCPKG_PATH=C:\dev\vcpkg
set TOOLCHAIN_FILE=!VCPKG_PATH!\scripts\buildsystems\vcpkg.cmake

if not exist "build" mkdir build
cd build

echo Generating Visual Studio project...
cmake -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="!TOOLCHAIN_FILE!" ..

if errorlevel 1 (
    echo CMake configuration failed!
    exit /b 1
)

echo Building project...
cmake --build . --config Release

if errorlevel 1 (
    echo Build failed!
    exit /b 1
)

echo Build successful!
echo Executable: Release\simpleble_test.exe
pause
```

Запуск:
```bash
.\build_windows.bat
```

## Запуск программы на Windows

```powershell
# Перейти в папку build/Release
cd build\Release

# Запустить с параметрами
.\simpleble_test.exe --scan-duration=10000 --right-port=8001 --left-port=8002

# Без параметров (использует значения по умолчанию)
.\simpleble_test.exe
```

## Запуск Python клиента на Windows

```powershell
# Убедитесь, что Python установлен
python --version

# Запустить клиент
python client.py --port 8002 --verbose

# Или с полным путем к python.exe
python.exe client.py --host localhost --port 8001
```

## Компиляция C++ клиента на Windows

```bash
# С MinGW
g++ -std=c++20 client.cpp -o client.exe -lws2_32 -lwsock32

# С Visual Studio (в Developer Command Prompt)
cl /std:c++latest client.cpp ws2_32.lib wsock32.lib /Fe:client.exe
```

## Проверка портов на Windows

```powershell
# Проверить какой процесс слушает порт 8001
netstat -ano | findstr :8001

# Более детальная информация
Get-NetTCPConnection -LocalPort 8001 -ErrorAction SilentlyContinue | Select-Object State, OwningProcess

# Завершить процесс если нужно (например, PID 1234)
Stop-Process -Id 1234 -Force

# Или через netstat
netstat -ano | findstr :8001
taskkill /PID <PID> /F
```

## Проверка подключения

### Способ 1: PowerShell

```powershell
# Простая проверка подключения
$socket = New-Object System.Net.Sockets.TcpClient
$socket.Connect("localhost", 8002)
$socket.Close()
Write-Host "Connected successfully"
```

### Способ 2: netcat (nc.exe)

Скачать nc.exe для Windows (http://joncraton.org/blog/46/netcat-for-windows/)

```bash
nc -v -w 5 localhost 8002
```

### Способ 3: Telnet

```bash
# Включить Telnet Client (если отключен):
dism /online /Enable-Feature /FeatureName:TelnetClient

# Подключиться
telnet localhost 8002
```

## Решение проблем

### Ошибка: "cmake is not recognized"
- CMake не установлен или не в PATH
- Решение: Переустановить CMake и выбрать "Add cmake to PATH"

### Ошибка: "Visual Studio generator not found"
- Visual Studio не установлена
- Решение: Установить Visual Studio или использовать MinGW: `-G "MinGW Makefiles"`

### Ошибка: "SimpleBLE not found"
- SimpleBLE не установлена
- Решение: Установить через vcpkg или указать правильный путь в CMakeLists.txt

### Ошибка: "Address already in use"
- Порт уже используется другим процессом
- Решение: Использовать другой порт или завершить процесс занимающий порт

### Ошибка подключения в Python клиенте
```
ConnectionRefusedError: [WinError 10061] No connection could be made
```
- TCP сервер не запущен или не слушает на указанном порту
- Решение: Запустить simpleble_test.exe с нужными параметрами

## Отладка

### Включить вывод отладки в CMake

```bash
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . --config Debug
```

### Использовать отладчик Visual Studio

```bash
# Встроенный отладчик
devenv /debugexe build\Debug\simpleble_test.exe --scan-duration=10000
```

## Примечания

- Программа автоматически определяет платформу через препроцессор `#ifdef _WIN32`
- Все сокет-операции кроссплатформенны
- SimpleBLE на Windows имеет те же интерфейсы, что и на Linux
- Убедитесь, что Bluetooth адаптер включен и драйверы установлены
