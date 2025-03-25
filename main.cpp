#include <iostream>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <random>
#include <atomic>
#include <vector>
#include <limits>

using namespace std;
using namespace std::chrono;

struct Task {
    function<void()> func;
    int expectedDuration;
    steady_clock::time_point enqueuedTime;
};

struct QueueStats {
    long long maxFullTimeMs;
    long long minFullTimeMs;
};

class TaskQueue {
public:
    TaskQueue(size_t capacity) : capacity(capacity), currentlyFull(false) {
        maxFullTimeMs = 0;
        minFullTimeMs = numeric_limits<long long>::max();
    }

    bool push(const Task& task) {
        lock_guard<mutex> lock(mtx);
        if (queue.size() >= capacity) {
            return false;
        }
        auto it = queue.begin();
        while (it != queue.end() && it->expectedDuration <= task.expectedDuration)
            ++it;
        queue.insert(it, task);
        cv.notify_one();
        if (queue.size() == capacity && !currentlyFull) {
            currentlyFull = true;
            fullStartTime = steady_clock::now();
        }
        return true;
    }

    bool pop(Task& task) {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this]() { return !queue.empty() || terminate; });
        if (queue.empty())
            return false;
        bool wasFull = currentlyFull;
        task = queue.front();
        queue.pop_front();
        if (wasFull && queue.size() < capacity) {
            currentlyFull = false;
            auto fullDuration = duration_cast<milliseconds>(steady_clock::now() - fullStartTime).count();
            if (fullDuration > maxFullTimeMs)
                maxFullTimeMs = fullDuration;
            if (fullDuration < minFullTimeMs)
                minFullTimeMs = fullDuration;
        }
        return true;
    }

    vector<Task> getTasksSnapshot() {
        lock_guard<mutex> lock(mtx);
        return vector<Task>(queue.begin(), queue.end());
    }

    bool removeTask(const Task& task) {
        lock_guard<mutex> lock(mtx);
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (it->enqueuedTime == task.enqueuedTime && it->expectedDuration == task.expectedDuration) {
                queue.erase(it);
                if (currentlyFull && queue.size() < capacity) {
                    currentlyFull = false;
                    auto fullDuration = duration_cast<milliseconds>(steady_clock::now() - fullStartTime).count();
                    if (fullDuration > maxFullTimeMs)
                        maxFullTimeMs = fullDuration;
                    if (fullDuration < minFullTimeMs)
                        minFullTimeMs = fullDuration;
                }
                return true;
            }
        }
        return false;
    }

    void setTerminateFlag(bool term) {
        lock_guard<mutex> lock(mtx);
        terminate = term;
        cv.notify_all();
    }

    QueueStats getStats() {
        lock_guard<mutex> lock(mtx);
        QueueStats stats;
        stats.maxFullTimeMs = maxFullTimeMs;
        stats.minFullTimeMs = (minFullTimeMs == numeric_limits<long long>::max() ? 0 : minFullTimeMs);
        return stats;
    }

private:
    deque<Task> queue;
    size_t capacity;
    mutex mtx;
    condition_variable cv;
    bool terminate = false;
    bool currentlyFull;
    steady_clock::time_point fullStartTime;
    long long maxFullTimeMs;
    long long minFullTimeMs;
};

class ThreadPoolVariant21 {
public:
    ThreadPoolVariant21() : primaryQueue(15), secondaryQueue(15), stopFlag(false),
        tasksAddedPrimary(0), tasksDiscardedPrimary(0), tasksTransferredToSecondary(0),
        tasksExecutedPrimary(0), tasksExecutedSecondary(0), tasksDiscardedSecondary(0),
        totalTaskWaitTimeMs(0), tasksCountForWaitTime(0), totalWorkerWaitTimeMs(0), workerWaitCount(0)
    {
    }

    void start() {
        for (int i = 0; i < 3; i++) {
            primaryWorkers.emplace_back(&ThreadPoolVariant21::primaryWorker, this, i);
        }
        secondaryWorker = thread(&ThreadPoolVariant21::secondaryWorkerFunc, this);
        monitorThread = thread(&ThreadPoolVariant21::monitorFunction, this);
    }

    void stop() {
        stopFlag = true;
        primaryQueue.setTerminateFlag(true);
        secondaryQueue.setTerminateFlag(true);
        for (auto& t : primaryWorkers) {
            if (t.joinable())
                t.join();
        }
        if (secondaryWorker.joinable())
            secondaryWorker.join();
        if (monitorThread.joinable())
            monitorThread.join();
    }

    bool addTask(int expectedDuration, function<void()> func) {
        Task task;
        task.func = func;
        task.expectedDuration = expectedDuration;
        task.enqueuedTime = steady_clock::now();
        tasksAddedPrimary++;
        bool pushed = primaryQueue.push(task);
        if (!pushed) {
            cout << "[AddTask] Задачу відхилено: первинна черга переповнена." << endl;
            tasksDiscardedPrimary++;
        }
        return pushed;
    }

    void printStatistics() {
        cout << "\n==== Статистика виконання ====\n";
        cout << "Загальна кількість задач, доданих до первинної черги: " << tasksAddedPrimary.load() << "\n";
        cout << "Задач відхилено при додаванні (черга первинна): " << tasksDiscardedPrimary.load() << "\n";
        cout << "Задач виконано первинними потоками: " << tasksExecutedPrimary.load() << "\n";
        cout << "Задач виконано вторинним потоком: " << tasksExecutedSecondary.load() << "\n";
        cout << "Задач перенесено до вторинної черги: " << tasksTransferredToSecondary.load() << "\n";
        cout << "Задач відхилено при перенесенні (черга вторинна): " << tasksDiscardedSecondary.load() << "\n";
        if (tasksCountForWaitTime.load() > 0) {
            cout << "Середній час очікування задач (від моменту додавання до початку виконання): "
                << totalTaskWaitTimeMs.load() / tasksCountForWaitTime.load() << " мс\n";
        }
        else {
            cout << "Середній час очікування задач: N/A\n";
        }
        if (workerWaitCount.load() > 0) {
            cout << "Середній час очікування потоку (в pop()): "
                << totalWorkerWaitTimeMs.load() / workerWaitCount.load() << " мс\n";
        }
        else {
            cout << "Середній час очікування потоку: N/A\n";
        }
        QueueStats primaryStats = primaryQueue.getStats();
        QueueStats secondaryStats = secondaryQueue.getStats();
        cout << "Первинна черга - максимальний час заповнення: " << primaryStats.maxFullTimeMs
            << " мс, мінімальний час заповнення: " << primaryStats.minFullTimeMs << " мс\n";
        cout << "Вторинна черга - максимальний час заповнення: " << secondaryStats.maxFullTimeMs
            << " мс, мінімальний час заповнення: " << secondaryStats.minFullTimeMs << " мс\n";
        cout << "Кількість створених робочих потоків: " << (int)(primaryWorkers.size() + 1) << "\n"; // 3 + 1
        cout << "Кількість інших потоків (монітор, продюсер): 2\n";
        cout << "================================\n";
    }

private:
    TaskQueue primaryQueue;
    TaskQueue secondaryQueue;
    vector<thread> primaryWorkers;
    thread secondaryWorker;
    thread monitorThread;
    atomic<bool> stopFlag;

    atomic<int> tasksAddedPrimary;
    atomic<int> tasksDiscardedPrimary;
    atomic<int> tasksTransferredToSecondary;
    atomic<int> tasksExecutedPrimary;
    atomic<int> tasksExecutedSecondary;
    atomic<int> tasksDiscardedSecondary;
    atomic<long long> totalTaskWaitTimeMs;
    atomic<int> tasksCountForWaitTime;
    atomic<long long> totalWorkerWaitTimeMs;
    atomic<int> workerWaitCount;

    void primaryWorker(int workerId) {
        while (!stopFlag) {
            Task task;
            auto waitStart = steady_clock::now();
            if (primaryQueue.pop(task)) {
                auto waitEnd = steady_clock::now();
                long long workerWait = duration_cast<milliseconds>(waitEnd - waitStart).count();
                totalWorkerWaitTimeMs += workerWait;
                workerWaitCount++;
                long long taskWait = duration_cast<milliseconds>(waitEnd - task.enqueuedTime).count();
                totalTaskWaitTimeMs += taskWait;
                tasksCountForWaitTime++;
                cout << "[Первинний поток " << workerId << "] Початок виконання задачі (очікуваний час "
                    << task.expectedDuration << " сек, час очікування " << taskWait << " мс)." << endl;
                task.func();
                tasksExecutedPrimary++;
                cout << "[Первинний поток " << workerId << "] Завершено виконання задачі." << endl;
            }
        }
    }

    void secondaryWorkerFunc() {
        while (!stopFlag) {
            Task task;
            auto waitStart = steady_clock::now();
            if (secondaryQueue.pop(task)) {
                auto waitEnd = steady_clock::now();
                long long workerWait = duration_cast<milliseconds>(waitEnd - waitStart).count();
                totalWorkerWaitTimeMs += workerWait;
                workerWaitCount++;
                long long taskWait = duration_cast<milliseconds>(waitEnd - task.enqueuedTime).count();
                totalTaskWaitTimeMs += taskWait;
                tasksCountForWaitTime++;
                cout << "[Вторинний поток] Початок виконання задачі (очікуваний час "
                    << task.expectedDuration << " сек, час очікування " << taskWait << " мс)." << endl;
                task.func();
                tasksExecutedSecondary++;
                cout << "[Вторинний поток] Завершено виконання задачі." << endl;
            }
        }
    }

    void monitorFunction() {
        while (!stopFlag) {
            auto tasks = primaryQueue.getTasksSnapshot();
            for (auto& task : tasks) {
                auto waited = duration_cast<seconds>(steady_clock::now() - task.enqueuedTime).count();
                if (waited > 2 * task.expectedDuration) {
                    bool removed = primaryQueue.removeTask(task);
                    if (removed) {
                        bool pushed = secondaryQueue.push(task);
                        if (pushed) {
                            tasksTransferredToSecondary++;
                            cout << "[Монітор] Перенесено задачу (очікуваний час "
                                << task.expectedDuration << " сек) з первинної до вторинної черги, "
                                << "час очікування: " << waited << " сек." << endl;
                        }
                        else {
                            tasksDiscardedSecondary++;
                            cout << "[Монітор] Вторинна черга переповнена. Задача відкинута." << endl;
                        }
                    }
                }
            }
            this_thread::sleep_for(seconds(1));
        }
    }
};

void simulatedTask(int execTime) {
    cout << "    [Task] Виконується задача на " << execTime << " сек." << endl;
    this_thread::sleep_for(seconds(execTime));
}

int main() {
    ThreadPoolVariant21 pool;
    pool.start();

    thread taskProducer([&pool]() {
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> distr(3, 14);
        for (int i = 0; i < 50; i++) {
            int execTime = distr(gen);
            pool.addTask(execTime, [execTime]() {
                simulatedTask(execTime);
                });
            this_thread::sleep_for(seconds(1));
        }
        });

    taskProducer.join();

    this_thread::sleep_for(seconds(120));
    pool.stop();

    pool.printStatistics();

    return 0;
}
