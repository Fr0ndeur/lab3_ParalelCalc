# ThreadPool Variant 21

This project is a C++ implementation of a thread pool designed for educational purposes as part of a laboratory work. The thread pool features two task queues (primary and secondary), each with a fixed capacity, and demonstrates basic synchronization primitives (mutexes, condition variables, atomics) along with task prioritization and migration.

## Overview

- **Primary Queue:**  
  - Capacity: 15 tasks  
  - Served by 3 worker threads  
  - Tasks are inserted in order based on their expected execution duration (shorter tasks have higher priority).

- **Secondary Queue:**  
  - Capacity: 15 tasks  
  - Served by 1 worker thread  
  - Tasks are transferred here if they wait in the primary queue for more than twice their expected duration.

- **Additional Threads:**  
  - A monitor thread that periodically checks for tasks waiting too long in the primary queue and transfers them to the secondary queue.  
  - A producer thread that simulates task submission with random execution times between 3 and 14 seconds.

- **Statistics:**  
  - The program collects and prints statistics such as the total number of tasks added, tasks executed by primary and secondary threads, tasks transferred, and various wait time metrics.

## How It Works

1. **Task Submission:**  
   A producer thread adds 50 tasks into the primary queue at an interval of 1 second each.

2. **Task Execution:**  
   - Primary worker threads continuously pop tasks from the primary queue and execute them.  
   - If a task remains in the primary queue longer than twice its expected execution time, the monitor thread transfers it to the secondary queue.
   - The secondary worker thread processes tasks from the secondary queue.

3. **Synchronization:**  
   - Mutexes and condition variables are used to ensure safe concurrent access to the queues.
   - Atomic variables keep track of various counters and statistics.

4. **Termination:**  
   After a fixed period (120 seconds after task production), the thread pool is gracefully stopped, and final statistics are printed to the console.

## Building and Running

This project is built using Visual Studio. To run the project:

1. **Clone the Repository:**
   ```bash
   git clone https://github.com/yourusername/threadpool-variant21.git
