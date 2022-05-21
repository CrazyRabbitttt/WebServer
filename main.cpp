#include "Locker/locker.h"
#include "ThreadPool/threadpool.h"
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include "http_conn.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>


#define MAX_FD  65535                   //最大文件描述符
#define MAX_EVENT_NUMBER    10000       //最大事件数目


// #define listenfdET                        //边缘触发模式
#define listenfdLT                        //水平触发模式


//epoll事件添加、删除、oneshot,在http...cpp中声明

extern void addfd(int epollfd, int fd, bool oneshot);
extern void removefd(int epollfd, int fd);
extern int  setnoblocking(int fd);
extern void modfd(int epollfd, int fd, int ev);


//信号的处理函数
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;                //一种更加健壮的信号处理的函数
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;            //处理函数
    sigfillset(&sa.sa_mask);            //首先进行屏蔽其他的信号
    sigaction(sig, &sa, NULL);          //Null就是先前的处理方式
}


int main(int argc, char** argv)
{
    if (argc <= 1) {
        printf("Usage: %s port_number\n", basename(argv[0]));
        exit(-1);
    }
    
    //对于SIGPIE信号进行处理

    addsig(SIGPIPE, SIG_IGN);           //忽略，默认就是终止进程

    //创建数据库连接池子,单例模式创建的对象
    connection_pool *connPool = connection_pool::GetInstance();
    connPool->init("localhost", "root", "shaoguixin1+", "AA", 3306, 8);

    threadpool<http_conn> *pool = NULL;
    try {
        //尝试创建线程池,将http传给线程池进行处理。调用任务类的process()
        pool = new threadpool<http_conn>(connPool);
    } catch (...) {
        return 1;
    }
    
    //客户端连接的数组
    http_conn * users = new http_conn[MAX_FD];
    assert(users);

    //创建socket
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);                         
    assert(listenfd >= 0);

    // 设置端口复用
    int reuse = 1, ret, epollfd;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));  

    struct sockaddr_in address;                         //通用地址？
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;                       //Ipv4
    address.sin_addr.s_addr = htonl(INADDR_ANY);        //主机转网络


    //bind
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));           //绑定
    assert(ret >= 0);

    //listen
    ret = listen(listenfd, 5);
    assert(ret >= 0);  

    //创建epoll对象事件表,epoll_wait中完成的事件都会写到这个数组中的
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);

    //Listen不能是one_shot,否则只能监听一个客户端连接
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;                    //将epollfd传进静态变量，供所有的http共享使用

    bool stop_server = false;
    while (!stop_server) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            //todo : Log
            break;
        }

        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            //处理新到的客户端的连接，listen
            if(sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
#ifdef listenfdLT
                //accept客户端连接请求
                int connfd = accept(listenfd, (struct sockaddr*) &client_address, &client_addrlength);
                if (connfd < 0) {
                    //todo : log
                    continue;       //失败，那就继续
                }
                if (http_conn::m_user_count >= MAX_FD) {        //目前的连接数目太多了，要回送给客户端信息
                    //todo : log
                    continue;
                }
                users[connfd].init(connfd, client_address);     //init HTTP请求连接
#endif

#ifdef listenfdET
            //因为ET模式仅仅提示一次，所以要while循环进行读取
            while (1) {
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                if (connfd < 0) {
                    //todo : log
                    break;
                }
                if (http_conn::m_user_count >= MAX_FD) {
                    //todo : log
                    break;
                }
                users[connfd].init(connfd, client_address);
            }

#endif
            }
        }

    }



}
