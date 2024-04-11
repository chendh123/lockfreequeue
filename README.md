# lockfreequeue
A Lockless Queue Algorithm and Implementation

基于Maged M. Michael和 Michael L. Scott发明的《Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms》算法改进，
原算法如下：
![Uploading image.png…]()
![Uploading image.png…]()

该算法主要有两个问题：
1、 在多写有读的环境下,E6是不安全的，tail.ptr有可能已经被释放
    在多读的环境下，D4、D12 是不安全的；
  该问题boost通过从常驻的内存池申请、归还内存的方式来解决，同时也解决了ABA问题；
2、D12的并发赋值，造成该算法无法保存 std:unique_ptr std::shared_ptr等元素；
   本次算法改进主要解决这个问题；
