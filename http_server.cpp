#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <unistd.h>
#include <sys/wait.h>

using namespace std;
using boost::asio::ip::tcp;

struct envVars{
    string request_method;
    string request_uri;
    string query_string;
    string server_protocol;
    string http_host;
    string server_addr;
    string server_port;
    string remote_addr;
    string remote_port;
};


class session
  : public std::enable_shared_from_this<session>
{
public:
    session(tcp::socket socket)
        : socket_(std::move(socket)) {
    }

    void start() {
        do_read();
    }

private:
    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {              
                string msg = data_;
                // A request example
                // GET /panel.cgi?queryString=hello HTTP/1.1   => REQUEST_METHOD and REQUEST_URI(and query string)
                // Host: 140.113.235.221:7000   => HTTP_HOST 
                // User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:107.0) Gecko/20100101 Firefox/107.0
                // Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8
                // Accept-Language: zh-TW,zh;q=0.8,en-US;q=0.5,en;q=0.3
                // Accept-Encoding: gzip, deflate
                // DNT: 1
                // Connection: keep-alive
                // Upgrade-Insecure-Requests: 1      
                vector<string> e;
                boost::split(e, msg, boost::is_any_of(" \n"), boost::token_compress_on);

                envVars envs = initVars(e);    

                processRequest(envs);
            }
            });
    }

    envVars initVars(vector<string>& e) {

        envVars envs;
        envs.request_method = e[0];
        envs.server_protocol = e[2];
        envs.http_host = e[4];

        string uri = e[1];
        int delimIndex = uri.find_first_of('?');

        envs.request_uri = uri;
        
        string requestedPath = uri.substr(0, delimIndex);

        // no query string or ? is at the end of requestedPath
        if(delimIndex == string::npos || delimIndex == requestedPath.size() - 1) {
            envs.query_string = "";
        } else{
            // +1 to skip the ?
            envs.query_string = uri.substr(delimIndex + 1);
        }


        // https://stackoverflow.com/questions/601763/how-to-get-ip-address-of-boostasioiptcpsocket
        // port is uint type
        envs.server_addr = socket_.local_endpoint().address().to_string();
        envs.server_port = to_string(socket_.local_endpoint().port());
        envs.remote_addr = socket_.remote_endpoint().address().to_string();
        envs.remote_port = to_string(socket_.remote_endpoint().port());
        return envs;
    }

    void processRequest(envVars envs) {
        auto self(shared_from_this());

        strcpy(data_, "HTTP/1.0 200 OK\r\n");
        boost::asio::async_write(socket_, boost::asio::buffer(data_, strlen(data_)),
            [this, self, envs](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                // for a child to process the request
                int pid = fork();
                if(pid == 0) {
                    initEnvs(envs);
                    // This function may be used to obtain the underlying representation of the socket. This is intended to allow access to native socket functionality that is not otherwise provided. 
                    // redirect the stdio to the socket fd
                    dup2(socket_.native_handle(), STDIN_FILENO);
                    dup2(socket_.native_handle(), STDOUT_FILENO);

                    // no need the use the fd anymore
                    close(socket_.native_handle());

                    string cgiPath;
                    if(envs.request_uri.find('?') == string::npos) {
                        cgiPath = "." + envs.request_uri;
                    } else {
                        cgiPath = "." + envs.request_uri.substr(0, envs.request_uri.find('?'));
                    }

                    char **args = new char*[2];
                    args[0] = new char[cgiPath.size() + 1];
                    strcpy(args[0], cgiPath.c_str());

                    args[1] = nullptr;

                    if(execv(cgiPath.c_str(), args) == -1) {
                        cout << strerror(errno) << endl;
                    }

                    exit(0);
                } else {
                    waitpid(pid, nullptr, 0);
                    socket_.close();
                }
            }
            });
    }

    void initEnvs(envVars envs) {
        // The following environment variables are required to set:
        // (a) REQUEST METHOD
        // (b) REQUEST URI
        // (c) QUERY STRING
        // (d) SERVER PROTOCOL
        // (e) HTTP HOST
        // (f) SERVER ADDR
        // (g) SERVER PORT
        // (h) REMOTE ADDR
        // (i) REMOTE PORT

        setenv("REQUEST_METHOD", envs.request_method.c_str(), 1);
        setenv("REQUEST_URI", envs.request_uri.c_str(), 1);
        setenv("QUERY_STRING", envs.query_string.c_str(), 1);
        setenv("SERVER_PROTOCOL", envs.server_protocol.c_str(), 1);
        setenv("HTTP_HOST", envs.http_host.c_str(), 1);
        setenv("SERVER_ADDR", envs.server_addr.c_str(), 1);
        setenv("SERVER_PORT", envs.server_port.c_str(), 1);
        setenv("REMOTE_ADDR", envs.remote_addr.c_str(), 1);
        setenv("REMOTE_PORT", envs.remote_port.c_str(), 1);
    }

    enum { max_length = 1024 };
    char data_[max_length];
    tcp::socket socket_;
};

class server {
public:
    server(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec)
            {
                // start a session here
                std::make_shared<session>(std::move(socket))->start();
            }

            do_accept();
            });
    }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
        std::cerr << "Usage: http_server <port>\n";
        return 1;
        }

        boost::asio::io_context io_context;

        server s(io_context, std::atoi(argv[1]));

        io_context.run();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

  return 0;
}