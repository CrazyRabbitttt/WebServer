#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

class http_conn {
public:
    http_conn() {}

    ~http_conn() {}

    void process();         //任务类,处理客户端的请求，拼接成响应的信息传递回主线程中

private:


};


#endif