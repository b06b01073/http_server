part1:
	g++ http_server.cpp -o http_server -std=c++14 -pedantic -pthread -lboost_system
	g++ console.cpp -o console.cgi -std=c++14 -pedantic -pthread -lboost_system
part2:
	g++ cgi_server.cpp -o cgi_server -lws2_32 -lwsock32 -std=c++14