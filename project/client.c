#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <bits/pthreadtypes.h>

#define BUFFSIZE 1280
#define HOST_IP "127.0.0.1"
#define PORT 8000

int sockfd;

void snd();

int main()
{
    pthread_t thread;             /*pthread_t 线程，gcc编译时需加上-lpthread*/
    struct sockaddr_in serv_addr; // struct sockaddr_in
    char buf[BUFFSIZE];
    /*初始化服务端地址结构*/
    memset(&serv_addr, 0, sizeof(struct sockaddr_in));
    serv_addr.sin_family = AF_INET;          // sin_family   AF_INET
    serv_addr.sin_port = htons(PORT);        // sin_port     htons(PORT)
    inet_pton(HOST_IP, &serv_addr.sin_addr); // inet_pton
    // 创建客户端套接字
    sockfd = socket(AF_INET, SOCK_STREAM, 0); // socket 创建套接字
    if (sockfd < 0)
    {
        perror("fail to socket");
        exit(-1);
    }
    // 与服务器建立连接
    printf("connecting... \n");
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) // connect
    {
        perror("fail to connect");
        exit(-2);
    }
    /* === 主进程接收数据，从此处开始 程序分做两个线程 === */
    // 创建发送消息的线程，调用发送消息的函数snd
    pthread_create(&thread, NULL, (void *)(&snd), NULL); // pthread_create
    // 接收消息的线程,将服务器上发送的消息打印到客户端的屏幕上
    while (1)
    {
        int len;
        if ((len = read(sockfd, buf, BUFFSIZE)) > 0) // read 读取通信套接字
        {
            buf[len] = '\0'; // 添加结束符，避免显示缓冲区中残留的内容
            printf("\n%s", buf);
            fflush(stdout); // fflush 冲洗标准输出，确保内容及时显示
        }
    }
    return 0;
}

/*发送消息的函数*/
void snd()
{
    char temp[32], buf[BUFFSIZE];

    while (1)
    {
        fgets(buf, BUFFSIZE, stdin);
        buf[strcspn(buf, "\n")] = '\0'; // 清除读入的回车
        // printf("客户端输入的信息为：%s", buf);
        write(sockfd, buf, strlen(buf));
        memset(buf, 0, sizeof(buf));
        // printf("传输完成后的信息为：%s", buf);
        /* 客户端输入bye则退出 */
        if (strcmp(buf, "bye\n") == 0) // 注意此处的\n
            exit(0);
    }
}