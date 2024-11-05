#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 46645
#define LISTEN_BACKLOG 5

// Shared variables
int total_requests = 0;
long total_received_bytes = 0;
long total_sent_bytes = 0;

// Mutex to protect shared variables
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to handle each client connection
void handleConnection(int a_client)
{
    char buffer[1024];
    int bytes_read = read(a_client, buffer, sizeof(buffer) - 1);

    if (bytes_read > 0)
    {
        buffer[bytes_read] = '\0';
        printf("Received: %s\n", buffer);

        char path[1000];
        char method[1000];
        char http_version[1000];
        sscanf(buffer, "%s %s %s", method, path, http_version);

        // Increment total requests and received bytes
        pthread_mutex_lock(&stats_mutex);
        total_requests++;
        total_received_bytes += bytes_read;
        pthread_mutex_unlock(&stats_mutex);

        if (!strcmp(method, "GET") && !strncmp(path, "/calc/", 6))
        {
            int a, b;
            sscanf(path, "/calc/%d/%d", &a, &b);
            dprintf(a_client,
                    "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n%d\n", a + b);
        }
        else if (!strcmp(method, "GET") && !strncmp(path, "/static/", 8))
        {
            // Extract the file path after "/static/"
            char filename[2000];
            snprintf(filename, sizeof(filename), ".%s", path);

            FILE *file = fopen(filename, "rb");
            if (file == NULL)
            {
                // If the file doesn't exist, return a 404 error
                dprintf(a_client, "HTTP/1.1 404 Not Found\nContent-Type: "
                                  "text/plain\n\nFile Not Found\n");
            }
            else
            {
                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                fseek(file, 0, SEEK_SET);

                // Send HTTP response header
                dprintf(a_client, "HTTP/1.1 200 OK\n");
                dprintf(a_client, "Content-Length: %ld\n", file_size);
                dprintf(a_client, "Content-Type: application/octet-stream\n\n");

                char file_buffer[1024];
                size_t bytes_read;
                while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer),
                                           file)) > 0)
                {
                    write(a_client, file_buffer, bytes_read);
                }

                fclose(file);

                // Update sent bytes count
                pthread_mutex_lock(&stats_mutex);
                total_sent_bytes += file_size;
                pthread_mutex_unlock(&stats_mutex);
            }
        }
        else if (!strcmp(method, "GET") && !strcmp(path, "/stats"))
        {
            // Return statistics in HTML format
            char stats_response[2048];
            int stats_len = snprintf(
                stats_response, sizeof(stats_response),
                "HTTP/1.1 200 OK\nContent-Type: text/html\n\n"
                "<html><head><title>Server Stats</title></head><body>"
                "<h1>Server Statistics</h1>"
                "<ul>"
                "<li>Total Requests: %d</li>"
                "<li>Total Bytes Received: %ld</li>"
                "<li>Total Bytes Sent: %ld</li>"
                "</ul>"
                "</body></html>",
                total_requests, total_received_bytes, total_sent_bytes);

            pthread_mutex_lock(&stats_mutex);
            total_sent_bytes += stats_len;
            pthread_mutex_unlock(&stats_mutex);

            write(a_client, stats_response, stats_len);
        }
        else
        {
            // Default case for unsupported HTTP methods
            dprintf(a_client, "HTTP/1.1 400 Bad Request\nContent-Type: "
                              "text/plain\n\nInvalid Request\n");
        }
    }

    close(a_client);
}

// Thread function to handle client connections
void *clientThread(void *arg)
{
    int client_fd = *((int *)arg);
    free(arg); // Free the allocated memory for client_fd

    handleConnection(client_fd);
    return NULL;
}

int main(int argc, char *argv[])
{
    int port = PORT; // default port
    if (argc > 2 && strcmp(argv[1], "-p") == 0)
    {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535)
        {
            fprintf(stderr, "Invalid port number. Please specify a port "
                            "between 1 and 65535.\n");
            return 1;
        }
    }
    else if (argc > 1)
    {
        fprintf(stderr, "Usage: %s [-p port]\n", argv[0]);
        return 1;
    }

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        perror("Failed to create socket");
        return 1;
    }

    struct sockaddr_in socket_address;
    memset(&socket_address, '\0', sizeof(socket_address));
    socket_address.sin_family = AF_INET;
    socket_address.sin_addr.s_addr = htonl(INADDR_ANY);
    socket_address.sin_port = htons(port);

    printf("Server listening on port %d\n", port);

    if (bind(socket_fd, (struct sockaddr *)&socket_address,
             sizeof(socket_address)) < 0)
    {
        perror("Failed to bind");
        close(socket_fd);
        return 1;
    }

    if (listen(socket_fd, LISTEN_BACKLOG) < 0)
    {
        perror("Failed to listen");
        close(socket_fd);
        return 1;
    }

    while (1)
    {
        struct sockaddr_in client_address;
        socklen_t client_address_len = sizeof(client_address);

        int *client_fd =
            malloc(sizeof(int)); // Dynamically allocate memory for client_fd
        if (client_fd == NULL)
        {
            perror("Failed to allocate memory");
            continue; // Skip to the next loop iteration
        }

        *client_fd = accept(socket_fd, (struct sockaddr *)&client_address,
                            &client_address_len);
        if (*client_fd < 0)
        {
            perror("Failed to accept connection");
            free(client_fd); // Free memory if accept fails
            continue;
        }

        // Create a new thread to handle the client connection
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, clientThread, client_fd) != 0)
        {
            perror("Failed to create thread");
            close(*client_fd); // Close socket if thread creation fails
            free(client_fd);   // Free allocated memory
        }
        else
        {
            pthread_detach(thread_id); // Detach the thread to automatically
                                       // clean up when finished
        }
    }

    close(socket_fd);
    return 0;
}
