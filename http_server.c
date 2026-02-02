#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define TIMEOUT_SEC 5

const char *response_body =
    "<html><body><h1>Hello from Keep-Alive Server!</h1></body></html>";

int check_keep_alive(const char *request) {
  // HTTP/1.1 defaults to keep-alive, HTTP/1.0 needs explicit header
  if (strstr(request, "HTTP/1.1"))
    return strstr(request, "Connection: close") == NULL;
  return strstr(request, "Connection: keep-alive") != NULL;
}

void send_response(int client_fd, int keep_alive) {
  char response[BUFFER_SIZE];
  int body_len = strlen(response_body);

  snprintf(response, sizeof(response),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/html\r\n"
           "Content-Length: %d\r\n"
           "Connection: %s\r\n"
           "\r\n"
           "%s",
           body_len, keep_alive ? "keep-alive" : "close", response_body);

  write(client_fd, response, strlen(response));
}

void handle_client(int client_fd) {
  char buffer[BUFFER_SIZE];
  struct timeval tv = {TIMEOUT_SEC, 0};

  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  while (1) {
    memset(buffer, 0, BUFFER_SIZE);
    int bytes = read(client_fd, buffer, BUFFER_SIZE - 1);

    if (bytes <= 0)
      break;

    printf("Request:\n%s\n", buffer);

    int keep_alive = check_keep_alive(buffer);
    send_response(client_fd, keep_alive);

    if (!keep_alive)
      break;
  }

  close(client_fd);
  printf("Connection closed\n");
}

int main() {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {.sin_family = AF_INET,
                             .sin_addr.s_addr = INADDR_ANY,
                             .sin_port = htons(PORT)};

  bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
  listen(server_fd, 10);

  printf("Server listening on port %d\n", PORT);

  while (1) {
    int client_fd = accept(server_fd, NULL, NULL);
    printf("New connection\n");
    handle_client(client_fd);
  }

  return 0;
}
