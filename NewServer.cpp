// Compile commands:
// Windows: g++ NewServer.cpp -o server.exe -pthread -std=c++17 -lws2_32
// Linux:   g++ WindowServer.cpp -o server -pthread -std=c++17

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
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
#include <unordered_set>
#include <ctime>
#include <iomanip>

// Assuming SimpleThreadPool.h is correctly included and works.
#include "SimpleThreadPool.h"

using namespace std;

// ========================== Globals ==========================
atomic<int> total_requests(0);
atomic<int> active_threads(0);
atomic<int> closed_connections(0);
atomic<int> peak_connections(0);
SimpleThreadPool pool(32);

string server_start_time_str; // For cache busting

unordered_set<thread::id> active_thread_ids;
mutex active_set_mutex;
mutex log_mutex;

atomic<bool> server_running(true);
int server_socket = -1;

// ========================== Function Declarations ==========================
void closeSocket(int sock);
bool endsWith(const string &str, const string &suffix);
string readFile(const string &path);
string getContentType(const string &path);
string listFilesAsJson(const string &directoryPath);
void handle_client(int client_socket);
string format_time_now();
string threadIdToString(const thread::id &id);

// ========================== Helper Functions ==========================
void closeSocket(int sock)
{
#ifdef _WIN32
    closesocket((SOCKET)sock);
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
    if (endsWith(path, ".mp4"))
        return "video/mp4";
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

string format_time_now()
{
    using namespace chrono;
    auto now = system_clock::now();
    time_t t = system_clock::to_time_t(now);
    tm local_tm;

#ifdef _WIN32
    localtime_s(&local_tm, &t);
#else
    localtime_r(&t, &local_tm);
#endif

    ostringstream oss;
    oss << put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

string threadIdToString(const thread::id &id)
{
    ostringstream oss;
    oss << id;
    return oss.str();
}

// ========================== Client Handler ==========================
void handle_client(int client_socket)
{
    thread::id tid = this_thread::get_id();
    {
        lock_guard<mutex> lock(active_set_mutex);
        active_thread_ids.insert(tid);
    }

    active_threads++;
    total_requests++;

    int current_active = active_threads.load();
    int current_peak = peak_connections.load();
    if (current_active > current_peak)
    {
        peak_connections.store(current_active);
    }

    string client_ip = "unknown";
    int client_port = 0;
    sockaddr_in addr;
    socklen_t addr_size = sizeof(sockaddr_in);

    if (getpeername(client_socket, (struct sockaddr *)&addr, &addr_size) == 0)
    {
        client_ip = inet_ntoa(addr.sin_addr);
        client_port = ntohs(addr.sin_port);
    }

    string time_str = format_time_now();
    {
        lock_guard<mutex> lock(log_mutex);
        cout << "[" << time_str << "] Thread " << threadIdToString(tid)
             << " handling request from " << client_ip << ":" << client_port << endl;

        ofstream logFile("server_log.txt", ios::app);
        if (logFile.is_open())
        {
            logFile << "[" << time_str << "] Thread " << threadIdToString(tid)
                    << " handling request from " << client_ip << ":" << client_port << endl;
            logFile.close();
        }
    }

    char buffer[4096];
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0)
    {
        closeSocket(client_socket);
        closed_connections++;
        active_threads--;
        {
            lock_guard<mutex> lock(active_set_mutex);
            active_thread_ids.erase(tid);
        }
        return;
    }

    buffer[bytes_received] = '\0';
    string request(buffer);
    string firstLine = request.substr(0, request.find("\r\n"));
    string path = "/";

    if (!firstLine.empty())
    {
        size_t firstSpace = firstLine.find(' ');
        if (firstSpace != string::npos)
        {
            size_t secondSpace = firstLine.find(' ', firstSpace + 1);
            if (secondSpace != string::npos && secondSpace > firstSpace + 1)
                path = firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
        }
    }

    if (path == "/" || path.empty())
        path = "index.html";
    else if (path[0] == '/')
        path = path.substr(1);

    string response_headers;
    string response_body;

    // --- /status Route ---
    if (path == "status" || path == "/status")
    {
        this_thread::sleep_for(chrono::milliseconds(10));

        ostringstream oss;

        // oss << "{\"activeThreads\":" << active_threads.load()
        //     << ",\"totalRequests\":" << total_requests.load()
        //     << ",\"closedConnections\":" << closed_connections.load()
        //     << ",\"peakConnections\":" << peak_connections.load()
        //     << ",\"queuedTasks\":" << pool.get_queue_size()
        //     << ",\"activeThreadIds\":[";


        //----

        oss << "{\"totalThreads\":" << pool.get_pool_size() 

            << ",\"activeThreads\":" << active_threads.load()
            << ",\"totalRequests\":" << total_requests.load()
            << ",\"closedConnections\":" << closed_connections.load()
            << ",\"peakConnections\":" << peak_connections.load()
            << ",\"queuedTasks\":" << pool.get_queue_size()
            << ",\"activeThreadIds\":[";

        {
            lock_guard<mutex> lock(active_set_mutex);
            bool first = true;
            for (const auto &id : active_thread_ids)
            {
                if (!first)
                    oss << ",";
                oss << "\"" << threadIdToString(id) << "\"";
                first = false;
            }
        }

        oss << "]}";
        response_body = oss.str();
        response_headers =
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
            to_string(response_body.size()) + "\r\n\r\n";

        string full_response = response_headers + response_body;
        send(client_socket, full_response.c_str(), (int)full_response.size(), 0);
    }

    // --- /api/files Route ---
    else if (path == "api/files" || path == "api/files/")
    {
        string file_list_json = listFilesAsJson("www/downloads");
        response_headers =
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
            to_string(file_list_json.size()) + "\r\n\r\n";
        string full_response = response_headers + file_list_json;
        send(client_socket, full_response.c_str(), (int)full_response.size(), 0);
    }

    // --- /shutdown Route ---
    else if (path == "shutdown" || path == "/shutdown")
    {
        response_headers = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nShutting down server...";
        send(client_socket, response_headers.c_str(), (int)response_headers.size(), 0);
        {
            lock_guard<mutex> lock(log_mutex);
            cout << "[" << format_time_now() << "] [INFO] Shutdown requested by "
                 << client_ip << ":" << client_port << endl;
        }
        server_running = false;
        if (server_socket != -1)
        {
            closeSocket(server_socket);
            server_socket = -1;
        }
    }

    // --- index.html ---
    else if (path == "index.html")
    {
        response_body = readFile("index.html");
        if (response_body.empty())
        {
            response_headers = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFile Not Found";
            send(client_socket, response_headers.c_str(), (int)response_headers.size(), 0);
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

            send(client_socket, response_headers.c_str(), (int)response_headers.size(), 0);
            send(client_socket, response_body.c_str(), (int)response_body.length(), 0);
        }
    }

    // --- Downloads / Media Files ---
    else if (path.rfind("downloads/", 0) == 0 || path.rfind("downloads%2F", 0) == 0)
    {
        this_thread::sleep_for(chrono::milliseconds(500));
        response_body = readFile(path);
        if (response_body.empty())
        {
            response_headers = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFile Not Found";
            send(client_socket, response_headers.c_str(), (int)response_headers.size(), 0);
        }
        else
        {
            response_headers = "HTTP/1.1 200 OK\r\n";
            response_headers += "Content-Type: " + getContentType(path) + "\r\n";
            response_headers += "Content-Length: " + to_string(response_body.length()) + "\r\n";
            string filename = path.substr(path.rfind("/") + 1);
            response_headers += "Content-Disposition: attachment; filename=\"" + filename + "\"\r\n\r\n";

            send(client_socket, response_headers.c_str(), (int)response_headers.size(), 0);
            send(client_socket, response_body.c_str(), (int)response_body.length(), 0);
        }
    }

    // --- Static File Serving ---
    else
    {
        this_thread::sleep_for(chrono::milliseconds(10));
        response_body = readFile(path);
        if (response_body.empty())
        {
            response_headers = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFile Not Found";
            send(client_socket, response_headers.c_str(), (int)response_headers.size(), 0);
        }
        else
        {
            response_headers = "HTTP/1.1 200 OK\r\n";
            response_headers += "Content-Type: " + getContentType(path) + "\r\n";
            response_headers += "Content-Length: " + to_string(response_body.length()) + "\r\n\r\n";

            send(client_socket, response_headers.c_str(), (int)response_headers.size(), 0);
            send(client_socket, response_body.c_str(), (int)response_body.length(), 0);
        }
    }

    this_thread::sleep_for(chrono::milliseconds(50));
    closeSocket(client_socket);
    closed_connections++;
    active_threads--;

    {
        lock_guard<mutex> lock(active_set_mutex);
        active_thread_ids.erase(tid);
    }
}

// ========================== Main Function ==========================
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

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
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
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) == -1)
    {
        cerr << "Listen failed: " << strerror(errno) << endl;
        closeSocket(server_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    cout << "=========================================\n";
    cout << "Server started with a Simple Thread Pool!\n";
    cout << "Open in browser: http://127.0.0.1:8080\n";
    cout << "Routes: / , /status , /api/files , /shutdown\n";
    cout << "=========================================\n";

    while (server_running)
    {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket_local = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        if (client_socket_local == -1)
        {
            if (!server_running)
                break;
            cerr << "Accept failed: " << strerror(errno) << endl;
            continue;
        }

        pool.enqueue(client_socket_local);
    }

    cout << "[INFO] Server main loop exited. Cleaning up..." << endl;

    if (server_socket != -1)
        closeSocket(server_socket);

#ifdef _WIN32
    WSACleanup();
#endif

    cout << "[INFO] Server stopped." << endl;
    return 0;
}
