#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "util.hpp"
#include <unistd.h>

int main()
{
    int sockfd = socket(AF_INET , SOCK_STREAM , 0);
    errif(sockfd == -1, "socket creation failed");
   
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(8080);
    server_address.sin_addr.s_addr = inet_addr("192.168.153.128");

    errif(connect(sockfd , (struct sockaddr*)&server_address , sizeof(server_address)) == -1, "connect failed");

    while(true)
    {
        char buffer[1024];
        memset(buffer , 0 ,sizeof(buffer));
        std::cin.getline(buffer, sizeof(buffer));
        ssize_t write_bytes = write(sockfd , buffer , strlen(buffer));
        if(write_bytes > 0) 
        {
            std::cout << "write_bytes: " << write_bytes << ", buffer: " << buffer << std::endl;
        }
        else if(write_bytes == 0)
        {
            std::cout << "Connection closed by server" << std::endl;
            close(sockfd);
            break;
        }
        else if(write_bytes == -1)
        {
            errif(true, "socket write failed");
            close(sockfd);
            break;
        }
    }
    close(sockfd);
    return 0;
}