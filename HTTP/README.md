#### Writev进行大文件传输

> `writev`就是进行聚集写，将多个文件聚集起来写到socket缓冲区中进行发送
>
> 但是如果是发送大文件的话，进行`while`循环写的时候，起始地址和文件的`len`有些许的毛病，需要进行不同环境下的判断
>
> **响应报文没发送完：**
>
> 继续发送响应报文，但是起始地址需要更改：增加已经发送了的字节数目

```cpp
 m_iv[0].iov_base = m_write_buf + bytes_have_send;
 m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
```

> **响应报文发送完毕：**
>
> 不需要发送响应报文了，将`m_iv[0].iov_len`设置为0，只发送文件
>
> 传送文件的起始地址，数据长度也是需要进行修改，如下

```cpp
 m_iv[0].iov_len = 0;  //响应报文待发
         //文件内存起始地址     （一共发送了多少 - 响应报文的数目） == 发送了多少文件                 
m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
m_iv[1].iov_len = bytes_to_send;
```

**使用writev进行发包与重置epollout事件的时机**
> send或write发包函数会涉及系统调用，存在一定开销，如果能将数据包聚合起来，然后调用writev将多个数据包一并发送，则可以减少系统调用次数，提高> 效 率。这时EPOLLOUT事件就派上用场了：当发包时，可以将先数据包发到数据buffer(用户缓存区)中存放，然后通过重新注册EPOLLOUT事件，从而触发   POLLOUT事件时，再将数据包一起通过writev发送出去。



