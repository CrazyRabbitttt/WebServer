#include "Locker/locker.h"
#include "ThreadPool/threadpool.h"
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include "HTTP/http_conn.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "MYSQL/SqlConnectionPool.h"
#include "Timer/timer.h"


#define MAX_FD  65535                   //最大文件描述符
#define MAX_EVENT_NUMBER    10000       //最大事件数目
#define TIMESLOT    5                   //最小的超时单位

// #define listenfdET                        //边缘触发模式
#define listenfdLT                        //水平触发模式


//epoll事件添加、删除、oneshot,在http...cpp中声明

extern void addfd(int epollfd, int fd, bool oneshot);
extern void removefd(int epollfd, int fd);
extern int  setnoblocking(int fd);
extern void modfd(int epollfd, int fd, int ev);


//进行定时器的设置
static int pipefd[2];
static time_wheel m_time_wheel;
static int epollfd = 0;

void show_error(int connfd, const char * info) {
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}


//设置信号函数集
void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;                //一种更加健壮的信号处理的函数
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;            //处理函数
    if (restart) 
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);            //首先进行屏蔽其他的信号
    assert(sigaction(sig, &sa, NULL) != -1);
}


//信号的处理函数
void sig_handler(int sig) {
    //为了保证可重入性质，保留errno
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}


//定时处理任务，重新定义时候不断触发SIGALRM信号，进行定时
void timer_handler() {
    m_time_wheel.tick();
    alarm(TIMESLOT);
}


//定时器回调函数，删除非活动socket、注册的事件并且进行关闭
void cb_function(client_data* user_data) {
    /* 进行事件的删除 */
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    /* 关闭掉对应的socket了连接 */
    close(user_data->sockfd);
    http_conn::m_user_count --;
    printf("Close fd %d because of the timeout event or some thing\n", user_data->sockfd);
}


int main(int argc, char** argv)
{
    if (argc <= 1) {
       printf("Usage: %s port_number\n", basename(argv[0]));
       exit(-1);
    }
    
    int port = atoi(argv[1]);

    //对于SIGPIE信号进行处理

    addsig(SIGPIPE, SIG_IGN);           //忽略，默认就是终止进程

    //创建数据库连接池子,单例模式创建的对象
    //change :
    connection_pool *connPool = connection_pool::GetInstance();
    connPool->init("localhost", "root", "shaoguixin1+", "AA", 3306, 8);

    printf("Calling begin...\n");
    threadpool<http_conn> *pool = NULL;
    try {
        //尝试创建线程池,将http传给线程池进行处理。调用任务类的process()
        pool = new threadpool<http_conn>(connPool);       //change
        printf("Calling 线程池·...\n");
    } catch (...) {
        return 1;
    }
    printf("线程池创建完成...\n");
    //客户端连接的数组
    http_conn * users = new http_conn[MAX_FD];
    assert(users);
    printf("客户端数组完成...\n");

    //将用户名、密码读取到内存中
    users->initmysql_result(connPool);

    //创建socket
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);                         
    assert(listenfd >= 0);

    // 设置端口复用
    int reuse = 1, ret;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));  

    struct sockaddr_in address;                         //通用地址？
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;                       //Ipv4
    address.sin_addr.s_addr = htonl(INADDR_ANY);        //主机转网络
    address.sin_port = htons(port);

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

    bool timeout = false;
    bool stop_server = false;

    //创建管道进行定时器的任务
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnoblocking(pipefd[1]);           //写是非阻塞的,防止信号处理的时间过长
    addfd(epollfd, pipefd[0], false);   //epoll注册读事件，主线程处理读取管道，然后进行信号的处理

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);

    client_data* users_timer = new client_data[MAX_FD];     //客户端的定时器的数组

    alarm(TIMESLOT);                    //设定好alarm的时间,定时进行传送alarm信号，通过信号处理函数进行处理

    while (!stop_server) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            //todo : Log
            printf("epoll failure!\n");
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

                /* 进行定时器的设置,初始化客户端的数据
                    创建定时器，设置回调函数、超时时间，绑定用户数据，添加定时器到链表中
                */
               //进行客户端的地址等的设置
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd  = connfd;
                //客户端定时器的参数的设置
                //设定超时时间就已经创建好定时器然后插入到时间轮上面了，返回创建好的定时器
                tw_timer* timer = m_time_wheel.add_timer(3 * TIMESLOT);             //超时时间设置为3 * 5
                timer->user_data = &users_timer[connfd];        //将对应的客户端的数据进行配置好
                timer->cb_func = cb_function;                   //回调函数
                //将定时器插入到时间轮上面
                users_timer[connfd].timer = timer;

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
                /* 进行定时器的设置,初始化客户端的数据
                    创建定时器，设置回调函数、超时时间，绑定用户数据，添加定时器到链表中
                */
               //进行客户端的地址等的设置
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd  = connfd;
                //客户端定时器的参数的设置
                //设定超时时间就已经创建好定时器然后插入到时间轮上面了，返回创建好的定时器
                tw_timer* timer = m_time_wheel.add_timer(3 * TIMESLOT);             //超时时间设置为3 * 5
                timer->user_data = &users_timer[connfd];        //将对应的客户端的数据进行配置好
                timer->cb_func = cb_function;                   //回调函数
                //将定时器插入到时间轮上面
                users_timer[connfd].timer = timer;
            }

#endif
            } else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {        //处理异常断开连接的事件
                //服务器需要进行连接的关闭，移除对应的定时器
                tw_timer * timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);       //回调函数进行定时器的移除
                //call_function会将连接进行关闭
                
                if (timer) {
                    m_time_wheel.del_timer(timer);          //从时间轮上面删除掉定时器
                }
            } else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {         //传递信号的管道可进行读取
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1) continue;
                else if (ret == 0) continue;
                else {
                    for (int i = 0; i < ret; i++) {
                        switch (signals[i])
                        {
                        case SIGALRM:
                            timeout = true;             //进行传送alarm
                            break;
                        
                        default:
                            stop_server = true;
                            break;
                        }
                    }
                }
            }else if(events[i].events & EPOLLIN) {                                //数据可以进行读取的事件
                //更新定时器的超时时间
                tw_timer* timer = users_timer[sockfd].timer; 

                //进行连接的sockfd
                int connfd = sockfd;         
                if(users[sockfd].read_once()) {
                    //一次性将所有的数据进行读取完毕
                    //将http_conn传到线程池上，工作线程进行连接的处理
                    pool->append(users + sockfd);

                    //新的数据传输，将定时器的超时时间进行往后延迟3个超时单位
                    if (timer) {
                        //首先删除掉，然后插入进去，时间都是O(1)
                        m_time_wheel.del_timer(timer);
                        tw_timer* timer =  m_time_wheel.add_timer(3 * TIMESLOT);
                        
                        timer->user_data = &users_timer[connfd];        //将对应的客户端的数据进行配置好
                        timer->cb_func = cb_function;                   //回调函数

                        users_timer[connfd].timer = timer;
                    }


                }else {     //读取数据失败
                    timer->cb_func(&users_timer[sockfd]);
                    printf("主线程读取数据失败\n");
                    if (timer) {
                        m_time_wheel.del_timer(timer);
                    }
                }
            } else if (events[i].events & EPOLLOUT) {                               //数据可以进行写操作
                tw_timer* timer = users_timer[sockfd].timer;

                int connfd = sockfd;
                //如果写入成功了，将定时器的超时时间进行更新
                if (users[sockfd].write()) {
                    //首先删除掉，然后插入进去，时间都是O(1)
                    m_time_wheel.del_timer(timer);
                    tw_timer* timer =  m_time_wheel.add_timer(3 * TIMESLOT);
                    
                    timer->user_data = &users_timer[connfd];        //将对应的客户端的数据进行配置好
                    timer->cb_func = cb_function;                   //回调函数

                    users_timer[connfd].timer = timer;
                }else { //否则调用回调函数进行连接的移除
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer) {
                        m_time_wheel.del_timer(timer);
                    }
                }
            }
        }

        //进行定时器的滴答
        if (timeout) {
            timer_handler();
            timeout = false;
        }

    }

    //一般来说程序是不会跳出循环的，到这一步就是说明程序进行了断开
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);

    delete [] users_timer;  //消除掉保存的客户时间轮的信息
    delete [] users;
    delete pool;
    return 0;

}
