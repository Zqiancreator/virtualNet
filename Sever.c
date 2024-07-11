#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 12345
#define IPADDR "192.168.23.12"

int main() {
    int server_fd, new_socket; 
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // 创建socket文件描述符
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 设置socket选项，防止端口占用错误
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    // address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_addr.s_addr = inet_addr(IPADDR); 
    address.sin_port = htons(PORT);

    // 绑定socket到端口
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address))<0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 开始监听端口
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    printf("Listening on port %d...\n", PORT);

    // 接受来自client的连接
    if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen))<0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    // 用于存储接收到的数据
    char buffer[1024] = {0};
    // 接收数据
    int bytes_received = recv(new_socket, buffer, sizeof(buffer), 0);
    if (bytes_received < 0) {
        perror("recv");
        exit(EXIT_FAILURE);
    }

    // 打印接收到的数据
    printf("Received message: %s\n", buffer);

    close(new_socket);
    close(server_fd);

    return 0;
}