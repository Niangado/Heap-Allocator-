#pragma once
#define NOMINMAX
#include <algorithm>
#include <new>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <windows.h>w
#include <cstdint>
#include <iostream>



class HeapAllocator {
private:
    struct Free_Head {
        std::size_t size;
        Free_Head* next;
    };

    struct UnFree_Head {
        std::size_t size;
        uint32_t magic;
    };

    static constexpr std::size_t alignment{16 };
    static_assert(sizeof(UnFree_Head) == sizeof(Free_Head));
    std::size_t heapSize;
    void* heapStart;
    Free_Head* head;

  


public:
    HeapAllocator(std::size_t hs) :heapSize{ hs } {

        heapStart = VirtualAlloc(nullptr, heapSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (heapStart == nullptr) { throw std::bad_alloc{}; }
        head = static_cast<Free_Head*>(heapStart);
        head->size = heapSize - sizeof(Free_Head);
        head->next = nullptr;

    }

    ~HeapAllocator() {
        if (heapStart == nullptr) { return; }
        VirtualFree(heapStart, 0, MEM_RELEASE);
    }

    HeapAllocator(const HeapAllocator&) = delete;
    HeapAllocator& operator=(const HeapAllocator&) = delete;
    HeapAllocator(HeapAllocator&& temp) noexcept {
        this->heapStart = temp.heapStart;
        this->head = temp.head;
        this->heapSize = temp.heapSize;
        temp.heapStart = nullptr;
        temp.head = nullptr;
        temp.heapSize = 0;


    }

    HeapAllocator& operator=(HeapAllocator&& temp) noexcept {
        if (this == &temp) { return *this; }
        if (this->heapStart != nullptr) { VirtualFree(this->heapStart, 0, MEM_RELEASE); }
        this->heapStart = temp.heapStart;
        this->head = temp.head;
        this->heapSize = temp.heapSize;
        temp.heapStart = nullptr;
        temp.head = nullptr;
        temp.heapSize = 0;
        return *this;
    }

    void print() const {
        if (head == nullptr) { std::cout << "There are no free blocks" << '\n'; return; }
        const Free_Head* temp{ head };
        std::uintptr_t start{ reinterpret_cast<std::uintptr_t>(heapStart) };
        while (temp != nullptr) {
            std::uintptr_t addr{ reinterpret_cast<std::uintptr_t>(temp) };
            std::cout << "OffSet: " << addr - start << '\n';
            std::cout << "Region Size: " << temp->size << '\n';
            std::cout << "------------------------------------------------" << '\n';
            temp = temp->next;
        }

    }

    void deallocate(void* p) {
        if (p == nullptr) { return; }
        UnFree_Head* header{ reinterpret_cast<UnFree_Head*>(p) - 1 }; //points to block header

        std::uintptr_t start{ reinterpret_cast<std::uintptr_t>(heapStart) + sizeof(Free_Head) }; //points to start of actual allocated memory
        std::uintptr_t addr{ reinterpret_cast<std::uintptr_t>(p) };

        if (addr % alignment != 0) { throw std::invalid_argument{ "Memory does not align" }; }
        if ((addr - start) >= (heapSize - sizeof(Free_Head))) { throw std::invalid_argument{ "Memory not in heap" }; }
        if (header->magic != 0xDEADBEEF) { throw std::invalid_argument{ "Integrity check failed" }; }

  
         Free_Head* point{ reinterpret_cast<Free_Head*>(header) };
        Free_Head* search{ head };
        Free_Head* prev{ nullptr }; 


        while (search != nullptr ) {
            if (search == point) { throw std::invalid_argument{ "Double free" }; }
            if (search < point) {
                prev = search;
                search = search->next;
            }
            else { break; }

        }

        if (prev == nullptr) {
            point->next = head;
            head = point;
           
        }
        else {
            prev->next = point;
            point->next = search;
        }

       

        //coalescing
        Free_Head* first{ head };
        Free_Head* second{ head };
     


        while (second->next != nullptr) {
            second = second->next;
            std::uintptr_t x{ reinterpret_cast<std::uintptr_t>(first) };
            std::uintptr_t y{ reinterpret_cast<std::uintptr_t>(second) };
            if ((y - x) == (first->size + sizeof(Free_Head))) {
                first->size = first->size + second->size + sizeof(Free_Head);
                first->next = second->next;
            }
            else {
                first = first->next;
            }

        }


    }

    void* allocate(std::size_t size) {
        if (size == 0) { throw std::bad_alloc{}; }
        if (size > SIZE_MAX - (alignment - 1)) { throw std::bad_alloc{}; }
        std::size_t s{ ((size + alignment - 1) / alignment) * alignment }; //rounded up size
        Free_Head* search{ head };
        Free_Head* prev{ nullptr };

        while (search != nullptr) {
            if (s > search->size) {
                prev = search;
                search = search->next;
            }
            else { break; }

        }

        if (search == nullptr) { throw std::bad_alloc{}; }

        std::size_t regionSize{ search->size };
        Free_Head* regionNext{ search->next };

        UnFree_Head* take{ reinterpret_cast<UnFree_Head*>(search) };

     

        if (search == head) {
            if (regionSize < s + sizeof(Free_Head) + alignment) { //no split
                take->size = regionSize;
                head = regionNext;
            }
            else {
                Free_Head* leftOver{ reinterpret_cast<Free_Head*>(reinterpret_cast<char*>(take) + sizeof(UnFree_Head) + s) };
                head = leftOver;
                head->size = regionSize - (s + sizeof(Free_Head));
                head->next = regionNext;
                take->size = s;
            }
        }
        else {
            if (regionSize < s + sizeof(Free_Head) + alignment) {
                take->size = regionSize;
                prev->next = regionNext;

            }
            else {
                Free_Head* leftOver{ reinterpret_cast<Free_Head*>(reinterpret_cast<char*>(take) + sizeof(UnFree_Head) + s) };
                leftOver->size = regionSize - (s + sizeof(Free_Head));
                leftOver->next = regionNext;
                prev->next = leftOver;
                take->size = s;
            }
        }
        take->magic = 0xDEADBEEF;

        return (take + 1);
    }

   
};
