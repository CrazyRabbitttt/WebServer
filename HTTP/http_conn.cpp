#include "http_conn.h"


//进行静态变量的初始化操作
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;


#define connfdET         //边缘触发模式
// #define connfdLT            //水平触发模式

// #define listenfdET
#define listenfdLT          

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";


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
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif

#ifdef connfdLT
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
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
    init();
}

//初始化类内部的字段，idx字段啥的
void http_conn::init() {

    m_check_state = CHECK_STATE_REQUESTLINE;        //初始化主状态机为解析首部

    m_checked_idx = 0;
    m_read_idx = 0;
    m_start_line = 0;

    m_method = GET;
    m_url = 0;
    m_version = 0;

    bzero(m_read_buf, sizeof(m_read_buf));
}

//进行http客户端连接的关闭
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {       //判断不能进行关闭的条件
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}


//非阻塞ET模式下需要一次性将数据进行读取
//循环读取客户的数据，直到没有数据可读或者是对方关闭了连接
bool http_conn::read_once() { 
    printf("一次性读取完所有的数据！\n");
    if (m_read_idx > READ_BUFFER_SIZE) return false;            //缓冲区已经是满了
    int bytes_read = 0;

#ifdef connfdLT
    //进行数据的读取，读到read_buf中
    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
    m_read_idx += bytes_read;

    if (bytes_read <= 0) {
        return false;
    } 
    printf("读取到了数据:%s\n", m_read_buf);
    return true;
#endif

#ifdef connfdET
    while(true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)    //没有数据可以进行读取了
                break;
            return false;
        }else if(bytes_read == 0) {
            return false;
        }
        //下面是正确的读取完数据
        m_read_idx += bytes_read;
    }
    printf("读取到了数据:%s\n", m_read_buf);
    return true;
#endif
}

//一次性写数据
bool http_conn::write() {
    printf("一次性写完所有的数据\n");
    return true;
}


//从状态机：用于分析出一行内容
//返回值为Line的读取状态
http_conn::LINE_STATUS http_conn::parse_line() {
    char tmp;
    //检查的不能追上读取了的
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        tmp = m_read_buf[m_checked_idx];
        //下面就是进行判断是否是\r\n
        if (tmp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx) {            //刚好就是最后读取到的就是\r,还没有读取到\n,那么就继续
                return LINE_OPEN;                               //继续读
            }else if (m_read_buf[m_checked_idx + 1] == '\n') {
                //可以的，是\r\n
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(tmp == '\n') {                       //就是上面讨论的特殊情况了
            if(m_checked_idx > 0 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;                          //除了上面的情况，都是出错了
        }       
    }
    return LINE_OPEN;                                 //需要继续进行数据行的解析
}


//主状态机：解析请求
http_conn::HTTP_CODE http_conn::process_read() {
    //初始化从状态机的状态
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    
    //进入解析循环的条件一定是读取到了一个完整的整行，然后进行解析
    //           开始进行解析主体，也就是不用一行行的进行解读了                             解析前面的头部等、成功的话
    while (((m_check_state == CHECK_STATE_CONTENT)&&(line_status = LINE_OK))|| (line_status = parse_line()) == LINE_OK) {
        //进入就是解析到了完整的数据或者是
        text = get_line();          //将解析到的一行数据赋值给text

        //因为现在已经是读取了一行数据了\r\n, 那么下一句的开始m_start_line就是m_checked_idx
        m_start_line = m_checked_idx;   

        printf("got one http line: %s\n", text);

        switch (m_check_state) 
        {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);         
                if (ret = BAD_REQUEST) return BAD_REQUEST;          //客户端的数据有问题
                break;
            }
            case CHECK_STATEATE_HEADER: {                           //解析头部字段
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) return BAD_REQUEST;         
                else if (ret == GET_REQUEST) {                      //获得了完整的客户请求
                    return do_request();                            //进行处理
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if (ret == GET_REQUEST) {                           //获得了完整的请求
                    return do_request();                            //进行处理
                }
                line_status = LINE_OPEN;                            //读取完content就是读取完毕了，更新line_status避免再次进入循环
                break;
            }
            default: 
                return INTERNAL_ERROR;
        }

    }
    return NO_REQUEST;      //请求不完整，继续进行读取

}

//解析请求的首行
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {

    //GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");       //获得到GET后面的那个位置
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';                    //GET\0/index.html HTTP/1.1
    char *method = text;              //进行method的赋值

    if (strcasecmp(method, "GET") == 0) 
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0) 
        m_method = POST;
    else 
        return BAD_REQUEST;


    //目前是已经判断了第一个空格或者是\t,但是可能后面还是有空格，需要跳过空格
    m_url += strspn(m_url, " \t");

    //目前的url...
    //是:/index.html HTTP1.1
    m_version = strpbrk(m_url, " \t");
    if(!m_version) 
        return BAD_REQUEST;
    *m_version++ = '\0';
    if(strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    //判断前缀是否是http://
    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');             //索引到‘/’
    }
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATEATE_HEADER;

    return NO_REQUEST;

}

//解析请求首部
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {

}

//解析请求实体
http_conn::HTTP_CODE http_conn::parse_content(char *text) {

}

//具体的进行解析一行数据
http_conn::LINE_STATUS http_conn::parse_line() {

}


//线程池中的工作线程进行调用，处理http请求的函数的入口
void http_conn::process() {
    //解析请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {           //重置为oneshot事件，继续用同一个线程监听可读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return ;
    }

    printf("Parse request & create response\n");
    //生成响应
}





