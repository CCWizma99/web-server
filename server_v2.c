#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/ainet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>

#define PORT 8080
#define WEB_ROOT "./web/"  // Serve files from the web directory
#define DEFAULT_FILE "index.html"
#define MAX_CLIENTS 10

static int server_socket; // Server socket descriptor
static int running = 1;   // Flag for server running status

// Function to handle graceful shutdown when SIGINT (Ctrl+C) is received
void signal_handler(int signum) {
    if (signum == SIGINT) {
        printf("\nServer shutting down gracefully...\n");
        running = 0; // Mark the server as not running anymore
        close(server_socket); // Close the server socket to stop accepting new connections
    }
}

// Function to determine the MIME type of a requested file
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

// Function to serve a requested file to the client
void serve_file(int client_socket, const char *file_path) {
    struct stat file_stat;
    if (stat(file_path, &file_stat) < 0 || S_ISDIR(file_stat.st_mode)) {
        // If file doesn't exist or is a directory, serve the 404 page
        char file_path[30];
        snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, "page-not-found.html");
        serve_file(client_socket, file_path);
        shutdown(client_socket, SHUT_WR);
        close(client_socket);
        return;
    }

    FILE *file = fopen(file_path, "rb");
    const char *mime_type = get_mime_type(file_path);

    if (!file) {
        // If file cannot be opened, return 404 error response
        if (strcmp(mime_type, "text/html") == 0) {
            char file_path[30];
            snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, "page-not-found.html");
            serve_file(client_socket, file_path);
            shutdown(client_socket, SHUT_WR);
            close(client_socket);
        } else {
            const char *error_msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(client_socket, error_msg, strlen(error_msg), 0);
        }
        return;
    }

    // Send HTTP response header
    char header[256];
    snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", mime_type);
    send(client_socket, header, strlen(header), 0);

    // Send file content
    char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }

    fclose(file);
    shutdown(client_socket, SHUT_WR);
    usleep(1000); // Small delay before closing socket
    close(client_socket);
}

// Thread function to handle client requests
void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[2048];
    recv(client_socket, buffer, sizeof(buffer) - 1, 0);

    char method[16], url[256], protocol[32];
    sscanf(buffer, "%15s %255s %31s", method, url, protocol);

    // Only support GET requests; return 400 Bad Request for others
    if (strcmp(method, "GET") != 0) {
        char file_path[30];
        snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, "bad-request.html");
        serve_file(client_socket, file_path);
        return NULL;
    }

    // Prevent directory traversal attacks
    if (strstr(url, "..")) {
        char file_path[30];
        snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, "access-denied.html");
        serve_file(client_socket, file_path);
        return NULL;
    }

    // Construct the full file path
    char file_path[512];
    if (strcmp(url, "/") == 0) {
        snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, DEFAULT_FILE);
    } else {
        snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, url + 1);
    }

    serve_file(client_socket, file_path);
    return NULL;
}

int main() {
    struct sockaddr_in server_address, client_address;
    socklen_t address_len = sizeof(client_address);

    // Create the server socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    // Bind the socket to the specified port
    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Binding failed");
        exit(1);
    }

    // Start listening for incoming connections
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("Listening failed");
        exit(1);
    }

    // Register signal handler for graceful shutdown
    signal(SIGINT, signal_handler);

    printf("Server is running on http://localhost:%d\n", PORT);

    while (running) {
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

        // Handle client requests in a separate thread
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client_socket) != 0) {
            perror("Thread creation failed");
            free(client_socket);
        }

        pthread_detach(thread_id);
    }

    close(server_socket);
    printf("Server has been shut down.\n");
    return 0;
}
