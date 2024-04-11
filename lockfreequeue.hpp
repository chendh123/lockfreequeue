#ifndef _CHENDH_LOCK_FREE_QUEUE
#define _CHENDH_LOCK_FREE_QUEUE
#include <cstring>
namespace chendh {
namespace lockfree {
	
#define likely(x)  __builtin_expect((x), 1)
#define unlikely(x)  __builtin_expect((x), 0)
namespace detail {
template <class T>
class tagged_ptr
{
    typedef std::uint64_t compressed_ptr_t;

public:
    typedef std::uint16_t tag_t;

private:
    union cast_unit
    {
        compressed_ptr_t value;
        tag_t tag[4];
    };

    static const int tag_index = 3;
    static const compressed_ptr_t ptr_mask = 0xffffffffffffUL; //(1L<<48L)-1;

    static T* extract_ptr(volatile compressed_ptr_t const & i)
    {
        return (T*)(i & ptr_mask);
    }

    static tag_t extract_tag(volatile compressed_ptr_t const & i)
    {
        cast_unit cu;
        cu.value = i;
        return cu.tag[tag_index];
    }

    static compressed_ptr_t pack_ptr(T * ptr, tag_t tag)
    {
        cast_unit ret;
        ret.value = compressed_ptr_t(ptr);
        ret.tag[tag_index] = tag;
        return ret.value;
    }

public:
    /** uninitialized constructor */
    tagged_ptr(void) noexcept//: ptr(0), tag(0)
    {}

    /** copy constructor */

    tagged_ptr(tagged_ptr const & p) = default;

    explicit tagged_ptr(T * p, tag_t t = 0):
        ptr(pack_ptr(p, t))
    {}

    /** unsafe set operation */
    /* @{ */

    tagged_ptr & operator= (tagged_ptr const & p) = default;

    void set(T * p, tag_t t)
    {
        ptr = pack_ptr(p, t);
    }
    /* @} */

    /** comparing semantics */
    /* @{ */
    bool operator== (volatile tagged_ptr const & p) const
    {
        return (ptr == p.ptr);
    }

    bool operator!= (volatile tagged_ptr const & p) const
    {
        return !operator==(p);
    }
    /* @} */

    /** pointer access */
    /* @{ */
    T * get_ptr() const
    {
        return extract_ptr(ptr);
    }

    void set_ptr(T * p)
    {
        tag_t tag = get_tag();
        ptr = pack_ptr(p, tag);
    }
    /* @} */

    /** tag access */
    /* @{ */
    tag_t get_tag() const
    {
        return extract_tag(ptr);
    }

    tag_t get_next_tag() const
    {
        tag_t next = (get_tag() + 1u) & (std::numeric_limits<tag_t>::max)();
        return next;
    }

    void set_tag(tag_t t)
    {
        T * p = get_ptr();
        ptr = pack_ptr(p, t);
    }
    /* @} */

    /** smart pointer support  */
    /* @{ */
    T & operator*() const
    {
        return *get_ptr();
    }

    T * operator->() const
    {
        return get_ptr();
    }

    operator bool(void) const
    {
        return get_ptr() != 0;
    }
    /* @} */

protected:
    compressed_ptr_t ptr;
};

template <typename T,
          typename Alloc = std::allocator<T>
         >
class freelist_stack:
    Alloc
{
    struct freelist_node
    {
        tagged_ptr<freelist_node> next;
    };

    typedef tagged_ptr<freelist_node> tagged_node_ptr;

public:
    typedef T *           index_t;
    typedef tagged_ptr<T> tagged_node_handle;

    template <typename Allocator>
    freelist_stack (Allocator const & alloc, std::size_t n = 0):
        Alloc(alloc),
        pool_(tagged_node_ptr(NULL))
    {
        for (std::size_t i = 0; i != n; ++i) {
            T * node = Alloc::allocate(1);
            std::memset((void*)node, 0, sizeof(T));
#ifdef BOOST_LOCKFREE_FREELIST_INIT_RUNS_DTOR
            destruct<false>(node);
#else
            deallocate<false>(node);
#endif
        }
    }

    template <bool ThreadSafe>
    void reserve (std::size_t count)
    {
        for (std::size_t i = 0; i != count; ++i) {
            T * node = Alloc::allocate(1);
            std::memset((void*)node, 0, sizeof(T));
            deallocate<ThreadSafe>(node);
        }
    }

     
    template <bool ThreadSafe, bool Bounded, typename... ArgumentTypes>
    T * construct (ArgumentTypes&& ...args)
    {
        T * node = allocate<ThreadSafe, Bounded>();
        if (node)
            new(node) T(std::forward<ArgumentTypes>(args)...);
        return node;
    }

    template <bool ThreadSafe>
    void destruct (tagged_node_handle const & tagged_ptr)
    {
        T * n = tagged_ptr.get_ptr();
        n->~T();
        deallocate<ThreadSafe>(n);
    }

    template <bool ThreadSafe>
    void destruct (T * n)
    {
        n->~T();
        deallocate<ThreadSafe>(n);
    }

    ~freelist_stack(void)
    {
        tagged_node_ptr current = pool_.load();

        while (current) {
            freelist_node * current_ptr = current.get_ptr();
            if (current_ptr)
                current = current_ptr->next;
            Alloc::deallocate((T*)current_ptr, 1);
        }
    }

    bool is_lock_free(void) const
    {
        return pool_.is_lock_free();
    }

    T * get_handle(T * pointer) const
    {
        return pointer;
    }

    T * get_handle(tagged_node_handle const & handle) const
    {
        return get_pointer(handle);
    }

    T * get_pointer(tagged_node_handle const & tptr) const
    {
        return tptr.get_ptr();
    }

    T * get_pointer(T * pointer) const
    {
        return pointer;
    }

    T * null_handle(void) const
    {
        return NULL;
    }

protected: // allow use from subclasses
    template <bool ThreadSafe, bool Bounded>
    T * allocate (void)
    {
        if (ThreadSafe)
            return allocate_impl<Bounded>();
        else
            return allocate_impl_unsafe<Bounded>();
    }

private:
    template <bool Bounded>
    T * allocate_impl (void)
    {
        tagged_node_ptr old_pool = pool_.load(std::memory_order_consume);

        for(;;) {
            if (!old_pool.get_ptr()) {
                if (!Bounded) {
                    T *ptr = Alloc::allocate(1);
                    std::memset((void*)ptr, 0, sizeof(T));
                    return ptr;
                }
                else
                    return 0;
            }

            freelist_node * new_pool_ptr = old_pool->next.get_ptr();
            tagged_node_ptr new_pool (new_pool_ptr, old_pool.get_next_tag());

            if (pool_.compare_exchange_weak(old_pool, new_pool)) {
                void * ptr = old_pool.get_ptr();
                return reinterpret_cast<T*>(ptr);
            }
        }
    }

    template <bool Bounded>
    T * allocate_impl_unsafe (void)
    {
        tagged_node_ptr old_pool = pool_.load(std::memory_order_relaxed);

        if (!old_pool.get_ptr()) {
            if (!Bounded) {
                T *ptr = Alloc::allocate(1);
                std::memset((void*)ptr, 0, sizeof(T));
                return ptr;
            }
            else
                return 0;
        }

        freelist_node * new_pool_ptr = old_pool->next.get_ptr();
        tagged_node_ptr new_pool (new_pool_ptr, old_pool.get_next_tag());

        pool_.store(new_pool, std::memory_order_relaxed);
        void * ptr = old_pool.get_ptr();
        return reinterpret_cast<T*>(ptr);
    }

protected:
    template <bool ThreadSafe>
    void deallocate (T * n)
    {
        if (ThreadSafe)
            deallocate_impl(n);
        else
            deallocate_impl_unsafe(n);
    }

private:
    void deallocate_impl (T * n)
    {
        void * node = n;
        tagged_node_ptr old_pool = pool_.load(std::memory_order_consume);
        freelist_node * new_pool_ptr = reinterpret_cast<freelist_node*>(node);

        for(;;) {
            tagged_node_ptr new_pool (new_pool_ptr, old_pool.get_tag());
            new_pool->next.set_ptr(old_pool.get_ptr());

            if (pool_.compare_exchange_weak(old_pool, new_pool))
                return;
        }
    }

    void deallocate_impl_unsafe (T * n)
    {
        void * node = n;
        tagged_node_ptr old_pool = pool_.load(std::memory_order_relaxed);
        freelist_node * new_pool_ptr = reinterpret_cast<freelist_node*>(node);

        tagged_node_ptr new_pool (new_pool_ptr, old_pool.get_tag());
        new_pool->next.set_ptr(old_pool.get_ptr());

        pool_.store(new_pool, std::memory_order_relaxed);
    }

    std::atomic<tagged_node_ptr> pool_;
};


struct copy_convertible
{
    template <typename T, typename U>
    static void copy(T&& t, U&& u)
    {
        u = std::forward<T>(t);
    }
};

struct copy_constructible_and_copyable
{
    template <typename T, typename U>
    static void copy(T && t, U && u)
    {
        u = U(std::forward<T>(t));
    }
};

template <typename T, typename U>
void copy_payload(T && t, U && u)
{
    typedef typename std::conditional<std::is_convertible<T, typename std::decay<U>::type>::value,
                                     copy_convertible,
                                     copy_constructible_and_copyable
                                    >::type copy_type;
    copy_type::copy(std::forward<T>(t), std::forward<U>(u));
}

} /* namespace detail */

template <typename T>
class queue
{
private:
	struct node;
	using counted_node_ptr = detail::tagged_ptr<node>;
	
	std::atomic<counted_node_ptr> head;
	std::atomic<counted_node_ptr> tail;
	detail::freelist_stack<node> pool;
	struct alignas(64) node
	{
		template <typename U>
		node(U&& u, node* ptr) :
			data(std::forward<U>(u))
		{
			/* increment tag to avoid ABA problem */
      counted_node_ptr old_next = next.load(std::memory_order_relaxed);
      counted_node_ptr new_next (ptr, old_next.get_next_tag());
      next.store(new_next, std::memory_order_release);
		}
		
		node (node* null_handle):
      next(counted_node_ptr(null_handle, 0))
    {}

    node(void)
    {
    	counted_node_ptr old_next = next.load(std::memory_order_relaxed);
      counted_node_ptr new_next (nullptr, old_next.get_next_tag());
      next.store(new_next, std::memory_order_release);
    }
        
		std::atomic<counted_node_ptr> next;
		T data;
		bool slide = false;
	};
		
	void initialize(void)
  {
      node* slide_node = pool.template construct<true, false>(pool.null_handle());
      slide_node->slide = true;
      counted_node_ptr dummy_node(pool.get_handle(slide_node), 0);
      head.store(dummy_node, std::memory_order_relaxed);
      tail.store(dummy_node, std::memory_order_release);
  }
	
	template <bool Bounded, typename U>
	bool do_push(U&& u)
	{
		node * n = pool.template construct<true, Bounded>(std::forward<U>(u), pool.null_handle());
		if (n == nullptr)
			return false;

		for (;;) {
      counted_node_ptr tail1 = tail.load(std::memory_order_acquire);
      node* tail_node = tail1.get_ptr();
      counted_node_ptr next = tail_node->next.load(std::memory_order_acquire);
      node * next_ptr = next.get_ptr();

      counted_node_ptr tail2 = tail.load(std::memory_order_acquire);
      if (likely(tail1 == tail2)) {
          if (next_ptr == 0) {
              counted_node_ptr new_tail_next(n, next.get_next_tag());
              if (tail_node->next.compare_exchange_weak(next, new_tail_next) ) {
                  counted_node_ptr new_tail(n, tail1.get_next_tag());
                  tail.compare_exchange_strong(tail1, new_tail);
                  return true;
              }
          }
          else {
              counted_node_ptr new_tail(next_ptr, tail1.get_next_tag());
              tail.compare_exchange_strong(tail1, new_tail);
          }
      }
    }

	}
public:
	explicit queue(std::size_t n) : 
	  head(counted_node_ptr(0, 0)),
    tail(counted_node_ptr(0, 0)),
    pool(std::allocator<node>(), n + 1)
  {
		initialize();
	}
	queue(const queue&) = delete;
	queue& operator=(const queue&) = delete;
	~queue(){
		  T v;
		  while (pop(v));
		  pool.template destruct<false>(head.load(std::memory_order_relaxed));
	}
  
  bool is_lock_free()
  {
    return head.is_lock_free() && tail.is_lock_free() && pool.is_lock_free();
  }
  
  template <typename U>
  bool push(U&& u)
  {
  	return do_push<false>(std::forward<U>(u));
  }
	
	template <class U>
	bool pop(U &ret)
	{
	  for (;;) {
	    counted_node_ptr head1 = head.load(std::memory_order_acquire);
	    node* head_ptr = head1.get_ptr();

	    counted_node_ptr tail1 = tail.load(std::memory_order_acquire);
	    counted_node_ptr next = head_ptr->next.load(std::memory_order_acquire);
	    node* next_ptr = next.get_ptr();
	    counted_node_ptr head2 = head.load(std::memory_order_acquire);
	    if (likely(head1 == head2)) {
        if (head1.get_ptr() == tail1.get_ptr()) {
          if (next_ptr == 0 && head_ptr->slide)
            return false;
          if (next_ptr != 0) 
          {
	          counted_node_ptr new_tail(next.get_ptr(), tail1.get_next_tag());
	          tail.compare_exchange_strong(tail1, new_tail);
	        }
	        else 
	        {
	        	node* slide_node = pool.template construct<true, false>();
	        	slide_node->slide = true;
	        	
          	counted_node_ptr new_tail_next(slide_node, next.get_next_tag());
            if (tail1.get_ptr()->next.compare_exchange_strong(next, new_tail_next)) {
              counted_node_ptr new_tail(slide_node, tail1.get_next_tag());
              tail.compare_exchange_strong(tail1, new_tail);
            }
            else 
            {
	            	pool.template destruct<true>(new_tail_next);
            }
	         
	        }

        } else {
          if (next_ptr == 0)
            /* this check is not part of the original algorithm as published by michael and scott
             *
             * however we reuse the tagged_ptr part for the freelist and clear the next part during node
             * allocation. we can observe a null-pointer here.
             * */
            continue;
          
          counted_node_ptr new_head(next.get_ptr(), head1.get_next_tag());
          if (head.compare_exchange_weak(head1, new_head)) {
          	if (head1->slide)
          	{
              pool.template destruct<true>(head1);
          		continue;
          	}
          	
          	detail::copy_payload(std::move(head1->data), ret);
            pool.template destruct<true>(head1);
            return true;
          }
        }
	    }
    }
	}
		
};

}   /* lockfree */
}   /* hundsun */

#undef _LFQ_GETPTR
#endif
