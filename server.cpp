
#include "reliable_udp.h"
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    std::cerr << "Failed to create socket\n";
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8080);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
    std::cerr << "Failed to bind socket\n";
    close(sock);
    return 1;
  }

  ReliableUDP rudp(sock);

  std::cout << "Reliable UDP server listening on :8080\n";

  while (true) {
    sockaddr_in client{};
    socklen_t clen = sizeof(client);
    auto data = rudp.recv((sockaddr *)&client, &clen);

    if (!data.empty()) {
      std::string msg(data.begin(), data.end());
      std::cout << "Received: " << msg << "\n";
      // отправляем обратно
      rudp.send(msg.data(), msg.size(), (sockaddr *)&client, clen);
    }
  }

  close(sock);
}