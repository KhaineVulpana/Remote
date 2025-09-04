#!/bin/bash

# Simple build script for the example client and server

OS="Unknown"
if [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]] || [[ "$OSTYPE" == "win32" ]]; then
    OS="Windows"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="Linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macOS"
fi

echo "Building for $OS"

CPP_FLAGS="-std=c++17 -O2 -Wall"
CLIENT_LIBS=""
SERVER_LIBS=""

if [[ "$OS" == "Windows" ]]; then
    CLIENT_LIBS="-lws2_32"
    SERVER_LIBS="-lws2_32"
    CLIENT_EXE="client.exe"
    SERVER_EXE="server.exe"
else
    CLIENT_LIBS="-pthread"
    SERVER_LIBS="-pthread"
    CLIENT_EXE="client"
    SERVER_EXE="server"
fi

echo "Compiling server..."
if g++ $CPP_FLAGS server.cpp -o $SERVER_EXE $SERVER_LIBS; then
    echo "Server built: $SERVER_EXE"
else
    echo "Server build failed"
    exit 1
fi

echo "Compiling client..."
if g++ $CPP_FLAGS client.cpp -o $CLIENT_EXE $CLIENT_LIBS; then
    echo "Client built: $CLIENT_EXE"
else
    echo "Client build failed"
    exit 1
fi

echo "Build complete"
