#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define HTML_FILE "index.html"

void serve_file(int client_socket) {
    FILE *file = fopen(HTML_FILE, "r");
    if (file == NULL) {
        char *error_msg = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n<html><body><h1>404 Not Found</h1></body></html>";
        send(client_socket, error_msg, strlen(error_msg), 0);
        exit(1);
    }

    char file_content[2048];
    size_t bytes;
    char response[2048];

    char header[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
    send(client_socket, header, strlen(header), 0);

    while ((bytes = fread(file_content, 1, sizeof(file_content), file)) != 0) {
        send(client_socket, file_content, bytes, 0);
    }

    fclose(file);
}

int main() {
    int server_socket, client_socket;
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

    if (listen(server_socket, 5) < 0) {
        perror("Listening failed");
        exit(1);
    }

    printf("Server is running on http://localhost:%d\n", PORT);

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_address, &address_len);
        if (client_socket < 0) {
            perror("Accepting failed");
            exit(1);
        }

        serve_file(client_socket);
        close(client_socket);
    }

    return 0;
}
