#include "reliable_udp.h"
#include <arpa/inet.h>
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
  ReliableUDP rudp(sock);

  sockaddr_in server{};
  server.sin_family = AF_INET;
  server.sin_port = htons(8080);
  inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

  std::string msg = "Привет от надёжного UDP!";
  if (rudp.send(msg.data(), msg.size(), (sockaddr *)&server, sizeof(server))) {
    std::cout << "Сообщение отправлено успешно\n";
  }

  auto reply = rudp.recv();
  if (!reply.empty()) {
    std::string r(reply.begin(), reply.end());
    std::cout << "Ответ от сервера: " << r << "\n";
  }

  close(sock);
}