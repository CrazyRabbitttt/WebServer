#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include "../Locker/locker.h"
#include "../MYSQL/SqlConnectionPool.h"


class http_conn {
public:

    http_conn() {}
    ~http_conn() {}
    void init(int sockfd, const sockaddr_in &addr);     //init，传入connfd和客户端地址
    void process();                                     //任务类,处理客户端的请求，拼接成响应的信息传递回主线程中
    void close_conn(bool real_close = true);            //进行客户端的连接关闭 
    bool read_once();                                   //非阻塞的读取，读数据的主入口
    bool write();                                       //向连接中写数据



public:
    static int m_epollfd;           //所有socket事件(http)都注册到同一个epoll对象,用静态变量就好了
    static int m_user_count;        //用户数量也同样是共享的
    // MYSQL *mysql;                   //用于connectionRAII时候传入进行初始化

private:
    int m_sockfd;                   //http的socket
    sockaddr_in m_address;          //进行通信的socket地址


};


#endif