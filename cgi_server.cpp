#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/asio/ip/address.hpp>
#include <fstream> 
#include <boost/algorithm/string.hpp>

#define MAX_SESSION 5

using namespace std;
using boost::asio::ip::tcp;

// the io_context will be passed between different classes, use global var here
boost::asio::io_context io_context;

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

struct queryFields {
    string host;
    string port;
    string file;
};


class npSingle
    : public std::enable_shared_from_this<npSingle>
{
    public:

    // the npSinlge will transmit data to two differenct socket,
    npSingle(tcp::socket socket, shared_ptr<tcp::socket> serverSocket, tcp::endpoint endpoint, queryFields qf, int id)
        : socket_(move(socket)), serverSocket(serverSocket), endpoint{endpoint}, qf{qf}, id{id}
    {
        string filePath = "./test_case/" + qf.file;
        file.open(filePath);

        terminated = false;
        

        string command;
        while(getline(file, command)) {
            commands.push_back(command);
        }

        cout << "initializing npSingle..." << endl;

        for(auto& c: commands)
            cout << c << endl;

    }

    void start()
    {
        auto self(shared_from_this());

        // cerr << "connecting..." << endl;
        connect();
        
    }

    private:
    // connect first, then read write loop
    void connect() {
        cout << "connecting np_single...";
        auto self(shared_from_this());
        // connect to the np_single here, since now every read or write to socket_ is to np_single
        socket_.async_connect(endpoint, 
        [this, self](const boost::system::error_code ec)
        {

            if(!ec) {
                // printWelcome();
                do_read();
            } else {
                cerr << "connect error" << endl;
                connect();
            }

        });
    }

    void outputShell(string msg, bool isCommand){
        auto self(shared_from_this());
        string processedMsg = preprocess(msg);
        if(isCommand) 
            processedMsg = "<span>" + processedMsg + "</span>";
        string scriptMsg = "<script>document.getElementById(\'s" + to_string(id) + "\').innerHTML += \'" + processedMsg + "\';</script>";

        // cout << "the inserted script is: " << scriptMsg << endl;

        // when we are outputting data to shell, we are outputting data to server(serverSocket) instead of np_single(socket_)
        boost::asio::async_write(*serverSocket, boost::asio::buffer(scriptMsg.c_str(), scriptMsg.size()), [this, self](boost::system::error_code ec, std::size_t /*length*/) {
            if(!ec) {
                // cout << "command written!";
            } else {
                cout << ec << endl;
            }
        });
    }

    string preprocess(string msg) {
        // string literal contains an unescaped line break, t1 use LF t2~t5 use CRLF
        boost::algorithm::replace_all(msg, "\n", "<br>");
        boost::algorithm::replace_all(msg, "\r", "");

        // escape single quote in msg string
        boost::algorithm::replace_all(msg, "'", "&apos;");

        return msg;
    }


    // the data here is read from the np_single_golden
    void do_read()
    {

        cout << "do_reading..." << endl;
        auto self(shared_from_this());
        
        

        // read from np_single
        try {
            socket_.async_read_some(boost::asio::buffer(inBuffer, max_length),
                [this, self](boost::system::error_code ec, std::size_t length)
                {
                    cerr << "The msg in the inBuffer is: " << inBuffer << endl;
                    if (!ec)
                    {
                        string msg = string(inBuffer);
                        outputShell(msg, false);

                        clearBuffer(inBuffer);
                        // check this condition, otherwise it will get caught in an infinite loop, note that the welcome messge contains %
                        

                        if(msg.find('%') == string::npos) {
                            // if the data read from np_single contains no %, then it means the np_single is still outputting messages, we should go and read it(socket_)
                            do_read();
                        } else {
                            // if there is a % in the msg, then it means the np_single is waiting for input, we should get the file content from commands and write it to np_single(socket_)
                            do_write();
                        }
                    } else {
                         
                    }
                });
        } catch (std::exception& e){
            std::cerr << "Exception: " << e.what() << "\n";
        }
    }

    // once the data is read, it need to be clear from the buffer
    void clearBuffer(char buffer[]) {
        memset(buffer, '\0', max_length);
    }

    void do_write()
    {
        cout << "do_writing..." << endl;
        
        
        auto self(shared_from_this());
        
        commands[commandsIdx].push_back('\n');
        strcpy(outBuffer, commands[commandsIdx].c_str());
        cerr << "The msg in the outBuffer is: " << outBuffer << endl;
        string msg = string(outBuffer);
        outputShell(msg, true);

        commandsIdx++;
        // write the command to socket_(np_single)
        boost::asio::async_write(socket_, boost::asio::buffer(outBuffer, strlen(outBuffer)),
            [this, self](boost::system::error_code ec, std::size_t /*length*/)
            {
                if (!ec)
                {
                    string msg = outBuffer;
                    msg.pop_back();

                    if(msg == "exit") {
                        terminated = true;
                    }

                    clearBuffer(outBuffer);
                    
                    if(terminated) {
                        socket_.close();
                    }
                    else
                        do_read();
                    // cerr << "back to do_read" << endl;
                }
                else {
                    cerr << "Error in do_write: " << ec << endl;
                }
            });
    }


    tcp::socket socket_;
    shared_ptr<tcp::socket> serverSocket;
    enum { max_length = 50000 };
    char data_[max_length];
    queryFields qf;
    int id;
    tcp::endpoint endpoint;
    ifstream file;
    vector<string> commands;
    int commandsIdx = 0;
    char inBuffer[max_length];
    char outBuffer[max_length];
    bool terminated = false;
};


class session
  : public std::enable_shared_from_this<session>
{
public:
    session(tcp::socket socket)
        : socket_(std::move(socket))
    {
        // the info socket is in socket_ now
    }

    void start()
    {
        do_read();
    }

private:
    void do_read()
    {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
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

                    // init the envs here, and then check the envs.request_uri to see which cgi to run, since we don't setenv here, the envs need to be stored as a private variable 
                    envs = initVars(e);    

                    // parse the cgi file name and execute it
                    dispatch(envs);

                    // processRequest(envs);
                }
            });
    }

    void dispatch(envVars envs) {
        string cgiPath = envs.request_uri.substr(0, envs.request_uri.find('?'));
        cout << cgiPath << endl;

        if(cgiPath == "/panel.cgi") {
            processPanel();
        } else {
            processConsole();
        }
    }

    void processPanel() {
        string panelHtml = getPanelHtml(envs);
        writePanelHtml(panelHtml);
    }

    void writePanelHtml(string panelHtml) {
        auto self(shared_from_this());
        // https://developer.mozilla.org/en-US/docs/Web/HTTP/Messages
        // need to explicitly add the header, otherwise the html will be treated as plain text and enclosed in a <body> tag predefined by the browser

        string header = "HTTP/1.0 200 OK\r\n\r\n";

        strcpy(outBuffer, header.c_str());
            boost::asio::async_write(
                socket_, boost::asio::buffer(outBuffer, strlen(outBuffer)),
                [this, self, panelHtml](boost::system::error_code ec, size_t /*length*/) {
                    if (!ec) {
                        writePanelHtmlBody(panelHtml);
                    }
                }
            );
    }
    void writePanelHtmlBody(string panelHtml) {
        auto self(shared_from_this());
        strcpy(outBuffer, panelHtml.c_str());
        boost::asio::async_write(
            socket_, boost::asio::buffer(outBuffer, strlen(outBuffer)),
            [this, self](boost::system::error_code ec, size_t /*length*/) {
                if (!ec) {
                    cout << "html written!" << endl;
                }
            }
        );
    }

    void processConsole() {
        string consoleHtml = getConsoleHtml(envs);

        // get the individual query string and use it to establish a client connection, the qfs in a private variable(vector<queryFields)
        qfs = parseQueryStrings();

        for(int i = 0; i < MAX_SESSION; i++) {
            if(qfs[i].host != "") {
                string remoteAddr = envs.remote_addr;
                string remotePort = envs.remote_port;
                string clientIp = remoteAddr + ":" + remotePort;
                string replacedSubstr = "ip " + to_string(i);
                boost::algorithm::replace_all(consoleHtml, replacedSubstr, clientIp);
            }
        }
        // for(auto& k: q) {
        //     cout << "host: " << k.host << ", port: " << k.port << ", file: " << k.file << endl;
        // }
        writeConsoleHtml(consoleHtml);

        
        // startConnections();
    }

    void writeConsoleHtml(string consoleHtml) {
        auto self(shared_from_this());
        // https://developer.mozilla.org/en-US/docs/Web/HTTP/Messages
        // need to explicitly add the header, otherwise the html will be treated as plain text and enclosed in a <body> tag predefined by the browser

        string header = "HTTP/1.0 200 OK\r\n\r\n";

        strcpy(outBuffer, header.c_str());
            boost::asio::async_write(
                socket_, boost::asio::buffer(outBuffer, strlen(outBuffer)),
                [this, self, consoleHtml](boost::system::error_code ec, size_t /*length*/) {
                    if (!ec) {
                        // read the query string now to see which host and port to connect to, then build the connections and sending commands to them
                        writeConsoleHtmlBody(consoleHtml);
                    }
                }
            );
    }

    void writeConsoleHtmlBody(string consoleHtml) {
        auto self(shared_from_this());
        strcpy(outBuffer, consoleHtml.c_str());
        boost::asio::async_write(
            socket_, boost::asio::buffer(outBuffer, strlen(outBuffer)),
            [this, self](boost::system::error_code ec, size_t /*length*/) {
                if (!ec) {
                    cout << "html written!" << endl;
                    startConnections();
                }
            }
        );
    }

    string getPanelHtml(envVars envs) {
        
        // panel.txt is a copy and paste html from running the panel.cgi in http_server
        ifstream rawPanelHtml("panel.txt");
        
        // https://stackoverflow.com/questions/2602013/read-whole-ascii-file-into-c-stdstring
        stringstream htmlBuffer;
        htmlBuffer << rawPanelHtml.rdbuf();

        rawPanelHtml.close();
        return htmlBuffer.str();
    }

    string getConsoleHtml(envVars envs) {
        ifstream rawConsoleHtml("console.txt");

        stringstream htmlBuffer;
        htmlBuffer << rawConsoleHtml.rdbuf();

        rawConsoleHtml.close();
        return htmlBuffer.str();
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


    void do_write(std::size_t length)
    {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(data_, length),
            [this, self](boost::system::error_code ec, std::size_t /*length*/)
            {
                if (!ec)
                {
                    do_read();
                }
            });
    }

    vector<queryFields> parseQueryStrings() {
        string queryString = envs.query_string;

        vector<string> queries;
        boost::split(queries, queryString, boost::is_any_of("&"), boost::token_compress_on);

        vector<queryFields> q(MAX_SESSION);

        // assume that the order is always host, port, file
        for(int i = 0; i < queries.size(); i++) {
            int index = i / 3;
            string value = getQueryValue(queries[i]);

            if(i % 3 == 0) {
                q[index].host = value;
            } else if(i % 3 == 1) {
                q[index].port = value;
            } else {
                q[index].file = value;
            }
        }

        return q;
    }
    
    
    string getQueryValue(string field) {
        return field.substr(field.find('=') + 1);
    }

    void startConnections() {
        shared_ptr<tcp::socket> serverSocket(&socket_);
        for(int i = 0; i < MAX_SESSION; i++) {
            // no session
            if(qfs[i].host == "") 
                break;

        

            cout <<  "\n the client " << i << " ->  host: " << qfs[i].host << ", port: " << qfs[i].port << endl;
            tcp::resolver resolver(io_context);
            tcp::resolver::query query(qfs[i].host, qfs[i].port);
            tcp::resolver::iterator iter = resolver.resolve(query);
            tcp::endpoint endpoint = iter -> endpoint();

            tcp::socket socket(io_context);
            // connect to the np_single_golden here, note that the i need to be passed so that the getElementById can work
            make_shared<npSingle>(move(socket), serverSocket, endpoint, qfs[i], i)->start();
        }
        io_context.run();
    }

    tcp::socket socket_;
    enum { max_length = 50000 };
    char data_[max_length];
    char outBuffer[max_length];
    envVars envs;
    vector<queryFields> qfs;
};


class server
{
public:
    server(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
    {
        do_accept();
    }

private:
    void do_accept()
    {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket)
            {
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


int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2)
        {
            std::cerr << "Usage: async_tcp_echo_server <port>\n";
            return 1;
        }

        
        server s(io_context, std::atoi(argv[1]));

        io_context.run();
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    cout << "here\n";

    return 0;
}

