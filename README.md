#### epoll_wait
> `epoll_wait` 不同于其他的多路复用处理函数，它最终返回的结果是 **已经可以处理的事件集合**，
> 然后写到对应的传入的数组中。之后我们遍历数组就可以操纵已经可以处理的事件了

#### http::init
> 主线程只负责进行接受连接，那么在`init()`中就需要将文件描述符添加到epoll事件中去
> 调用了公共的`init()`之后还是需要调用私有的`init()`进行一些私有变量的初始化
> 传递的参数包含`sockfd`,因为要通过这个`sockfd`进行通信嘛

#### 数据库连接池

> 维护一个类：`conn_pool`,通过静态变量的方式创建 **单例模式**的连接池变量。初始化的时候创建数据库连接池，通过此操作我们在获得数据库连接的时候能够极大的节省时间和资源

#### 数据库连接

> 创建一个 **数据库连接的类**，通过`RAII`模式获得数据库的连接，这样就不必担心数据库连接的资源泄漏，和数据库连接池进行协同工作

#### 状态机进行HTTP报文解析

> **主状态机**：三种状态
>
> - 解析`requestline`
> - 解析`header`
> - 解析`content`

`enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATEATE_HEADER, CHECK_STATE_CONTENT};`

> **从状态机（解析具体的行）：**三种状态
>
> - 读取到完整的行成功`LINE_OK`
> - 解析行出错`LINE_BAD`
> - 行数据尚且不完整`LINE_OPEN`

`enum {LINE_OK, LINE_BAD, LINE_OPEN};`

#### process_read的解析逻辑

- 判断条件

- - 主状态机转移到CHECK_STATE_CONTENT，该条件涉及解析消息体
  - 从状态机转移到LINE_OK，该条件涉及解析请求行和请求头部
  - 两者为或关系，当条件为真则继续循环，否则退出

- 循环体

- - 从状态机读取数据
  - 调用get_line函数，通过m_start_line将从状态机读取数据间接赋给text
  - 主状态机解析text


#### 响应报文的构建
  - 请求的文件存在，通过`io`向量机制`iovec`,第一个向量指向`m_write_buf`,第二个指向`mmap`的地址`m_file_address`；
  - 请求出错，这时候只是申请一个`iovec`,指向`m_write_buf`

  调用`writev`函数进行聚集写


#### main:读写流程

- 主线程接受到可以进行读数据的请求的时候就进行数据的读取

- 将对应的http_conn加入到工作队列中供线程进行读取处理,调用`process()`进行处理

- `process()`函数进行http的解析以及生成响应的报文，主线程可写的时就返回给客户端