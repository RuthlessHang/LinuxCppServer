#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>
#include "util.h"

int main()
{
	int sockfd = socket(AF_INET ,SOCK_STREAM , 0);

	sockaddr_in client_addr;
	memset(&client_addr , 0 ,sizeof(client_addr));

	client_addr.sin_family = AF_INET;
	client_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	client_addr.sin_port = htons(8888);
	
	errif(connect(sockfd, (sockaddr*)&client_addr, sizeof(client_addr)) != 0, "connect failed!!");
	std::cout << "client start!!" << std::endl;

	while (true)
	{
		char buffer[1024];
		memset(buffer, 0, sizeof(buffer));
		std::cout << "Please input Msg to Server:" << std::endl;

		std::cin.getline(buffer, sizeof(buffer));
		ssize_t writeBytes =  write(sockfd, buffer, sizeof(buffer));
		errif(writeBytes == -1, "write is failed!!!");

		memset(buffer, 0, sizeof(buffer));
		ssize_t recvBytes = read(sockfd, buffer, sizeof(buffer));
		if(recvBytes > 0)
		{
			std::cout << "Recv Server Msg:" << buffer << std::endl;
		}
		else if (recvBytes == 0)
		{
			std::cout << "Server is disconnect!!!" << std::endl;
			close(sockfd);
			break;
		}
		else if(recvBytes == -1)
		{
			close(sockfd);
			errif(true ,"read is failed!!!");
		}
	}

	close(sockfd);
	return 0;
}