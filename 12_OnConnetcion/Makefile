server:
	g++ -std=c++17 -pthread -g main.cpp src/*.cpp -I src -o server && \
	g++ -std=c++17 client.cpp src/Connection.cpp src/Channel.cpp src/EventLoop.cpp src/Epoll.cpp src/util.cpp src/Socket.cpp src/InetAddress.cpp src/Buffer.cpp -I src -o client

clean:
	rm -rf server client
