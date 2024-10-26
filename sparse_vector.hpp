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

#ifndef SPARSE_VECTOR_DEFAULT_CONTAINER
#   include <vector>
#   define SPARSE_VECTOR_DEFAULT_CONTAINER std::vector
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
#include <initializer_list>

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
                template <class...> class ContainerT = SPARSE_VECTOR_DEFAULT_CONTAINER>
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
        typedef ContainerT<SPARSE_VECTOR_SIZE_TYPE> container_type;

        public:
        typedef typename AllocatorT::rebind<value_info>::other allocator_type;
        typedef std::allocator_traits<allocator_type> allocator_traits;

        private:
        typename allocator_traits::pointer data_;
        size_type size_;
        size_type capacity_;
        allocator_type allocator_;
        container_type freeIndeces_;

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
                    details::move_place<value_type>(newData[i].value, data_[i].value);
                    newData[i].exist = true;
                }
            }
            allocator_.deallocate(data_, capacity_);
            data_ = newData;
            capacity_ = newCapacity;
        }
        void mark_as_free(size_type i) {
            data_[i].exist = false;
            freeIndeces_.push_back(i);
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
                value_info& cell = data_[i];
                value_info& otherCell = other.data_[i];

                if (!otherCell.exist) {
                    cell.exist = false;
                } else {
                    new(&cell.value)value_type(otherCell.value);
                    cell.exist = true;
                }
            }
        }
        sparse_vector(sparse_vector&& other) : data_(other.data_), size_(other.size_), capacity_(other.capacity_), allocator_(SPARSE_VECTOR_MOVE(other.allocator_)), freeIndeces_(SPARSE_VECTOR_MOVE(other.freeIndeces_)) {
            other.data_ = nullptr;
            other.size_ = 0;
        }
        sparse_vector(std::initializer_list<value_type> other) : data_(nullptr), size_(other.size()), capacity_(other.size()), allocator_(), freeIndeces_() {
            data_ = allocator_.allocate(capacity_);
            for (size_type i = 0; i < size_; ++i) {
                value_info& cell = data_[i];
                new(&cell.value)value_type(*(other.begin() + i));
                cell.exist =  true;
            }
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
        size_type push_free(const_referens val) {
            size_type index;
            if (freeIndeces_.empty()) {
                if (size_ == capacity_)
                    reallocate(capacity_ * 2);
                index = size_;
                ++size_;
            } else {
                index = freeIndeces_.back();
                freeIndeces_.pop_back();
            }

            value_info& cell = data_[index];
            new(&cell.value)value_type(val);
            cell.exist = true;
            return index;
        }
        template<class... ArgsT>
        size_type emplace_free(ArgsT&&... args) {
            size_type index;
            if (freeIndeces_.empty()) {
                if (size_ == capacity_)
                    reallocate(capacity_ * 2);
                index = size_;
                ++size_;
            } else {
                index = freeIndeces_.back();
                freeIndeces_.pop_back();
            }

            value_info& cell = data_[index];
            new(&cell.value)value_type(std::forward<ArgsT>(args)...);
            cell.exist = true;
            return index;
        }
        void erase_at(size_type index) {
            if (index > size_)
                throw std::out_of_range("out of sparse_vector range on erase_at.");
            value_info& cell = data_[index];
            if (!cell.exist)
                throw std::out_of_range("value doesnt exist in sparse_vector on this index. erase_at.");
            cell.value.~value_type();
            mark_as_free(index);
        }
        void pop_back() {
            if (size_ == 0)
                throw std::out_of_range("sparse_vector is empty on pop_back.");
            --size_;
            value_info& cell = data_[size_]; // Это НЕЛЬЗЯ ложить в контейнер свободных индексов, ведь размер был изменён
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
            freeIndeces_.clear(); // Все значения были заняты, так что свободных индексов больше не существует
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
                mark_as_free(i);
            }
            size_ = newSize;
        }
        [[nodiscard]] bool exist_at(size_type i) const noexcept {
            if (size_ <= i)
                return false;
            value_info& cell = data_[i];
            if (!cell.exist)
                return false;
            return true;
        }
        template<class... ArgsT>
        void emplace_at(size_type i, ArgsT&&... args) {
            if (size_ <= i)
                throw std::out_of_range("index out of sparse_vector size on put_at.");
            value_info& cell = data_[i];
            if (!cell.exist)
                throw std::out_of_range("value already exist in sparse_vector on this index. put_at.");
            new(&cell.value)value_type(std::forward<ArgsT>(args)...);
            cell.exist = true;
        }
        void clear() {
            for (size_type i = 0; i < size_; ++i) {
                value_info& cell = data_[i];
                if (cell.exist) { // Сдесь НЕ нужно пополнять freeIndeces_, даже наоборот
                    cell.value.~value_type();
                    cell.exist = false;
                }
            }
            freeIndeces_.clear();
            size_ = 0;
        }
        [[nodiscard]] size_type size() const noexcept {
            return size_;
        }
        [[nodiscard]] size_type capacity() const noexcept {
            return capacity_;
        }
        [[nodiscard]] const container_type& get_free_cells() const noexcept {
            return freeIndeces_;
        }

        public:
        [[nodiscard]] referens operator[](size_type i) {
            return data_[i].value;
        }
        [[nodiscard]] const_referens operator[](size_type i) const {
            return data_[i].value;
        }
        [[nodiscard]] referens at(size_type i) {
            if (size_ <= i)
                throw std::out_of_range("index out of sparse_vector size on at.");
            value_info& cell = data_[i];
            if (!cell.exist)
                throw std::out_of_range("value doesnt exist in sparse_vector on this index. at.");
            return data_[i].value;
        }
        [[nodiscard]] const_referens at(size_type i) const {
            if (size_ <= i)
                throw std::out_of_range("index out of sparse_vector size on at.");
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
            value_info* ptr_;
            const value_info* endPtr_; // yeah, weird

            public:
            iterator(value_info* ptr, const value_info* endPtr) noexcept : ptr_(ptr), endPtr_(endPtr) {
                while ((ptr_ != endPtr_) && !ptr_->exist) {
                    ++ptr_;
                }
            }

            public:
            [[nodiscard]] pointer operator->() noexcept {
                return &ptr_->value;
            }
            [[nodiscard]] const_pointer operator->() const noexcept {
                return &ptr_->value;
            }
            [[nodiscard]] referens operator*() noexcept {
                return ptr_->value;
            }
            [[nodiscard]] const_referens operator*() const noexcept {
                return ptr_->value;
            }
            

            public:
            iterator& operator++() noexcept {
                ++ptr_;
                while ((ptr_ != endPtr_) && !ptr_->exist) {
                    ++ptr_;
                }
                return *this;
            }

            public:
            [[nodiscard]] bool operator==(const iterator& other) const noexcept {
                return ptr_ == other.ptr_;
            }
            [[nodiscard]] bool operator!=(const iterator& other) const noexcept {
                return ptr_ != other.ptr_;
            }
            
        };
        struct const_iterator {
            public:
            typedef T value_type;
            typedef const T& const_referens;
            typedef const T* const_pointer;
            
            private:
            const value_info* ptr_;
            const value_info* endPtr_; // yeah, weird
            
            public:
            const_iterator(const value_info* ptr, const value_info* endPtr) : ptr_(ptr), endPtr_(endPtr) {
                while ((ptr_ != endPtr_) && !ptr_->exist) {
                    ++ptr_;
                }
            }

            public:
            [[nodiscard]] const_pointer operator->() const noexcept {
                return &ptr_->value;
            }
            [[nodiscard]] const_referens operator*() const noexcept {
                return ptr_->value;
            }

            public:
            const_referens& operator++() noexcept {
                ++ptr_;
                while ((ptr_ != endPtr_) && !ptr_->exist) {
                    ++ptr_;
                }
                return *this;
            }

            [[nodiscard]] bool operator==(const const_referens& other) const noexcept {
                return ptr_ == other.ptr_;
            }
            [[nodiscard]] bool operator!=(const const_referens& other) const noexcept {
                return ptr_ != other.ptr_;
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