#pragma once
#include "file_protocol.h"
#include "file_storage.h"
#include "src/reliable_udp.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace file_transfer {

// Задача для worker-потока
struct ChunkTask {
  std::string file_id;
  uint32_t chunk_index;
  std::vector<uint8_t> data;
};

// Состояние загрузки файла
struct DownloadState {
  std::string file_id;
  std::string output_path;
  uint64_t file_size = 0;
  uint32_t total_chunks = 0;
  std::atomic<bool> response_received{false};
  std::atomic<bool> completed{false};
  std::atomic<bool> failed{false};
  std::string error_message;
  std::chrono::steady_clock::time_point request_time;
};

// Callback для событий клиента
using DownloadCompleteCallback = std::function<void(
    const std::string &file_id, bool success, const std::string &error)>;
using DownloadProgressCallback =
    std::function<void(const std::string &file_id, double progress)>;

class FileClient {
public:
  explicit FileClient(size_t num_workers = 4);
  ~FileClient();

  bool connect(const std::string &server_host, uint16_t server_port);

  void disconnect();

  // Запросить файл
  // Возвращает true если запрос отправлен успешно
  bool request_file(const std::string &file_id, const std::string &output_path);

  // Запустить обработку событий (в отдельном потоке или вручную)
  void start_event_loop();
  void stop_event_loop();
  void poll(int timeout_ms = 0);

  // Проверить статус загрузки
  bool is_download_complete(const std::string &file_id) const;
  bool is_download_failed(const std::string &file_id) const;
  double get_progress(const std::string &file_id) const;

  // Установить callbacks
  void on_download_complete(DownloadCompleteCallback cb) {
    complete_cb_ = std::move(cb);
  }
  void on_progress(DownloadProgressCallback cb) {
    progress_cb_ = std::move(cb);
  }

  // Ждать завершения загрузки (блокирующий)
  bool
  wait_for_download(const std::string &file_id,
                    std::chrono::seconds timeout = std::chrono::seconds(60));

  // Получить список активных загрузок
  std::vector<std::string> get_active_downloads() const;

private:
  void handle_message(const std::vector<uint8_t> &data);
  void handle_file_response(const FileResponse &resp);
  void handle_file_chunk(const FileChunk &chunk);

  void worker_thread();
  void event_loop_thread();
  void check_timeouts();
  void update_progress(const std::string &file_id);

private:
  size_t num_workers_;
  int sock_fd_ = -1;
  std::unique_ptr<rudpr::ReliableUDP> rudp_;

  sockaddr_storage server_addr_;
  socklen_t server_addrlen_ = 0;
  std::atomic<bool> connected_{false};

  // Worker threads
  std::vector<std::thread> workers_;
  std::queue<ChunkTask> task_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::atomic<bool> workers_running_{false};

  // Event loop thread
  std::thread event_thread_;
  std::atomic<bool> event_loop_running_{false};

  // Storage
  FileStorage storage_;

  // Downloads state
  mutable std::mutex downloads_mutex_;
  std::unordered_map<std::string, std::unique_ptr<DownloadState>> downloads_;

  // Callbacks
  DownloadCompleteCallback complete_cb_;
  DownloadProgressCallback progress_cb_;
};

} // namespace file_transfer
