server:
	g++ -std=c++17 -pthread -g main.cpp src/*.cpp -I src -o server && \
	g++ -std=c++17 client.cpp src/util.cpp src/Socket.cpp src/InetAddress.cpp src/Buffer.cpp -o client

clean:
	rm -rf server client
