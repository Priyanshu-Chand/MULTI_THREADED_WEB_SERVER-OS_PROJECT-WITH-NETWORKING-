// g++ WindowServer.cpp -o server.exe -pthread -std=c++17 -lws2_32

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

#include <errno.h>
#include <cstring>
#include <iostream>
#include <thread>

#include <vector>
#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <dirent.h>

#include "SimpleThreadPool.h"

using namespace std;

// Function declarations
void closeSocket(int sock);
bool endsWith(const string &str, const string &suffix);
string readFile(const string &path);
string getContentType(const string &path);
string listFilesAsJson(const string &directoryPath);
void handle_client(int client_socket);

// --- Global Variables ---
atomic<int> total_requests(0);
atomic<int> active_threads(0);
atomic<int> closed_connections(0);
atomic<int> peak_connections(0);
SimpleThreadPool pool(32);
string server_start_time_str; // For cache busting

// --- Function Implementations ---

void closeSocket(int sock)
{
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

bool endsWith(const string &str, const string &suffix)
{
    if (str.length() < suffix.length())
        return false;
    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

string readFile(const string &path)
{
    ifstream file("www/" + path, ios::in | ios::binary);
    if (!file.is_open())
        return "";
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

string getContentType(const string &path)
{
    if (endsWith(path, ".html"))
        return "text/html";
    if (endsWith(path, ".css"))
        return "text/css";
    if (endsWith(path, ".js"))
        return "application/javascript";
    if (endsWith(path, ".jpg") || endsWith(path, ".jpeg"))
        return "image/jpeg";
    if (endsWith(path, ".png"))
        return "image/png";
    if (endsWith(path, ".pdf"))
        return "application/pdf";
    if (endsWith(path, ".txt"))
        return "text/plain";
    return "application/octet-stream";
}

string listFilesAsJson(const string &directoryPath)
{
    string json = "[";
    DIR *dir = opendir(directoryPath.c_str());
    if (dir == nullptr)
    {
        cerr << "Error: Could not open directory at " << directoryPath << endl;
        return "[]";
    }
    struct dirent *entry;
    bool first = true;
    while ((entry = readdir(dir)) != nullptr)
    {
        string name = entry->d_name;
        if (name != "." && name != "..")
        {
            if (!first)
                json += ",";
            json += "\"" + name + "\"";
            first = false;
        }
    }
    closedir(dir);
    json += "]";
    return json;
}

void handle_client(int client_socket)
{
    active_threads++;
    total_requests++;
    char buffer[4096];
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0)
    {
        buffer[bytes_received] = '\0';
        string request(buffer); //chnage char array to string for easier manipulation
        string firstLine = request.substr(0, request.find("\r\n"));//extracting the first line of the request


        string path = firstLine.substr(firstLine.find(" ") + 1);//extract requested path
        path = path.substr(0, path.find(" "));

        if (path == "/" || path.empty())
            path = "index.html";

        string response_headers;
        string response_body;

        if (path == "/status")
        {
            response_headers = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
            long current_connected = total_requests.load() - closed_connections.load();
            int current_peak = peak_connections.load();
            if (current_connected > current_peak)
                peak_connections.store(current_connected);
            response_body = "{\"activeThreads\":" + to_string(active_threads.load()) +
                            ",\"totalRequests\":" + to_string(total_requests.load()) +
                            ",\"connectedUsers\":" + to_string(current_connected) +
                            ",\"closedConnections\":" + to_string(closed_connections.load()) +
                            ",\"peakConnections\":" + to_string(peak_connections.load()) + "}";
            string full_response = response_headers + response_body;
            send(client_socket, full_response.c_str(), full_response.size(), 0);
        }
        else if (path == "/api/files")
        {
            string file_list_json = listFilesAsJson("www/downloads");
            response_headers = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
            string full_response = response_headers + file_list_json;
            send(client_socket, full_response.c_str(), full_response.size(), 0);
        }
        else if (path == "index.html")
        {
            response_body = readFile("index.html");
            if (response_body.empty())
            {
                response_headers = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFile Not Found";
                send(client_socket, response_headers.c_str(), response_headers.size(), 0);
            }
            else
            {
                string placeholder = "__CACHE_BUST__";
                size_t pos = response_body.find(placeholder);
                while (pos != string::npos)
                {
                    response_body.replace(pos, placeholder.length(), server_start_time_str);
                    pos = response_body.find(placeholder, pos + 1);
                }
                response_headers = "HTTP/1.1 200 OK\r\n";
                response_headers += "Content-Type: text/html\r\n";
                response_headers += "Content-Length: " + to_string(response_body.length()) + "\r\n\r\n";
                send(client_socket, response_headers.c_str(), response_headers.size(), 0);
                send(client_socket, response_body.c_str(), response_body.length(), 0);
            }
        }
        else
        {
            response_body = readFile(path);
            if (response_body.empty())
            {
                response_headers = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFile Not Found";
                send(client_socket, response_headers.c_str(), response_headers.size(), 0);
            }
            else
            {
                response_headers = "HTTP/1.1 200 OK\r\n";
                response_headers += "Content-Type: " + getContentType(path) + "\r\n";
                response_headers += "Content-Length: " + to_string(response_body.length()) + "\r\n";
                if (path.rfind("downloads/", 0) == 0)
                {
                    string filename = path.substr(path.rfind("/") + 1);
                    response_headers += "Content-Disposition: attachment; filename=\"" + filename + "\"\r\n";
                }
                response_headers += "\r\n";
                send(client_socket, response_headers.c_str(), response_headers.size(), 0);
                send(client_socket, response_body.c_str(), response_body.length(), 0);
            }
        }
    }
    this_thread::sleep_for(chrono::seconds(1));
    closeSocket(client_socket);
    closed_connections++;
    active_threads--;
}




int main()
{
    server_start_time_str = to_string(chrono::system_clock::now().time_since_epoch().count());

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup failed." << endl;
        return 1;
    }
#endif
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        cerr << "Socket creation failed: " << strerror(errno) << endl;
        return 1;
    }
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);
    int opt = 1;
#ifdef _WIN32
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
#else
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        cerr << "Bind failed: " << strerror(errno) << endl;
        closeSocket(server_socket);
        return 1;
    }
    if (listen(server_socket, SOMAXCONN) == -1)
    {
        cerr << "Listen failed: " << strerror(errno) << endl;
        closeSocket(server_socket);
        return 1;
    }
    cout << "=========================================" << endl;
    cout << "Server started with a Simple Thread Pool!" << endl;
    cout << "Open in browser: http://127.0.0.1:8080" << endl;
    cout << "=========================================" << endl;
    while (true)
    {
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == -1)
        {
            cerr << "Accept failed: " << strerror(errno) << endl;
            continue;
        }
        pool.enqueue(client_socket);
    }
    closeSocket(server_socket);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}