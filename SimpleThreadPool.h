// #ifndef SIMPLE_THREAD_POOL_H
// #define SIMPLE_THREAD_POOL_H

// #include <vector>
// #include <queue>
// #include <thread>
// #include <mutex>
// #include <condition_variable>
// #include <atomic>
// #include <functional>

// // Forward declaration of handle_client (implemented in server cpp)
// void handle_client(int client_socket);

// class SimpleThreadPool
// {
// public:
//     // num_threads: count of worker threads
//     SimpleThreadPool(int num_threads) : stop(false), queued_count(0)
//     {
//         for (int i = 0; i < num_threads; ++i)
//         {
//             workers.emplace_back([this]
//                                  {
//                 while (true) {
//                     int client_socket = -1;

//                     {
//                         std::unique_lock<std::mutex> lock(this->queue_mutex);
//                         this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
//                         if (this->stop && this->tasks.empty())
//                             return;

//                         // Take task from queue
//                         client_socket = this->tasks.front();
//                         this->tasks.pop();

//                         // Decrement queued counter immediately because this task is no longer in the queue
//                         queued_count.fetch_sub(1, std::memory_order_relaxed);
//                     }

//                     // Execute the client handler (user-provided)
//                     handle_client(client_socket);
//                 } });
//         }
//     }

//     ~SimpleThreadPool()
//     {
//         {
//             std::unique_lock<std::mutex> lock(queue_mutex);
//             stop = true;
//         }
//         condition.notify_all();
//         for (std::thread &worker : workers)
//         {
//             if (worker.joinable())
//                 worker.join();
//         }
//     }

//     // enqueue a client socket: increments queued_count and notifies worker
//     void enqueue(int client_socket)
//     {
//         {
//             std::unique_lock<std::mutex> lock(queue_mutex);
//             tasks.push(client_socket);
//             queued_count.fetch_add(1, std::memory_order_relaxed);
//         }
//         condition.notify_one();
//     }

//     // Return the number of queued tasks (snapshot)
//     size_t get_queue_size() const
//     {
//         return queued_count.load(std::memory_order_relaxed);
//     }

// private:
//     std::vector<std::thread> workers;
//     std::queue<int> tasks;
//     mutable std::mutex queue_mutex;
//     std::condition_variable condition;
//     std::atomic<bool> stop;
//     std::atomic<size_t> queued_count; // reliable queued tasks counter
// };

// #endif // SIMPLE_THREAD_POOL_H

#ifndef SIMPLE_THREAD_POOL_H
#define SIMPLE_THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

// Forward declaration of handle_client (implemented in server cpp)
void handle_client(int client_socket);

class SimpleThreadPool
{
public:
    // num_threads: count of worker threads
    SimpleThreadPool(int num_threads) : stop(false), queued_count(0)
    {
        for (int i = 0; i < num_threads; ++i)
        {
            workers.emplace_back([this]
                                 {
                while (true) {
                    int client_socket = -1;

                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty())
                            return;

                        // Take task from queue
                        client_socket = this->tasks.front();
                        this->tasks.pop();

                        // Decrement queued counter immediately because this task is no longer in the queue
                        queued_count.fetch_sub(1, std::memory_order_relaxed);
                    }

                    // Execute the client handler (user-provided)
                    handle_client(client_socket);
                } });
        }
    }

    ~SimpleThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers)
        {
            if (worker.joinable())
                worker.join();
        }
    }

    // enqueue a client socket: increments queued_count and notifies worker
    void enqueue(int client_socket)
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(client_socket);
            queued_count.fetch_add(1, std::memory_order_relaxed);
        }
        condition.notify_one();
    }

    // Return the number of queued tasks (snapshot)
    size_t get_queue_size() const
    {
        return queued_count.load(std::memory_order_relaxed);
    }

    // --- THIS IS THE NEW FUNCTION YOU ADDED ---
    // Return the total number of worker threads in the pool
    size_t get_pool_size() const
    {
        // The 'workers' vector is set in the constructor and doesn't change,
        // so we can safely return its size.
        return workers.size();
    }
    // ------------------------------------------

private:
    std::vector<std::thread> workers;
    std::queue<int> tasks;
    mutable std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
    std::atomic<size_t> queued_count; // reliable queued tasks counter
};

#endif // SIMPLE_THREAD_POOL_H