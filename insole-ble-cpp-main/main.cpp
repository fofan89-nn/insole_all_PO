#include <simpleble/SimpleBLE.h>
#include <iostream>
#include <csignal>
#include <thread>
#include <algorithm>
#include <atomic>
#include <vector>
#include <mutex>
#include <cstring>
#include <map>
#include <queue>
#include <memory>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wsock32.lib")
#define close closesocket
#define SHUT_RDWR SD_BOTH
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
typedef int SOCKET;
const int INVALID_SOCKET = -1;
const int SOCKET_ERROR = -1;
#endif

SimpleBLE::BluetoothUUID INSOLE_SERVICE_UUID = "12345678-aaaa-aaaa-aaaa-1234567890ab";
SimpleBLE::BluetoothUUID DATA_CHAR_UUID = "87654321-aaaa-aaaa-aaaa-abcdefabcdef";
SimpleBLE::BluetoothUUID TYPE_CHAR_UUID = "abcdefab-1234-5678-90ab-abcdefabcdef";
SimpleBLE::BluetoothUUID STRING_CHAR_UUID = "abcdefab-1234-5678-90ab-abcdefabcddd";
SimpleBLE::BluetoothUUID BATTERY_LEVEL_UUID = "abcdefab-1234-5678-90ab-abcdefabcded";

int get_packet_size(int ch_count)
{
    return (12 * ch_count + 7) / 8 + 5;
}

SimpleBLE::Adapter *current_adapter = nullptr;

enum InsoleType
{
    INSOLE_RIGHT = 1,
    INSOLE_LEFT = 2
};

struct SensorData
{
    uint64_t timestamp;
    int ch_count;
    const int16_t *values;
};

struct ConnectedDevice
{
    SimpleBLE::Peripheral peripheral;
    int type;
};

struct TcpServer
{
    SOCKET listen_socket = INVALID_SOCKET;
    std::vector<SOCKET> client_sockets;
    std::mutex clients_mutex;
    std::atomic<bool> running = false;

    bool init(int port)
    {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            std::cerr << "WSAStartup failed\n";
            return false;
        }
#endif

        listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_socket == INVALID_SOCKET)
        {
            std::cerr << "socket() failed\n";
            return false;
        }

        int reuse = 1;
        if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR,
                       (const char *)&reuse, sizeof(reuse)) == SOCKET_ERROR)
        {
            std::cerr << "setsockopt() failed\n";
            close(listen_socket);
            return false;
        }

        sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        server_addr.sin_port = htons(port);

        if (bind(listen_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
        {
            std::cerr << "bind() failed on port " << port << "\n";
            close(listen_socket);
            return false;
        }

        if (listen(listen_socket, 5) == SOCKET_ERROR)
        {
            std::cerr << "listen() failed\n";
            close(listen_socket);
            return false;
        }

        running = true;
        std::cout << "TCP server listening on port " << port << "\n";
        return true;
    }

    void accept_clients()
    {
        while (running)
        {
            sockaddr_in client_addr = {};
            socklen_t client_addr_len = sizeof(client_addr);

            SOCKET client_socket = accept(listen_socket, (struct sockaddr *)&client_addr, &client_addr_len);
            if (client_socket == INVALID_SOCKET)
            {
                if (!running)
                    break;
                std::cerr << "accept() failed\n";
                continue;
            }

            std::cout << "Client connected from " << inet_ntoa(client_addr.sin_addr)
                      << ":" << ntohs(client_addr.sin_port) << "\n";

            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                client_sockets.push_back(client_socket);
            }
        }
    }

    void send_data(const SensorData &data)
    {
        std::lock_guard<std::mutex> lock(clients_mutex);

        std::vector<uint8_t> buffer(sizeof(uint64_t) + sizeof(int16_t) * data.ch_count);
        memcpy(buffer.data(), &data.timestamp, sizeof(uint64_t));
        memcpy(buffer.data() + sizeof(uint64_t), data.values, sizeof(int16_t) * data.ch_count);

        auto it = client_sockets.begin();
        while (it != client_sockets.end())
        {
            SOCKET client = *it;
            int send_result = send(client, (const char *)buffer.data(), (int)buffer.size(), 0);

            if (send_result == SOCKET_ERROR)
            {
                std::cout << "Client disconnected\n";
                close(client);
                it = client_sockets.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void close_socket()
    {
        running = false;

        if (listen_socket != INVALID_SOCKET)
        {
#ifdef _WIN32
            ::shutdown(listen_socket, SD_BOTH);
#else
            ::shutdown(listen_socket, SHUT_RDWR);
#endif
            close(listen_socket);
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (auto client : client_sockets)
            {
                close(client);
            }
            client_sockets.clear();
        }

#ifdef _WIN32
        WSACleanup();
#endif
    }
};

std::vector<ConnectedDevice> connected_devices;
std::mutex devices_mutex;

std::map<int, TcpServer> tcp_servers;
std::mutex servers_mutex;

std::atomic<int> cnt1 = 0;
std::atomic<int> cnt2 = 0;

// Для расчета бегущего среднего PPS
std::queue<int> pps1_history;
std::queue<int> pps2_history;
const int PPS_WINDOW_SIZE = 10; // окно усреднения в секундах
std::mutex pps_mutex;
int second_counter = 0;

std::atomic<bool> exit_flag = false;

std::atomic<bool> connect_flag = false;

void signal_handler(int)
{
    exit_flag = true;
}

const char *get_insole_type_name(int type)
{
    switch (type)
    {
    case INSOLE_RIGHT:
        return "Right";
    case INSOLE_LEFT:
        return "Left";
    default:
        return "Unknown";
    }
}

void unpack12bit(uint16_t *adcValues, int N, const uint8_t *packed)
{
    uint32_t bitBuffer = 0;
    int bitCount = 0;
    size_t inIndex = 0;
    size_t outIndex = 0;

    while (outIndex < (size_t)N)
    {

        while (bitCount < 12)
        {
            bitBuffer |= static_cast<uint32_t>(packed[inIndex++]) << bitCount;
            bitCount += 8;
        }

        adcValues[outIndex++] = static_cast<uint16_t>(bitBuffer & 0x0FFF) - 2048;

        bitBuffer >>= 12;
        bitCount -= 12;
    }
}

bool has_connected_insole_of_type(int type)
{
    std::lock_guard<std::mutex> lock(devices_mutex);
    return std::any_of(connected_devices.begin(), connected_devices.end(),
                       [type](const ConnectedDevice &entry)
                       { return entry.type == type; });
}

void connect_to_insole(SimpleBLE::Peripheral dev)
{
    dev.connect();

    std::vector<SimpleBLE::Service> services = dev.services();

    auto insole_service_it = std::find_if(
        services.begin(),
        services.end(),
        [](SimpleBLE::Service &s)
        { return s.uuid() == INSOLE_SERVICE_UUID; });

    if (insole_service_it == services.end())
    {
        std::cout << "Insole service not found on device: " << dev.identifier() << "\n";
        dev.disconnect();
        return;
    }

    SimpleBLE::Service insole_service = *insole_service_it;
    std::vector<SimpleBLE::Characteristic> characteristics = insole_service.characteristics();

    if (std::find_if(
            characteristics.begin(),
            characteristics.end(),
            [](SimpleBLE::Characteristic &c)
            { return c.uuid() == STRING_CHAR_UUID; }) == characteristics.end())
    {
        std::cout << "String characteristic not found on device: " << dev.identifier() << "\n";
        dev.disconnect();
        return;
    }

    auto str_data = dev.read(INSOLE_SERVICE_UUID, STRING_CHAR_UUID);

    std::string str(str_data);

    std::cout << "Device string: \"" << str << "\"\n";

    int ch_count = 8;

    if (str.find("v2") != std::string::npos)
    {
        ch_count = 16;
    }

    std::cout << "Channel count: " << ch_count << "\n";

    if (std::find_if(
            characteristics.begin(),
            characteristics.end(),
            [](SimpleBLE::Characteristic &c)
            { return c.uuid() == TYPE_CHAR_UUID; }) == characteristics.end())
    {
        std::cout << "Type characteristic not found on device: " << dev.identifier() << "\n";
        dev.disconnect();
        return;
    }

    auto type_data = dev.read(INSOLE_SERVICE_UUID, TYPE_CHAR_UUID);

    int type = type_data[0];
    std::cout << "Insole type: " << get_insole_type_name(type) << "\n";

    if (type != INSOLE_RIGHT && type != INSOLE_LEFT)
    {
        std::cout << "Unknown insole type on device: " << dev.identifier() << "\n";
        dev.disconnect();
        return;
    }

    if (has_connected_insole_of_type(type))
    {
        std::cout << "Already connected insole of type " << get_insole_type_name(type)
                  << ". Disconnecting duplicate device " << dev.identifier() << "\n";
        dev.disconnect();
        return;
    }

    SimpleBLE::BluetoothAddress addr = dev.address();
    dev.set_callback_on_disconnected([addr]()
                                     {
                                         if (exit_flag)
                                             return;
                                         std::cout << "Device disconnected: " << addr << "\n";
                                         std::lock_guard<std::mutex> lock(devices_mutex);
                                         connected_devices.erase(std::remove_if(connected_devices.begin(), connected_devices.end(),
                                                                                [addr](ConnectedDevice &entry)
                                                                                { return entry.peripheral.address() == addr; }),
                                                                 connected_devices.end()); });

    {
        std::lock_guard<std::mutex> lock(devices_mutex);
        connected_devices.push_back({dev, type});
    }

    if (std::find_if(
            characteristics.begin(),
            characteristics.end(),
            [](SimpleBLE::Characteristic &c)
            { return c.uuid() == DATA_CHAR_UUID; }) == characteristics.end())
    {
        std::cout << "Data characteristic not found on device: " << dev.identifier() << "\n";
        dev.disconnect();
        return;
    }

    if (std::find_if(
            characteristics.begin(),
            characteristics.end(),
            [](SimpleBLE::Characteristic &c)
            { return c.uuid() == BATTERY_LEVEL_UUID; }) == characteristics.end())
    {
        std::cout << "Battery level characteristic not found on device: " << dev.identifier() << "\n";
        dev.disconnect();
        return;
    }

    dev.notify(
        INSOLE_SERVICE_UUID,
        DATA_CHAR_UUID,
        [type, ch_count](SimpleBLE::ByteArray data)
        {
            if (!connect_flag)
                return;

            int packet_size = get_packet_size(ch_count);
            int pktCount = data.size() / packet_size;

            if (type == 1)
                cnt1 += pktCount;
            else
                cnt2 += pktCount;

            for (int i = 0; i < pktCount; i++)
            {
                uint8_t *pkt = data.data() + i * packet_size;
                uint64_t timestamp = 0;
                memcpy(&timestamp, pkt, 5);

                std::vector<uint16_t> adcValues(ch_count);

                unpack12bit(adcValues.data(), ch_count, pkt + 5);

                std::vector<int16_t> values(ch_count);

                for (int j = 0; j < ch_count; j++)
                {
                    values[j] = static_cast<int16_t>(adcValues[j]);
                }

                SensorData sensor_data;
                sensor_data.timestamp = timestamp;
                sensor_data.values = values.data();
                sensor_data.ch_count = ch_count;

                {
                    std::lock_guard<std::mutex> lock(servers_mutex);
                    auto it = tcp_servers.find(type);
                    if (it != tcp_servers.end())
                    {
                        it->second.send_data(sensor_data);
                    }
                }
            }
        });

    dev.notify(INSOLE_SERVICE_UUID, BATTERY_LEVEL_UUID, [type](SimpleBLE::ByteArray data)
               {
                   if (!connect_flag)
                       return;

                   const char *type_name = "Right";

                   if (type == InsoleType::INSOLE_LEFT)
                   {
                       type_name = "Left";
                   }

                   uint16_t voltage = 0;
                   memcpy(&voltage, data.data(), 2);
                   uint8_t percent = data[2];

                   std::cout << type_name << " insole battery voltage is " << voltage << " mV (" << (int)percent << "%)\n";
               });

    std::cout << "Connected to insole: " << dev.identifier() << "(" << dev.address() << ")\n";
}

void count_pps()
{
    while (!exit_flag)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        int current_cnt1 = cnt1.exchange(0);
        int current_cnt2 = cnt2.exchange(0);

        {
            std::lock_guard<std::mutex> lock(pps_mutex);

            // Добавляем новые значения в историю
            pps1_history.push(current_cnt1);
            pps2_history.push(current_cnt2);

            // Удаляем старые значения, если окно переполнено
            if (pps1_history.size() > PPS_WINDOW_SIZE)
            {
                pps1_history.pop();
            }
            if (pps2_history.size() > PPS_WINDOW_SIZE)
            {
                pps2_history.pop();
            }

            // Вычисляем бегущее среднее
            float avg_pps1 = 0;
            float avg_pps2 = 0;

            if (!pps1_history.empty())
            {
                int sum1 = 0;
                std::queue<int> temp1 = pps1_history;
                while (!temp1.empty())
                {
                    sum1 += temp1.front();
                    temp1.pop();
                }
                avg_pps1 = (float)sum1 / pps1_history.size();
            }

            if (!pps2_history.empty())
            {
                int sum2 = 0;
                std::queue<int> temp2 = pps2_history;
                while (!temp2.empty())
                {
                    sum2 += temp2.front();
                    temp2.pop();
                }
                avg_pps2 = (float)sum2 / pps2_history.size();
            }
            second_counter++;
            std::cout << "Time: " << second_counter << " PPS1: " << current_cnt1 << " (avg: " << (int)avg_pps1 << ") "
                      << "PPS2: " << current_cnt2 << " (avg: " << (int)avg_pps2 << ")\n";
        }
    }
}

int main(int argc, char **argv)
{
    int scan_duration = 5000;
    int right_insole_port = 8001;
    int left_insole_port = 8002;
    int adapter_index = 0;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg.find("--scan-duration=") == 0)
        {
            scan_duration = std::stoi(arg.substr(16));
        }
        else if (arg.find("--right-port=") == 0)
        {
            right_insole_port = std::stoi(arg.substr(13));
        }
        else if (arg.find("--left-port=") == 0)
        {
            left_insole_port = std::stoi(arg.substr(12));
        }
        else if (arg.find("--adapter=") == 0)
        {
            adapter_index = std::stoi(arg.substr(10));
        }
        else
        {
            std::cout << "Unknown argument: " << arg << "\n";
            std::cout << "Usage: " << argv[0] << " [--scan-duration=ms] [--right-port=port] [--left-port=port] [--adapter=index]\n";
            return 1;
        }
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    if (!SimpleBLE::Adapter::bluetooth_enabled())
    {
        std::cout << "Bluetooth is not enabled\n";
        return 1;
    }

    auto adapters = SimpleBLE::Adapter::get_adapters();

    if (adapters.empty())
    {
        std::cerr << "No BLE adapters found\n";
        return 1;
    }

    for (size_t i = 0; i < adapters.size(); i++)
    {
        std::cout << "Adapter " << i << ": " << adapters[i].identifier() << "\n";
    }

    if (adapter_index < 0 || adapter_index >= (int)adapters.size())
    {
        std::cerr << "Invalid adapter index: " << adapter_index << "\n";
        return 1;
    }

    auto adapter = adapters[adapter_index];

    if (!adapter.is_powered())
    {
        std::cerr << "Adapter is not powered on\n";
        return 1;
    }

    current_adapter = &adapter;

    std::cout << "Using adapter: " << adapter.identifier() << "\n";
    std::cout << "Right insole port: " << right_insole_port << "\n";
    std::cout << "Left insole port: " << left_insole_port << "\n";

    {
        std::lock_guard<std::mutex> lock(servers_mutex);
        if (!tcp_servers[INSOLE_RIGHT].init(right_insole_port))
        {
            std::cerr << "Failed to initialize right insole TCP server\n";
            return 1;
        }
        if (!tcp_servers[INSOLE_LEFT].init(left_insole_port))
        {
            std::cerr << "Failed to initialize left insole TCP server\n";
            tcp_servers[INSOLE_RIGHT].close_socket();
            return 1;
        }
    }

    std::thread right_accept_thread([]()
                                    { tcp_servers[INSOLE_RIGHT].accept_clients(); });
    std::thread left_accept_thread([]()
                                   { tcp_servers[INSOLE_LEFT].accept_clients(); });
    right_accept_thread.detach();
    left_accept_thread.detach();

    std::cout << "Scanning for " << scan_duration << " ms...\n";
    adapter.scan_for(scan_duration);

    bool insole_found = false;

    for (auto &dev : adapter.scan_get_results())
    {

        for (auto &s : dev.services())
        {

            if (s.uuid() == INSOLE_SERVICE_UUID)
            {
                insole_found = true;
                std::cout << "Insole found! Device: " << dev.identifier() << "(" << dev.address() << ")\n";
                connect_to_insole(dev);

                break;
            }
        }
    }

    if (!insole_found)
    {
        std::cout << "No insoles found during scan period\n";
        {
            std::lock_guard<std::mutex> lock(servers_mutex);
            tcp_servers[INSOLE_RIGHT].close_socket();
            tcp_servers[INSOLE_LEFT].close_socket();
        }
        return 0;
    }

    connect_flag = true;

    std::thread counter_thread(count_pps);
    counter_thread.detach();

    while (!exit_flag)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (current_adapter && current_adapter->scan_is_active())
        current_adapter->scan_stop();

    devices_mutex.lock();
    if (!connected_devices.empty())
    {
        for (auto &entry : connected_devices)
        {
            if (!entry.peripheral.is_connected())
                continue;
            entry.peripheral.disconnect();
        }
        std::cout << "Disconnected all devices\n";
    }
    devices_mutex.unlock();

    {
        std::lock_guard<std::mutex> lock(servers_mutex);
        tcp_servers[INSOLE_RIGHT].close_socket();
        tcp_servers[INSOLE_LEFT].close_socket();
    }

    return 0;
}
