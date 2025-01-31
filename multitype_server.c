#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>

#define PORT 8080
#define WEB_ROOT "./"  // Serve files from the current directory
#define DEFAULT_FILE "index.html"

// Function to determine MIME type
const char *get_mime_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream"; // Default for unknown files

    if (strcmp(dot, ".html") == 0) return "text/html";
    if (strcmp(dot, ".css") == 0) return "text/css";
    if (strcmp(dot, ".js") == 0) return "application/javascript";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".gif") == 0) return "image/gif";
    if (strcmp(dot, ".json") == 0) return "application/json";
    if (strcmp(dot, ".txt") == 0) return "text/plain";
    if (strcmp(dot, ".pdf") == 0) return "application/pdf";
    
    return "application/octet-stream"; // Default for unknown files
}

void serve_file(int client_socket, const char *file_path) {
    struct stat file_stat;
    
    if (stat(file_path, &file_stat) < 0 || S_ISDIR(file_stat.st_mode)) {
        const char *error_msg = 
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n"
            "<html><body><h1>404 Not Found</h1></body></html>";
        send(client_socket, error_msg, strlen(error_msg), 0);
        shutdown(client_socket, SHUT_WR);
        close(client_socket);
        return;
    }

    FILE *file = fopen(file_path, "rb"); // Use "rb" to handle binary files properly
    if (!file) {
        serve_file(client_socket, DEFAULT_FILE);
        return;
    }

    char buffer[4096];
    size_t bytes_read;
    
    // Get MIME type
    const char *mime_type = get_mime_type(file_path);

    // Send HTTP Header
    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", mime_type);
    send(client_socket, header, strlen(header), 0);

    // Send File Content
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }

    fclose(file);
    shutdown(client_socket, SHUT_WR);
    usleep(1000);
    close(client_socket);
}

void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[2048];
    recv(client_socket, buffer, sizeof(buffer) - 1, 0);

    // Extract requested file path
    char method[16], url[256], protocol[32];
    sscanf(buffer, "%15s %255s %31s", method, url, protocol);

    if (strcmp(method, "GET") != 0) {
        close(client_socket);
        return NULL;
    }

    // Prevent directory traversal attacks
    if (strstr(url, "..")) {
        serve_file(client_socket, DEFAULT_FILE);
        return NULL;
    }

    // If root ("/") is requested, serve default file
    char file_path[512];
    if (strcmp(url, "/") == 0) {
        snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, DEFAULT_FILE);
    } else {
        snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, url + 1); // Remove leading "/"
    }

    serve_file(client_socket, file_path);
    return NULL;
}

int main() {
    int server_socket;
    struct sockaddr_in server_address, client_address;
    socklen_t address_len = sizeof(client_address);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Binding failed");
        exit(1);
    }

    if (listen(server_socket, 50) < 0) {
        perror("Listening failed");
        exit(1);
    }

    printf("Server is running on http://localhost:%d\n", PORT);

    while (1) {
        int *client_socket = malloc(sizeof(int));
        if (!client_socket) {
            perror("Memory allocation failed");
            continue;
        }

        *client_socket = accept(server_socket, (struct sockaddr *)&client_address, &address_len);
        if (*client_socket < 0) {
            perror("Accepting failed");
            free(client_socket);
            continue;
        }

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client_socket) != 0) {
            perror("Thread creation failed");
            free(client_socket);
        }

        pthread_detach(thread_id);
    }

    close(server_socket);
    return 0;
}
