#include "protocol.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <pthread.h>       
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>

#define SERVER_FILES_DIR "./server_files"

// Global pthread mutex for file operations
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Ensure server files directory exists
 */
void ensure_directory() {
    struct stat st;
    if (stat(SERVER_FILES_DIR, &st) != 0) {
        mkdir(SERVER_FILES_DIR, 0755);
    }
}

/**
 * @brief Get full path for a file in server storage
 */
std::string get_file_path(const std::string& filename) {
    return std::string(SERVER_FILES_DIR) + "/" + filename;
}

/**
 * @brief Handle LIST command
 */
void handle_list(int client_fd) {
    pthread_mutex_lock(&file_mutex);
    
    DIR* dir = opendir(SERVER_FILES_DIR);
    if (!dir) {
        pthread_mutex_unlock(&file_mutex);
        send_line(client_fd, std::string(RESP_ERROR) + "|Failed to open directory");
        return;
    }
    
    // Send OK 
    send_line(client_fd, std::string(RESP_OK) + "|File list");
    
    // Send each filename
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG) {
            send_line(client_fd, entry->d_name);
        }
    }
    
    // Send empty line to signal end
    send_line(client_fd, "");
    
    closedir(dir);
    
    pthread_mutex_unlock(&file_mutex);
}

/**
 * @brief Handle UPLOAD command
 */
void handle_upload(int client_fd, const std::string& filename, size_t filesize) {
   
    std::vector<char> buffer(filesize);
    if (!recv_all(client_fd, buffer.data(), filesize)) {
        send_line(client_fd, std::string(RESP_ERROR) + "|Failed to receive file data");
        return;
    }
    
    pthread_mutex_lock(&file_mutex);
    
    std::string filepath = get_file_path(filename);
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        pthread_mutex_unlock(&file_mutex);  
        send_line(client_fd, std::string(RESP_ERROR) + "|Failed to create file");
        return;
    }
    
    file.write(buffer.data(), filesize);
    file.close();
    
  
    pthread_mutex_unlock(&file_mutex);
    
    send_line(client_fd, std::string(RESP_OK) + "|File uploaded successfully");
    std::cout << "Uploaded: " << filename << " (" << filesize << " bytes)\n";
}

/**
 * @brief Handle DOWNLOAD command
 */
void handle_download(int client_fd, const std::string& filename) {
    
    pthread_mutex_lock(&file_mutex);
    
    std::string filepath = get_file_path(filename);
    
    // Open file
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        pthread_mutex_unlock(&file_mutex);  // MUST unlock before return!
        send_line(client_fd, std::string(RESP_ERROR) + "|File not found");
        return;
    }
    
    size_t filesize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Read data
    std::vector<char> buffer(filesize);
    if (!file.read(buffer.data(), filesize)) {
        file.close();
        pthread_mutex_unlock(&file_mutex);  
        send_line(client_fd, std::string(RESP_ERROR) + "|Failed to read file");
        return;
    }
    file.close();
    
    // Unlock after reading (before sending to client)
    pthread_mutex_unlock(&file_mutex);
    
    // Send response 
    std::string response = std::string(RESP_OK) + "|" + std::string(RESP_DATA) + "|" + std::to_string(filesize);
    if (!send_line(client_fd, response)) {
        return;
    }

    if (!send_all(client_fd, buffer.data(), filesize)) {
        std::cerr << "Failed to send file data\n";
        return;
    }
    
    std::cout << "Downloaded: " << filename << " (" << filesize << " bytes)\n";
}

/**
 * @brief Handle DELETE command
 */
void handle_delete(int client_fd, const std::string& filename) {
    
    pthread_mutex_lock(&file_mutex);
    
    std::string filepath = get_file_path(filename);
    
    if (unlink(filepath.c_str()) != 0) {
        pthread_mutex_unlock(&file_mutex);  
        send_line(client_fd, std::string(RESP_ERROR) + "|Failed to delete file");
        return;
    }
    
    
    pthread_mutex_unlock(&file_mutex);
    
    send_line(client_fd, std::string(RESP_OK) + "|File deleted successfully");
    std::cout << "Deleted: " << filename << "\n";
}

/**
 * @brief Handle a single client connection
 * This function runs in its own thread
 */
void* handle_client(void* arg) {
    int client_fd = *(int*)arg;  
    free(arg);                    
    
    std::cout << "Client connected (fd: " << client_fd << ")\n";
    
    while (true) {
        
        std::string request = read_line(client_fd);
        if (request.empty()) {
            break; // Client disconnected
        }
        
        std::cout << "Received: " << request << "\n";
        
        std::vector<std::string> parts = split_string(request, '|');
        if (parts.empty()) continue;
        
        std::string cmd = parts[0];
        
        if (cmd == CMD_LIST) {
            handle_list(client_fd);
        }
        else if (cmd == CMD_UPLOAD) {
            if (parts.size() < 3) {
                send_line(client_fd, std::string(RESP_ERROR) + "|Invalid UPLOAD command");
                continue;
            }
            std::string filename = parts[1];
            size_t filesize = std::stoull(parts[2]);
            handle_upload(client_fd, filename, filesize);
        }
        else if (cmd == CMD_DOWNLOAD) {
            if (parts.size() < 2) {
                send_line(client_fd, std::string(RESP_ERROR) + "|Invalid DOWNLOAD command");
                continue;
            }
            std::string filename = parts[1];
            handle_download(client_fd, filename);
        }
        else if (cmd == CMD_DELETE) {
            if (parts.size() < 2) {
                send_line(client_fd, std::string(RESP_ERROR) + "|Invalid DELETE command");
                continue;
            }
            std::string filename = parts[1];
            handle_delete(client_fd, filename);
        }
        else {
            send_line(client_fd, std::string(RESP_ERROR) + "|Unknown command");
        }
    }
    
    close(client_fd);
    std::cout << "Client disconnected (fd: " << client_fd << ")\n";
    
    return NULL;  
}

/**
 * @brief Main server
 */
int main(int argc, char* argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : 8080;
    
    // Setup server directory
    ensure_directory();
    
   
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(server_fd);
        return 1;
    }
    
   
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }
    
   
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }
    
    std::cout << "Cloud storage server listening on port " << port << "\n";
    std::cout << "Storage directory: " << SERVER_FILES_DIR << "\n";
    
    
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }
        
       
        int* client_fd_ptr = (int*)malloc(sizeof(int));
        *client_fd_ptr = client_fd;
        
      
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client_fd_ptr) != 0) {
            perror("Thread creation failed");
            free(client_fd_ptr);
            close(client_fd);
            continue;
        }
        
        
        pthread_detach(thread_id);
    }
    
    close(server_fd);
    
    pthread_mutex_destroy(&file_mutex);
    
    return 0;
}