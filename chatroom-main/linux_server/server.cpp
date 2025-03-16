// 编译时要有 -lpthread
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <map>
#include <fstream>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <sstream>
#include <iomanip>
#include <thread>
#include <sys/stat.h>
#include <signal.h>

#define MAX_EVENTS 1000
#define BUF_SIZE 65536
#define PORT 9999
#define EPOLL_SIZE 50

pthread_mutex_t mutex;
// client socket fd, client user，name(ip)
std::map<int, std::string>* map_clients;

void error_handling(const char* msg)
{
    fputs(msg, stderr);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

// 设置为非阻塞态
void setnonblockingmode(int fd)
{
    int flag = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

bool is_fd_valid(int fd)
{
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

void* send_msg_all(void* msg, int len)
{
    pthread_mutex_lock(&mutex);
    for (auto it = map_clients->begin(); it != map_clients->end(); it++)
    {
        if (is_fd_valid(it->first))
            write(it->first, msg, len);
    }
    pthread_mutex_unlock(&mutex);
    return NULL;
}


void handle_file_upload(int client_sock, std::string filename, size_t file_size, const std::string& initial_data, size_t initial_size)
{
    char buf[65536];
    std::ofstream file(filename, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        perror("fopen() error");
        return;
    }

    size_t bytes_received = 0;

    // **1. 先写入已经解析出来的数据**
    if (initial_size > 0)
    {
        file.write(initial_data.c_str(), initial_size);
        bytes_received += initial_size;
        std::cout << "Initial data written: " << initial_size << " bytes\n";
    }

    // **2. 继续 `recv()` 剩余数据**
    while (bytes_received < file_size)
    {
        ssize_t bytes = recv(client_sock, buf, sizeof(buf), 0);
        if (bytes > 0)
        {
            file.write(buf, bytes);
            bytes_received += bytes;
        }
        else if (bytes < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            std::cerr << "Recv failed: " << strerror(errno) << std::endl;
            break;
        }
        else
        {
            std::cout << "Disconnected Connection closed by the peer" << std::endl;
            break;
        }
    }

    file.close();
    std::cout << "File upload complete: " << filename << " Received bytes: " << bytes_received << std::endl;
	std::string msg = "FILE " + filename + " " + std::to_string(file_size);
    send_msg_all((void *)msg.c_str(), msg.size() + 1);
    std::string notification = u8"上传了文件: " + filename;
    send_msg_all((void *)notification.c_str(), notification.size() + 1);
}

void handle_file_download(int client_sock, const std::string &filename)
{
    char buf[BUF_SIZE];
    size_t bytes_sent = 0;
    FILE* fp = fopen(filename.c_str(), "rb");
    
    if (!fp)
    {
        perror("fopen() 错误");
        send(client_sock, "ERROR", 5, 0);  // 发送错误信息
        close(client_sock);
        return;
    }

    int fd = fileno(fp);
    if (fd == -1)
    {
        perror("fileno() 错误");
        fclose(fp);
        close(client_sock);
        return;
    }

    struct stat file_stat;
    if (fstat(fd, &file_stat) != 0)
    {
        perror("fstat() 错误");
        fclose(fp);
        close(client_sock);
        return;
    }

    size_t file_size = file_stat.st_size;
    printf("开始发送文件: %s, 大小: %zu 字节\n", filename.c_str(), file_size);

    // 发送文件大小
    std::string file_size_str = std::to_string(file_size) + "\n";
    if (send(client_sock, file_size_str.c_str(), file_size_str.size(), 0) < 0)
    {
        perror("发送文件大小失败");
        fclose(fp);
        close(client_sock);
        return;
    }

    while (bytes_sent < file_size)
    {
        size_t bytes_read = fread(buf, 1, BUF_SIZE, fp);
        if (bytes_read <= 0)
        {
            break;
        }

        size_t bytes_to_send = bytes_read;
        char *send_ptr = buf;
        int retry_count = 0;  // 添加重试机制

        while (bytes_to_send > 0)
        {
            ssize_t sent = send(client_sock, send_ptr, bytes_to_send, 0);
            if (sent < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    if (retry_count < 10)  // **最多重试 10 次**
                    {
                        std::cerr << "TCP缓冲区满，等待50ms..." << std::endl;
                        usleep(50000);  // **等待 50ms
                        retry_count++;
                        continue;
                    }
                    else
                    {
                        std::cerr << "重试失败，断开连接。" << std::endl;
                        fclose(fp);
                        close(client_sock);
                        return;
                    }
                }
                std::cerr << "send() 错误: " << strerror(errno) << "，断开连接。" << std::endl;
                fclose(fp);
                close(client_sock);
                return;
            }
            send_ptr += sent;
            bytes_to_send -= sent;
            retry_count = 0;  // 发送成功后重置重试次数
        }

        bytes_sent += bytes_read;
        usleep(500);  // **延迟，避免过快发送导致 TCP 队列积压**
    }

    printf("文件发送完成: %s, 总共发送 %zu 字节\n", filename.c_str(), bytes_sent);
    
    fclose(fp);
    close(client_sock);
}





void broadcast_userlist()
{
    std::string userlist = "USERLIST ";
    for (auto it = map_clients->begin(); it != map_clients->end(); it++)
    {
        userlist += it->second + "\n";
    }
    printf("Broadcasting user list: \n%s\n", userlist.c_str());
    send_msg_all((void*)userlist.c_str(), userlist.size() + 1);
}

void* handle_client(void* arg)
{
    int client_sock = *((int*)arg);
    setnonblockingmode(client_sock);

    int epfd = epoll_create(1);
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
    event.data.fd = client_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &event);

    char msg[BUF_SIZE];
    bool name_saved = false;

    while (true)
    {
        int n = epoll_wait(epfd, &event, 1, -1);
        if (n == 1 && event.events & EPOLLIN)
        {
            // **1. 读取客户端数据**
            ssize_t bytes_read = recv(client_sock, msg, BUF_SIZE - 1, 0);
            if (bytes_read == 0)
            {
                // 断开连接
                break;
            }
            else if (bytes_read < 0)
            {
                if (errno == EAGAIN)
                    continue;
                break;
            }

            msg[bytes_read] = '\0';
            std::string message(msg);

            // **2. 解析上传命令**
            if (message.substr(0, 6) == "UPLOAD")
            {
                puts("Upload file message");
                
                std::istringstream iss(msg);
                std::string cmd, filename;
                size_t filesize;
                iss >> cmd >> filename >> filesize;
                
                std::cout << "Filename: " << filename << " Filesize: " << filesize << std::endl;

                // **3. 获取 UPLOAD 后的剩余数据（可能部分文件数据已被 `recv()` 读取）**
                size_t header_length = message.find("\n");  // 计算 `UPLOAD <filename> <filesize>` 头部长度
                if (header_length == std::string::npos)
                    header_length = message.length();
                else
                    header_length += 1;  // 可能有换行符

                std::string file_data = message.substr(header_length);  // 提取已读取的部分文件数据
                size_t initial_data_size = file_data.size();

                // **4. 传递文件数据**
                handle_file_upload(client_sock, filename, filesize, file_data, initial_data_size);
                break;
            }
            else if (message.substr(0, 8) == "DOWNLOAD")
            {
                puts("Download file message");
                std::string filename = message.substr(strlen("DOWNLOAD") + 1);
                printf("Download filename: [%s]\n", filename.c_str());
                handle_file_download(client_sock, filename);
                puts("Download file Finished");
                break;
            }
            else if (message.compare("USERLIST") == 0)
            {
                puts("User list request");
                broadcast_userlist();
            }
            else
            {
                printf("Message: %s\n", msg);
                if (!name_saved)
                {
                    name_saved = true;
                    std::string str(msg);
                    std::string name = str.substr(0, str.find(' '));
                    std::cout << "Client Username: " << name << std::endl;
                    map_clients->at(client_sock) += ":" + name;
                    printf("Client Username Saved: %d %s\n", client_sock, map_clients->at(client_sock).c_str());
                }
                send_msg_all(msg, bytes_read);
            }
        }

        if (n == -1 || event.events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
        {
            printf("client: %d %s epoll_wait() error\n", client_sock, map_clients->at(client_sock).c_str());
            break;
        }
    }

    // 清理资源
    pthread_mutex_lock(&mutex);
    map_clients->erase(client_sock);
    epoll_ctl(epfd, EPOLL_CTL_DEL, client_sock, NULL);
    close(client_sock);
    printf("Closed client: %d\n", client_sock);
    pthread_mutex_unlock(&mutex);

    return NULL;
}


int run_server(int port)
{
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_size = sizeof(client_addr);

    // init mutex
    pthread_mutex_init(&mutex, NULL);

    // prepare server socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
        error_handling("bind() error");
    if (listen(server_sock, 5) == -1)
        error_handling("listen() error");

   
    int epfd = epoll_create(1);
    
    struct epoll_event event;
    
    event.events = EPOLLIN | EPOLLET,
        event.data.fd = server_sock,
        epoll_ctl(epfd, EPOLL_CTL_ADD, server_sock, &event);

    int str_len;
    char buf[BUF_SIZE];
    map_clients = new std::map<int, std::string>();
    while (true)
    {
        int ret = epoll_wait(epfd, &event, EPOLL_SIZE, -1);
        if (ret == -1)
        {
            puts("epoll_wait() error");
            break;
        }
        // server sock IO ready
        if (event.data.fd == server_sock && (event.events & EPOLLIN))
        {
            // new connection, wait/block until accept
            client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_size);
            map_clients->insert(std::pair<int, std::string>(client_sock, inet_ntoa(client_addr.sin_addr)));
            pthread_mutex_lock(&mutex);
            printf("New Connected client: %d\n", client_sock);
            pthread_t tid;
            pthread_create(&tid, NULL, handle_client, (void*)&client_sock);
            pthread_detach(tid);
            pthread_mutex_unlock(&mutex);
        }
    }
    close(server_sock);
    close(epfd);
    delete map_clients;
    return 0;
}

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    run_server(atoi(argv[1]));
    return 0;
}
