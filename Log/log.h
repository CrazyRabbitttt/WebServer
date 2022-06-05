#ifndef _LOG_H_
#define _LOG_H_

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;
const int N = 128;

class Log {
public:
    //通过局部静态变量返回单例模式的对象
    static Log* get_instance() {
        static Log instance;
        return &instance;
    }

    //公有的方法调用单例指针调用私有方法
    static void* flush_log_thread(void* args) {
        Log::get_instance()->async_write_log();
    }

    //                  日志文件名          缓冲区大小                  行数        最长日志条列
    bool init(const char* file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    //将日志按照标准格式进行整理
    void write_log(int level, const char* format, ...);

    //强制刷新缓冲区
    void flush(void);

private:
    //只能是私有的调用
    Log();
    virtual ~Log();

    //异步写日志方式
    void* async_write_log() {
        string single_log;

        //循环从blockqueueu中取出日志来写入到文件中
        while(m_log_queue->pop(single_log)) {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fptr);      //将内容写到file中
            m_mutex.unlock();
        }
    }


private:
    char dir_name[N];
    char log_name[N];
    int m_split_lines;          //最大的行数
    int m_log_buf_size;         //缓冲区大小    
    long long m_count;          //日志行数的记录
    int m_today;                //按照日期进行设置，今天是那一天
    FILE* m_fptr;               //log文件指针
    char* m_buf;                //需要输出的内容
    block_queue<string> *m_log_queue;       //阻塞对垒
    bool m_is_async;            //是否同步
    locker m_mutex;             //维护互斥的访问
};



//用于不同类型的日志输出
#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif