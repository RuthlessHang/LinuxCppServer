#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>

int main() 
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        std::cout << "socket creation failed" << std::endl;
        return 1;
    }

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(8888);
    server_address.sin_addr.s_addr = inet_addr("192.168.153.128");
    bind(sockfd, (struct sockaddr*)&server_address, sizeof(server_address));
    listen(sockfd, SOMAXCONN);

    struct sockaddr_in client_address;
    socklen_t client_address_len = sizeof(client_address);
    memset(&client_address, 0, sizeof(client_address));
    int client_socket = accept(sockfd, (struct sockaddr*)&client_address, &client_address_len);

    std::cout << "Client connected: " << inet_ntoa(client_address.sin_addr)  << server_address.sin_port  << std::endl;
    return 0;
}