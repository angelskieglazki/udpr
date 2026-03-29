#pragma once
#include <sys/epoll.h>
#include <vector>
#include <functional>

namespace rudpr {

// Событие epoll
struct EpollEvent {
  int fd;
  uint32_t events;
};

// Обертка над epoll
class EpollLoop {
public:
  explicit EpollLoop(int max_events = 64);
  ~EpollLoop();
  EpollLoop(const EpollLoop&) = delete;
  EpollLoop& operator=(const EpollLoop&) = delete;
  
  bool add(int fd, uint32_t events = EPOLLIN);
  bool modify(int fd, uint32_t events);
  bool remove(int fd);
  
  // Ожидать события
  // timeout_ms: -1 = бесконечно, 0 = не блокировать
  // Возвращает количество событий или -1 при ошибке
  int wait(std::vector<EpollEvent>& events, int timeout_ms = -1);
  
  int fd() const { return epoll_fd_; }

private:
  int epoll_fd_ = -1;
  int max_events_;
  std::vector<struct epoll_event> event_buffer_;
};

} // namespace rudpr
