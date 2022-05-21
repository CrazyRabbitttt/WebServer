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


class connection_pool
{
public:
	MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取连接
	void DestroyPool();					 //销毁所有连接

	//单例模式通过静态变量获得单例的对象
	static connection_pool *GetInstance();

	void init(string url, string User, string PassWord, string DataBaseName, int Port, unsigned int MaxConn); 
	
	connection_pool();
	~connection_pool();

private:
	unsigned int MaxConn;  //最大连接数
	unsigned int CurConn;  //当前已使用的连接数
	unsigned int FreeConn; //当前空闲的连接数

private:
	locker lock;			//Lock锁
	list<MYSQL *> connList; //连接池
	sem reserve;			//信号量，用于设置可用的数量

private:
	string url;			 //主机地址
	string Port;		 //数据库端口号
	string User;		 //登陆数据库用户名
	string PassWord;	 //登陆数据库密码
	string DatabaseName; //使用数据库名
};


//	RAII,利用类进行数据的维护
class connectionRAII{

public:
	//调用池子中的GetConnection()获得一个可用的连接
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();
	
private:
	MYSQL *conRAII;					//从池子获得的MYSQL连接
	connection_pool *poolRAII;		//池子
};


#endif