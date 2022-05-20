#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <errno.h>
#include <list>
#include <string.h>
#include <mysql/mysql.h>
#include <iostream>
#include "../Locker/locker.h"
using namespace std;

//创建数据库连接池
class connection_pool {
public:
    MYSQL *GetConnection();                 //获得数据库的连接，单例模式下的连接，创建线程池
    bool ReleaseConnection(MYSQL *conn);    //断开连接
    int  GetFreeConn;                       //获得可用的连接
    void DestoryPool();                     //断开所有的连接

    //通过静态局部对象获得单例模式的对象
    static connection_pool * Getinstance();     
    void init(string url, string User, string PassWord, string DataBaseName, int Port, unsigned int MaxConn);


private: 
    unsigned int MaxConn;               //最大连接数目
    unsigned int CurConn;               //当前已经连接的数目
    unsigned int FreeConn;              //当前的空闲连接

private:
    string url;                         //主机地址
    string Port;
    string User;
    string Password;
    string DataBaseName;
};

class connectionRAII {
public:
                //需要修改数据库的连接，传递双指针
    connectionRAII(MYSQL **conn, connection_pool *connPool);    
    ~connectionRAII();

private:
    MYSQL *connRAII;                    //数据库连接的指针
    connection_pool *poolRAII;          //连接池对象
};



#endif