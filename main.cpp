#include "Locker/locker.h"
#include "ThreadPool/threadpool.h"
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include "http_conn.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>


#define MAX_FD  65535                   //最大文件描述符




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

    //创建数据库连接池子
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

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);                         //listen

    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));  //端口复用

    struct sockaddr_in address;                 //通用地址？
    address.sin_family = AF_INET;               //Ipv4
    address.sin_addr.s_addr = INADDR_ALLHOSTS_GROUP;

    bind(listenfd, (struct sockaddr*)&address, sizeof(address));           //绑定

    





}
