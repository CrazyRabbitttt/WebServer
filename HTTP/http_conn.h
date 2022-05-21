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

    static int m_epollfd;           //所有socket事件(http)都注册到同一个epoll对象,用静态变量就好了
    static int m_user_count;        //用户数量 

    http_conn() {}

    ~http_conn() {}

    void process();         //任务类,处理客户端的请求，拼接成响应的信息传递回主线程中


public:
    MYSQL *mysql;           //用于connectionRAII时候传入进行初始化


private:


};


#endif