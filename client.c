#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define STREAM_BUF_SIZE 16384
volatile sig_atomic_t keep_running = 1;

void handle_alarm(int sig) { keep_running = 0; }

void do_select(int fd, int type) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(fd, &set);
  if (type == 0)
    select(fd + 1, &set, NULL, NULL, NULL);
  else
    select(fd + 1, NULL, &set, NULL, NULL);
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
  epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
  struct epoll_event events[1];
  epoll_wait(epfd, events, 1, -1);
}

int main(int argc, char *argv[]) {
  if (argc != 6) {
    fprintf(stderr,
            "Usage: %s <server_ip> <server_port> <time_s> <stream|rr> "
            "<poll|select|epoll>\n",
            argv[0]);
    exit(1);
  }

  const char *server_ip = argv[1];
  int port = atoi(argv[2]);
  int time_s = atoi(argv[3]);
  const char *pattern = argv[4]; // stream or rr
  const char *mux = argv[5];     // poll or select or epoll

  int is_stream = (strcmp(pattern, "stream") == 0);
  int mode = 0; // 0=select
  if (strcmp(mux, "poll") == 0)
    mode = 1;
  if (strcmp(mux, "epoll") == 0)
    mode = 2;

  int sock = 0;
  struct sockaddr_in serv_addr;
  char buffer[STREAM_BUF_SIZE];
  memset(buffer, 'X', sizeof(buffer));

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Socket creation error");
    exit(EXIT_FAILURE);
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
    perror("Invalid address");
    exit(EXIT_FAILURE);
  }

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("Connection Failed");
    exit(EXIT_FAILURE);
  }

  // --- HANDSHAKE ---
  char mux_char = 'S';
  if (mode == 1)
    mux_char = 'P';
  if (mode == 2)
    mux_char = 'E';
  write(sock, &mux_char, 1);

  // Epoll Setup
  int epfd = -1;
  if (mode == 2) {
    epfd = epoll_create1(0);
    struct epoll_event ev;
    ev.events = EPOLLOUT; // Start default
    ev.data.fd = sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev);
  }

  signal(SIGALRM, handle_alarm);
  alarm(time_s);

  unsigned long long bytes_count = 0;
  unsigned long long loop_count = 0;

  printf("Starting %s test using %s for %d seconds...\n", pattern, mux, time_s);

  while (keep_running) {
    if (is_stream) {
      // STREAM: Wait Write -> Write
      if (mode == 1)
        do_poll(sock, 1);
      else if (mode == 2)
        do_epoll(epfd, sock, 1);
      else
        do_select(sock, 1);

      int n = write(sock, buffer, STREAM_BUF_SIZE);
      if (n > 0)
        bytes_count += n;
      else if (n < 0)
        break;
    } else {
      // RR: Wait Write -> Write 1
      if (mode == 1)
        do_poll(sock, 1);
      else if (mode == 2)
        do_epoll(epfd, sock, 1);
      else
        do_select(sock, 1);

      if (write(sock, buffer, 1) <= 0)
        break;

      // RR: Wait Read -> Read 1
      if (mode == 1)
        do_poll(sock, 0);
      else if (mode == 2)
        do_epoll(epfd, sock, 0);
      else
        do_select(sock, 0);

      if (read(sock, buffer, 1) <= 0)
        break;
      loop_count++;
    }
  }

  printf("Test finished.\n");
  if (is_stream) {
    printf("Mode: STREAM | Total Bytes Sent: %llu | Speed: %.2f MB/s\n",
           bytes_count, (double)bytes_count / time_s / 1024 / 1024);
  } else {
    printf("Mode: RR | Total Ping-Pongs: %llu | Latency: %.2f us/op\n",
           loop_count, (double)time_s * 1000000 / loop_count);
  }

  if (epfd != -1)
    close(epfd);
  close(sock);
  return 0;
}
