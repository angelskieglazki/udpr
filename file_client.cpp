#include "file_client.h"
#include <arpa/inet.h>
#include <csignal>
#include <iostream>
#include <netdb.h>
#include <unistd.h>

namespace file_transfer {

FileClient::FileClient(size_t num_workers) : num_workers_(num_workers) {}

FileClient::~FileClient() { disconnect(); }

bool FileClient::connect(const std::string &server_host, uint16_t server_port) {
  if (connected_) {
    disconnect();
  }

  // Создаём UDP сокет
  sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd_ < 0) {
    std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
    return false;
  }

  // Резолвим адрес
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  addrinfo *result = nullptr;
  std::string port_str = std::to_string(server_port);

  int err = getaddrinfo(server_host.c_str(), port_str.c_str(), &hints, &result);
  if (err != 0) {
    std::cerr << "Failed to resolve host: " << gai_strerror(err) << std::endl;
    close(sock_fd_);
    sock_fd_ = -1;
    return false;
  }

  // Копируем адрес сервера
  std::memcpy(&server_addr_, result->ai_addr, result->ai_addrlen);
  server_addrlen_ = result->ai_addrlen;
  freeaddrinfo(result);

  // Инициализируем ReliableUDP
  rudp_ = std::make_unique<rudpr::ReliableUDP>(sock_fd_);
  if (!rudp_->init()) {
    std::cerr << "Failed to init ReliableUDP" << std::endl;
    close(sock_fd_);
    sock_fd_ = -1;
    return false;
  }

  // Устанавливаем callback для получения сообщений
  rudp_->on_receive([this](const std::vector<uint8_t> &data,
                           const sockaddr *src, socklen_t srclen) {
    (void)src;
    (void)srclen;
    handle_message(data);
  });

  connected_ = true;

  // Запускаем worker threads
  workers_running_ = true;
  for (size_t i = 0; i < num_workers_; ++i) {
    workers_.emplace_back(&FileClient::worker_thread, this);
  }

  std::cout << "Connected to " << server_host << ":" << server_port
            << std::endl;
  return true;
}

void FileClient::disconnect() {
  stop_event_loop();

  workers_running_ = false;
  queue_cv_.notify_all();

  for (auto &t : workers_) {
    if (t.joinable()) {
      t.join();
    }
  }
  workers_.clear();

  if (rudp_) {
    rudp_.reset();
  }

  if (sock_fd_ >= 0) {
    close(sock_fd_);
    sock_fd_ = -1;
  }

  connected_ = false;

  // Очищаем загрузки
  std::lock_guard<std::mutex> lock(downloads_mutex_);
  downloads_.clear();
}

bool FileClient::request_file(const std::string &file_id,
                              const std::string &output_path) {
  if (!connected_) {
    std::cerr << "Not connected to server" << std::endl;
    return false;
  }

  // Проверяем, нет ли уже активной загрузки (или удаляем завершённую)
  {
    std::lock_guard<std::mutex> lock(downloads_mutex_);
    auto it = downloads_.find(file_id);
    if (it != downloads_.end()) {
      if (!it->second->completed && !it->second->failed) {
        std::cerr << "Download already in progress: " << file_id << std::endl;
        return false;
      }
      // Удаляем старую завершённую загрузку
      downloads_.erase(it);
    }
  }
  // Также удаляем из storage, если там есть старый файл
  storage_.remove_file(file_id);

  // Отправляем запрос
  FileRequest req{file_id};
  auto data = req.serialize();

  if (!rudp_->send(data.data(), data.size(),
                   reinterpret_cast<sockaddr *>(&server_addr_),
                   server_addrlen_)) {
    std::cerr << "Failed to send file request" << std::endl;
    return false;
  }

  // Создаём состояние загрузки
  {
    std::lock_guard<std::mutex> lock(downloads_mutex_);
    auto state = std::make_unique<DownloadState>();
    state->file_id = file_id;
    state->output_path = output_path;
    state->request_time = std::chrono::steady_clock::now();
    downloads_[file_id] = std::move(state);
  }

  std::cout << "Requested file: " << file_id << " -> " << output_path
            << std::endl;
  return true;
}

void FileClient::start_event_loop() {
  if (event_loop_running_) {
    return;
  }
  event_loop_running_ = true;
  event_thread_ = std::thread(&FileClient::event_loop_thread, this);
}

void FileClient::stop_event_loop() {
  event_loop_running_ = false;
  if (event_thread_.joinable()) {
    event_thread_.join();
  }
}

void FileClient::poll(int timeout_ms) {
  if (rudp_) {
    rudp_->poll(timeout_ms);
  }
  check_timeouts();
}

void FileClient::handle_message(const std::vector<uint8_t> &data) {
  if (data.empty())
    return;

  CommandType cmd = get_command_type(data.data(), data.size());

  switch (cmd) {
  case CMD_FILE_RESPONSE: {
    FileResponse resp;
    if (FileResponse::deserialize(data.data(), data.size(), resp)) {
      handle_file_response(resp);
    }
    break;
  }
  case CMD_FILE_CHUNK: {
    FileChunk chunk;
    if (FileChunk::deserialize(data.data(), data.size(), chunk)) {
      handle_file_chunk(chunk);
    }
    break;
  }
  default:
    std::cout << "Unknown command: " << (int)cmd << std::endl;
    break;
  }
}

void FileClient::handle_file_response(const FileResponse &resp) {
  std::lock_guard<std::mutex> lock(downloads_mutex_);
  auto it = downloads_.find(resp.file_id);
  if (it == downloads_.end()) {
    std::cerr << "Received response for unknown file: " << resp.file_id
              << std::endl;
    return;
  }

  auto &state = it->second;
  state->response_received = true;

  if (resp.status != STATUS_OK) {
    state->failed = true;
    state->error_message =
        (resp.status == STATUS_NOT_FOUND) ? "File not found" : "Server error";
    std::cerr << "File request failed: " << resp.file_id << " - "
              << state->error_message << std::endl;

    if (complete_cb_) {
      complete_cb_(resp.file_id, false, state->error_message);
    }
    return;
  }

  state->file_size = resp.file_size;
  state->total_chunks = resp.total_chunks;

  // Инициализируем хранилище
  if (!storage_.request_file(resp.file_id, resp.file_size,
                             state->output_path)) {
    state->failed = true;
    state->error_message = "Failed to initialize storage";
    std::cerr << "Storage init failed: " << resp.file_id << std::endl;

    if (complete_cb_) {
      complete_cb_(resp.file_id, false, state->error_message);
    }
    return;
  }

  std::cout << "Receiving file: " << resp.file_id << " (" << resp.file_size
            << " bytes, " << resp.total_chunks << " chunks)" << std::endl;
}

void FileClient::handle_file_chunk(const FileChunk &chunk) {
  // Добавляем задачу в очередь для worker thread
  ChunkTask task;
  task.file_id = chunk.file_id;
  task.chunk_index = chunk.chunk_index;
  task.data = chunk.data;

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    task_queue_.push(std::move(task));
  }
  queue_cv_.notify_one();
}

void FileClient::worker_thread() {
  while (workers_running_) {
    ChunkTask task;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(
          lock, [this] { return !task_queue_.empty() || !workers_running_; });

      if (!workers_running_)
        break;
      if (task_queue_.empty())
        continue;

      task = std::move(task_queue_.front());
      task_queue_.pop();
    }

    // Обрабатываем чанк
    bool is_complete = storage_.add_chunk(task.file_id, task.chunk_index,
                                          task.data.data(), task.data.size());

    update_progress(task.file_id);

    if (is_complete) {
      std::lock_guard<std::mutex> lock(downloads_mutex_);
      auto it = downloads_.find(task.file_id);
      if (it != downloads_.end()) {
        it->second->completed = true;

        if (complete_cb_) {
          complete_cb_(task.file_id, true, "");
        }
      }
    }
  }
}

void FileClient::event_loop_thread() {
  while (event_loop_running_) {
    poll(100);
  }
}

void FileClient::check_timeouts() {
  auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(downloads_mutex_);

  for (auto &[file_id, state] : downloads_) {
    if (state->completed || state->failed)
      continue;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - state->request_time);

    // Таймаут на получение ответа от сервера
    if (!state->response_received && elapsed > std::chrono::seconds(10)) {
      state->failed = true;
      state->error_message = "Response timeout";
      if (complete_cb_) {
        complete_cb_(file_id, false, state->error_message);
      }
      continue;
    }

    // Таймаут на загрузку файла
    if (state->response_received && elapsed > std::chrono::seconds(300)) {
      state->failed = true;
      state->error_message = "Download timeout";
      if (complete_cb_) {
        complete_cb_(file_id, false, state->error_message);
      }
    }
  }
}

void FileClient::update_progress(const std::string &file_id) {
  double progress = storage_.get_progress(file_id);

  if (progress_cb_) {
    progress_cb_(file_id, progress);
  }
}

bool FileClient::is_download_complete(const std::string &file_id) const {
  std::lock_guard<std::mutex> lock(downloads_mutex_);
  auto it = downloads_.find(file_id);
  if (it == downloads_.end())
    return false;
  return it->second->completed;
}

bool FileClient::is_download_failed(const std::string &file_id) const {
  std::lock_guard<std::mutex> lock(downloads_mutex_);
  auto it = downloads_.find(file_id);
  if (it == downloads_.end())
    return false;
  return it->second->failed;
}

double FileClient::get_progress(const std::string &file_id) const {
  return storage_.get_progress(file_id);
}

bool FileClient::wait_for_download(const std::string &file_id,
                                   std::chrono::seconds timeout) {
  auto start = std::chrono::steady_clock::now();

  // Сначала ждём response от сервера (чтобы storage был инициализирован)
  while (true) {
    {
      std::lock_guard<std::mutex> lock(downloads_mutex_);
      auto it = downloads_.find(file_id);
      if (it == downloads_.end())
        return false;
      if (it->second->failed)
        return false;
      if (it->second->response_received)
        break;
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > std::chrono::seconds(10)) {
      std::cerr << "Timeout waiting for server response" << std::endl;
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Теперь ждём, пока файл появится в storage
  while (!storage_.has_file(file_id)) {
    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > timeout) {
      std::cerr << "Timeout waiting for file initialization" << std::endl;
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Вычисляем оставшееся время
  auto elapsed = std::chrono::steady_clock::now() - start;
  auto remaining =
      timeout - std::chrono::duration_cast<std::chrono::seconds>(elapsed);

  if (remaining.count() <= 0) {
    return false;
  }

  // Теперь используем condition variable для ожидания завершения
  if (storage_.wait_for_file(file_id, remaining)) {
    return true;
  }

  // Проверяем failed
  std::lock_guard<std::mutex> lock(downloads_mutex_);
  auto it = downloads_.find(file_id);
  if (it != downloads_.end() && it->second->failed) {
    return false;
  }

  return false;
}

std::vector<std::string> FileClient::get_active_downloads() const {
  std::lock_guard<std::mutex> lock(downloads_mutex_);
  std::vector<std::string> result;
  for (const auto &[id, state] : downloads_) {
    if (!state->completed && !state->failed) {
      result.push_back(id);
    }
  }
  return result;
}

} // namespace file_transfer