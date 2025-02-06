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

// Function to identify the MIME type
const char *get_mime_type(const char *path) {
    /*
     * strrchr() function is used to find the last occurance of a specific character and return a pointer to it.
     * In this case we are getting the '.' (dot)'s location and return the pointer to it.
     */
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

/*
 * serve_file() function is used to send a file to a client over a socket.
 * It takes 2 arguments.
 * First argument is the file descriptor of the client socket.
 * Second argument is the file to serve and the file path will not be changed inside the function.
 */
void serve_file(int client_socket, const char *file_path) {
    /*
     * The stat struct comes from sys/stat.h header file.
     * That structure holds information about a file such as file size, permissions. type of file and etc.
     */
    struct stat file_stat;

    /*
     * stat() function can be used to get file information by passing 2 arguments.
     * First argument specifies the file path.
     * Second argument is the location where you want to save the information about the file.
       It should be a pointer in the type of stat struct.
     * If stat() function failed to find the file in the specified path, it will return -1.
     * Even though stat() function was able to access the specified file path, it could be a directory. In that case, it should not be served.
     * So, S_ISDIR() function will be used to check whether the accessed file is a directory or not.
     * If conditions are not met, a 404 page will be showed.
     */
    if (stat(file_path, &file_stat) < 0 || S_ISDIR(file_stat.st_mode)) {
        const char *error_msg = 
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n"
            "<html><body><h1>404 Not Found</h1></body></html>";
        /*
         * send() function is used to send data through a socket.
         * First argument specifies the socket descriptor representing the connection with the client.
         * Second argument is the message that will be sent (a string).
         * Third argument specifies the length of the message or how many bytes to send.
         * Forth argument is 0 as we do not need special options.
         */
        send(client_socket, error_msg, strlen(error_msg), 0);
        /*
         * After sending the 404 response, the server stops writing but ensures the client has time to process the response before closing the connection.
         */
        shutdown(client_socket, SHUT_WR); // Stop sending but still allow reading
        close(client_socket); // Fully close the connection.
        return;
    }

    /*
     * Open the file at file_path for reading (r) in binary mode (b).
     * Binary mode is for serving both text and binary files.
     */
    FILE *file = fopen(file_path, "rb"); // 'rb' is for opening the file for reading in binary mode.

    const char *mime_type = get_mime_type(file_path); // Get MIME type of the file

    if (!file) {    
        if (strcmp(mime_type, "text/html") == 0) {
            // If it's an HTML page, show a 404 error page
            const char *error_msg =
                "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n"
                "<html><body><h1>404 Not Found</h1></body></html>";
            send(client_socket, error_msg, strlen(error_msg), 0);
            shutdown(client_socket, SHUT_WR);
            usleep(1000);
            close(client_socket);
        } 
        else {
            // For other files (images, CSS, JS, etc.), just send a 404 response
            const char *error_msg =
                "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(client_socket, error_msg, strlen(error_msg), 0);
        }
    }

    /*
     * Send HTTP Header, so the browser know how to handle the file properly.
     */
    char header[256];
    
    /*
     * snprintf() function formats a string safely into header, ensuring it does not exceed 256 bytes.
     * First argument specifies where to write the formatted string.
     * Second argument specifies the maximum size of the string, more than that will not formatted.
     * Third argument is the string to be formatted.
     * Other arguments are values for formatting the string.
     */
    snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", mime_type);
    send(client_socket, header, strlen(header), 0);

    char buffer[4096]; // To temporarily hold data before sending to the client.
    size_t bytes_read; // To track how much data is read from the file.

    /*
     * After completing the confirmations about the file, we can send the file to the client using send().
     * If the file we are sending is large, it is not an efficient way to send the whole file at once. So, we send the file chunk by chunk.
     * fread() reads up to sizeof(buffer) from file into buffer.
     * bytes_read holds the actual number of bytes read. In the last chunk, it will be lesser than the buffer size.
     * First argument is the buffer which is where we store data temporarily for sending.
     * Second argument tells fread() that read the file byte by byte.
     * Third argument specify how many bytes to store (size of the buffer)
     * Final argument is the pointer to the file we need to read.
     */
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }

    fclose(file); // Closes the file that was opened by fopen() and resources are freed.
    
    /*
     * Tell the client that the server will not send any more data.
     * But the client might still send data back.
     * This helps ensuring the client processes the full response.
     */
    shutdown(client_socket, SHUT_WR);

    /*
     * Before we close the socket, client needs a moment to fully receive the shutdown signal before closing the socket.
     * So, we can pause the execution for 1 millisecond (1000 microseconds).
     * Without this some clients might not receive the data properly.
     */
    usleep(1000);

    /*
     * Closes the socket and free up system resources.
     * After this, no further communication can happen on this socket.
     * But, server still runs until you close.
    */
    close(client_socket);
}

/*
 * We created a thread for each client and inside the thread we are running handle_client() function to perform necessary tasks on client request.
 * handle_client() function returns NULL after the execution, thus we used return type as void pointer.
 * pthread_create() function passes a void pointer (4th arg in that function) as the parameter of the function which is performing inside the thread.
   you can see that as 'void *arg' here.
 */
void *handle_client(void *arg) {
    int client_socket = *(int *)arg; // Gets the client socket's file descriptor which we need when we work with OS.
    free(arg); // While we got the file descriptor of client socket, we are freeing that memory allocated for arg (client_socket in main function), so we will not have memory leaks later on.

    char buffer[2048]; // To temporarily store incoming requests from the client.

    /*
     * recv() function is used to read data from the client_socket and store in buffer.
     * Third argument specifies the size of buffer size with a 1 space left which will be used to store a null terminator (\0).
     * Forth argument is used to specify extra options. But, we have used default which is 0.
     */
    recv(client_socket, buffer, sizeof(buffer) - 1, 0);

    /*
     * Client's HTTP request is consist of 3 parts, Method, URL and Protocol.
     * We will need to extract those 3 parts seperately to handle the user request efficiently.
     * Method refers to the HTTP method of the request.
     * URL specifies the requested URL (e.g., /index.html).
     * Protocol specifies the HTTP protocol version (e.g., HTTP/1.1).

     * After defining the strings (array of characters) for those 3 parts, use sscanf function will be used to capture different parts in the HTTP request.
     * As the first argument in the sscanf() function, add the source.
     * As the second argument, specify the format how you want to seperate the source. If you are extracting a string, amke sure you keep 1 space for null terminator.
     * Other arguments are the pointers to the locations where the separated parts are stored. 
     * While array variables are already a pointer, do not use & (e.g., &method).
     */
    char method[16], url[256], protocol[32];
    sscanf(buffer, "%15s %255s %31s", method, url, protocol);

    /*
     * The server is lightweight and it will only handle GET request.
     * That means server will serve static files.
     * So, if a user make a request other than a GET, server will stop serving to that user.
     */
    if (strcmp(method, "GET") != 0) {
        close(client_socket);
        return NULL;
    }

    /* 
     * Not mandatory, but if somebody tries to access files in other directories than where you serve
       This code snippet will stop it. 
     * '..' means "go up one directory". If this was found in the user will be directed to the DEFAULT_FILE which is homepage.
     * To serve any file, we will use serve_file().
     */
    if (strstr(url, "..")) {
        serve_file(client_socket, DEFAULT_FILE);
        return NULL;
    }

    char file_path[512]; // This is used to store the correct file path to serve the client.
    if (strcmp(url, "/") == 0) {
        /*
         * snprintf() function can be used to make a formatted string and store it inside a variable.
         * First argument specifies the variable, where the formatted string is saved.
         * Second argument specifies the size of that variable or maximum number of characters that can be stored.
         * Third argument specifies the format of the string.
         * Other arguments will be necessary values for the specified format.
         */
        snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, DEFAULT_FILE); // If requested file is '/', send the DEFAULT_FILE.
    } else {
        snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, url + 1); // Remove leading "/"
    }

    // Finally, serve the file using serve_file() function by passing the client_socket and file_path.
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
