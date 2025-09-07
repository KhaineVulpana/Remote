#include <windows.h>
#include <winhttp.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

using namespace Gdiplus;

class StealthBase64 {
private:
    static const std::string chars;
public:
    static std::string decode(const std::string& input) {
        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++) T[chars[i]] = i;
        std::string out;
        int val = 0, valb = -8;
        for (unsigned char c : input) {
            if (T[c] == -1) break;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                out.push_back(char((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }
};

const std::string StealthBase64::chars = "QWERTYUIOPASDFGHJKLZXCVBNMqwertyuiopasdfghjklzxcvbnm1234567890+/";

class SimpleCompressor {
public:
    static std::vector<BYTE> decompressRLE(const std::vector<BYTE>& data) {
        std::vector<BYTE> out;
        for (size_t i = 0; i < data.size();) {
            if (data[i] == 0xFF && i + 2 < data.size()) {
                BYTE count = data[i + 1];
                BYTE val = data[i + 2];
                out.insert(out.end(), count, val);
                i += 3;
            } else {
                out.push_back(data[i]);
                i++;
            }
        }
        return out;
    }
};

struct HttpResponse {
    bool success = false;
    int status_code = 0;
    std::string body;
};

class RemoteViewer {
    std::string host;
    int port;
    std::string session;
    HWND hwnd = nullptr;
    int width = 0, height = 0;
    std::vector<BYTE> currentFrame;

    HttpResponse makeRequest(const std::string& path) {
        HttpResponse resp;
        HINTERNET hSession = WinHttpOpen(L"RemoteViewer/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
        if (!hSession) return resp;
        std::wstring whost(host.begin(), host.end());
        HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), port, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return resp; }
        std::wstring wpath(path.begin(), path.end());
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hRequest && WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &size, NULL);
            resp.status_code = status;
            std::string body;
            DWORD dwSize = 0, dwDownloaded = 0;
            do {
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                if (dwSize == 0) break;
                std::string buffer(dwSize, 0);
                if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
                body.append(buffer.data(), dwDownloaded);
            } while (dwSize > 0);
            resp.body = body;
            resp.success = true;
        }
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
        return resp;
    }

    std::string extract(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos += search.size();
        if (json[pos] == '"') {
            pos++; size_t end = json.find('"', pos);
            if (end != std::string::npos) return json.substr(pos, end - pos);
        } else {
            size_t end = json.find_first_of(",}\n", pos);
            if (end != std::string::npos) return json.substr(pos, end - pos);
        }
        return "";
    }

    bool fetchFrame() {
        HttpResponse r = makeRequest("/api/frame/" + session);
        if (!r.success || r.status_code != 200) return false;
        std::string wstr = extract(r.body, "width");
        std::string hstr = extract(r.body, "height");
        std::string data = extract(r.body, "screen");
        if (wstr.empty() || hstr.empty() || data.empty()) return false;
        width = std::stoi(wstr);
        height = std::stoi(hstr);
        std::string decoded = StealthBase64::decode(data);
        std::vector<BYTE> compressed(decoded.begin(), decoded.end());
        currentFrame = SimpleCompressor::decompressRLE(compressed);
        return true;
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    void drawFrame() {
        if (currentFrame.empty() || width <= 0 || height <= 0) return;
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;
        HDC hdc = GetDC(hwnd);
        StretchDIBits(hdc, 0, 0, width, height, 0, 0, width, height, currentFrame.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(hwnd, hdc);
    }

    void createWindow() {
        WNDCLASSA wc = {};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "RemoteViewerWindow";
        RegisterClassA(&wc);
        hwnd = CreateWindowA(wc.lpszClassName, "Remote Viewer", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);
        ShowWindow(hwnd, SW_SHOW);
    }

public:
    RemoteViewer(const std::string& h, int p, const std::string& s)
        : host(h), port(p), session(s) {}

    void run() {
        GdiplusStartupInput gdiplusStartupInput; ULONG_PTR gdiplusToken;
        GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
        createWindow();
        MSG msg;
        while (true) {
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { GdiplusShutdown(gdiplusToken); return; }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            if (fetchFrame()) drawFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cout << "Usage: " << argv[0] << " <server_host> <server_port> <session_id>" << std::endl;
        return 1;
    }
    std::string host = argv[1];
    int port = std::stoi(argv[2]);
    std::string session = argv[3];
    RemoteViewer viewer(host, port, session);
    viewer.run();
    return 0;
}
