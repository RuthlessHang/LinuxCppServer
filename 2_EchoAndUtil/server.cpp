#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "util.hpp"
#include <unistd.h>

int main() 
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    errif(sockfd == -1, "socket creation failed");
    
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(8080);
    server_address.sin_addr.s_addr = inet_addr("192.168.153.128");

    errif(bind(sockfd, (struct sockaddr*)&server_address, sizeof(server_address)) == -1, "bind failed");
    errif(listen(sockfd, SOMAXCONN) == -1, "listen failed");
    std::cout << "Server started on port 8080" << std::endl;

    struct sockaddr_in client_address;
    socklen_t client_address_len = sizeof(client_address);
    memset(&client_address, 0, sizeof(client_address));
    int client_socket = accept(sockfd, (struct sockaddr*)&client_address, &client_address_len);
    errif(client_socket == -1, "accept failed");

    std::cout << "Client connected: " << inet_ntoa(client_address.sin_addr) << std::endl;

    while(true)
    {
        char buffer[1024];
        memset(buffer , 0 , sizeof(buffer));
        ssize_t read_bytes = read(client_socket , buffer ,sizeof(buffer));
        if(read_bytes > 0 )
        {
           std::cout << "Received: " << buffer << std::endl;
            write(client_socket , buffer , read_bytes);
        }
        else if(read_bytes == 0)
        {
            std::cout << "Client disconnected" << std::endl;
            close(client_socket);
            break;
        }
        else if(read_bytes == -1)
        {
            errif(true, "socket read failed");
            close(client_socket);
            break;
        }
        
    }

    close(sockfd);
    return 0;
}