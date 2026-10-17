
#include "allocator.h"
#include <iostream>
#include<random>
#include <vector>
#include <utility>
#include <cassert>
#include <cstring>
#include<string>

unsigned int seed{ 54 };
std::mt19937 gen(seed);



struct Live {
    int* ptr;
    unsigned char pattern;
};

void remove_element(std::vector<Live>& num, size_t index) {
    if (index < num.size()) {
        num[index] = std::move(num.back());
        num.pop_back();
    }
}

int badAllocs{ 0 };
std::size_t maxLive{ 0 };

int main() {

    std::vector<Live> holder;
    std::size_t heapSize{ 4096 };
    std::uniform_int_distribution<int> c(1, 10);
    std::uniform_int_distribution<int> s(1, 500);

    std::size_t free_bytes{ 0 };
    std::size_t live_bytes{ 0 };
    try {
      
       
        HeapAllocator test{ heapSize };


       for (int i{ 0 }; i < 100000; i++) {

            int choice{ c(gen) };
            int size{ s(gen) };
            if (choice <= 6) {
                try {
                    holder.push_back({ static_cast<int*>(test.allocate(size)), 0 });
                    std::size_t size{ test.grabHeader(holder.back().ptr) };
                    std::memset(holder.back().ptr, i & 0xFF, size);
                    holder.back().pattern = i & 0xFF;
                    live_bytes = live_bytes + size + 16;
                        if (holder.size() > maxLive) { maxLive = holder.size(); }
                }
                catch (const std::bad_alloc& e) {
                   // std::cerr << "Allocator error: " << e.what() << '\n';
                    badAllocs = badAllocs + 1;
                }

            }
            else {
               if (!holder.empty()) {
                   std::uniform_int_distribution<std::size_t> index(0, holder.size() - 1); 
                    std::size_t ind{ index(gen) }; 

                   const unsigned char* checker{reinterpret_cast<unsigned char*>(holder[ind].ptr)};
                    std::size_t blockSize{ test.grabHeader(holder[ind].ptr) };
                  for (std::size_t j{ 0 }; j < blockSize ; j++) {
                        if (checker[j] != holder[ind].pattern) { throw std::logic_error{ "Pattern mismatch at iter " + std::to_string(i) + ", byte " + std::to_string(j) + ": expected " + std::to_string(holder[ind].pattern) + ", got " + std::to_string(checker[j]) }; }
                    }

                    live_bytes = live_bytes - blockSize - 16;
                    try { test.deallocate(holder[ind].ptr); } catch(const std::invalid_argument& e){    std::cerr << "InvalidArgument error: " << e.what() << '\n';}
                    remove_element(holder, static_cast<std::size_t>(ind));
                }

            }
            free_bytes = test.check();
            assert((free_bytes + live_bytes) == heapSize);
        }

       for (const auto& temp : holder) {
            test.deallocate(temp.ptr);
       }

        std::cout << "Final Check: " << test.check() <<'\n';
       
    }


    catch (const std::exception& e) {
        std::cerr << "Allocator error: " << e.what() << '\n';
    }

    std::cout << "BadAlloc Count: " << badAllocs << '\n';
    std::cout << "Max Live Block Count: " << maxLive << '\n';
    std::cout << "Seed: " << seed << '\n';
    return 0;
}