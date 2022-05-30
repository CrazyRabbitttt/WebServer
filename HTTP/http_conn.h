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
    static const int FILENAME_LEN = 200;               //文件名称的长度
    static const int READ_BUFFER_SIZE = 2048;       //读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;      //写缓冲区的大小

    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATEATE_HEADER, CHECK_STATE_CONTENT};
    //从状态机的状态
    enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};
    enum HTTP_CODE
    {
        NO_REQUEST,     //请求不完整
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    //HTTP请求方法
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT,PATH};


    http_conn() {}
    ~http_conn() {}
    void init(int sockfd, const sockaddr_in &addr);     //init，传入connfd和客户端地址
    void process();                                     //任务类,处理客户端的请求，拼接成响应的信息传递回主线程中
    void close_conn(bool real_close = true);            //进行客户端的连接关闭 
    bool read_once();                                   //非阻塞的读取，读数据的主入口
    bool write();                                       //向连接中写数据
    

public:
    static int m_epollfd;               //所有socket事件(http)都注册到同一个epoll对象,用静态变量就好了
    static int m_user_count;            //用户数量也同样是共享的
    // MYSQL *mysql;                   //用于connectionRAII时候传入进行初始化
private:
    void init();                                //进行类中字段的初始化
    void unmap();                                //文件映射到内存的解
    bool process_write(HTTP_CODE ret);          //进行响应，根据process_read解析的结果进行响应
    HTTP_CODE process_read();                   //进行HTTP请求数据的读取, 解析？
    HTTP_CODE parse_request_line(char *text);   //解析请求首行
    HTTP_CODE parse_headers(char *text);        //解析请求头部
    HTTP_CODE parse_content(char *text);        //解析请求实体
    HTTP_CODE do_request();                     //读取到完整的数据行之后进行解析
    LINE_STATUS parse_line();                   //解析行

    bool add_response(const char* format, ...);             //接收可变参数进行Response  shuxie 
    bool add_content(const char* content);                  //向响应报文中写content
    bool add_status_line(int status, const char * title);   //添加状态行到响应报文中
    bool add_headers(int content_length);                   //content长度
    bool add_content_length(int content_length);            //添加响应的长度到响应报文中
    bool add_linger();                                      //是否是长连接
    bool add_blank_line();                                  //添加空行
    bool add_content_type();                                //类型

private:
    int m_sockfd;                       //http的socket
    sockaddr_in m_address;              //进行通信的socket地址
    char m_write_buf[WRITE_BUFFER_SIZE];//写缓冲区         
    int m_write_idx;                    //写到缓冲区中的位置指针
    char m_read_buf[READ_BUFFER_SIZE];  //读缓冲区
    int m_read_idx;                     //当前已经读取的最后一个数据的下一个位置
    int m_checked_idx;                  //已经解析了的位置
    int m_start_line;                   //解析行的开始位置，用于将数据拿出来嘛

    char m_real_file[FILENAME_LEN];     //用于存储真正的文件路径

    METHOD m_method;                      //HTTP请求的method
    char *m_url;                          //HTTP请求的url
    char *m_version;                      //HTTP请求的版本，HTTP1.1
    char *m_host;                         //HTTP请求的host
    bool m_linger;                        //HTTP是否是keepalive

    char* m_string;                       //用于存储POST上传的用户名&密码
    char* get_line() {return m_read_buf + m_start_line; }      //获得解析好的语句的开始

    CHECK_STATE m_check_state;              //主状态机的状态
    int m_content_length;                  //content报文的长度
    
    struct stat m_file_stat;                //需要访问的文件的状态
    char *m_file_address;                   //mmap, 文件到内存之间的映射
    struct iovec m_iv[2];                   //,多元素数组：起始地址 + 数据的size
    int m_iv_count;                         //iovec的数目
    int bytes_to_send;                      //准备发送的字节数目
    int bytes_have_send;                    //已经发送了的字节的数目
    int m_ispost;                            //HTTP发送的是否是post请求

};


#endif