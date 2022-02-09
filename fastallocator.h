#include <iostream>
#include <type_traits>
#include <iterator>
#include <vector>
#include <list>

template <size_t chunkSize>
class FixedAllocator {
  private:
    size_t pos = chunkSize;
    size_t cnt = 0;
    std::vector<void*> chunks;
  public:
    FixedAllocator() = default;
    
    FixedAllocator(const FixedAllocator&) {}
    
    FixedAllocator& operator = (const FixedAllocator&) {}
    
    void* allocate(size_t sz) {
        if (pos + sz > chunkSize) {
            chunks.push_back(::operator new(chunkSize));
            pos = 0;
            cnt += 1;
        }
        pos += sz;
        return static_cast<char*>(chunks[cnt - 1]) + pos - sz;
    }

    void deallocate(void*, size_t) {}

    bool operator == (const FixedAllocator&) const {
        return false;
    }

    bool operator != (const FixedAllocator& other) const {
        return !(*this == other);
    }

    ~FixedAllocator() {
        for (size_t i = 0; i < chunks.size(); ++i) {
            ::operator delete(chunks[i], chunkSize);
        }
    }
};

template <typename T>
class FastAllocator {
  private:
    static const size_t SIZE = 64;
    FixedAllocator<512> alloc;
  public:
    template<typename U>
    struct rebind {
        typedef FastAllocator<U> other;
    };

    typedef T value_type;

    FastAllocator() {}

    FastAllocator& operator = (const FastAllocator&) {
        return *this;
    }

    template<typename U>
    FastAllocator(const FastAllocator<U>&) {}

    T* allocate(size_t sz) {
        if (sz * sizeof(T) <= SIZE) {
            return static_cast<T*>(alloc.allocate(sizeof(T) * sz));
        }
        return static_cast<T*>(::operator new(sizeof(T) * sz));
    }

    void deallocate(T* it, size_t sz) {
        if (sz * sizeof(T) <= SIZE) {
            alloc.deallocate(it, sz * sizeof(T));
        }
        else {
            ::operator delete(it);
        }
    }

    template<typename... Args>
    void construct(T* p, Args&... args) {
        new (p) T(args...);
    }

    bool operator == (const FastAllocator& other) const {
        return alloc == other.alloc;
    }
    
    bool operator != (const FastAllocator& other) const {
        return !(*this == other);
    }
    
    void destroy(T* ptr) {
        ptr->~T();
    }

    ~FastAllocator() {}
};


//template <typename T, typename Allocator = std::allocator<T>> using List = std::list<T, Allocator>;
template <typename T, typename Allocator = std::allocator<T>>
class List {
  private:
    struct Node {
        T value;
        Node* left = nullptr;
        Node* right = nullptr;
        Node(const T& value, Node* left = nullptr, Node* right = nullptr) : value(value), left(left), right(right) {}
        Node(Node* left = nullptr, Node* right = nullptr) : left(left), right(right) {}
    };
    
    void insert_helper(Node* iter, const T& value) {
        sz++;
        Node* ptr = alloc.allocate(1);
        alloc.construct(ptr, value, iter->left, iter);
        iter->left->right = ptr;
        iter->left = ptr;
    }
    
    void erase_helper(Node* iter) {
        sz--;
        iter->left->right = iter->right;
        iter->right->left = iter->left;
        alloc.destroy(iter);
        alloc.deallocate(iter, 1);
    }

    void destroy() {
        for (size_t i = 0; i < sz; ++i) {
            head = head->right;
            alloc.destroy(head->left);
            alloc.deallocate(head->left, 1);
        }
        alloc.deallocate(head, 1);
        sz = 0;
    }

    void create(size_t sz) {
        this->sz = sz;
        head = alloc.allocate(1);
        Node* tmp = head;
        for (size_t i = 0; i < sz; ++i) {
            Node* tmp2 = alloc.allocate(1);
            tmp2->left = tmp;
            tmp->right = tmp2;
            tmp = tmp2;
        }
        head->left = tmp;
        tmp->right = head;
    }

    void copy(const List& other) {
        if (std::allocator_traits<Allocator>::propagate_on_container_copy_assignment::value) {
            alloc = other.get_allocator();
        }
        create(other.sz);
        if (other.sz == 0)
            return;
        Node* ptr2 = other.head->right;
        Node* ptr1 = head->right;
        for (size_t i = 0; i < sz; ++i) {
            alloc.construct(ptr1, ptr2->value, ptr1->left, ptr1->right);
            ptr1 = ptr1->right;
            ptr2 = ptr2->right;
        }
    }

    Node* head = nullptr;
    size_t sz = 0;
    using Alloc = typename Allocator::template rebind<Node>::other;
    Alloc alloc;

  public:
    template<bool isConst>
    class common_iterator {
      private:
        using myT = std::conditional_t<isConst, const T, T>;
        using myTreference = std::conditional_t<isConst, const T&, T&>;
        using myptr = std::conditional_t<isConst, const Node*, Node*>;
        using myiterator = std::conditional_t<isConst, const common_iterator, common_iterator>;
        Node* ptr;
        
      public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using reference = T&;
        using pointer = T*;
        using difference_type = std::ptrdiff_t;
        
        common_iterator (Node* ptr) : ptr(ptr) {}
        
        common_iterator(const common_iterator& other) {
            ptr = other.ptr;
        }

        operator common_iterator<true>() {
            return common_iterator<true>(ptr);
        }
        
        bool operator == (const myiterator& other) const {
            return ptr == other.ptr;
        }
        
        bool operator != (const myiterator& other) const {
            return ptr != other.ptr;
        }
        
        myT& operator*() {
            return ptr->value;
        }
        
        myT* operator->() {
            return &(ptr->value);
        }
        
        myiterator& operator = (const myiterator& other) {
            ptr = other.ptr;
            return *this;
        }
        
        myiterator& operator++() {
            ptr = ptr->right;
            return *this;
        }
        
        myiterator operator++(int) {
            myiterator tmp(ptr);
            ptr = ptr->right;
            return tmp;
        }
        
        myiterator& operator--() {
            ptr = ptr->left;
            return *this;
        }
        
        myiterator operator--(int) {
            myiterator tmp(ptr);
            ptr = ptr->left;
            return tmp;
        }
        
        Node* get_ptr() const {
            return ptr;
        }
        
        myiterator get_iter() const {
            return *this;
        }
        
    };
    
    template <typename Iterator>
    class myreverse_iterator {
      private:
        Iterator iter;
      public:
        template <typename revIterator>
        myreverse_iterator(const revIterator& other) : iter(other.get_iter()) {}
        
        myreverse_iterator& operator++() {
            iter--;
            return *this;
        }
        
        myreverse_iterator operator++(int) {
            myreverse_iterator tmp(iter);
            iter--;
            return tmp;
        }
        
        myreverse_iterator& operator--() {
            iter++;
            return *this;
        }
        
        myreverse_iterator operator--(int) {
            myreverse_iterator tmp(iter);
            iter++;
            return tmp;
        }
        
        T& operator*() const {
            return (iter.get_ptr()->value);
        }
        
        T* operator->() const {
            return *iter;
        }
        
        Iterator base() {
            return Iterator(iter.get_ptr()->right);
        }
        
        Iterator get_iter() const {
            return iter;
        }
        
    };
    
    using iterator = common_iterator<false>;
    using const_iterator = common_iterator<true>;
    using reverse_iterator = myreverse_iterator<iterator>;
    using const_reverse_iterator = myreverse_iterator<const_iterator>;
    
    iterator begin() const {
        return iterator(head->right);
    }
    
    iterator end() const {
        return iterator(head);
    }
    
    const_iterator cbegin() const {
        return const_iterator(head->right);
    }
    
    const_iterator cend() const {
        return const_iterator(head);
    }
    
    reverse_iterator rbegin() const {
        return reverse_iterator(iterator(head->left));
    }
    
    reverse_iterator rend() const {
        return reverse_iterator(iterator(head));
    }
    
    const_reverse_iterator crbegin() const {
        return const_reverse_iterator(const_iterator(head->left));
    }
    
    const_reverse_iterator crend() const {
        return const_reverse_iterator(const_iterator(head));
    }
    
    explicit List(const Allocator& allocator = Allocator()) : alloc(allocator) {
        sz = 0;
        head = alloc.allocate(1);
        head->left = head;
        head->right = head;
    }

    List(size_t count, const T& value, const Allocator& alloc = Allocator()) : List(alloc) {
        create(count);
        Node* ptr = head->right;
        for (size_t i = 0; i < sz; ++i) {
            this->alloc.construct(ptr, value, ptr->left, ptr->right);
            ptr = ptr->right;
        }
    }
    
    List(size_t count, const Allocator& alloc = Allocator()) : List(alloc) {
        create(count);
        Node* ptr = head->right;
        for (size_t i = 0; i < sz; ++i) {
            this->alloc.construct(ptr, ptr->left, ptr->right);
            ptr = ptr->right;
        }
    }

    List(const List& other) :
    List(std::allocator_traits<Allocator>::select_on_container_copy_construction(other.get_allocator())) {
        for (auto it = other.begin(); it != other.end(); ++it) {
            push_back(*it);
        }
    }
    
    List& operator = (const List& other) {
        destroy();
        copy(other);
        return *this;
    }
    
    void insert(iterator iter, const T& value) {
        insert_helper(iter.get_ptr(), value);
    }
    
    void insert(const_iterator iter, const T& value) {
        insert_helper(iter.get_ptr(), value);
    }
    
    void erase(iterator iter) {
        erase_helper(iter.get_ptr());
    }
    
    void erase(const_iterator iter) {
        erase_helper(iter.get_ptr());
    }

    ~List() {
        destroy();
    }

    size_t size() const {
        return sz;
    }

    void push_back(const T& value) {
        insert_helper(head, value);
    }
    
    void push_front(const T& value) {
        insert_helper(head->right, value);
    }
    
    void pop_back() {
        erase_helper(head->left);
    }
    
    void pop_front() {
        erase_helper(head->right);
    }

    void print() const {
        std::cerr << sz << std::endl;
        Node* ptr = head;
        for (size_t i = 0; i < sz; ++i) {
            ptr = ptr->right;
            std::cerr << ptr->value << std::endl;
        }
        std::cerr << std::endl;
    }

    Allocator get_allocator() const {
        return alloc;
    }
};
