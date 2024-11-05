#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 46645
#define LISTEN_BACKLOG 5

int total_requests = 0;
long total_received_bytes = 0;
long total_sent_bytes = 0;

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
            snprintf(filename, sizeof(filename), ".%s",
                     path); // Prepend '.' to make it relative to the current
                            // directory

            // Open the requested file
            FILE *file = fopen(filename, "rb");
            if (file == NULL)
            {
                // If the file doesn't exist, return a 404 error
                dprintf(a_client, "HTTP/1.1 404 Not Found\nContent-Type: "
                                  "text/plain\n\nFile Not Found\n");
            }
            else
            {
                // Get file size
                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                fseek(file, 0, SEEK_SET);

                // Send HTTP response header
                dprintf(a_client, "HTTP/1.1 200 OK\n");
                dprintf(a_client, "Content-Length: %ld\n", file_size);
                dprintf(a_client, "Content-Type: application/octet-stream\n\n");

                // Read file and send content to client
                char file_buffer[1024];
                size_t bytes_read;
                while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer),
                                           file)) > 0)
                {
                    write(a_client, file_buffer, bytes_read);
                }

                fclose(file);
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

            total_sent_bytes += stats_len;
            write(a_client, stats_response, stats_len);
        }
        else
        {
            // Default case for unsupported HTTP methods
            dprintf(a_client, "HTTP/1.1 400 Bad Request\nContent-Type: "
                              "text/plain\n\nInvalid Request\n");
        }

        write(a_client, buffer, bytes_read);
    }
    close(a_client);
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

        int client_fd = accept(socket_fd, (struct sockaddr *)&client_address,
                               &client_address_len);
        if (client_fd < 0)
        {
            perror("Failed to accept connection");
            continue; // continue accepting new connections
        }

        handleConnection(client_fd);
    }

    close(socket_fd);
    return 0;
}