#include "file_server.h"
#include <arpa/inet.h>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace file_transfer {

FileServer::FileServer(const std::string &files_directory, uint16_t port)
    : files_directory_(files_directory), port_(port) {}

FileServer::~FileServer() { stop(); }

bool FileServer::start() {
  if (running_) {
    return false;
  }

  // Проверяем директорию
  if (!std::filesystem::exists(files_directory_) ||
      !std::filesystem::is_directory(files_directory_)) {
    std::cerr << "Invalid files directory: " << files_directory_ << std::endl;
    return false;
  }

  // Создаём UDP сокет
  sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd_ < 0) {
    std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
    return false;
  }

  // Bind
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(sock_fd_, (sockaddr *)&addr, sizeof(addr)) < 0) {
    std::cerr << "Failed to bind socket: " << strerror(errno) << std::endl;
    close(sock_fd_);
    sock_fd_ = -1;
    return false;
  }

  // Получаем реальный порт (если был 0)
  if (port_ == 0) {
    socklen_t len = sizeof(addr);
    getsockname(sock_fd_, (sockaddr *)&addr, &len);
    port_ = ntohs(addr.sin_port);
  }

  // Инициализируем ReliableUDP
  rudp_ = std::make_unique<rudpr::ReliableUDP>(sock_fd_);
  if (!rudp_->init()) {
    std::cerr << "Failed to init ReliableUDP" << std::endl;
    close(sock_fd_);
    sock_fd_ = -1;
    return false;
  }

  // Устанавливаем callback для получения сообщений
  rudp_->on_receive(
      [this](const std::vector<uint8_t> &data, const sockaddr *src,
             socklen_t srclen) { handle_message(data, src, srclen); });

  running_ = true;

  // Запускаем cleanup thread
  cleanup_running_ = true;
  cleanup_thread_ = std::thread([this]() {
    while (cleanup_running_) {
      std::unique_lock<std::mutex> lock(cleanup_mutex_);
      // Ждём 30 секунд или сигнала остановки
      cleanup_cv_.wait_for(lock, std::chrono::seconds(30),
                           [this]() { return !cleanup_running_; });
      if (cleanup_running_) {
        cleanup_sessions();
      }
    }
  });

  // std::cout << "[DEBUG] About to call event_cb_, port=" << port_ <<
  // std::endl;
  if (event_cb_) {
    // std::cout << "[DEBUG] Calling event_cb_" << std::endl;
    event_cb_("SERVER_STARTED", "", "port=" + std::to_string(port_));
    // std::cout << "[DEBUG] event_cb_ done" << std::endl;
  }

  // std::cout << "[DEBUG] start() returning true" << std::endl;
  return true;
}

void FileServer::stop() {
  running_ = false;
  cleanup_running_ = false;

  // Сигнализируем cleanup thread для быстрого завершения
  cleanup_cv_.notify_one();

  if (cleanup_thread_.joinable()) {
    cleanup_thread_.join();
  }

  if (rudp_) {
    rudp_.reset();
  }

  if (sock_fd_ >= 0) {
    close(sock_fd_);
    sock_fd_ = -1;
  }
}

void FileServer::run() {
  if (!running_) {
    std::cerr << "Server not started" << std::endl;
    return;
  }

  std::cout << "FileServer running on port " << port_ << std::endl;
  std::cout << "Serving files from: " << files_directory_ << std::endl;

  while (running_) {
    poll(-1); // Бесконечное ожидание
  }
}

void FileServer::poll(int timeout_ms) {
  if (rudp_) {
    rudp_->poll(timeout_ms);
  }
}

void FileServer::handle_message(const std::vector<uint8_t> &data,
                                const sockaddr *src, socklen_t srclen) {
  if (data.empty())
    return;

  std::string client_key = get_client_key(src);
  std::cout << "[DEBUG] Message from " << client_key << " size=" << data.size()
            << std::endl;

  CommandType cmd = get_command_type(data.data(), data.size());
  std::cout << "[DEBUG] Command type: " << (int)cmd << std::endl;

  // Обновляем/создаём сессию
  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(client_key);
    if (it != sessions_.end()) {
      it->second.last_activity = std::chrono::steady_clock::now();
    } else {
      sessions_.emplace(client_key, ClientSession(src, srclen));
    }
  }

  switch (cmd) {
  case CMD_FILE_REQUEST: {
    FileRequest req;
    if (FileRequest::deserialize(data.data(), data.size(), req)) {
      handle_file_request(req, src, srclen);
    }
    break;
  }
  default:
    std::cout << "Unknown command: " << (int)cmd << std::endl;
    break;
  }
}

void FileServer::handle_file_request(const FileRequest &req,
                                     const sockaddr *dest, socklen_t destlen) {
  std::cout << "File request: " << req.file_id << std::endl;

  // Формируем путь к файлу
  std::filesystem::path file_path =
      std::filesystem::path(files_directory_) / req.file_id;

  // Проверяем безопасность пути (не выходим за пределы директории)
  std::filesystem::path canonical_dir =
      std::filesystem::canonical(files_directory_);
  std::filesystem::path canonical_file;
  try {
    canonical_file = std::filesystem::canonical(file_path);
  } catch (...) {
    canonical_file = file_path;
  }

  if (canonical_file.string().find(canonical_dir.string()) != 0) {
    FileResponse resp{STATUS_ERROR, req.file_id, 0, 0};
    auto data = resp.serialize();
    rudp_->send(data.data(), data.size(), dest, destlen);

    if (event_cb_) {
      event_cb_("ACCESS_DENIED", req.file_id, "");
    }
    return;
  }

  // Проверяем существование файла
  if (!std::filesystem::exists(file_path) ||
      !std::filesystem::is_regular_file(file_path)) {
    FileResponse resp{STATUS_NOT_FOUND, req.file_id, 0, 0};
    auto data = resp.serialize();
    rudp_->send(data.data(), data.size(), dest, destlen);

    if (event_cb_) {
      event_cb_("FILE_NOT_FOUND", req.file_id, "");
    }
    return;
  }

  // Получаем размер файла
  uint64_t file_size = std::filesystem::file_size(file_path);
  uint32_t total_chunks = calc_total_chunks(file_size);

  // Отправляем ответ
  FileResponse resp{STATUS_OK, req.file_id, file_size, total_chunks};
  auto resp_data = resp.serialize();
  rudp_->send(resp_data.data(), resp_data.size(), dest, destlen);

  if (event_cb_) {
    event_cb_("FILE_REQUESTED", req.file_id,
              "size=" + std::to_string(file_size));
  }

  // Отправляем чанки
  send_file_chunks(req.file_id, file_path.string(), dest, destlen);
}

void FileServer::send_file_chunks(const std::string &file_id,
                                  const std::string &file_path,
                                  const sockaddr *dest, socklen_t destlen) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    std::cerr << "Failed to open file: " << file_path << std::endl;
    return;
  }

  uint64_t file_size = std::filesystem::file_size(file_path);
  uint32_t total_chunks = calc_total_chunks(file_size);

  std::cout << "Sending " << file_id << " (" << file_size << " bytes, "
            << total_chunks << " chunks)" << std::endl;

  std::vector<uint8_t> buffer(CHUNK_SIZE);
  uint32_t chunk_index = 0;

  while (file.good() && chunk_index < total_chunks) {
    file.read(reinterpret_cast<char *>(buffer.data()), CHUNK_SIZE);
    size_t bytes_read = file.gcount();

    if (bytes_read > 0) {
      FileChunk chunk;
      chunk.file_id = file_id;
      chunk.chunk_index = chunk_index;
      chunk.data.assign(buffer.data(), buffer.data() + bytes_read);

      auto data = chunk.serialize();
      rudp_->send(data.data(), data.size(), dest, destlen);

      chunk_index++;
    }
  }

  // Ждём завершения отправки
  int retries = 0;
  while (rudp_->has_pending_send() && retries < 100) {
    rudp_->poll(10);
    retries++;
  }

  if (event_cb_) {
    event_cb_("FILE_SENT", file_id, "chunks=" + std::to_string(total_chunks));
  }

  std::cout << "File " << file_id << " sent successfully" << std::endl;
}

std::string FileServer::get_client_key(const sockaddr *addr) {
  char addr_str[INET6_ADDRSTRLEN];
  uint16_t port = 0;

  if (addr->sa_family == AF_INET) {
    const sockaddr_in *sin = reinterpret_cast<const sockaddr_in *>(addr);
    inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
    port = ntohs(sin->sin_port);
  } else if (addr->sa_family == AF_INET6) {
    const sockaddr_in6 *sin6 = reinterpret_cast<const sockaddr_in6 *>(addr);
    inet_ntop(AF_INET6, &sin6->sin6_addr, addr_str, sizeof(addr_str));
    port = ntohs(sin6->sin6_port);
  } else {
    return "unknown";
  }

  return std::string(addr_str) + ":" + std::to_string(port);
}

void FileServer::cleanup_sessions() {
  auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(sessions_mutex_);

  for (auto it = sessions_.begin(); it != sessions_.end();) {
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
        now - it->second.last_activity);
    if (elapsed > std::chrono::minutes(5)) {
      std::cout << "Cleaning up inactive session: " << it->first << std::endl;
      it = sessions_.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace file_transfer