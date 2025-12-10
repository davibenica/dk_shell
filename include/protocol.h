#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <vector>
#include <cstring>
#include <sstream>

#define MAX_FILENAME_LEN 256
#define BUFFER_SIZE 8192

#define CMD_LIST "LIST"
#define CMD_UPLOAD "UPLOAD"
#define CMD_DOWNLOAD "DOWNLOAD"
#define CMD_DELETE "DELETE"

#define RESP_OK "OK"
#define RESP_ERROR "ERROR"
#define RESP_DATA "DATA"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>



/**
 * @brief Split a string by delimiter
 */
inline std::vector<std::string> split_string(const std::string& str, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim)) {
        tokens.push_back(token);
    }
    return tokens;
}

/**
 * @brief Read a line from socket (blocking)
 * @return Empty string on error/disconnect
 */
inline std::string read_line(int sockfd) {
    std::string line;
    char c;
    while (true) {
        ssize_t n = recv(sockfd, &c, 1, 0);
        if (n <= 0) return "";
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

/**
 * @brief Send a line to socket
 */
inline bool send_line(int sockfd, const std::string& line) {
    std::string msg = line + "\n";
    ssize_t sent = send(sockfd, msg.c_str(), msg.length(), 0);
    return sent == (ssize_t)msg.length();
}

/**
 * @brief Send exact number of bytes
 */
inline bool send_all(int sockfd, const char* data, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t sent = send(sockfd, data + total, len - total, 0);
        if (sent <= 0) return false;
        total += sent;
    }
    return true;
}

/**
 * @brief Receive exact number of bytes
 */
inline bool recv_all(int sockfd, char* buffer, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t received = recv(sockfd, buffer + total, len - total, 0);
        if (received <= 0) return false;
        total += received;
    }
    return true;
}

#endif 