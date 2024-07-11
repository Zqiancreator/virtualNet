#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SERVER "192.168.23.12"  // 目标服务器的IP地址
#define BUFLEN 512  // 最大缓冲区大小
#define PORT 12345   // 目标服务器的端口号
int main() {
    struct sockaddr_in serv_addr;
    int sock = 0;
    char *hello = "Test Hello from client";
    char buffer[1024] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // 将IPv4地址从文本转换为二进制形式
    if(inet_pton(AF_INET, SERVER, &serv_addr.sin_addr)<=0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // 连接到服务器上的监听端口
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    send(sock, hello, 20, 0);
    printf("Hello message sent\n");
    read(sock, buffer, 1024);
    printf("Message from server: %s\n", buffer);

    close(sock);
    
    return 0;
}