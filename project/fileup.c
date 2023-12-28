#include "string.h"
#include "dirent.h"
#include "unistd.h"
#include "fcntl.h"
#include "errno.h"
#include "fs.h"

#define PORT 50000

Message msg;

int main()
{
    struct sockaddr_in s_addr;

    int s_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_sockfd == -1)
    {
        perror("socket error!");
        exit(1);
    }

    memset(&s_addr, 0, sizeof(struct sockaddr_in));
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(PORT);
    s_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(s_sockfd, (struct sockaddr *)(&s_addr), sizeof(struct sockaddr_in)) == -1)
    {
        perror("connect error!");
        exit(1);
    }

    int choice;
    while (1)
    {
        printf("Welcome to Shared File Server\n");
        printf("=============================\n");
        printf("0. exit\n");
        printf("1. list all files\n");
        printf("2. delete file\n");
        printf("3. upload file\n");
        printf("4. download file\n");
        printf("5. search file\n");
        scanf("%d", &choice);
        switch (choice)
        {
        case EXIT_SYSTEM:
            printf("GoodBye!\n");
            shutdown(s_sockfd, SHUT_RDWR);
            close(s_sockfd);
            exit(0);
        case LIST_ALL_FILES:
            msg.type = LIST_ALL_FILES;
            write(s_sockfd, &msg, sizeof(Message));
            read(s_sockfd, &msg, sizeof(Message));
            printf("received:\n%s\n", msg.data);
            break;
        case DELETE_FILE:
        case SEARCH_FILE:
            msg.type = choice;
            printf("input the file name please:\n");
            scanf("%s", msg.filename);
            write(s_sockfd, &msg, sizeof(Message));
            read(s_sockfd, &msg, sizeof(Message));
            printf("received:\n%s\n", msg.data);
            break;
        case UPLOAD_FILE:
        {
            char filepath[100];
            msg.type = UPLOAD_FILE;
            printf("input the file name please:\n");
            scanf("%s", msg.filename);
            sprintf(filepath, "/home/liyb/project/%s", msg.filename);
            int fd = open(filepath, O_RDONLY);
            if (fd > 0)
            {
                printf("file open successful! fd:%d\n", fd);
            }
            else if (fd == -1)
            {
                printf("Error:%s, errno:%d\n", strerror(errno), errno);
                close(fd);
                break;
            }
            long length = 0;
            while (1)
            {
                int data_len = read(fd, &msg.data, sizeof(msg.data));
                if (data_len <= 0)
                {
                    printf("read over, data_len=%d\n", data_len);
                    break;
                }
                msg.data_len = data_len;
                printf("data_len:%d\n", msg.data_len);
                write(s_sockfd, &msg, sizeof(Message));
                length += data_len;
            }
            printf("%s.length=%ld\n", msg.filename, length);
            close(fd);
            break;
        }
        case DOWNLOAD_FILE:
        {
            char filepath[100];
            msg.type = DOWNLOAD_FILE;
            printf("input the file name please:\n");
            scanf("%s", msg.filename);
            sprintf(filepath, "/home/jason/c.d/%s", msg.filename);
            int fd = open(filepath, O_RDWR | O_CREAT, 0644);
            if (fd > 0)
            {
                printf("file open successful! fd:%d\n", fd);
            }
            else if (fd == -1)
            {
                printf("Error:%s, errno:%d\n", strerror(errno), errno);
                close(fd);
                break;
            }
            long length = 0;
            write(s_sockfd, &msg, sizeof(Message));
            while (1)
            {
                read(s_sockfd, &msg, sizeof(Message));
                if (msg.data_len == -1)
                {
                    printf("file is not exist,download failed.\n");
                    break;
                }
                printf("data_len=%d\n", msg.data_len);
                write(fd, msg.data, msg.data_len);
                length += msg.data_len;
                if (msg.data_len < 1024)
                {
                    break;
                }
            }
            printf("%s.length=%ld\n", msg.filename, length);
            close(fd);
            break;
        }
        default:
            printf("You have selected wrong option!\n");
            exit(0);
        }
    }
    return 0;
}