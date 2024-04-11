# lockfreequeue
A Lockless Queue Algorithm and Implementation

基于Maged M. Michael和 Michael L. Scott发明的《Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms》算法改进，
原算法如下：
![Uploading image.png…]()
![Uploading image.png…]()

该算法在dequeue函数D12行的赋值语句是不安全的，可能出现悬置指针访问。

基于C++11的实现晚点放上来。
