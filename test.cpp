#include "MYSQL/SqlConnectionPool.cpp"
using namespace std;
#include <map>
#include <cstring>
map<string, string> users;


int main(int argc, char ** argv)
{
    printf("进行数据库连接的测试！\n");
    
    connection_pool* connPool = connection_pool::GetInstance();        //获得单例模式的数据库连接池
    connPool->init("localhost", "root", "shaoguixin1+", "AA", 3306, 8);      //进行数据库连接池的初始化

    MYSQL *mysql = NULL;

    connectionRAII mysqlcon(&mysql, connPool);      //创建了数据库连接

    if (mysql_query(mysql, "SELECT name, passwd from users")) {
        printf("Select error: %s", mysql_error(mysql));
    } else {
        printf("Query success!\n");
    }

    //用于进行数据的存储
    MYSQL_RES *result = mysql_store_result(mysql);
    int number_fields = mysql_num_fields(result);

    //存储字段结构的数组
    MYSQL_FIELD * fields = mysql_fetch_fields(result);


    //从结果集中读取下一行，存储用户名、密码
    while(MYSQL_ROW row = mysql_fetch_row(result)) {
        string tmp1(row[0]);
        string tmp2(row[1]);
        users[tmp1] = tmp2;
    }
    string aa = "bing", bb = "xin";
    cout << users[aa] << endl;
    cout << users[bb] << endl;
    

    delete connPool;
    return 0;
    /*

    

    MYSQL *mysql = NULL;
    mysql = mysql_init(nullptr);
    
    mysql = mysql_real_connect(mysql, "localhost", "root", "shaoguixin1+", "AA", 3306, NULL, 0);

    if (mysql_query(mysql, "SELECT name,passwd FROM users"))
    {
        printf("Select error: %s", mysql_error(mysql));
    } else {
        printf("Query success!\n");
    }

    printf("running after query!\n");
    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
    cout << users["bing"] << endl;
    cout << users["xin"] << endl;
    */
}
