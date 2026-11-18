#include <iostream>
#include <cstring>
#include <cstdint>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    typedef int socklen_t;
#else
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
#endif

struct SensorData
{
    uint64_t timestamp;
    int16_t values[8];
};

int main(int argc, char **argv)
{
    const char *host = "localhost";
    int port = 8002;
    int max_packets = 0;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg.find("--host=") == 0)
        {
            host = argv[i] + 7;
        }
        else if (arg.find("--port=") == 0)
        {
            port = std::stoi(arg.substr(7));
        }
        else if (arg.find("--count=") == 0)
        {
            max_packets = std::stoi(arg.substr(8));
        }
    }

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
    {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0)
    {
        std::cerr << "Invalid address: " << host << "\n";
        close(sock);
        return 1;
    }

    std::cout << "Connecting to " << host << ":" << port << "...\n";

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "Connection failed\n";
        close(sock);
        return 1;
    }

    std::cout << "Connected!\n";

    SensorData data;
    int packet_count = 0;

    while (true)
    {
        int bytes_read = recv(sock, (char *)&data, sizeof(SensorData), 0);
        if (bytes_read != sizeof(SensorData))
        {
            std::cout << "Connection closed\n";
            break;
        }

        packet_count++;
        std::cout << "[" << packet_count << "] Timestamp: " << data.timestamp
                  << " | Values: ";
        for (int i = 0; i < 8; i++)
        {
            std::cout << data.values[i];
            if (i < 7) std::cout << ", ";
        }
        std::cout << "\n";

        if (max_packets > 0 && packet_count >= max_packets)
        {
            break;
        }
    }

    close(sock);

#ifdef _WIN32
    WSACleanup();
#endif

    std::cout << "Received " << packet_count << " packets\n";
    return 0;
}
