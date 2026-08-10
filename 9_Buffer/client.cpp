#include <cstring>
#include <iostream>
#include "src/util.hpp"
#include <unistd.h>
#include "src/Socket.hpp"
#include "src/InetAddress.hpp"
#include "src/Buffer.hpp"
#define MAX_BUFFER_SIZE 1024

int main()
{
    Socket* client = new Socket();
    InetAddress* serverAddr = new InetAddress("192.168.48.128" , 8888);
    client->connect(serverAddr);

    int sockfd = client->getSockfd();

    Buffer* sendBuffer = new Buffer();
    Buffer* readBuffer = new Buffer();

    while(true)
    {
        sendBuffer->getLine();
        ssize_t write_bytes = write(sockfd , sendBuffer->c_str() , sendBuffer->size());
        if(write_bytes == -1)
        {
            errif(true , "write failed");       
            break;   
        }
        

        int already_read = 0;
        char buff[MAX_BUFFER_SIZE];
        while(true)
        {
            memset(buff , 0 , sizeof(buff));
            ssize_t read_bytes = read(sockfd , buff , sizeof(buff));
            if(read_bytes > 0)
            {
                already_read += read_bytes;
                readBuffer->append(buff , read_bytes);
            }
            else if(read_bytes == 0)
            {
                std::cout << "server disconnect" << std::endl;
                break;
            }
            
            if(already_read >= sendBuffer->size())
            {
                std::cout << "read: " << readBuffer->c_str() << std::endl;
                break;
            }
        }
        readBuffer->clear();
       
        
    }
    delete sendBuffer;
    delete readBuffer;
    delete serverAddr;
    delete client;
    return 0;
}