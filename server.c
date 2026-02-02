#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 131072

void do_select(int fd, int type) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(fd, &set);
  if (type == 0)
    select(fd + 1, &set, NULL, NULL, NULL); // Read
  else
    select(fd + 1, NULL, &set, NULL, NULL); // Write
}

void do_poll(int fd, int type) {
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = (type == 0) ? POLLIN : POLLOUT;
  poll(&pfd, 1, -1);
}

void do_epoll(int epfd, int fd, int type) {
  struct epoll_event ev;
  ev.events = (type == 0) ? EPOLLIN : EPOLLOUT;
  ev.data.fd = fd;
  // Modify the existing entry to wait for the specific event
  epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
  struct epoll_event events[1];
  epoll_wait(epfd, events, 1, -1);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(1);
  }

  int port = atoi(argv[1]);
  int server_fd, client_fd;
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);
  char buffer[BUF_SIZE];

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
    perror("setsockopt SO_REUSEPORT");
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, 3) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  printf("Server listening on port %d (SO_REUSEPORT)...\n", port);

  while (1) {
    if ((client_fd = accept(server_fd, (struct sockaddr *)&address,
                            (socklen_t *)&addrlen)) < 0) {
      perror("accept");
      continue;
    }

    // --- HANDSHAKE START ---
    char mode_char = 0;
    if (recv(client_fd, &mode_char, 1, MSG_WAITALL) <= 0) {
      close(client_fd);
      continue;
    }

    int mode = 0; // 0=select, 1=poll, 2=epoll
    if (mode_char == 'P')
      mode = 1;
    if (mode_char == 'E')
      mode = 2;

    int epfd = -1;
    if (mode == 2) {
      epfd = epoll_create1(0);
      struct epoll_event ev;
      ev.events = EPOLLIN; // Default init
      ev.data.fd = client_fd;
      epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);
    }
    // --- HANDSHAKE END ---

    ssize_t valread;
    while (1) {
      // Wait for Read Readiness
      if (mode == 1)
        do_poll(client_fd, 0);
      else if (mode == 2)
        do_epoll(epfd, client_fd, 0);
      else
        do_select(client_fd, 0);

      valread = read(client_fd, buffer, BUF_SIZE);

      if (valread <= 0)
        break;

      // (2-1) Heuristic: 1 byte = RR, >1 byte = Stream
      if (valread == 1) {
        // RR Mode: Wait for Write Readiness -> Write
        if (mode == 1)
          do_poll(client_fd, 1);
        else if (mode == 2)
          do_epoll(epfd, client_fd, 1);
        else
          do_select(client_fd, 1);

        write(client_fd, buffer, 1);
      }
    }
    if (epfd != -1)
      close(epfd);
    close(client_fd);
  }
  return 0;
}
