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

const char *doc_root = "/root/TinyWeb/root";

locker m_lock;

map<string, string> users;

void http_conn::initmysql_result(connection_pool *connPool) {

    //从数据库连接池中取得连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);      //从数据库连接池子中获取一个连接

    //将所有的用户名密码提取到内存中

    if(mysql_query(mysql, "SELECT name, passwd from users")) {
        printf("select from table error\n");
    }

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
}



//设置fd为非阻塞，ET模式下必备
int setnoblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);    
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将读事件加入到epoll事件表中
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

#ifdef listenfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef listenfdLT
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

    // int reuse = 1;
    //设置端口复用
    // setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true);         //添加事件，将这个conn连接添加到epoll
    m_user_count++;                         //用户数量自➕
    
    //todo : 私有的init();
    init();
}

//初始化类内部的字段，idx字段啥的
void http_conn::init() {

    mysql = NULL;
    m_check_state = CHECK_STATE_REQUESTLINE;        //初始化主状态机为解析首部

    bytes_have_send = 0;
    bytes_to_send = 0;

    m_linger = false;


    m_checked_idx = 0;
    m_read_idx = 0;
    m_start_line = 0;

    m_write_idx = 0;
    
    m_method = GET;
    m_url = 0;
    m_version = 0;

    m_content_length = 0;


    m_host = 0;

    m_ispost = 0;

    memset(m_read_buf, '\0', sizeof(READ_BUFFER_SIZE));
    memset(m_write_buf, '\0', sizeof(WRITE_BUFFER_SIZE));
    memset(m_real_file, '\0', FILENAME_LEN);
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
    // printf("一次性读取完所有的数据！\n");
    if (m_read_idx >= READ_BUFFER_SIZE) return false;            //缓冲区已经是满了
    int bytes_read = 0;

#ifdef connfdLT
    //进行数据的读取，读到read_buf中
    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
    m_read_idx += bytes_read;

    if (bytes_read <= 0) {
        return false;
    } 
    // printf("LT:读取到了数据:%s\n", m_read_buf);
    return true;
#endif

#ifdef connfdET
    while(true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)    //没有数据可以进行读取了
                break;
            return false;//如果不是上述的话就return false
        }else if(bytes_read == 0) {
            return false;
        }
        //下面是正确的读取完数据
        m_read_idx += bytes_read;
    }
    return true;
#endif
}

bool http_conn::write()
{
    int tmp = 0;
    int newadd = 0;

    //如果发送的数据长度是0，响应为空
    if (bytes_to_send == 0){        //重新注册读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    //如果是大文件，需要进行循环写，每次修改起始地址和len
    while(1) {
        //将响应报文，文件发送给浏览器
        tmp = writev(m_sockfd, m_iv, m_iv_count);

        //正常
        if (tmp > 0) {
            bytes_have_send += tmp;
            //修改发送文件的偏移指针
            newadd = bytes_have_send - m_write_idx;
        }

        if (tmp <= -1) {
            //缓冲区满了：wouldblock or eagain,因为是非阻塞的
            if (errno == EAGAIN) {
                //如果说响应的报文写完了，将len置为0
                if (bytes_have_send >= m_iv[0].iov_len) {
                    //将len置为0，不再发送头部信息
                    m_iv[0].iov_len = 0;
                    m_iv[1].iov_base = m_file_address + newadd;
                    m_iv[1].iov_len = bytes_to_send;        //还需要发送的数据大小
                }else { //响应头部都还没写完
                    m_iv[0].iov_base = m_write_buf + bytes_to_send;
                    m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
                }

                //重新注册写的事件
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            //发送失败，并且不是因为缓冲区满了，unmap
            unmap();
            return false;
        }
        
        //更行发送字节数目
        bytes_to_send -= tmp;

        //判断数据是否全部发送完毕
        if (bytes_to_send <= 0) {
            unmap();

            //重置epolloneshot事件
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            //是否是长连接
            if (m_linger) {
                init();
                return true;
            }else return false;
        }

    }

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
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
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
    
    //进入解析循环的条件一定是读取到了一个完整的整行，然后进行解析，解析完成content之后改变line_status状态，退出循环
    //           开始进行解析主体，也就是不用一行行的进行解读了                             解析前面的头部等、成功的话
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)|| ((line_status = parse_line()) == LINE_OK)) {
        //进入就是解析到了完整的数据或者是
        text = get_line();          //将解析到的一行数据赋值给text

        //因为现在已经是读取了一行数据了\r\n, 那么下一句的开始m_start_line就是m_checked_idx
        m_start_line = m_checked_idx;   

        switch (m_check_state) 
        {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);         
                if (ret == BAD_REQUEST) return BAD_REQUEST;          //客户端的数据有问题
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


//主状态机：响应请求,根据解析到的读取报文进行逻辑判断
bool http_conn::process_write(HTTP_CODE ret) {

    //首先是status_line
    //之后是headers,传入的是form的len，
    //其中headers:  1.len  2. linger 3.blankline
    switch(ret) {
        case INTERNAL_ERROR: {      //内部错误，500
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)) 
                return false;
            break;
        }
        case BAD_REQUEST: {         //报文语法有错误，404
            // printf("报文语法有错误\n");
            // printf("开始执行加入状态行\n");
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) 
                return false;
            break;
        }
        case FORBIDDEN_REQUEST: { //资源访问没有权限，403
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) 
                return false;   
            break;
        }
        case FILE_REQUEST: {      //文件存在，200
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) {             //POST方法,将文件进行传输
                add_headers(m_file_stat.st_size);
                //第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len  = m_write_idx;

                //第二个iovec指针指向mmap返回的文件指针，长度指向文件的大小
                m_iv[1].iov_base = m_file_address;          //mmap映射的文件
                m_iv[1].iov_len  = m_file_stat.st_size;     //文件的size
                m_iv_count = 2;

                //发送的数据是响应报文头部信息和文件的大小
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true; 

            }else {
                const char *ok_string = "<html><body>空白</body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        }
        default: 
            return false;
    }

    //其余的状态只是写响应报文，没有映射到文件的发送
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len  = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

//解析请求的首行,获得Method，url，HTTPversion
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    //GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");       //第一个空行的位置
    if (!m_url) return BAD_REQUEST;

    *m_url++ = '\0';        //用于确认Method
    char *method = text;
    if (strcasecmp(method, "GET") == 0) 
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0) {
        m_method = GET;
        m_ispost = true;
    }else {
        printf("请求方法不是GET or POST\n");
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");      //第一个空格的位置
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)         //只能进行匹配HTTP/1.1版本
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/') 
        return BAD_REQUEST;
    
    //如果url只是/，那么就判断显示的界面
    if (strlen(m_url) == 1) 
        strcat(m_url, "index.html");
    m_check_state = CHECK_STATEATE_HEADER;          //下面进行头部的解析
    return NO_REQUEST;


}

//解析请求首部
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    //头部结束就是以空开头的
    //strspn就是判断第一个不存在于str2中的下标
    if (text[0] == '\0') {
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;          //转到解析content
        }
        return GET_REQUEST;             //如果content为0， 那么就是GET请求
    }else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");                    //去掉前置的空格,返回的是length
        if (strcasecmp(text, "keep-alive") == 0) {      //长相连模式
            m_linger = true;    
        }
    } else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
    }
    return NO_REQUEST;
}

//解析请求实体,用于POST方法
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    //判断一下buffer中是否是读取到了消息体
    //read_idx如果包含了这个contenlen大小，那么就是完整读取到了buffer中
    //传入的text就是新解析到的一行数据
    if (m_read_idx >= m_content_length + m_checked_idx) {
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}



//根据解析到的结果进行响应
http_conn::HTTP_CODE http_conn::do_request() {
    //1.将文件进行传输
    strcpy(m_real_file, doc_root);      //进行文件名的传输
    //2.进行资源的拼接
    int len = strlen(doc_root);

    //下面进行POST方法的处理,登陆、注册的具体的处理
    const char *p = strrchr(m_url, '/');        //p指向/的位置
    if (m_ispost && (*(p + 1) == '2' || *(p + 1) == 3)) {       //进行注册 or 登录
        //根据符号位进行判断：登陆检测或者是注册检测
        printf("now in cgi handler\n");
        char ch = m_url[1];
        char *m_real_url = (char*)malloc(sizeof(char) * 200);

        strcpy(m_real_url, "/");
        printf("533\n");
        strcat(m_real_url, m_url + 2);
        printf("535\n");
        strncpy(m_real_file + len, m_real_url, FILENAME_LEN - len - 1);
        printf("537\n");
        free(m_real_url);


        //content进行用户名，密码的传输
        //format: user=1122&passwd=1234
        // char *name, *password, *ptr;  //提取出用户名 & 密码
        // ptr = name;
        // int i;
        // for (i = 5; m_string[i] != '&'; i++) 
        //     *(ptr++) = m_string[i];
        // *ptr = '\0';
        // printf("The Name we parse:%s\n", name);
        // ptr = password;
        // for (i = i + 10; m_string[i] != '\0'; i++) 
        //     *(ptr++) = m_string[i];
        // *ptr = '\0';
        // printf("The Password we parse:%s\n", password);

        printf("The m_string: %s\n", m_string);
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        printf("The name: %s\n", name);

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        printf("The password:%s\n", password);
        //同步线程登陆校验
        if (*(p + 1) == '3') {
            //如果是注册的话，需要查表进行判断是否是有重名的
            //如果没有重名的话，进行数据的增加
            char *sql_insert = (char*)malloc(sizeof(char) * 200);
            //insert into users values(username, passwd)
            strcpy(sql_insert, "insert into users values(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "','");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if(users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string,string>(name, password));
                m_lock.unlock();


                if (!res)
                    strcpy(m_url, "/login.html");
                else 
                    strcpy(m_url, "/register.html");
            }else 
                strcpy(m_url, "/registerError.html");

        }else if (*(p + 1) == '2') {
            if (users.find(name) != users.end() && users[name] == password) {
                strcpy(m_url, "/welcome.html");
            }else 
                strcpy(m_url, "/loginError.html");
        }
         //CGI多进程登陆校验 
    }

    /*下面进行请求资源页面的判读*/
    if(*(p + 1) == '0') {       //注册界面，判断的字符是0
        char *m_real_url = (char*)malloc(sizeof(char) * 200);
        strcpy(m_real_url, "/register.html");

        //将对应的注册的文件地址拼接到real_file上面
        // strncpy(m_real_file + len, m_real_url, strlen(m_real_url));
        strcat(m_real_file, m_real_url);        //将注册的url拼接的real_file中
        free(m_real_url);
    }else if (*(p + 1) == '1') {    //登陆界面，判断的字符是1
        char *m_url_real = (char*)malloc(sizeof(char*) * 200);
        strcpy(m_url_real, "/login.html");

        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }else if(*(p + 1) == '5') {         //访问图片的地址
        char *m_real_url = (char*)malloc(sizeof(char) * 200);
        strcpy(m_real_url, "/picture.html");

        strncpy(m_real_file + len, m_real_url, strlen(m_real_url));
        free(m_real_url);
    }else if (*(p + 1) == '6') {        //访问视频的网址
        char *m_real_url = (char*)malloc(sizeof(char) * 200);
        strcpy(m_real_url, "/video.html");

        strncpy(m_real_file + len, m_real_url, strlen(m_real_url));
        free(m_real_url);   
    }else strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);  //如果都不是的话，那么就是默认的界面      



    //3.判断文件的状态，是否存在，是否有权限，是否是一个可发送的文件
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_REQUEST;      //没有这个文件
    }
    //4.判断文件的访问权限
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;       //没有访问的权限
    }

    //5.判断这是不是一个目录
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    //将文件进行mmap映射到内存
    int fd = open(m_real_file, O_RDONLY);   
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

//进行内存映射的解除
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

//通过可变参数列表进行数据的写入 -> m_write_buf
bool http_conn::add_response(const char *format, ...)
{
    //如果写入的内容超出了write_buf的大小
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;

    //可变参数列表
    va_list arg_list;

    //将变量arg_list初始化为传入的参数
    va_start(arg_list, format);

    //将可变参数列表写入缓冲区
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    
    
    //如果写入的数据长度超过缓冲区，baocuo
    if (len >= (WRITE_BUFFER_SIZE-1-m_write_idx)) {
        va_end(arg_list);
        return false;
    }

    //更新m_write_idx
    m_write_idx += len;
    va_end(arg_list);

    return true;
}


//响应报文中添加首部状态行
bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title); 
}


//向响应报文中添加头部字段
bool http_conn::add_headers(int content_length) {
    add_content_length(content_length);         //头部首先写长度
    add_linger();                               //是否是长连接
    add_blank_line();                           //添加空行
}

//添加content的长度
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);  
}

//content文本的类型：html
bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

//是否是长连接
bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

//写一个空行进去
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

//添加文本content,就是状态行对应的文本信息
bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}

//线程池中的工作线程进行调用，处理http请求的函数的入口
void http_conn::process() {
    //解析请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {           //报文不完整，重置为oneshot事件，继续用同一个线程监听读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return ;
    }

    //生成响应
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();       //不行就关闭连接
    }

    //数据准备好了就直接进行发送,而不依靠事件驱动主函数进行捕捉可写的事件

    // if (!write()) {     //如果说写失败了或者不是长连接的话就关闭连接
    //     cout << "Now sub pthread running the write function...\n";
    //     close_conn();
    // }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);       //注册写事件

}