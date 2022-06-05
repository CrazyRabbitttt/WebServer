#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log() {
    m_count = 0;
    m_is_async = false;
}

Log::~Log() {
    //关闭文件
    if (m_fptr != NULL) {
        fclose(m_fptr);
    }
}

//进行日志文件的初始化设置
bool Log::init(const char* file_name, int log_buf_size, int split_lines, int max_queue_size) {
    //如果说设置了max_queue_size, 就是异步的
    if (max_queue_size >= 1) {
        m_is_async = true;

        //create the block queue
        m_log_queue = new block_queue<string>(max_queue_size);
        
        pthread_t tid;

        //调用最终的循环从blockqueue中取出日志内容写到文件中
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', sizeof(m_buf));

    //日志的最大行数？
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm *sys_time = localtime(&t);
    struct tm my_tm = *sys_time;

    //进行log文件名称的更改
    //1.lalala -> 2022_09_20_lalala
    //2.Home/WA/lala -> Home/WA/2022_09_20_lala

    //找到末尾的'/'的位置
    const char* ptr = strrchr(file_name, '/');      
    char full_log_name[256] = {0};
    if (ptr == NULL) {          //
        snprintf(full_log_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    } else {
        //首先拆分开目录和最终的文件，拼接最终的文件
        strcpy(log_name, ptr + 1);
        strncpy(dir_name, file_name, ptr - file_name + 1);
        snprintf(full_log_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    //打开文件
    //open for appending 
    m_fptr = fopen(full_log_name, "a");

    if (m_fptr == NULL) {
        return false;
    }

    return true;
}


void Log::write_log(int level, const char* format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    char s[16] = {0};

    //日志等级之间的划分
    switch (level) {
        case 0: 
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[error]:");
            break;
        default:
            break;    
    }


    m_mutex.lock();

    //更新现在有的行数
    m_count++;

    //日志不是今天写入的或者行数是最大行(m_split_lines)的倍数
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        char new_log[256] = {0};
        fflush(m_fptr);         //将内容刷新到文件
        fclose(m_fptr);         //关掉旧的文件，创建新的文件了
        char tail[16] = {0};

        //格式化日志中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        //如果时间不是今天的话，创建今天的日志，更新m_today 和 m_count : (0)
        if (m_today != my_tm.tm_mday) {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        } else {
            //超过了最大行，在之前的日志名的基础上面加上后缀，
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        //打开新的文件
        m_fptr = fopen(new_log, "a");
    }
    
    m_mutex.unlock();


    va_list valist;
    //将传入的format参数赋值给valist,用于格式化的输出
    va_start(valist, format);


    string log_str;

    m_mutex.lock();
    //写入log文件的内容：时间 + 内容
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                      my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                      my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);

    //vsnprintf:格式化带size的向字符串写入数据
    //内容的格式化,
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valist);
    m_buf[n + m] = '\n';
    m_buf[m + m + 1] = '\0';

    log_str = m_buf;
    m_mutex.unlock();

    //默认是同步
    //如果是异步，将日志信息加入到阻塞队列中，同步则枷锁向文件中写
    if (m_is_async && !m_log_queue->full()) {
        m_log_queue->push(log_str);
    } else {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fptr);
        m_mutex.unlock();
    }
    va_end(valist);
    

}





void Log::flush() {
    //强制进行刷新到文件
    m_mutex.lock();
    fflush(m_fptr);
    m_mutex.unlock();

}











