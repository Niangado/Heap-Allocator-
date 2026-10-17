#pragma once
#define NOMINMAX
#include <algorithm>
#include <new>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <iostream>


#pragma comment(lib, "bcrypt.lib") 



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
    static constexpr std::size_t FLAG{ 0xF };
    static constexpr std::size_t ALLOCATED{ 1 };

    static constexpr std::size_t alignment{16 };
    static_assert(sizeof(UnFree_Head) == sizeof(Free_Head));

    std::uint32_t secret{};
    std::uintptr_t pointerSecrert{};
 
    std::size_t heapSize;
    void* heapStart{ nullptr };
    Free_Head* head{ nullptr };


    std::uint32_t getMagic(const void* header) const {
        std::uintptr_t addr{ reinterpret_cast<std::uintptr_t>(header) };
        addr = addr >> 4;
        std::uint32_t addrBits{ static_cast<std::uint32_t>(addr) };
        
        return secret ^ addrBits;
    }

    void encodeNext(){

    }

    void decodeNext(){}

    bool validateNext(const Free_Head *cur, const Free_Head *next, bool allowTouch) const {
        if (next == nullptr) { return true; }
        std::uintptr_t start{ reinterpret_cast<std::uintptr_t>(heapStart) };
        std::uintptr_t addr{ reinterpret_cast<std::uintptr_t>(next) };
        std::uintptr_t curAddr{ reinterpret_cast<std::uintptr_t>(cur) };

        if (!(start <= addr && addr < start + heapSize && addr % alignment == 0)) { return false; }
        if (allowTouch) {
            if (addr < (curAddr + sizeof(Free_Head) + cur->size)) { return false; }
        }
        else {

            if (addr <= (curAddr + sizeof(Free_Head) + cur->size)) { return false; }
        }

        if ((next->size & ALLOCATED) != 0) { return false; } 
        if (next->size == 0) { return false; }
        if (next->size > (start + heapSize) - addr - sizeof(Free_Head)) { return false; }
        

        
        if (next->size % alignment != 0) { return false; }

        return true;
    }
        

public:
    HeapAllocator(std::size_t hs) :heapSize{ hs } {
        if (heapSize < sizeof(Free_Head) + alignment){ throw std::bad_alloc{ } ; }
        NTSTATUS status = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&secret), sizeof(secret), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        NTSTATUS status2 = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&pointerSecrert), sizeof(pointerSecrert), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!BCRYPT_SUCCESS(status)) { throw std::runtime_error{ "Failed to create secret" }; }
        if (!BCRYPT_SUCCESS(status2)) { throw std::runtime_error{ "Failed to create Pointer secret" }; }
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
        this->secret = temp.secret;
        this->pointerSecrert = temp.pointerSecrert;
        temp.heapStart = nullptr;
        temp.head = nullptr;
        temp.heapSize = 0;
        temp.secret = 0;
        temp.pointerSecrert = 0;


    }

    HeapAllocator& operator=(HeapAllocator&& temp) noexcept {
        if (this == &temp) { return *this; }
        if (this->heapStart != nullptr) { VirtualFree(this->heapStart, 0, MEM_RELEASE); }
        this->heapStart = temp.heapStart;
        this->head = temp.head;
        this->heapSize = temp.heapSize;
        this->secret = temp.secret;
        this->pointerSecrert = temp.pointerSecrert;
        temp.heapStart = nullptr;
        temp.head = nullptr;
        temp.heapSize = 0;
        temp.secret = 0;
        temp.pointerSecrert = 0;

        return *this;
    }

    std::size_t grabHeader (void* p) const {
        if (p == nullptr) { return 0; }
       const UnFree_Head* header{ reinterpret_cast<const UnFree_Head*>(p) - 1 }; //points to block header
        return (header->size & ~FLAG);
    }

  
    void deallocate(void* p) {
        if (heapStart == nullptr) { throw std::invalid_argument{"Heap doesnt exist"}; }
        if (p == nullptr) { return; }
        UnFree_Head* header{ reinterpret_cast<UnFree_Head*>(p) - 1 }; //points to block header

        std::uintptr_t start{ reinterpret_cast<std::uintptr_t>(heapStart) + sizeof(Free_Head) }; //points to start of actual allocated memory
        std::uintptr_t addr{ reinterpret_cast<std::uintptr_t>(p) };

        if (addr % alignment != 0) { throw std::invalid_argument{ "Memory does not align" }; }
        if ((addr - start) >= (heapSize - sizeof(Free_Head))) { throw std::invalid_argument{ "Memory not in heap" }; }
        if ((header->size & ALLOCATED) == 0) { throw std::invalid_argument{ "Memory is already free" }; }
        if (header->magic != getMagic(header)) { throw std::invalid_argument{ "Integrity check failed" }; }
      
         Free_Head* point{ reinterpret_cast<Free_Head*>(header) };
        Free_Head* search{ head };
        Free_Head* prev{ nullptr }; 


        while (search != nullptr ) {
            if (search == point) { throw std::invalid_argument{ "Double free" }; }
            if (search < point) {
                prev = search;
                if (!validateNext(search, search->next,false)) { throw std::runtime_error{ "Heap corruption: invalid free-list pointer" }; }
              
                search = search->next;
            }
            else { break; }

        }

        if (prev == nullptr) {
            point->next = head;
            head = point;
            head->size = (head->size & ~FLAG);
           
        }
        else {
            prev->next = point;
            point->next = search;
            point->size = (point->size & ~FLAG);
        }

       

        //coalescing
        Free_Head* first{ head };
       
     


        while (first->next != nullptr) {
            if (!validateNext(first, first->next, true)) { throw std::runtime_error{ "Heap corruption: invalid free-list pointer" }; }
            Free_Head* second{ first->next };
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
                if(!(validateNext(search, search->next,false))){ throw std::runtime_error{ "Heap corruption: invalid free-list pointer" }; }
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
        take->size = take->size | ALLOCATED;
        take->magic = getMagic(take);

        return (take + 1);
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

    std::size_t check() const {
        std::size_t sizeCheck{ 0 };
        if (head == nullptr) { return sizeCheck; }

        const Free_Head* temp{ head };
        const Free_Head* prev{ nullptr };
        std::uintptr_t start{ reinterpret_cast<std::uintptr_t>(heapStart) };

        while (temp != nullptr) {
          
            std::uintptr_t addr{ reinterpret_cast<std::uintptr_t>(temp) };
            if (!(start <= addr && addr < start + heapSize && addr % alignment == 0)) { throw std::logic_error{ "Check: Bounds or alignment failed" }; }
            prev = temp;

            std::uintptr_t x_prev{ reinterpret_cast<std::uintptr_t>(prev) };

            if (prev->size > (start + heapSize) - addr - sizeof(Free_Head)) { throw std::logic_error{"Check: Address is not within bounds"}; }//bounds check
            if (prev->size == 0) { throw std::logic_error{ "Check: Blocksize cannot be 0" }; }//nonzero check
            if (prev->size % alignment != 0) { throw std::logic_error{ "Check: size not a multiple of alignment" }; }//alignment check
            if ((prev->size & ALLOCATED) != 0) { throw std::logic_error{ "Check: free node marked as allocated" }; } //Flag check
            sizeCheck = sizeCheck + sizeof(Free_Head) + prev->size;
            if (sizeCheck > heapSize) { throw std::logic_error{ "Check: Total bytes exceeded heap size" }; } //total bytes check


            temp = temp->next;
                if (temp != nullptr) {
                    std::uintptr_t y_temp{ reinterpret_cast<std::uintptr_t>(temp) };
                    if (!(start < y_temp)) { throw std::logic_error{ "Check: Address is not within bounds" }; }//bounds check
                    std::uintptr_t offSet_prevend{ addr + sizeof(Free_Head) + prev->size }; //track where prev block ends
            
                    if (prev >= temp) { throw std::logic_error{ "Check: Address not sorted" }; }// Sorted by address
                    if ((y_temp - x_prev) == (prev->size + sizeof(Free_Head))) { throw std::logic_error{ "Check: List not fully coalesced" }; } //coalesced check
                    if (offSet_prevend > y_temp) { throw std::logic_error{ "Check: Overlap" }; } // overlap between free regions check
                   
                }

        }
        return sizeCheck;
    }
};