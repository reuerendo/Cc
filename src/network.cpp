#include "network.h"
#include <cstring>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>

NetworkManager::NetworkManager() 
    : socketFd(-1), udpSocketFd(-1) {
}

NetworkManager::~NetworkManager() {
    disconnect();
    closeUDPSocket();
}

bool NetworkManager::createUDPSocket() {
    udpSocketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocketFd < 0) {
        return false;
    }
    
    // Enable broadcast
    int broadcastEnable = 1;
    if (setsockopt(udpSocketFd, SOL_SOCKET, SO_BROADCAST, 
                   &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        close(udpSocketFd);
        udpSocketFd = -1;
        return false;
    }
    
    // Bind to any address on port 8134 (companion port)
    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port = htons(8134);
    
    if (bind(udpSocketFd, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
        close(udpSocketFd);
        udpSocketFd = -1;
        return false;
    }
    
    return true;
}

void NetworkManager::closeUDPSocket() {
    if (udpSocketFd >= 0) {
        close(udpSocketFd);
        udpSocketFd = -1;
    }
}

bool NetworkManager::sendUDPBroadcast(int port) {
    struct sockaddr_in broadcastAddr;
    memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_addr.s_addr = inet_addr("255.255.255.255");
    broadcastAddr.sin_port = htons(port);
    
    const char* message = "hello";
    int result = sendto(udpSocketFd, message, strlen(message), 0,
                       (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
    
    return result >= 0;
}

bool NetworkManager::receiveUDPResponse(std::string& host, int& port, int timeoutMs) {
    struct timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(udpSocketFd, &readfds);
    
    int result = select(udpSocketFd + 1, &readfds, NULL, NULL, &timeout);
    if (result <= 0) {
        return false;
    }
    
    char buffer[1024];
    struct sockaddr_in fromAddr;
    socklen_t fromLen = sizeof(fromAddr);
    
    int received = recvfrom(udpSocketFd, buffer, sizeof(buffer) - 1, 0,
                           (struct sockaddr*)&fromAddr, &fromLen);
    
    if (received <= 0) {
        return false;
    }
    
    buffer[received] = '\0';
    
    // Parse response: "calibre wireless device client (on hostname);content_port,socket_port"
    std::string response(buffer);
    size_t portPos = response.rfind(',');
    if (portPos == std::string::npos) {
        return false;
    }
    
    port = atoi(response.substr(portPos + 1).c_str());
    host = inet_ntoa(fromAddr.sin_addr);
    
    return port > 0;
}

bool NetworkManager::discoverCalibreServer(std::string& host, int& port,
                                           std::function<bool()> cancelCallback) {
    if (!createUDPSocket()) {
        return false;
    }
    
    // Try each broadcast port
    for (int i = 0; i < BROADCAST_PORT_COUNT; i++) {
        if (cancelCallback && cancelCallback()) {
            closeUDPSocket();
            return false;
        }
        
        if (!sendUDPBroadcast(BROADCAST_PORTS[i])) {
            continue;
        }
        
        if (receiveUDPResponse(host, port, 3000)) {
            closeUDPSocket();
            return true;
        }
    }
    
    closeUDPSocket();
    return false;
}

bool NetworkManager::connectToServer(const std::string& host, int port) {
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) {
        return false;
    }
    
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
        close(socketFd);
        socketFd = -1;
        return false;
    }
    
    // Set connection timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    if (connect(socketFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(socketFd);
        socketFd = -1;
        return false;
    }
    
    return true;
}

void NetworkManager::disconnect() {
    if (socketFd >= 0) {
        close(socketFd);
        socketFd = -1;
    }
}

bool NetworkManager::sendAll(const void* data, size_t length) {
    const char* ptr = (const char*)data;
    size_t remaining = length;
    
    while (remaining > 0) {
        ssize_t sent = send(socketFd, ptr, remaining, 0);
        if (sent <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        ptr += sent;
        remaining -= sent;
    }
    
    return true;
}

bool NetworkManager::receiveAll(void* buffer, size_t length) {
    char* ptr = (char*)buffer;
    size_t remaining = length;
    
    while (remaining > 0) {
        ssize_t received = recv(socketFd, ptr, remaining, 0);
        if (received <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        ptr += received;
        remaining -= received;
    }
    
    return true;
}

std::string NetworkManager::receiveString() {
    // Read length prefix (format: "1234[...")
    char lengthBuf[32];
    int lengthPos = 0;
    
    while (lengthPos < sizeof(lengthBuf) - 1) {
        char c;
        if (!receiveAll(&c, 1)) {
            return "";
        }
        
        if (c == '[') {
            lengthBuf[lengthPos] = '\0';
            break;
        }
        
        lengthBuf[lengthPos++] = c;
    }
    
    int dataLength = atoi(lengthBuf);
    if (dataLength <= 0 || dataLength > 10 * 1024 * 1024) { // 10MB max
        return "";
    }
    
    // Read JSON data (including the '[' we already saw)
    std::vector<char> buffer(dataLength + 1);
    buffer[0] = '[';
    
    if (!receiveAll(&buffer[1], dataLength - 1)) {
        return "";
    }
    
    buffer[dataLength] = '\0';
    return std::string(buffer.data());
}

bool NetworkManager::sendJSON(CalibreOpcode opcode, const char* jsonData) {
    if (socketFd < 0) return false;
    
    // Format: length + JSON data
    // JSON data format: [opcode, {data}]
    std::string message = "[" + std::to_string((int)opcode) + "," + jsonData + "]";
    std::string packet = std::to_string(message.length()) + message;
    
    return sendAll(packet.c_str(), packet.length());
}

bool NetworkManager::receiveJSON(CalibreOpcode& opcode, std::string& jsonData) {
    if (socketFd < 0) return false;
    
    std::string message = receiveString();
    if (message.empty()) {
        return false;
    }
    
    jsonData = message;
    
    // Parse opcode from JSON array: [opcode, {...}]
    size_t opcodeEnd = message.find(',');
    if (opcodeEnd == std::string::npos || message[0] != '[') {
        return false;
    }
    
    int opcodeValue = atoi(message.substr(1, opcodeEnd - 1).c_str());
    opcode = (CalibreOpcode)opcodeValue;
    
    return true;
}

bool NetworkManager::sendBinaryData(const void* data, size_t length) {
    if (socketFd < 0) return false;
    return sendAll(data, length);
}

bool NetworkManager::receiveBinaryData(void* buffer, size_t length) {
    if (socketFd < 0) return false;
    return receiveAll(buffer, length);
}
