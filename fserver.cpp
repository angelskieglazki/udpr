// ============================================================================
// File Transfer Server
// Usage: ./file_server <port> <files_directory>
// ============================================================================
#include "file_server.h"
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>

using namespace file_transfer;

std::atomic<bool> g_running{true};

void signal_handler(int sig) {
  std::cout << "\nReceived signal " << sig << ", shutting down..." << std::endl;
  g_running = false;
}

void print_usage(const char *program) {
  std::cout << "Usage: " << program << " <port> <files_directory>" << std::endl;
  std::cout << std::endl;
  std::cout << "Examples:" << std::endl;
  std::cout << "  " << program << " 8080 ./files" << std::endl;
  std::cout << "  " << program << " 9000 /var/shared_files" << std::endl;
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    print_usage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  uint16_t port = static_cast<uint16_t>(std::atoi(argv[1]));
  std::string files_directory = argv[2];

  std::cout << "============================================" << std::endl;
  std::cout << "        File Transfer Server" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "Port:      " << port << std::endl;
  std::cout << "Directory: " << files_directory << std::endl;
  std::cout << "--------------------------------------------" << std::endl;

  FileServer server(files_directory, port);

  // Устанавливаем callback для событий
  server.on_event([](const std::string &event, const std::string &file_id,
                     const std::string &details) {
    if (event == "SERVER_STARTED") {
      std::cout << "[SERVER] Started on " << details << std::endl;
    } else if (event == "FILE_REQUESTED") {
      std::cout << "[REQUEST] " << file_id << " (" << details << ")"
                << std::endl;
    } else if (event == "FILE_SENT") {
      std::cout << "[SENT] " << file_id << " (" << details << ")" << std::endl;
    } else if (event == "FILE_NOT_FOUND") {
      std::cout << "[NOT FOUND] " << file_id << std::endl;
    } else if (event == "ACCESS_DENIED") {
      std::cout << "[ACCESS DENIED] " << file_id << std::endl;
    }
  });

  if (!server.start()) {
    std::cerr << "Failed to start server" << std::endl;
    return 1;
  }

  std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;
  std::cout << "============================================" << std::endl;

  // Основной цикл с возможностью graceful shutdown
  while (g_running && server.is_running()) {
    server.poll(100); // Poll с таймаутом 100ms
  }

  std::cout << "Stopping server..." << std::endl;
  server.stop();

  std::cout << "Server stopped." << std::endl;
  return 0;
}
