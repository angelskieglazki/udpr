#pragma once
#include "file_protocol.h"
#include "src/reliable_udp.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace file_transfer {

// Информация о подключённом клиенте
struct ClientSession {
  sockaddr_storage addr;
  socklen_t addrlen;
  std::chrono::steady_clock::time_point last_activity;
  std::string current_file;

  ClientSession(const sockaddr *a, socklen_t len)
      : addrlen(len), last_activity(std::chrono::steady_clock::now()) {
    std::memcpy(&addr, a, len);
  }
};

// Callback для событий сервера
using ServerEventCallback =
    std::function<void(const std::string &event, const std::string &file_id,
                       const std::string &client_info)>;

class FileServer {
public:
  FileServer(const std::string &files_directory, uint16_t port);
  ~FileServer();

  bool start();

  void stop();

  void run();

  void poll(int timeout_ms = 0);

  void on_event(ServerEventCallback cb) { event_cb_ = std::move(cb); }

  uint16_t get_port() const { return port_; }

  bool is_running() const { return running_; }

private:
  void handle_message(const std::vector<uint8_t> &data, const sockaddr *src,
                      socklen_t srclen);

  void handle_file_request(const FileRequest &req, const sockaddr *dest,
                           socklen_t destlen);
  void send_file_chunks(const std::string &file_id,
                        const std::string &file_path, const sockaddr *dest,
                        socklen_t destlen);

  std::string get_client_key(const sockaddr *addr);
  void cleanup_sessions();

private:
  std::string files_directory_;
  uint16_t port_;
  int sock_fd_ = -1;
  std::unique_ptr<rudpr::ReliableUDP> rudp_;
  std::atomic<bool> running_{false};

  std::mutex sessions_mutex_;
  std::unordered_map<std::string, ClientSession> sessions_;

  ServerEventCallback event_cb_;

  std::thread cleanup_thread_;
  std::atomic<bool> cleanup_running_{false};
  std::condition_variable cleanup_cv_;
  std::mutex cleanup_mutex_;
};

} // namespace file_transfer