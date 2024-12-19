// Filename: server.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>  // For fixed-size integer types
#include <winsock2.h>
#include <windows.h>
#include <time.h>
// #include <ctype.h> 

#pragma comment(lib, "ws2_32.lib")

#define PASSWORD "password"  // Set your desired password here
#define PORT 8080
#define BUFFER_SIZE 1024

// Status codes
#define STATUS_SUCCESS 0
#define STATUS_ERROR 1

// Function prototypes
DWORD WINAPI ClientHandler(void* socket_desc);
void LogOperation(const char* operation);

// Entry point for the server application
int main() {
    WSADATA wsa;
    SOCKET server_socket, client_socket;
    struct sockaddr_in server, client;
    int c = sizeof(struct sockaddr_in);
    HANDLE thread_handle;

    // Initialize Winsock
    printf("Initializing Winsock...\n");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Failed. Error Code: %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    printf("Initialized.\n");

    // Create socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Could not create socket: %d\n", WSAGetLastError());
        WSACleanup();
        exit(EXIT_FAILURE);
    }
    printf("Socket created.\n");

    // Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    // Bind
    if (bind(server_socket, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        exit(EXIT_FAILURE);
    }
    printf("Bind done.\n");

    // Listen to incoming connections
    listen(server_socket, 3);
    printf("Waiting for incoming connections...\n");

    while ((client_socket = accept(server_socket, (struct sockaddr*)&client, &c)) != INVALID_SOCKET) {
        printf("Connection accepted.\n");
        LogOperation("Connection accepted");

        // Create a new thread for each client
        thread_handle = CreateThread(NULL, 0, ClientHandler, (void*)client_socket, 0, NULL);
        if (thread_handle == NULL) {
            printf("Could not create thread: %d\n", GetLastError());
            closesocket(client_socket);
        } else {
            CloseHandle(thread_handle);
        }
    }

    if (client_socket == INVALID_SOCKET) {
        printf("Accept failed: %d\n", WSAGetLastError());
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}

// Function to handle each client connection
DWORD WINAPI ClientHandler(void* socket_desc) {
    SOCKET client_socket = (SOCKET)socket_desc;
    char client_message[BUFFER_SIZE];
    int recv_size;
    char *command, *filename;
    FILE *file;
    char file_buffer[BUFFER_SIZE];
    int bytes_read;

    // **Authentication Step**
    // Receive password from client
    recv_size = recv(client_socket, client_message, BUFFER_SIZE - 1, 0);
    if (recv_size <= 0) {
        printf("Failed to receive password.\n");
        closesocket(client_socket);
        return 0;
    }
    client_message[recv_size] = '\0';

    // Remove any newline characters
    client_message[strcspn(client_message, "\r\n")] = '\0';

    // Debugging print
    printf("Password received: '%s'\n", client_message);

    // Check if the password matches
    if (strcmp(client_message, PASSWORD) != 0) {
        // Authentication failed
        send(client_socket, "Authentication Failed", strlen("Authentication Failed"), 0);
        printf("Client failed authentication.\n");
        closesocket(client_socket);
        return 0;
    } else {
        // Authentication successful
        send(client_socket, "Authentication Successful", strlen("Authentication Successful"), 0);
        printf("Client authenticated successfully.\n");
    }

    // Receive messages from client
    while ((recv_size = recv(client_socket, client_message, BUFFER_SIZE - 1, 0)) > 0) {
        client_message[recv_size] = '\0';
        printf("Received: %s\n", client_message);

        // Parse command
        char client_message_copy[BUFFER_SIZE];
        strncpy(client_message_copy, client_message, BUFFER_SIZE);
        command = strtok(client_message_copy, " ");
        filename = strtok(NULL, " ");

        if (command == NULL) {
            send(client_socket, "Error: Invalid command format\n", strlen("Error: Invalid command format\n"), 0);
            continue;
        }

        // Convert command to lowercase for case-insensitive comparison
        for (int i = 0; command[i]; i++) {
            command[i] = tolower((unsigned char)command[i]);
        }

        if (filename == NULL && strcmp(command, "exit") != 0) {
            send(client_socket, "Error: Filename is required\n", strlen("Error: Filename is required\n"), 0);
            continue;
        }

        // Handle READ command
        if (strcmp(command, "read") == 0) {
            file = fopen(filename, "rb");
            if (file == NULL) {
                LogOperation("File read error");
                // Send status code for error
                uint32_t status_code = htonl(STATUS_ERROR); // 1 indicates error
                send(client_socket, (char*)&status_code, sizeof(status_code), 0);
                // Send length of error message
                const char *error_msg = "Error: File not found\n";
                uint32_t error_len = htonl(strlen(error_msg));
                send(client_socket, (char*)&error_len, sizeof(error_len), 0);
                // Send error message
                send(client_socket, error_msg, strlen(error_msg), 0);
            } else {
                LogOperation("File read");
                // Send status code for success
                uint32_t status_code = htonl(STATUS_SUCCESS); // 0 indicates success
                send(client_socket, (char*)&status_code, sizeof(status_code), 0);

                // Get the file size
                fseek(file, 0, SEEK_END);
                uint32_t file_size = ftell(file);
                fseek(file, 0, SEEK_SET); // Reset file pointer to the beginning

                // Convert file size to network byte order
                uint32_t net_file_size = htonl(file_size);

                // Send the file size
                if (send(client_socket, (char*)&net_file_size, sizeof(net_file_size), 0) == SOCKET_ERROR) {
                    printf("Send failed: %d\n", WSAGetLastError());
                    fclose(file);
                    break;
                }

                // Send file contents
                while ((bytes_read = fread(file_buffer, 1, BUFFER_SIZE, file)) > 0) {
                    int total_sent = 0;
                    while (total_sent < bytes_read) {
                        int sent = send(client_socket, file_buffer + total_sent, bytes_read - total_sent, 0);
                        if (sent == SOCKET_ERROR) {
                            printf("Send failed: %d\n", WSAGetLastError());
                            break;
                        }
                        total_sent += sent;
                    }
                }
                fclose(file);
            }
        }
        // Handle WRITE command
        else if (strcmp(command, "write") == 0) {
            file = fopen(filename, "wb");
            if (file == NULL) {
                LogOperation("File write error");
                // Send status code for error
                uint32_t status_code = htonl(STATUS_ERROR);
                send(client_socket, (char*)&status_code, sizeof(status_code), 0);
                // Send length of error message
                const char *error_msg = "Error: Cannot open file for writing\n";
                uint32_t error_len = htonl(strlen(error_msg));
                send(client_socket, (char*)&error_len, sizeof(error_len), 0);
                // Send error message
                send(client_socket, error_msg, strlen(error_msg), 0);
            } else {
                LogOperation("File write");
                // Send status code for success
                uint32_t status_code = htonl(STATUS_SUCCESS); // 0 indicates ready to receive file
                send(client_socket, (char*)&status_code, sizeof(status_code), 0);

                // Receive the file size
                uint32_t net_file_size;
                int bytes_left = sizeof(net_file_size);
                char *ptr = (char*)&net_file_size;

                while (bytes_left > 0) {
                    recv_size = recv(client_socket, ptr, bytes_left, 0);
                    if (recv_size <= 0) {
                        printf("Failed to receive file size.\n");
                        fclose(file);
                        break;
                    }
                    ptr += recv_size;
                    bytes_left -= recv_size;
                }

                uint32_t file_size = ntohl(net_file_size);

                // Receive file contents
                int total_received = 0;
                while (total_received < file_size) {
                    int bytes_to_receive = BUFFER_SIZE;
                    if (file_size - total_received < BUFFER_SIZE) {
                        bytes_to_receive = file_size - total_received;
                    }
                    recv_size = recv(client_socket, file_buffer, bytes_to_receive, 0);
                    if (recv_size <= 0) {
                        printf("Failed to receive file data.\n");
                        break;
                    }
                    fwrite(file_buffer, 1, recv_size, file);
                    total_received += recv_size;
                }
                fclose(file);

                // Send confirmation message
                const char *success_msg = "File written successfully\n";
                uint32_t success_len = htonl(strlen(success_msg));
                send(client_socket, (char*)&success_len, sizeof(success_len), 0);
                send(client_socket, success_msg, strlen(success_msg), 0);
            }
        }
        // Handle DELETE command
        else if (strcmp(command, "delete") == 0) {
            if (remove(filename) == 0) {
                // Send status code for success
                uint32_t status_code = htonl(STATUS_SUCCESS);
                send(client_socket, (char*)&status_code, sizeof(status_code), 0);

                // Send success message
                const char *success_msg = "File deleted successfully\n";
                uint32_t success_len = htonl(strlen(success_msg));
                send(client_socket, (char*)&success_len, sizeof(success_len), 0);
                send(client_socket, success_msg, strlen(success_msg), 0);

                LogOperation("File deleted");
            } else {
                // Send status code for error
                uint32_t status_code = htonl(STATUS_ERROR);
                send(client_socket, (char*)&status_code, sizeof(status_code), 0);

                // Send error message
                const char *error_msg = "Error: Cannot delete file\n";
                uint32_t error_len = htonl(strlen(error_msg));
                send(client_socket, (char*)&error_len, sizeof(error_len), 0);
                send(client_socket, error_msg, strlen(error_msg), 0);

                LogOperation("File delete error");
            }
        }
        // Handle EXIT command
        else if (strcmp(command, "exit") == 0) {
            printf("Client requested to exit.\n");
            closesocket(client_socket);
            break;
        }
        // Handle unknown command
        else {
            // Send status code for error
            uint32_t status_code = htonl(STATUS_ERROR);
            send(client_socket, (char*)&status_code, sizeof(status_code), 0);

            // Send error message
            const char *error_msg = "Error: Unknown command\n";
            uint32_t error_len = htonl(strlen(error_msg));
            send(client_socket, (char*)&error_len, sizeof(error_len), 0);
            send(client_socket, error_msg, strlen(error_msg), 0);
        }
    }

    if (recv_size == SOCKET_ERROR) {
        printf("Recv failed: %d\n", WSAGetLastError());
    } else if (recv_size == 0) {
        printf("Client disconnected.\n");
        LogOperation("Client disconnected");
    }

    closesocket(client_socket);
    return 0;
}

// Function to log operations with timestamps
void LogOperation(const char* operation) {
    FILE *log_file = fopen("server_log.txt", "a");
    time_t now = time(NULL);
    char *timestamp = ctime(&now);
    // Remove newline character from timestamp
    timestamp[strcspn(timestamp, "\n")] = '\0';
    fprintf(log_file, "[%s] %s\n", timestamp, operation);
    fclose(log_file);
}
