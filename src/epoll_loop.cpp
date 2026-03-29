#include "epoll_loop.h"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>

namespace rudpr {

EpollLoop::EpollLoop(int max_events) : max_events_(max_events) {
  epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd_ >= 0) {
    event_buffer_.resize(max_events_);
  }
}

EpollLoop::~EpollLoop() {
  if (epoll_fd_ >= 0) {
    close(epoll_fd_);
  }
}

bool EpollLoop::add(int fd, uint32_t events) {
  struct epoll_event ev;
  ev.events = events;
  ev.data.fd = fd;
  return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool EpollLoop::modify(int fd, uint32_t events) {
  struct epoll_event ev;
  ev.events = events;
  ev.data.fd = fd;
  return epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool EpollLoop::remove(int fd) {
  return epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

int EpollLoop::wait(std::vector<EpollEvent>& events, int timeout_ms) {
  events.clear();
  
  int nfds = epoll_wait(epoll_fd_, event_buffer_.data(), max_events_, timeout_ms);
  if (nfds < 0) {
    return nfds;
  }
  
  events.reserve(nfds);
  for (int i = 0; i < nfds; ++i) {
    events.push_back({event_buffer_[i].data.fd, event_buffer_[i].events});
  }
  
  return nfds;
}

} // namespace rudpr
