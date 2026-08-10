#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "util.hpp"
#include <unistd.h>

#define MAX_BUFFER_SIZE 1024

int main()
{
    int sockfd = socket(AF_INET , SOCK_STREAM , 0);
    errif(sockfd == -1, "socket creation failed");
   
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(8080);
    server_address.sin_addr.s_addr = inet_addr("192.168.48.128");

    errif(connect(sockfd , (struct sockaddr*)&server_address , sizeof(server_address)) == -1, "connect failed");

    while(true)
    {
        //服务端write 直接传 sizeof(buf)，而不是实际读到的 bytes_read，所以客户端这里必须大小必须大于或等于服务器端buf大小，不然会出错
        char buffer[MAX_BUFFER_SIZE]; 
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