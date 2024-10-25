/*  sparse_vector.hpp
    MIT License

    Copyright (c) 2024 Aidar Shigapov

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

*/

#ifndef SPARSE_VECTOR_HPP_
#define SPARSE_VECTPR_HPP_ 1

#ifndef SPARSE_VECTOR_DEFAULT_STACK
#   include <stack>
#   define SPARSE_VECTOR_DEFAULT_STACK std::stack
#endif

#ifndef SPARSE_VECTOR_SIZE_TYPE
#   include <cstddef>
#   define SPARSE_VECTOR_SIZE_TYPE std::size_t
#endif

#ifndef SPARSE_VECTOR_MOVE
#   include <utility>
#   define SPARSE_VECTOR_MOVE std::move
#endif

#include <type_traits>
#include <exception>

namespace sv {
    namespace details {
        template<class U>
        typename std::enable_if<std::is_move_constructible<U>::value>::type
        move_place(U& dst, U& src) {
            new(&dst)U(SPARSE_VECTOR_MOVE(src));
        }
        template<class U>
        typename std::enable_if<!std::is_move_constructible<U>::value>::type
        move_place(U& dst, U& src) {
            new(&dst)U(src);
            src.~U();
        }
    };

    template <  class T,
                class AllocatorT = std::allocator<T>,
                template <class...> class StackT = SPARSE_VECTOR_DEFAULT_STACK>
    class sparse_vector {
        public:
        typedef T value_type;
        typedef T& referens;
        typedef const T& const_referens;
        typedef T* pointer;
        typedef const T* const_pointer;
        typedef SPARSE_VECTOR_SIZE_TYPE size_type;

        protected:
        struct value_info {
            public:
            value_type value;
            bool exist;

        };

        private:
        typedef StackT<SPARSE_VECTOR_SIZE_TYPE> stack_type;

        public:
        typedef typename AllocatorT::rebind<value_info>::other allocator_type;
        typedef std::allocator_traits<allocator_type> allocator_traits;

        private:
        typename allocator_traits::pointer data_;
        size_type size_;
        size_type capacity_;
        allocator_type allocator_;
        stack_type freeIndeces_;

        private:
        
        /* 
            ПОЧЕМУ НЕ СУЩЕСТВУЕТ allocator.expand - ЭТО ЖЕ БУКВАЛЬНО функция C (realloc +-).

            Если хранилище под указателем можно расширить - оно будет расширено и вернётся true, нет? - вернёт false.

            Тогда жизнь стала бы значительно быстрее(+0.5% прозводительности).
        */

        // changes data and capacity
        // newCapacity MUST BE more than current capacity
        void reallocate(size_type newCapacity) { 
            typename allocator_traits::pointer newData = allocator_.allocate(newCapacity);
            for (size_type i = 0; i < size_; ++i) {
                if (!data_[i].exist) {
                    newData[i].exist = false;
                } else {
                    details::move_place<T>(newData[i].value, data_[i].value);
                    newData[i].exist = true;
                }
            }
            allocator_.deallocate(data_, capacity_);
            data_ = newData;
            capacity_ = newCapacity;
        }

        public:
        sparse_vector() : data_(nullptr), size_(0), capacity_(2), allocator_(), freeIndeces_() {
            data_ = allocator_.allocate(capacity_); // bad allocation check provided by allocator_type, maybe
        }
        sparse_vector(allocator_type allocator) : data_(nullptr), size_(0), capacity_(2), allocator_(allocator), freeIndeces_() {
            data_ = allocator_.allocate(capacity_); // bad allocation check provided by allocator_type, maybe
        }
        sparse_vector(const sparse_vector& other) : size_(other.size_), capacity_(other.capacity_), allocator_(other.allocator_), freeIndeces_(other.freeIndeces_) {
            data_ = allocator_.allocate(capacity_);
            for (size_type i = 0; i < size_; ++i) {
                if (!other.data_[i].exist) {
                    data_[i].exist = false;
                } else {
                    new(&data_[i].value)value_type(other.data_[i].value);
                    data_[i].exist = true;
                }
            }
        }
        sparse_vector(sparse_vector&& other) : data_(other.data_), size_(other.size_), capacity_(other.capacity_), allocator_(SPARSE_VECTOR_MOVE(other.allocator_)), freeIndeces_(SPARSE_VECTOR_MOVE(other.freeIndeces_)) {
            other.data_ = nullptr;
            other.size_ = 0;
        }

        public:
        ~sparse_vector() {
            if (data_ == nullptr)
                return;
            for (auto& i : (*this)) {
                i.~value_type();
            }
            allocator_.deallocate(data_, capacity_);
            data_ = nullptr;
            size_ = 0;
        }

        public:
        void push_free(const_referens val) {
            if (freeIndeces_.empty()) {
                if (size_ == capacity_)
                    reallocate(capacity_ * 2);
                auto& cell = data_[size_];
                new(&cell.value)value_type(val);
                cell.exist = true;
                ++size_;
            } else {
                auto& cell = data_[freeIndeces_.top()];
                new(&cell.value)value_type(val);
                cell.exist = true;
                freeIndeces_.pop();
            }
        }
        template<class... ArgsT>
        void emplace_free(ArgsT&&... args) {
            if (freeIndeces_.empty()) {
                if (size_ == capacity_)
                    reallocate(capacity_ * 2);
                value_info& cell = data_[size_];
                new(&cell.value)value_type(std::forward<ArgsT>(args)...);
                cell.exist = true;
                ++size_;
            } else {
                value_info& cell = data_[freeIndeces_.top()];
                new(&cell.value)value_type(std::forward<ArgsT>(args)...);
                cell.exist = true;
                freeIndeces_.pop();
            }
        }
        void erase_at(size_type index) {
            if (index > size_)
                throw std::out_of_range("out of sparse_vector range on erase_at.");

            value_info& cell = data_[index];
            if (cell.exist) {
                cell.value.~value_type();
                cell.exist = false;
                freeIndeces_.push(index);
            } 
        }
        void pop_back() {
            if (size_ == 0)
                throw std::out_of_range("sparse_vector is empty on pop_back.");
            --size_;
            value_info& cell = data_[size_];
            cell.value.~value_type();
            cell.exist = false;
        }
        template<class FunctT>
        void feel_free_cells(FunctT funct) {
            for (size_type i = 0; i < size_; ++i) {
                value_info& cell = data_[i];
                if (!cell.exist) {
                    new(&cell.value)value_type(funct());
                    cell.exist = true;
                }
            }
        }
        void reserve(size_type newCapacity) {
            if (capacity_ >= newCapacity)
                return;
            reallocate(newCapacity);
        }
        // resize with free cells
        void resize(size_type newSize) {
            reserve(newSize);
            for (size_type i = size_; i < newSize; ++i) {
                data_[i].exist = false;
            }
            size_ = newSize;
        }
        size_type size() const noexcept {
            return size_;
        }
        size_type capacity() const noexcept {
            return capacity_;
        }

        public:
        referens operator[](size_type i) {
            return data_[i].value;
        }
        const_referens operator[](size_type i) const {
            return data_[i].value;
        }
        referens at(size_type i) {
            if (size_ <= i)
                throw std::out_of_range("sparse_vector is empty on at.");
            value_info& cell = data_[i];
            if (!cell.exist)
                throw std::out_of_range("value doesnt exist in sparse_vector on this index. at.");
            return data_[i].value;
        }
        const_referens at(size_type i) const {
            if (size_ <= i)
                throw std::out_of_range("sparse_vector is empty on at.");
            value_info& cell = data_[i];
            if (!cell.exist)
                throw std::out_of_range("value doesnt exist in sparse_vector on this index. at.");
            return data_[i].value;
        }

        public:
        struct iterator {
            public:
            typedef T value_type;
            typedef T& referens;
            typedef const T& const_referens;
            typedef T* pointer;
            typedef const T* const_pointer;

            private:
            value_info* ptr;
            const value_info* endPtr; // yeah, weird

            public:
            iterator(value_info* ptr, const value_info* endPtr) noexcept : ptr(ptr), endPtr(endPtr) {

            }

            public:
            [[nodiscard]] pointer operator->() noexcept {
                return &ptr->value;
            }
            [[nodiscard]] const_pointer operator->() const noexcept {
                return &ptr->value;
            }
            [[nodiscard]] referens operator*() noexcept {
                return ptr->value;
            }
            [[nodiscard]] const_referens operator*() const noexcept {
                return ptr->value;
            }
            

            public:
            iterator& operator++() noexcept {
                ++ptr;
                while ((ptr != endPtr) && !ptr->exist) {
                    ++ptr;
                }
                return *this;
            }
            /* iterator& operator++(iterator& iter) const noexcept {
                ++iter.ptr;
                while ((iter.ptr != iter.endPtr) && !iter.ptr->exist) {
                    ++iter.ptr;
                }
                return iter;
            } */

            public:
            [[nodiscard]] bool operator==(const iterator& other) const noexcept {
                return ptr == other.ptr;
            }
            [[nodiscard]] bool operator!=(const iterator& other) const noexcept {
                return ptr != other.ptr;
            }
            
        };
        struct const_iterator {
            public:
            typedef T value_type;
            typedef const T& const_referens;
            typedef const T* const_pointer;
            
            private:
            const value_info* ptr;
            const value_info* endPtr; // yeah, weird
            
            public:
            const_iterator(const value_info* ptr, const value_info* endPtr) : ptr(ptr), endPtr(endPtr) {

            }

            public:
            [[nodiscard]] const_pointer operator->() const noexcept {
                return &ptr->value;
            }
            [[nodiscard]] const_referens operator*() const noexcept {
                return ptr->value;
            }

            public:
            const_referens& operator++() noexcept {
                ++ptr;
                while ((ptr != endPtr) && !ptr->exist) {
                    ++ptr;
                }
                return *this;
            }
            /* const_referens& operator++(const_referens& iter) noexcept {
                ++iter.ptr;
                while ((iter.ptr != iter.endPtr) && !iter.ptr->exist) {
                    ++iter.ptr;
                }
                return iter;
            } */
            [[nodiscard]] bool operator==(const const_referens& other) const noexcept {
                return ptr == other.ptr;
            }
            [[nodiscard]] bool operator!=(const const_referens& other) const noexcept {
                return ptr != other.ptr;
            }
            
        };

        public:
        [[nodiscard]] iterator begin() noexcept {
            return iterator(data_, data_ + size_);
        }
        [[nodiscard]] iterator end() noexcept {
            return iterator(data_ + size_, data_ + size_);
        }
        [[nodiscard]] const_iterator begin() const noexcept {
            return const_iterator(data_, data_ + size_);
        }
        [[nodiscard]] const_iterator end() const noexcept {
            return const_iterator(data_ + size_, data_ + size_);
        }
        
    };
};

#endif