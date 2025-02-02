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
    int server_socket; //For the file descriptor, the file descriptor will be used to handle the socket by OS.
    struct sockaddr_in server_address, client_address; //To store IPV4 address information
    /*
    Structure of sockaddr_in
    struct sockaddr_in{
        sa_family_t sin_family; // Address family
        in_addr_t sin_addr; // Internet address
        unsigned short sin_port; // Port number
        char sin_zero[8]; // Padding to make the sockaddr_in structure the same size as sockaddr structure which is used for other address families.
    }
    */
    socklen_t address_len = sizeof(client_address); // Getting the size of client_address, it will be used when accepting a connection to store the size of client's address.

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    /*
     * socket() creates a new network socket and return a file descriptor.
     * AF_INET (AF_* means address family) and SOCK_STREAM (SOCK_* means socket type) macros are from sys/socket.h which is included by arpa/inet.h
     * AF_INET tells the socket function that you that you want to use IPV4 Addressing. If you wanted to use IPV6, you could have used AF_INET6
     * SOCK_STREAM tell the socket function that socket type is TCP. If you wanted the socket type as UDP, you could have used SOCK_DGRAM instead of SOCK_STREAM.
     * 3rd parameter is for defining the protocol to follow, in this case 0 which is default for TCP.
     */

    //If socket fails, server_socket's value will be -1, meaning failed to create a file descriptor.
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(1); // Exiting the program because the server cannot continue without a socket.
    }

    server_address.sin_family = AF_INET; // Setting the address family to IPV4
    server_address.sin_addr.s_addr = INADDR_ANY; 
    /*
    * Setting the IP address to listen on.
    * INADDR_ANY is a special constant that represent any available network interface on the machine.
    * If you want to restict you server to listen on a specific IP address, you can define it like this:
        server.sin_addr.s_addr = inet_addr("192.168.1.10");
    */
    server_address.sin_port = htons(PORT); // Define the port where the server will listen for incoming connections. htons() function converts the port number from host byte order to network byte order.

    /*
    * bind() function assigns the server_socket to a specific IP sddress and port number (from server_address)
    * If bind() fails, that means another server is already using the spot or permissions are restricted.
    */
    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Binding failed");
        exit(1);
    }

    /*
    * listen() funtion tells the OS that server_socket is now ready to accept incoming connections.
    * The 50 is the backlog, the maximum numberof connections that can wait in the queue before they are accepted.
    * If listen() fails, it shows an error message and exit the program. This might fail if the soocket was not properly bound or there is an issue with system resources.
    */
    if (listen(server_socket, 50) < 0) {
        perror("Listening failed");
        exit(1);
    }

    printf("Server is running on http://localhost:%d\n", PORT);

    // Run an infinity loop that will accept new clients continuously.
    while (1) {
        int *client_socket = malloc(sizeof(int)); //Dinamically allocate memmory for an int to store the client's socket descriptor.
        if (!client_socket) {
            perror("Memory allocation failed");
            continue; //If memory allocation fails, loop will continue running.
        }

        /*
        * When a new client tries to connect, a new socket descriptor is assigned to *client_socket.
        * If accept() fails, it will print an error message and free the client_socket and continue the loop again.
        */
        *client_socket = accept(server_socket, (struct sockaddr *)&client_address, &address_len);
        if (*client_socket < 0) {
            perror("Accepting failed");
            free(client_socket);
            continue;
        }

        /*
        * If the program waits until completing the tasks for a single client, then requests from other clients will be rejected.
        * So, we can use threads which is a lightweight mini-process that runs inside a program. It allows a program to do multiple things at the same time.
        * In this program, each client gets its own thread. So, multiple clients can be handled simultaneosly.
        */
        pthread_t thread_id; // Declares a thread identifier (a unique ID for the thread)

        /*
        * pthread_create() function will create a new thread and run the necessary process inside it.
        * In the first argument, you pass the identifier of the thread.
        * Second argument is to specify attributes of the thread. In this program, it is NULL as we want it as a default.
        * Third argument specify the process to run inside the thread, which is a function that returns a void pointer and takes a single argument that is a void pointer.
        * Forth argument is a void pointer which is the argument that we should pass to run the function we defined in third argument.
        */
        if (pthread_create(&thread_id, NULL, handle_client, client_socket) != 0) {
            perror("Thread creation failed");
            free(client_socket);
        }

        pthread_detach(thread_id); // You tells the system that you don't need this thread anymore and clean its resources when it finishes.
    }

    close(server_socket); //Shutting down the Server and free up the resources used by the socket.
    return 0;
}
