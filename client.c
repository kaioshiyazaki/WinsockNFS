// Filename: client.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>   // For fixed-size integer types
#include <winsock2.h>
#include <windows.h>
/*#include <ctype.h> For case-insensitive command handling  */
#include <stdint.h>  // For fixed-size integer types

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 1024

// Function prototypes
void Authenticate(SOCKET socket);
void SendFile(SOCKET socket, const char* filename);
void ReceiveFile(SOCKET socket, const char* filename);

int main() {
    WSADATA wsa;
    SOCKET client_socket;
    struct sockaddr_in server;
    char message[BUFFER_SIZE], server_reply[BUFFER_SIZE];
    int recv_size;

    // Initialize Winsock
    printf("Initializing Winsock...\n");
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Failed. Error Code: %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    printf("Initialized.\n");

    // Create socket
    if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Could not create socket: %d\n", WSAGetLastError());
        WSACleanup();
        exit(EXIT_FAILURE);
    }
    printf("Socket created.\n");

    // Prepare the sockaddr_in structure
    server.sin_addr.s_addr = inet_addr(SERVER_IP);
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    // Connect to remote server
    if (connect(client_socket, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("Connect error: %d\n", WSAGetLastError());
        closesocket(client_socket);
        WSACleanup();
        exit(EXIT_FAILURE);
    }
    printf("Connected to server.\n");

    // Authenticate with the server
    Authenticate(client_socket);

    // Keep communicating with server
    while (1) {
        printf("Enter command (READ/WRITE/DELETE filename or EXIT): ");
        fgets(message, BUFFER_SIZE, stdin);
        // Remove newline character
        message[strcspn(message, "\n")] = '\0';

        // Parse command
        char message_copy[BUFFER_SIZE];
        strncpy(message_copy, message, BUFFER_SIZE);
        char *command = strtok(message_copy, " ");
        char *filename = strtok(NULL, " ");

        if (command == NULL) {
            printf("Invalid command format.\n");
            continue;
        }

        // Convert command to lowercase for case-insensitive comparison
        for (int i = 0; command[i]; i++) {
            command[i] = tolower((unsigned char)command[i]);
        }

        if (strcmp(command, "exit") == 0) {
            break;
        }

        if (filename == NULL && strcmp(command, "exit") != 0) {
            printf("Filename is required for this command.\n");
            continue;
        }

        // Send command to server
        if (send(client_socket, message, strlen(message), 0) == SOCKET_ERROR) {
            printf("Send failed: %d\n", WSAGetLastError());
            break;
        }

        // Handle WRITE command (send file contents)
        if (strcmp(command, "write") == 0) {
            SendFile(client_socket, filename);
        }

        // Receive a reply from the server
        recv_size = recv(client_socket, server_reply, BUFFER_SIZE - 1, 0);
        if (recv_size == SOCKET_ERROR) {
            printf("Recv failed: %d\n", WSAGetLastError());
            break;
        }
        server_reply[recv_size] = '\0';
        printf("Server reply: %s\n", server_reply);

        // Handle READ command (receive file contents)
        if (strcmp(command, "read") == 0 && strstr(server_reply, "Error") == NULL) {
            ReceiveFile(client_socket, filename);
        }
    }

    closesocket(client_socket);
    WSACleanup();
    return 0;
}

// Function to authenticate with the server
void Authenticate(SOCKET socket) {
    char password[BUFFER_SIZE];
    char server_reply[BUFFER_SIZE];
    int recv_size;

    printf("Enter password: ");
    fgets(password, BUFFER_SIZE, stdin);
    // Remove newline character
    password[strcspn(password, "\n")] = '\0';

    // Send password to server
    if (send(socket, password, strlen(password), 0) == SOCKET_ERROR) {
        printf("Send failed: %d\n", WSAGetLastError());
        closesocket(socket);
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    // Receive authentication response
    recv_size = recv(socket, server_reply, BUFFER_SIZE - 1, 0);
    if (recv_size <= 0) {
        printf("Failed to receive authentication response.\n");
        closesocket(socket);
        WSACleanup();
        exit(EXIT_FAILURE);
    }
    server_reply[recv_size] = '\0';

    if (strcmp(server_reply, "Authentication Successful") == 0) {
        printf("Authenticated successfully.\n");
    } else {
        printf("Authentication failed.\n");
        closesocket(socket);
        WSACleanup();
        exit(EXIT_FAILURE);
    }
}

// Function to send file contents to the server
void SendFile(SOCKET socket, const char* filename) {
    FILE *file = fopen(filename, "rb");
    char file_buffer[BUFFER_SIZE];
    int bytes_read;

    if (file == NULL) {
        printf("Error: Cannot open file.\n");
        return;
    }

    // Receive the server's readiness status code
    uint32_t net_status_code;
    int bytes_left = sizeof(net_status_code);
    char *ptr = (char*)&net_status_code;

    while (bytes_left > 0) {
        int recv_size = recv(socket, ptr, bytes_left, 0);
        if (recv_size <= 0) {
            printf("Failed to receive status code from server.\n");
            fclose(file);
            return;
        }
        ptr += recv_size;
        bytes_left -= recv_size;
    }

    uint32_t status_code = ntohl(net_status_code);
    if (status_code != 0) {
        printf("Server is not ready to receive the file.\n");
        fclose(file);
        return;
    }

    // Get the file size
    fseek(file, 0, SEEK_END);
    uint32_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint32_t net_file_size = htonl(file_size);

    // Send the file size
    if (send(socket, (char*)&net_file_size, sizeof(net_file_size), 0) == SOCKET_ERROR) {
        printf("Send failed: %d\n", WSAGetLastError());
        fclose(file);
        return;
    }

    // Send file contents
    while ((bytes_read = fread(file_buffer, 1, BUFFER_SIZE, file)) > 0) {
        int total_sent = 0;
        while (total_sent < bytes_read) {
            int sent = send(socket, file_buffer + total_sent, bytes_read - total_sent, 0);
            if (sent == SOCKET_ERROR) {
                printf("Send failed: %d\n", WSAGetLastError());
                fclose(file);
                return;
            }
            total_sent += sent;
        }
    }
    fclose(file);
}

// Function to receive file contents from the server
void ReceiveFile(SOCKET socket, const char* filename) {
    char file_buffer[BUFFER_SIZE];
    int recv_size;

    // Receive the status code
    uint32_t net_status_code;
    int bytes_left = sizeof(net_status_code);
    char *ptr = (char*)&net_status_code;

    while (bytes_left > 0) {
        recv_size = recv(socket, ptr, bytes_left, 0);
        if (recv_size <= 0) {
            printf("Failed to receive status code.\n");
            return;
        }
        ptr += recv_size;
        bytes_left -= recv_size;
    }

    uint32_t status_code = ntohl(net_status_code);

    if (status_code == 0) {
        // Status success, proceed to receive file size and data
        // Receive the file size
        uint32_t net_file_size;
        bytes_left = sizeof(net_file_size);
        ptr = (char*)&net_file_size;

        while (bytes_left > 0) {
            recv_size = recv(socket, ptr, bytes_left, 0);
            if (recv_size <= 0) {
                printf("Failed to receive file size.\n");
                return;
            }
            ptr += recv_size;
            bytes_left -= recv_size;
        }

        // Convert file size from network byte order to host byte order
        uint32_t file_size = ntohl(net_file_size);
        printf("File size: %u bytes\n", file_size);

        // Open the file for writing
        FILE *file = fopen(filename, "wb");
        if (file == NULL) {
            printf("Error: Cannot create file.\n");
            return;
        }

        // Receive the file data
        int total_received = 0;
        while (total_received < file_size) {
            int bytes_to_receive = BUFFER_SIZE;
            if (file_size - total_received < BUFFER_SIZE) {
                bytes_to_receive = file_size - total_received;
            }
            recv_size = recv(socket, file_buffer, bytes_to_receive, 0);
            if (recv_size <= 0) {
                printf("Failed to receive file data.\n");
                break;
            }
            fwrite(file_buffer, 1, recv_size, file);
            total_received += recv_size;
        }

        fclose(file);

        if (total_received == file_size) {
            printf("File received and saved as '%s'.\n", filename);
        } else {
            printf("File transfer incomplete. Received %d out of %u bytes.\n", total_received, file_size);
        }
    } else if (status_code == 1) {
        // Status error, receive error message length
        uint32_t net_error_len;
        bytes_left = sizeof(net_error_len);
        ptr = (char*)&net_error_len;

        while (bytes_left > 0) {
            recv_size = recv(socket, ptr, bytes_left, 0);
            if (recv_size <= 0) {
                printf("Failed to receive error length.\n");
                return;
            }
            ptr += recv_size;
            bytes_left -= recv_size;
        }

        uint32_t error_len = ntohl(net_error_len);
        if (error_len >= BUFFER_SIZE) {
            printf("Error message too long.\n");
            return;
        }

        // Receive the error message
        bytes_left = error_len;
        ptr = file_buffer; // Reuse file_buffer to hold error message
        while (bytes_left > 0) {
            recv_size = recv(socket, ptr, bytes_left, 0);
            if (recv_size <= 0) {
                printf("Failed to receive error message.\n");
                return;
            }
            ptr += recv_size;
            bytes_left -= recv_size;
        }

        file_buffer[error_len] = '\0'; // Null-terminate the error message
        printf("Server reply: %s\n", file_buffer);
    } else {
        printf("Unknown status code received: %u\n", status_code);
    }
}

