#include <iostream>
#include <future>
#include <thread>
#include <list>
#include <chrono>
#include <assert.h>
#include <string.h>
#include "lockfreequeue.hpp"

#define WRITE_THREADS 10
#define READ_THREADS 10
#define CIRCLES  100000

static std::atomic<bool> go(false);
static std::atomic<bool>  done(false);
static std::atomic<int>  push_done(WRITE_THREADS);
static std::atomic<int> _value{1};
char _flags[CIRCLES * WRITE_THREADS];
char _pushed[CIRCLES * WRITE_THREADS];
void* nodes[CIRCLES * WRITE_THREADS];
void* nodes_b[CIRCLES * WRITE_THREADS];

using namespace chendh;
class Integer {
	int _v;
	std::thread::id id;
public:
	Integer() : _v(0) {}
	Integer(int v) : _v(v), id(std::this_thread::get_id()){
//	  std::cout << "Integer " << v << std::endl;
	}

	int get() const { return _v; }
	std::thread::id getid() const {return id; }
};

int productor(lockfree::queue<Integer>& lfqueue)
{
	int i = 0;
	while (!go.load()) std::this_thread::yield();
//	std::chrono::milliseconds dura( 2000 );
//  std::this_thread::sleep_for( dura );
	for (; i < CIRCLES; i++) {
	  if (!lfqueue.push(Integer {_value.fetch_add(1, std::memory_order_relaxed)}))
	  	std::cout << "push failed." << std::endl;
	}
	
	--push_done;
	return i;
}

int consumer(lockfree::queue<Integer>& lfqueue)
{
	int i = 0;
	while (!go.load()) std::this_thread::yield();
//  while (push_done.load()) std::this_thread::yield();
	while (!done.load())
	{
		Integer v;
    while (lfqueue.pop(v)) { 
  	  ++i;
  	  if (v.get() == WRITE_THREADS * CIRCLES)
  	  	done.store(true);
//      std::cout << v.get() << " " << v.getid() << " " << std::endl;
    }
  }
	
	return i;
}

int main()
{  
	for (int i = 0; i < 1000000; i++) {
		done.store(false);
		push_done.store(WRITE_THREADS);
		_value.store(1);
		memset(_flags, 0, sizeof(_flags));
		memset(_pushed, 0, sizeof(_flags));
		memset(nodes, 0, sizeof(nodes));
		memset(nodes_b, 0, sizeof(nodes_b));
		lockfree::queue<Integer> lfqueue { 1000 };
		std::list<std::future<int>> productors;
		std::list<std::future<int>>  consumers;
		
		std::unique_ptr<Integer> v;
	  lockfree::queue<std::unique_ptr<Integer>> lpfqueue { 100 };
	  
	  lpfqueue.push(std::unique_ptr<Integer>(new Integer(20)));
	  lpfqueue.pop(v);
	  std::cout << "v=" << v->get() << std::endl;
  	
	  int *p = new int(2);
	  std::cout << &lfqueue << std::endl;
	  std::cout << p << std::endl;
		std::cout << lfqueue.is_lock_free() << std::endl;
		std::cout << std::atomic<bool>{}.is_lock_free() << " " << sizeof(bool)  << std::endl;
		std::cout << std::atomic<int>{}.is_lock_free() << " " << sizeof(int)  <<std::endl;
		std::cout << std::atomic<long long>{}.is_lock_free() << " " << sizeof(long long) << std::endl;
		std::cout << "begin..." << std::endl;
		
		{
	//		lfqueue.push(Integer{});
			
	//		auto v = lfqueue.pop();
	  }

		for (int i = 0; i < WRITE_THREADS; i++)
		{
			std::future<int> future = std::async(std::launch::async, productor, std::ref(lfqueue));
			productors.push_back(std::move(future));
		}
		
		for (int i = 0; i < READ_THREADS; i++)
		{
			std::future<int> future = std::async(std::launch::async, consumer, std::ref(lfqueue));
			consumers.push_back(std::move(future));
		}
		int count = 0;
		int send = 0;
		go = true;
		for (auto &f : productors)
		{
			send += f.get();
		}
		
		std::cout << "send = " << send << std::endl;
			
		for (auto &f : consumers)
		{
			count += f.get();
		}
		
		
		std::cout << "count = " << count << std::endl;
  }
  

}
