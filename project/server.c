#include <stdio.h>
#include <stdlib.h> // exit
#include <string.h>
#include <unistd.h> // bind listen
#include <time.h>   // time(NULL)   ctime(&ticks)
#include <netinet/in.h>
#include <arpa/inet.h> // 必须包含，用于inet_ntop
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sqlite3.h>

#define PORT 8000
#define MAXMEM 10
#define BUFFSIZE 1280
#define MAXCOUNT 10000

// #define DEBUG_PRINT 1         // 宏定义 调试开关
#ifdef DEBUG_PRINT
#define DEBUG(format, ...) printf("FILE: "__FILE__            \
                                  ", LINE: %d: " format "\n", \
                                  __LINE__, ##__VA_ARGS__)
#else
#define DEBUG(format, ...)
#endif

char *repo = "/home/liyb/project"; // 服务器的根目录
FILE *fp;
int listenfd, connfd[MAXMEM];

struct user
{
    int id; // 存储用户的唯一标识
    char *username;
    char *password;
    char *isLogin; // 存储当前用户的状态 true表示已经登录，false表示没有登录
} Users[MAXCOUNT]; // 存储所有查询到的用户的信息

/* 结构体存储当前用户的相关信息*/
struct client
{
    char name[20];     /* 用户名 */
    int socket_name;   /* 套接字 */
    char chatroom[20]; /* 加入的聊天室 */
};
struct client info_class[MAXMEM]; /* 用来存储连接的所有用户 */

sqlite3 *db;             // 数据库指针
char *error_message = 0; // 存储错误信息

void quit(); /*输入quit时退出服务器*/
void rcv_snd(int n);
void regist(int n);                                                            /* 注册账户 */
void login(int n);                                                             /* 登录 */
void create_chatroom(int n);                                                   /* 创建聊天室函数 */
void join_chatroom(int n);                                                     /* 加入聊天室函数 */
void show(int n);                                                              /* 服务命令处理函数 */
void initDb();                                                                 /* 数据库的初始化 */
int CheckIsLoginUser(sqlite3 *db, const char *username, const char *password); /* 检查用户是否是数据库中的用户的函数 */
void addDataToDbMessage(sqlite3 *db, char *sender, char *receiver, char *message);
void selectDataFromDbMessage(sqlite3 *db, char *name, int n);
void deleteDataFromDbMessage(sqlite3 *db, char *name);
int selectUserFromDbUser(sqlite3 *db, char *name, struct user *us); // 数据库中查询是否存在该用户，如果存在发送离线消息

int main()
{
    // 创建数据库
    initDb();

    struct sockaddr_in serv_addr, cli_addr;
    int i;
    time_t ticks;
    pthread_t thread; // 用于执行退出线程
    char buff[BUFFSIZE];

    printf("running...\n(提示: 输入命令 "
           "quit"
           " 来断开与服务器的连接)\n");
    DEBUG("=== 正在初始化中..."); // 初始化填充服务端地址结构
    /* 将之前的冗余信息清空，用来存储新的信息 */
    memset(&serv_addr, 0, sizeof(struct sockaddr_in));

    serv_addr.sin_family = AF_INET;                // TCP协议
    serv_addr.sin_port = htons(PORT);              // 服务器的端口号
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 设置为任意可用地址

    DEBUG("=== socket...");
    /* 创建服务器端的监听套接字 */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
    {
        perror("socket 创建失败, 请检查代码!");
        exit(-1);
    }

    DEBUG("=== bind...");
    /* 将监听套接字和ip+端口进行绑定 */
    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind 失败, 请检查代码!");
        exit(-2);
    }

    DEBUG("=== listening...");
    /* listen 将主动连接套接字变为被动倾听套接字，且指定最大连接数量为10 */
    listen(listenfd, MAXMEM);

    /* === 创建一个线程，对服务器程序进行管理，调用quit函数 === */
    pthread_create(&thread, NULL, (void *)(quit), NULL);

    // 将套接字描述符数组初始化为-1，表示空闲
    for (i = 0; i < MAXMEM; i++)
        connfd[i] = -1;

    while (1)
    {
        int len;                     // = sizeof(cli_addr); 存储客户端的地址长度
        for (i = 0; i < MAXMEM; i++) // 找到一个空闲的位置
        {
            if (connfd[i] == -1)
                break;
        }
        // accept 从listen接受的连接队列中取得一个连接
        connfd[i] = accept(listenfd, (struct sockaddr *)&cli_addr, &len);
        if (connfd[i] < 0)
        {
            perror("accept 失败, 请检查代码!");
            exit(0);
        }
        ticks = time(NULL); // 获得当前系统时间
        // sprintf(buff, "%.24s\r\n", ctime(&ticks));
        printf("%.24s\n\tconnect from: %s, port %d\n",
               ctime(&ticks), inet_ntop(AF_INET, &(cli_addr.sin_addr), buff, BUFFSIZE),
               ntohs(cli_addr.sin_port)); // 注意 inet_ntop的使用，#include <arpa/inet.h>

        /* === 针对当前套接字创建一个线程，对当前套接字的消息进行处理 === */
        pthread_create(malloc(sizeof(pthread_t)), NULL, (void *)(&rcv_snd), (void *)i);
    }
    return 0;
}

void quit()
{
    char msg[10];
    while (1)
    {
        scanf("%s", msg);             // scanf 不同于fgets, 它不会读入最后输入的换行符
        if (strcmp(msg, "quit") == 0) // 如果输入的是quit
        {
            printf("Byebye... \n");
            close(listenfd);
            exit(0);
        }
    }
}

// 当前函数是用于服务器和客户端进行交互操作的
void rcv_snd(int n)
{
    int len, i;
    char buf[BUFFSIZE], mytime[32], v[5]; /*v用接受服务命令字符*/
    time_t ticks;
    int ret; // 存储当线程的退出状态信息

    write(connfd[n], "---服务选择(请输入服务前对应的数字进行选择,例如:1)---\n\t1.用户注册\n\t2.用户登录\n",
          strlen("---服务选择(请输入服务前对应的数字进行选择,例如:1)---\n\t1.用户注册\n\t2.用户登录\n"));
    len = read(connfd[n], v, 1); /* 读取客户端输入的１或者２命令 */
    v[len] = '\0';
    if (strcmp(v, "1") == 0)
    { /*如果选择注册服务*/
        strcpy(buf, "---用户注册---\n");
        /*在客户端显示当前状态*/
        write(connfd[n], buf, strlen(buf));
        regist(n);
    }
    else if (strcmp(v, "2") == 0)
    { /*如果选择登录服务*/
        strcpy(buf, "---用户登录---\n");
        /*在客户端显示当前状态*/
        write(connfd[n], buf, strlen(buf));
        login(n);
    }
    else
    {
        write(connfd[n], "无效的命令，请重新进行输入.\n",
              strlen("无效的命令，请重新进行输入.\n"));
        rcv_snd(n); /* 输入其他值时重新进入服务选项 */
    }

    // TODO: 对发送过来的消息进行显示
    selectDataFromDbMessage(db, info_class[n].name, n); // 查询是否有人发送消息给自己

    /* 登陆成功后服务提示，这里就是大厅 */
    write(connfd[n], "---服务选择(1/2/3/4)---\n\t1.显示在线人数\n\t2.展示所有的聊天室\n\t3.创建一个新的聊天室\n\t4.加入现有的聊天室\n",
          strlen("---服务选择(1/2/3/4)---\n\t1.显示在线人数\n\t2.展示所有的聊天室\n\t3.创建一个新的聊天室\n\t4.加入现有的聊天室\n\t"));
    show(n); // 对上面用户进行的指令分别进行处理

    // 在大厅处理完命令后开始对用户的输入进行监听，默认情况下进入群聊系统
    while (1)
    {
        char temp[BUFFSIZE];
        char *str;
        if ((len = read(connfd[n], temp, BUFFSIZE)) > 0)
        {
            // printf("客户端得到的输入为：%s", temp);
            temp[len] = '\0';
            /* 当用户输入bye时，当前用户退出 */
            if (strcmp(temp, "bye") == 0)
            {
                close(connfd[n]);
                connfd[n] = -1;
                pthread_exit(&ret);
                printf("执行bye指令，当前的connfd的值为：%d\n", connfd[n]);
            }
            else if (strcmp(temp, "help") == 0)
            {
                memset(buf, 0, sizeof(buf));
                strcpy(buf, "\t你可以尝试输入下面的命令.\n");
                write(connfd[n], buf, strlen(buf));
                strcpy(buf, "[chat to user1]: 和某一个用户进行私聊.\n");
                write(connfd[n], buf, strlen(buf));
                strcpy(buf, "[back]: 返回大厅.\n");
                write(connfd[n], buf, strlen(buf));
                strcpy(buf, "[bye]: 断开和服务器的连接.\n");
                write(connfd[n], buf, strlen(buf));
            }
            else if (strcmp(temp, "back") == 0)
            {
                write(connfd[n], "---服务选择(1/2/3/4)---\n\t1.显示在线的用户数\n\t2.显示所有的聊天室\n\t3.创建一个新的聊天室\n\t4.加入一个聊天室\n",
                      strlen("---服务选择(1/2/3/4)---\n\t1.显示在线的用户数\n\t2.显示所有的聊天室\n\t3.创建一个新的聊天室\n\t4.加入一个聊天室\n"));
                memset(info_class[n].chatroom, 0, sizeof(info_class[n].chatroom)); /* 返回大厅后即退出聊天室 */
                show(n);
            }
            /* 如果当前客户端在聊天室内 */
            else if (strlen(info_class[n].chatroom) != 0)
            {
                /* 如果发现关键字符串"chat to"，从聊天室转入私聊程序段 */
                if ((str = strstr(temp, "chat to ")) != 0)
                {
                    char des[10]; /* 用来存储目标用户的名称，从chat to user中获取 */
                    strncpy(des, temp + 8, sizeof(des) - 1);
                    memset(temp, 0, sizeof(temp));
                    int flag = 0; // 默认是不存在的
                    // 首先在所有上线的用户中查找是否存在当前用户
                    for (i = 0; i < MAXMEM; i++)
                    {
                        if (strcmp(des, info_class[i].name) == 0) // 找到要私聊的对象
                        {
                            flag = 1;
                            // 循环处理当前请求
                            while (1)
                            {
                                memset(temp, 0, sizeof(temp));
                                if ((len = read(connfd[n], temp, BUFFSIZE)) > 0) // 从发送方的客户端不断进行读取消息
                                {
                                    temp[len] = '\0';
                                    if (strcmp(temp, "back") == 0)
                                    { /* 如果收到back字段，则返回群聊 */
                                        write(connfd[n], "你已经返回群聊界面, 请发送消息或者是使用'back'命令退出.\n",
                                              strlen("你已经返回群聊界面, 请发送消息或者是使用'back'命令退出.\n"));
                                        break;
                                    }
                                    else
                                    {
                                        // strcat用来实现字符串追加
                                        ticks = time(NULL);
                                        strftime(mytime, sizeof(mytime), "[%H:%M:%S]", localtime(&ticks));
                                        strcpy(buf, mytime); // 显示时间
                                        strcat(buf, "\t");
                                        strcat(buf, info_class[n].name); // 将发送方的名字追加过去，即显示姓名
                                        strcat(buf, ":\t");
                                        strcat(buf, temp); // 客户端消息内容
                                        temp[len] = '\0';
                                        strcat(buf, "\n");
                                        write(connfd[i], buf, strlen(buf));
                                        write(connfd[n], buf, strlen(buf));
                                    }
                                }
                            }
                        }
                    }
                }
                else if (strcmp(temp, "show member") == 0)
                { /* show member显示聊天室内成员 */
                    char member[50];
                    strcpy(member, "---当前聊天室的成员如下---\n");
                    for (int i = 0; i < MAXMEM; i++)
                    {
                        if (strcmp(info_class[i].chatroom, info_class[n].chatroom) == 0)
                        {
                            strcat(member, info_class[i].name); /* 将在线用户名添加到info */
                            strcat(member, "\n");
                        }
                    }
                    write(connfd[n], member, strlen(member));
                }
                else
                { /* 不是私聊那就是在聊天室内群发 */
                    for (i = 0; i < MAXMEM; i++)
                    {
                        /* 寻找结构体中聊天室名相同的发送信息 */
                        if (strcmp(info_class[i].chatroom, info_class[n].chatroom) == 0)
                        {
                            char chatroom[20];
                            strcpy(chatroom, info_class[n].chatroom);
                            chatroom[strlen(chatroom) - 1] = '\0'; // 聊天室名称多加了一个回车

                            ticks = time(NULL);
                            strftime(mytime, sizeof(mytime), "[%H:%M:%S] [", localtime(&ticks));
                            strcpy(buf, mytime);   // 显示时间
                            strcat(buf, chatroom); // 显示群聊名称
                            strcat(buf, "]\t");
                            strcat(buf, info_class[n].name); // 显示用户名
                            strcat(buf, ":\t");
                            strcat(buf, temp); // 客户端消息内容
                            temp[len] = '\0';
                            strcat(buf, "\n");
                            write(connfd[i], buf, strlen(buf)); /* 将消息发送给聊天室内的每一个人 */
                        }
                    }
                }
            }
            /* 如果发现关键字符串"chat to"，转入私聊程序段 */
            else if ((str = strstr(temp, "chat to ")) != 0)
            {
                int x;                                        /* 查找chat to后跟用户名的开始下标值 */
                char des[10];                                 /* 用来存储目标用户的名称，从chat to user中获取 */
                x = strspn(temp, "chat to ");                 /* chat to 后面用户名字符串开始的索引 */
                strncpy(des, temp + x, strlen(temp) - x + 1); // 将用户名复制到des中
                int flag = 0;

                write(connfd[n], "你已经进入私聊程序段\n", strlen("你已经进入私聊程序段\n"));
                memset(temp, 0, sizeof(temp));
                for (i = 0; i < MAXMEM; i++)
                {
                    if (strcmp(des, info_class[i].name) == 0)
                    { /* 遍历匹配user1的姓名 */
                        flag = 1;
                        write(connfd[n], "找到该在线用户，发送消息给他吧：\n", strlen("找到该在线用户，发送消息给他吧：\n"));
                        while (1)
                        {
                            if ((len = read(connfd[n], temp, BUFFSIZE)) > 0) // 等待发送方输入下一步的指令
                            {
                                temp[len] = '\0';
                                printf("用户输入的消息为：%s", temp);
                                if (strcmp(temp, "back") == 0)
                                { /* 如果收到back字段，则返回大厅 */
                                    write(connfd[n], "你已经返回群聊界面, 请发送消息或者是使用'back'命令退出.\n",
                                          strlen("你已经返回群聊界面, 请发送消息或者是使用'back'命令退出.\n"));
                                    break;
                                }
                                else
                                {
                                    ticks = time(NULL);
                                    strftime(mytime, sizeof(mytime), "[%H:%M:%S]", localtime(&ticks));
                                    strcpy(buf, mytime); // 显示时间
                                    strcat(buf, "\t");
                                    strcat(buf, info_class[n].name); // 显示用户名
                                    strcat(buf, "[私聊消息]:\t");
                                    strcat(buf, temp); // 客户端消息内容
                                    temp[len] = '\0';
                                    strcat(buf, "\n");
                                    printf("我被执行了！");
                                    write(connfd[i], buf, strlen(buf));
                                    write(connfd[n], buf, strlen(buf));
                                }
                            }
                        }
                    }
                    if (flag)
                    {
                        break;
                    }
                }
                // TODO: 现在已经从数据库中查询出来了用户，只要不断从发送方接受数据然后存储到数据库中就行了
                if (!flag)
                {
                    struct user us;
                    int result = selectUserFromDbUser(db, des, &us);
                    printf("这里的result值为：%d\n", result);
                    printf("从数据库中查询出来用户名为：%s", us.username);
                    if (result == 1)
                    {
                        printf("用户未上线，但在数据库中查询到当前用户存在。\n");
                        write(connfd[n], "已找到该用户但用户未上线，可发送离线消息：\n",
                              strlen("已找到该用户但用户未上线，可发送离线消息：\n"));
                        flag = 1;
                        // 接收发送方的消息
                        while (1)
                        {
                            if ((len = read(connfd[n], temp, BUFFSIZE)) > 0)
                            {
                                temp[len] = '\0';
                                if (strcmp(temp, "back") == 0)
                                { /* 如果收到back字段，则返回大厅 */
                                    write(connfd[n], "你已经返回群聊界面, 请发送消息或者是使用'back'命令退出.\n",
                                          strlen("你已经返回群聊界面, 请发送消息或者是使用'back'命令退出.\n"));
                                    break;
                                }
                                else
                                {
                                    ticks = time(NULL);
                                    strftime(mytime, sizeof(mytime), "[%H:%M:%S]", localtime(&ticks));
                                    strcpy(buf, mytime); // 显示时间
                                    strcat(buf, "\t");
                                    strcat(buf, info_class[n].name); // 显示用户名
                                    strcat(buf, "[离线消息]:\t");
                                    strcat(buf, temp); // 客户端消息内容
                                    temp[len] = '\0';
                                    strcat(buf, "\n");

                                    printf("消息的内容如下：\n%s", buf);

                                    // 将消息写入数据库中进行保存，当用户上线的时候进行显示
                                    addDataToDbMessage(db, info_class[n].name, us.username, buf);
                                }
                            }
                        }
                    }
                    else if (result == 0)
                    {
                        printf("用户不存在。\n");
                    }
                    else
                    {
                        fprintf(stderr, "检查用户是否存在时出错。\n");
                    }
                }
                if (!flag)
                    write(connfd[n], "未找到该用户，请重新键入命令输入\n",
                          strlen("未找到该用户，请重新键入命令输入\n"));
            }
            // TODO : recent 实现文件的发送功能
            else if ((str = strstr(temp, "send file to ")) != 0)
            {
                printf("文件发送模块中接受者的姓名为：%s\n", temp);
                char des[50]; /* 用来存储目标用户的名称，从send file to user中获取 */
                strncpy(des, temp + 13, sizeof(des));
                printf("得到的des中的值为：%s\n", des);

                int flag = 0;
                // 文件存储的目录位置默认为该用户的用户名
                char directory_name[100];
                memcpy(directory_name, des, sizeof(des));
                // 当前目录下文件的名字
                char file_name[100];
                // 文件的数据
                char data[MAXCOUNT];

                printf("收件人的姓名为：%s\n", directory_name);
                for (int i = 0; i < MAXMEM; i++)
                {
                    printf("用户%d的姓名为：%s,长度为：%d\n", i, info_class[i].name, strlen(info_class[i].name));
                    printf("%d\n", strcmp(des, info_class[i].name));
                    if (connfd[i] != -1 && strcmp(des, info_class[i].name) == 0)
                    { /* 遍历匹配user1的姓名 */
                        write(connfd[n], "成功找到该用户\n", strlen("成功找到该用户\n"));
                        write(connfd[n], "请输入发送文件的名字：\n", strlen("请输入发送文件的名字：\n"));
                        flag = 1;
                        while (1)
                        {
                            if ((len = read(connfd[n], temp, BUFFSIZE)) > 0)
                            {
                                temp[len] = '\0';
                                if (strcmp(temp, "back") == 0)
                                { /* 如果收到back字段，则返回大厅 */
                                    write(connfd[n], "你已经返回群聊界面, 请发送消息或者是使用'back'命令退出.\n",
                                          strlen("你已经返回群聊界面, 请发送消息或者是使用'back'命令退出.\n"));
                                    break;
                                }
                                else
                                {
                                    strcpy(file_name, temp);
                                    ticks = time(NULL);
                                    strftime(mytime, sizeof(mytime), "[%H:%M:%S]", localtime(&ticks));
                                    strcpy(buf, mytime); // 显示时间
                                    strcat(buf, "\t");
                                    strcat(buf, info_class[n].name); // 显示用户名
                                    strcat(buf, ":\t");
                                    strcat(buf, "向你发送了一个文件!!!\n");
                                    write(connfd[i], buf, strlen(buf));

                                    // 将发送方中文件的内容复制到数组中
                                    char send_path[1000] = "/home/liyb/project/";
                                    strcat(send_path, info_class[n].name);
                                    strcat(send_path, "/");
                                    strcat(send_path, file_name);
                                    FILE *send_file = fopen(send_path, "r");
                                    if (send_file == NULL)
                                    {
                                        write(connfd[n], "文件传输失败,请重试\n",
                                              strlen("文件传输失败,请重试\n"));
                                    }
                                    fseek(send_file, 0, SEEK_END);
                                    int file_size = ftell(send_file);
                                    fseek(send_file, 0, SEEK_SET);
                                    size_t bytes_read = fread(data, 1, file_size, send_file);
                                    data[bytes_read] = '\0';
                                    fclose(send_file);

                                    printf("文件中的数据为：\n%s\n", data);

                                    // 将内容传送到接收方的目录中
                                    // TODO：这部分内容应该在客户端实现，因为应该是在客户端存储对应的文件
                                    char dir_path[1000] = "/home/liyb/project/";
                                    strcat(dir_path, directory_name);
                                    // 检查目录是否存在
                                    if (access(dir_path, F_OK) == 0)
                                    {
                                        // 目录存在，下载文件
                                        strcat(dir_path, "/");
                                        strcat(dir_path, file_name);
                                        printf("存储的目录为；%s", dir_path);
                                        FILE *file = fopen(dir_path, "w");
                                        if (file != NULL)
                                        {
                                            printf("文件创建成功！\n");
                                            fprintf(file, "%s", data);
                                            fclose(file);
                                            write(connfd[n], "文件传输成功!\n",
                                                  strlen("文件传输成功!\n"));
                                        }
                                        else
                                        {
                                            printf("无法创建文件");
                                        }
                                    }
                                    else
                                    {
                                        printf("%s\n", dir_path);
                                        // 目录不存在，可以进行创建
                                        int status = mkdir(dir_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH | S_IRWXU);

                                        if (status == 0)
                                        {
                                            printf("目录被成功创建！.\n");
                                            // 在当前目录下下载该文件
                                            strcat(dir_path, "/");
                                            strcat(dir_path, file_name);
                                            FILE *file = fopen(dir_path, "w");
                                            if (file != NULL)
                                            {
                                                printf("文件创建成功！");
                                                fprintf(file, "%s", data);
                                                fclose(file);
                                                write(connfd[n], "文件传输成功！",
                                                      strlen("文件传输成功"));
                                            }
                                            else
                                            {
                                                printf("无法创建文件");
                                            }
                                        }
                                        else
                                        {
                                            write(connfd[i], "文件目录创建失败，请重试",
                                                  strlen("文件目录创建失败，请重试"));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (!flag)
                {
                    write(connfd[n], "未找到该用户，请重新键入命令\n",
                          strlen("未找到该用户，请重新键入命令\n"));
                }
            }
            // TODO: 实现图片的发送
            else if ((str = strstr(temp, "send img to ")) != 0)
            {
                printf("图片发送模块中接受者的姓名为：%s\n", temp);
                char des[50]; /* 用来存储目标用户的名称，从send image to user中获取 */
                strncpy(des, temp + 12, sizeof(des) - 1);
                printf("得到的des中的值为：%s\n", des);

                int flag = 0;
                // 文件存储的目录位置默认为该用户的用户名
                char directory_name[100];
                memcpy(directory_name, des, sizeof(des));
                // 当前目录下文件的名字
                char file_name[100];
                // 文件的数据
                char data[MAXCOUNT];

                printf("收件人的姓名为：%s\n", directory_name);
                for (int i = 0; i < MAXMEM; i++)
                {
                    printf("用户%d的姓名为：%s,长度为：%d\n", i, info_class[i].name, strlen(info_class[i].name));
                    printf("%d\n", strcmp(des, info_class[i].name));
                    if (connfd[i] != -1 && strcmp(des, info_class[i].name) == 0)
                    { /* 遍历匹配user1的姓名 */
                        write(connfd[n], "成功找到该用户\n", strlen("成功找到该用户\n"));
                        write(connfd[n], "请输入发送图片的名字：\n", strlen("请输入发送图片的名字：\n"));
                        flag = 1;
                        while (1)
                        {
                            if ((len = read(connfd[n], temp, BUFFSIZE)) > 0)
                            {
                                temp[len] = '\0';
                                if (strcmp(temp, "back") == 0)
                                { /* 如果收到back字段，则返回大厅 */
                                    write(connfd[n], "你已经返回群聊界面, 请发送消息或者是使用'back'命令退出.\n",
                                          strlen("你已经返回群聊界面, 请发送消息或者是使用'back'命令退出.\n"));
                                    break;
                                }
                                else
                                {
                                    strcpy(file_name, temp);
                                    ticks = time(NULL);
                                    strftime(mytime, sizeof(mytime), "[%H:%M:%S]", localtime(&ticks));
                                    strcpy(buf, mytime); // 显示时间
                                    strcat(buf, "\t");
                                    strcat(buf, info_class[n].name); // 显示用户名
                                    strcat(buf, ":\t");
                                    strcat(buf, "向你发送了一张图片!!!\n");
                                    write(connfd[i], buf, strlen(buf));

                                    // 将发送方中图片的内容复制到数组中
                                    char send_path[1000] = "/home/liyb/project/";
                                    printf("发送方的姓名为：");
                                    strcat(send_path, info_class[n].name);
                                    strcat(send_path, "/");
                                    strcat(send_path, file_name);
                                    printf("发送方的路径为：%s\n", send_path);
                                    FILE *send_file = fopen(send_path, "rb");
                                    if (send_file == NULL)
                                    {
                                        write(connfd[n], "图片传输失败\n",
                                              strlen("图片传输失败\n"));
                                        return 1;
                                    }
                                    fseek(send_file, 0, SEEK_END);
                                    int file_size = ftell(send_file);
                                    fseek(send_file, 0, SEEK_SET);
                                    size_t bytes_read = fread(data, 1, file_size, send_file);
                                    fclose(send_file);

                                    // 将内容传送到接收方的目录中
                                    // TODO：这部分内容应该在客户端实现，因为应该是在客户端存储对应的图片
                                    char dir_path[1000] = "/home/liyb/project/";
                                    strcat(dir_path, directory_name);
                                    printf("%s\n", dir_path);
                                    // 检查目录是否存在
                                    if (access(dir_path, F_OK) == 0)
                                    {
                                        // 目录存在，下载图片
                                        strcat(dir_path, "/");
                                        strcat(dir_path, file_name);
                                        printf("存储的目录为：%s", dir_path);
                                        FILE *file = fopen(dir_path, "wb");
                                        if (file != NULL)
                                        {
                                            printf("图片创建成功！\n");
                                            fwrite(data, 1, bytes_read, file);
                                            fclose(file);
                                            write(connfd[n], "图片传输成功!\n",
                                                  strlen("图片传输成功!\n"));
                                        }
                                        else
                                        {
                                            printf("无法创建图片文件\n");
                                        }
                                    }
                                    else
                                    {
                                        printf("%s\n", dir_path);
                                        // 目录不存在，可以进行创建
                                        int status = mkdir(dir_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH | S_IRWXU);

                                        if (status == 0)
                                        {
                                            printf("目录被成功创建！.\n");
                                            // 在当前目录下下载该图片
                                            strcat(dir_path, "/");
                                            strcat(dir_path, file_name);
                                            FILE *file = fopen(dir_path, "wb");
                                            if (file != NULL)
                                            {
                                                printf("图片创建成功！");
                                                fwrite(data, 1, bytes_read, file);
                                                fclose(file);
                                                write(connfd[n], "图片传输成功！\n",
                                                      strlen("图片传输成功！\n"));
                                            }
                                            else
                                            {
                                                printf("无法创建图片文件\n");
                                            }
                                        }
                                        else
                                        {
                                            write(connfd[i], "图片目录创建失败，请重试\n",
                                                  strlen("图片目录创建失败，请重试\n"));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (!flag)
                {
                    write(connfd[n], "未找到该用户，请重新键入命令\n",
                          strlen("未找到该用户，请重新键入命令\n"));
                }
            }
            /* 默认状态下在大厅消息将发送给所有在线用户 */
            else
            {
                ticks = time(NULL);
                strftime(mytime, sizeof(mytime), "[%H:%M:%S]", localtime(&ticks));
                strcpy(buf, mytime); // 显示时间
                strcat(buf, "\t");
                strcat(buf, info_class[n].name); // 显示用户名
                strcat(buf, ":[群聊消息]\t");
                strcat(buf, temp); // 客户端消息内容
                temp[strlen(temp) - 1] = '\0';
                strcat(buf, "\n");

                /* 发给在线所有用户 */
                for (i = 0; i < MAXMEM; i++)
                {
                    if (connfd[i] != -1)
                        write(connfd[i], buf, strlen(buf));
                }
            }
            memset(temp, 0, sizeof(temp));
        }
    }
}

/* 注册函数 */
void regist(int n)
{
    char buf[BUFFSIZE], temp[50], username[50], password[50];
    int len, i;

    strcpy(buf, "输入用户名:");
    write(connfd[n], buf, strlen(buf));
    if ((len = read(connfd[n], username, 20)) > 0) /* 读注册时从客户端传回来的用户名 */
    {
        username[len] = '\0';
        // TODO: 新增数据库存储用户信息方式
        char select_query[100];
        snprintf(select_query, sizeof(select_query), "SELECT * FROM users WHERE username = '%s';", username);
        sqlite3_stmt *stmt;
        int rc = sqlite3_prepare_v2(db, select_query, -1, &stmt, 0);
        if (rc != SQLITE_OK)
        {
            fprintf(stderr, "执行查询语句失败: %s\n", sqlite3_errmsg(db));
            return 1;
        }
        // 执行查询
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW)
        { // 存在行，即重名
            strcpy(buf, "用户已存在! 请换一个名字!\n");
            write(connfd[n], buf, strlen(buf));
            regist(n); /* 如果用户已存在就跳转到函数开始重新注册 */
        }
        else if (rc == SQLITE_DONE)
        { // 不存在行，即可以进行注册
            strcpy(buf, "输入用户的密码:");
            write(connfd[n], buf, strlen(buf));
            if ((len = read(connfd[n], password, 50)) > 0) /* 读注册时从客户端传回来的密码 */
            {
                password[len] = '\0';
                char insert_query[MAXCOUNT];
                snprintf(insert_query, sizeof(insert_query), "INSERT INTO users (username, password, isLogin) VALUES ('%s', '%s', 'false');", username, password);
                char *error_message = 0;
                int rc = sqlite3_exec(db, insert_query, 0, 0, &error_message);
                if (rc != SQLITE_OK)
                {
                    fprintf(stderr, "插入用户数据时出错: %s\n", error_message);
                    sqlite3_free(error_message);
                    return 1;
                }
                printf("成功插入用户数据，用户名: %s，密码：%s\n", username, password);
            }
        }
        else
        {
            fprintf(stderr, "执行查询时出错: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return -1; // 出错时返回-1
        }
    }
    strcpy(info_class[n].name, username);
    strcpy(buf, "注册并登录成功!\n");
    write(connfd[n], buf, strlen(buf));
}

/* 登录函数 */
void login(int n)
{
    char buf[BUFFSIZE], username[50], password[50];
    int len;

    // TODO: 使用数据库进行登录
    strcpy(buf, "请输入用户名:");
    write(connfd[n], buf, strlen(buf));
    if ((len = read(connfd[n], username, 50)) > 0)
    {
        strcpy(buf, "请输入密码:");
        write(connfd[n], buf, strlen(buf));
        if ((len = read(connfd[n], password, 50)) > 0) /* 读从客户端传回来的密码 */
        {
            printf("输入的姓名为：%s, 输入的密码为：%s\n", username, password);
            int result = CheckIsLoginUser(db, username, password);
            if (result == 1) // 表示查询成功
            {
                info_class[n].socket_name = connfd[n]; /* 登陆成功后将客户套接字赋给结构体的套接字成员变量 */
                strcpy(info_class[n].name, username);

                printf("当前登录用户的姓名为：%s\n", info_class[n].name);
                strcpy(buf, "登录成功.\n");
                write(connfd[n], buf, strlen(buf));
            }
            else
            {
                strcpy(buf, "用户不存在或密码错误! 请重新输入.\n");
                write(connfd[n], buf, strlen(buf));
                login(n);
            }
        }
    }
}

/* 检查当前用户是不是数据库中的用户 */
int CheckIsLoginUser(sqlite3 *db, const char *username, const char *password)
{
    char select_query[100];
    snprintf(select_query, sizeof(select_query), "SELECT * FROM users WHERE username = '%s' AND password = '%s';", username, password);
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, select_query, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "无法准备查询语句: %s\n", sqlite3_errmsg(db));
        return -1; // 出错时返回-1
    }
    // 执行查询
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        // 有结果行，表示登录成功
        sqlite3_finalize(stmt);
        return 1;
    }
    else if (rc == SQLITE_DONE)
    {
        // 没有结果行，表示登录失败
        printf("登录失败，用户名和密码不匹配。\n");
        sqlite3_finalize(stmt);
        return 0;
    }
    else
    {
        fprintf(stderr, "执行查询时出错: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1; // 出错时返回-1
    }
}

/* 服务端接收处理大厅相应客户端命令 */
void show(int n)
{
    char buf[BUFFSIZE], temp[5], info[50];
    int len;

    if ((len = read(connfd[n], temp, 1)) > 0)
    {
        temp[len] = '\0';
        if (strcmp(temp, "1") == 0)
        { /* 显示在线用户 */
            strcpy(info, "---当前在线的用户如下---\n");
            for (int i = 0; i < MAXMEM; i++)
            {
                if (connfd[i] != -1)
                {
                    strcat(info, info_class[i].name); /* 将在线用户名添加到info */
                    strcat(info, "\n");
                }
            }
            write(connfd[n], info, strlen(info));
            write(connfd[n], "不知道下一步操作了, 尝试使用'help'命令.\n", strlen("不知道下一步操作了, 尝试使用'help'命令.\n"));
        }
        else if (strcmp(temp, "2") == 0)
        { /* 显示所有聊天室 */
            strcpy(info, "---当前所有的聊天室如下---\n");
            FILE *file;
            char line[100]; // 假设每行最多包含100个字符

            // 打开文件
            file = fopen("ChatInfo.txt", "r");

            // 检查文件是否成功打开
            if (file == NULL)
            {
                perror("无法打开文件");
                return 1;
            }

            int lineNumber = 1;

            // 读取文件中的每一行
            while (fgets(line, sizeof(line), file) != NULL)
            {
                // 如果是奇数行，输出或处理该行数据
                if (lineNumber % 2 != 0)
                {
                    strcat(info, line);
                }

                lineNumber++;
            }

            // 关闭文件
            fclose(file);
            write(connfd[n], info, strlen(info));
            write(connfd[n], "不知道下一步操作了, 尝试使用'help'命令.\n", strlen("不知道下一步操作了, 尝试使用'help'命令.\n"));
        }
        else if (strcmp(temp, "3") == 0)
        { /* 创建聊天室 */
            create_chatroom(n);
        }
        else if (strcmp(temp, "4") == 0)
        { /* 加入聊天室 */
            join_chatroom(n);
        }
        else
        {
            strcpy(buf, "非法的命令, 请重新进行输入.\n");
            write(connfd[n], buf, strlen(buf));
            show(n); /* 无效命令码，重新输入 */
        }
    }
}

/* 创建聊天室函数 */
void create_chatroom(int n)
{
    char buf[BUFFSIZE], temp[50], info[20];
    int len;

    strcpy(buf, "请给聊天室进行命名:");
    write(connfd[n], buf, strlen(buf));
    if ((len = read(connfd[n], info, 20)) > 0)
    {
        info[len] = '\0';   /* 接收聊天室名字 */
        strcat(info, "\n"); // 从客户端中读取的信息是不带回车的，为了进行区分每一个输入，手动添加回车
        printf("聊天室的名称为：%s", info);
        fp = fopen("ChatInfo.txt", "a+");
        while (!feof(fp))
        {                        /* 一直读到文件结束 */
            fgets(temp, 50, fp); /* 读取一行 */
            if (strcmp(temp, info) == 0)
            { /* 如果存在某一行的值与输入用户名相同，则提示聊天室已被创建 */
                strcpy(buf, "已经存在相同名字的聊天室了! 请选择其他的名字!\n");
                write(connfd[n], buf, strlen(buf));
                create_chatroom(n); /* 如果聊天室已存在就跳转到函数开始重新创建 */
            }
        }
        strcpy(buf, "为聊天室设定一个密码:");
        write(connfd[n], buf, strlen(buf));
        if ((len = read(connfd[n], temp, 50)) > 0)
        { /* 接收聊天室密码 */
            temp[len] = '\0';
            strcat(temp, "\n");
            printf("聊天室的密码为：%s", temp);
            strcpy(info_class[n].chatroom, info); /* 创建完成后自动加入聊天室 */
            strcat(info, temp);
            if (fp = fopen("ChatInfo.txt", "a+")) /* 将聊天室信息添加到文件中保存 */
            {
                fputs(info, fp);
                fclose(fp); /* 写入后关闭文件内容才会最终保存 */
            }
        }
    }
    strcpy(buf, "创建成功, 现在你已经在聊天室中了.\n");
    write(connfd[n], buf, strlen(buf));
    write(connfd[n], "你可以在聊天室中发送消息或者是使用'back'命令返回大厅.\n", strlen("你可以在聊天室中发送消息或者是使用'back'命令返回大厅.\n"));
}

/* 加入聊天室函数 */
void join_chatroom(int n)
{
    char buf[BUFFSIZE], name[50], password[20], Cname[50], Cpassowrd[50];
    int len;

    strcpy(buf, "输入你想加入的聊天室的名字:");
    write(connfd[n], buf, strlen(buf));
    if ((len = read(connfd[n], name, 50)) > 0)
    {
        name[len] = '\n';
        name[len + 1] = '\0';
        fp = fopen("ChatInfo.txt", "r");
        while (!feof(fp))
        {
            fgets(Cname, 20, fp); /* 读取一行 */
            if (strcmp(name, Cname) == 0)
            {
                /* 如果聊天室存在，那么下一行就是本聊天室的密码 */
                fgets(Cpassowrd, 50, fp); /* 再读一行，将密码读取出来 */
                fclose(fp);
                break;
            }
        }
        strcpy(buf, "请输入聊天室的密码:");
        write(connfd[n], buf, strlen(buf));
        if ((len = read(connfd[n], password, 50)) > 0) /* 读从客户端传回来的聊天室密码 */
        {
            password[len] = '\n';
            password[len + 1] = '\0';
            strcpy(info_class[n].chatroom, Cname); /* 将聊天室名赋给结构体chatroom成员变量 */

            if (strcmp(password, Cpassowrd) == 0)
            {
                strcpy(buf, "成功加入, 现在你可以在聊天室中进行发言了.\n");
                write(connfd[n], buf, strlen(buf));
            }
            else
            {
                strcpy(buf, "聊天室不存在或者是密码错误! 请重新输入.\n");
                write(connfd[n], buf, strlen(buf));
                join_chatroom(n);
            }
        }
    }
}

void initDb()
{
    // 打开或创建数据库
    int rc = sqlite3_open("chatDb.db", &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "无法打开数据库：%s\n", sqlite3_errmsg(db));
        return 1;
    }

    // 创建信息表
    char *create_table_query = "CREATE TABLE IF NOT EXISTS messages ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                               "send_name TEXT,"
                               "receive_name TEXT,"
                               "message TEXT);";
    rc = sqlite3_exec(db, create_table_query, 0, 0, &error_message);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "无法创建表: %s\n", error_message);
        sqlite3_free(error_message);
        return 1;
    }
    // 创建用户表
    char *create_user_table_query = "CREATE TABLE IF NOT EXISTS users ("
                                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                    "username TEXT,"
                                    "password TEXT,"
                                    "isLogin TEXT DEFAULT 'false');";
    rc = sqlite3_exec(db, create_user_table_query, 0, 0, &error_message);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "无法创建用户信息表: %s\n", error_message);
        sqlite3_free(error_message);
        return 1;
    }
}

// 在数据库中插入语句
void addDataToDbMessage(sqlite3 *db, char *sender, char *receiver, char *message)
{
    char *error_message;
    char *sql = "INSERT INTO messages (send_name, receive_name, message) VALUES (?, ?, ?)";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        // 处理错误
        fprintf(stderr, "Failed to prepare statement\n");
        return -1;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, receiver, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, message, -1, SQLITE_STATIC);

    // 执行语句
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        // 处理错误
        fprintf(stderr, "Failed to execute statement\n");
        return -1;
    }

    // 释放语句对象
    sqlite3_finalize(stmt);
}

void selectDataFromDbMessage(sqlite3 *db, char *name, int n)
{
    // SQL 查询语句
    const char *select_query = "SELECT * FROM messages WHERE receive_name = ?";
    // 准备语句对象
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, select_query, -1, &stmt, NULL) != SQLITE_OK)
    {
        // 处理错误
        fprintf(stderr, "Failed to prepare statement\n");
        sqlite3_close(db);
        return;
    }
    // 绑定参数
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    // 执行查询
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        // 处理查询结果，例如获取列的值
        int messageId = sqlite3_column_int(stmt, 0);
        const char *sendName = (const char *)sqlite3_column_text(stmt, 1);
        const char *receiveName = (const char *)sqlite3_column_text(stmt, 2);
        const char *messageText = (const char *)sqlite3_column_text(stmt, 3);

        write(connfd[n], messageText, strlen(messageText));
    }
    // 释放语句对象
    sqlite3_finalize(stmt);
    deleteDataFromDbMessage(db, name);
}

void deleteDataFromDbMessage(sqlite3 *db, char *name)
{
    char *error_message;
    // 构建 DELETE 语句
    char delete_query[100];
    snprintf(delete_query, sizeof(delete_query), "DELETE FROM messages WHERE receive_name = '%s';", name);

    int rc = sqlite3_exec(db, delete_query, 0, 0, &error_message);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "删除数据时出错: %s\n", error_message);
        sqlite3_free(error_message);
        return 1;
    }
}

// 在用户表中查询是否存在用户
int selectUserFromDbUser(sqlite3 *db, char *name, struct user *us)
{
    char select_query[100];
    snprintf(select_query, sizeof(select_query), "SELECT * FROM users WHERE username = '%s';", name);
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, select_query, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "无法准备查询语句: %s\n", sqlite3_errmsg(db));
        return -1; // 出错时返回-1
    }
    // 执行查询
    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW)
    {
        // 有结果行，表示用户存在
        // 获取用户信息
        us->id = sqlite3_column_int(stmt, 0);
        us->username = strdup((const char *)sqlite3_column_text(stmt, 1));
        us->password = strdup((const char *)sqlite3_column_text(stmt, 2));
        us->isLogin = strdup((const char *)sqlite3_column_text(stmt, 3));

        printf("用户 %s 存在。\n", us->username);
        sqlite3_finalize(stmt);
        return 1;
    }
    else if (rc == SQLITE_DONE)
    {
        // 没有结果行，表示用户不存在
        printf("用户 %s 不存在。\n", name);
        sqlite3_finalize(stmt);
        return 0;
    }
    else
    {
        fprintf(stderr, "执行查询时出错: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1; // 出错时返回-1
    }
}