// New file: source/thread_pool.h
class MapRenderThreadPool {
public:
    MapRenderThreadPool(size_t threads = std::thread::hardware_concurrency());
    ~MapRenderThreadPool();
    
    void QueueRenderTask(const RenderTask& task);
    void WaitForCompletion();
    
private:
    std::vector<std::thread> workers;
    std::queue<RenderTask> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
};