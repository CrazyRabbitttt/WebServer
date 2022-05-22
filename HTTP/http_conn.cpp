#include "http_conn.h"

// #define connfdET         //边缘触发模式
#define connfdLT            //水平触发模式

// #define listenfdET
#define listenfdLT          

//设置fd为非阻塞，ET模式下必备
int setnoblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);    
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将事件加入到epoll事件表中
//Listen不能设置one_shot,否则只能接受一个客户端
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;                 //epoll数据配置

//配置epoll事件，RDHUP就是异常断开可处理
#ifdef connfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;          
#endif

#ifdef connfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

#ifndef listenfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifndef listenfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

    if(one_shot) 
        event.events |= EPOLLONESHOT;               //oneshot:保证一个文件描述符被一个线程处理
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnoblocking(fd);
}

//从内核事件表中删除掉文件描述符号
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}


//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;      
    event.data.fd = fd;     //用户数据
#ifdef connfdET
    event.events = ev | EPOLLIN | EPOLLONESHOT | EPOLLRDHUP;
#endif

#ifdef connfdLT
    event.events = ev | EPOLLIN | EPOLLONESHOT | EPOLLRDHUP;
#endif

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//初始化http连接，外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &client_addr) {
    m_sockfd  = sockfd;
    m_address = client_addr;

    int reuse = 1;
    //设置端口复用
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true);         //添加事件，将这个conn连接添加到epoll
    m_user_count++;                         //用户数量自➕
    
    //todo : 私有的init();
}


//进行http客户端连接的关闭
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {       //判断不能进行关闭的条件
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}



