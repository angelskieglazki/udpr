// ============================================================================
// File Transfer Client
// Usage: ./file_client <server_host> <server_port> <file_id> [output_path]
// ============================================================================
#include "file_client.h"
#include "file_storage.h"
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>

using namespace file_transfer;

std::atomic<bool> g_running{true};
std::atomic<bool> g_download_complete{false};

void signal_handler(int sig) {
  std::cout << "\nReceived signal " << sig << ", shutting down..." << std::endl;
  g_running = false;
}

void print_usage(const char *program) {
  std::cout << "Usage: " << program
            << " <server_host> <server_port> <file_id> [output_path]"
            << std::endl;
  std::cout << std::endl;
  std::cout << "Examples:" << std::endl;
  std::cout << "  " << program << " localhost 8080 document.pdf" << std::endl;
  std::cout << "  " << program
            << " 192.168.1.100 9000 video.mp4 ./downloads/movie.mp4"
            << std::endl;
}

int main(int argc, char *argv[]) {
  if (argc < 4) {
    print_usage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  std::string server_host = argv[1];
  uint16_t server_port = static_cast<uint16_t>(std::atoi(argv[2]));
  std::string file_id = argv[3];
  std::string output_path = (argc >= 5) ? argv[4] : file_id;

  std::cout << "============================================" << std::endl;
  std::cout << "        File Transfer Client" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "Server:    " << server_host << ":" << server_port << std::endl;
  std::cout << "File ID:   " << file_id << std::endl;
  std::cout << "Output:    " << output_path << std::endl;
  std::cout << "Workers:   4" << std::endl;
  std::cout << "--------------------------------------------" << std::endl;

  FileClient client(4); // 4 worker threads

  client.on_progress([](const std::string &file_id, double progress) {
    int percent = static_cast<int>(progress * 100);
    static int last_percent = -1;
    if (percent != last_percent && percent % 5 == 0) {
      std::cout << "[" << file_id << "] Progress: " << percent << "%"
                << std::endl;
      last_percent = percent;
    }
  });

  client.on_download_complete(
      [](const std::string &file_id, bool success, const std::string &error) {
        if (success) {
          std::cout << "[" << file_id << "] Download completed successfully!"
                    << std::endl;
        } else {
          std::cout << "[" << file_id << "] Download failed: " << error
                    << std::endl;
        }
        g_download_complete = true;
      });

  if (!client.connect(server_host, server_port)) {
    std::cerr << "Failed to connect to server" << std::endl;
    return 1;
  }

  if (!client.request_file(file_id, output_path)) {
    std::cerr << "Failed to request file" << std::endl;
    return 1;
  }

  client.start_event_loop();

  std::cout << "Waiting for download to complete..." << std::endl;
  bool success = client.wait_for_download(file_id, std::chrono::minutes(5));

  if (!g_running) {
    std::cout << "Download interrupted by signal" << std::endl;
  }

  client.disconnect();

  if (success) {
    std::cout << "============================================" << std::endl;
    std::cout << "Download finished. Check: " << output_path << std::endl;
    return 0;
  } else {
    std::cout << "============================================" << std::endl;
    std::cout << "Download incomplete or failed." << std::endl;
    return 1;
  }
}
