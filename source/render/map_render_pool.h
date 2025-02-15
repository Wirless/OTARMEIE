#ifndef RME_MAP_RENDER_POOL_H_
#define RME_MAP_RENDER_POOL_H_

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <vector>
#include <memory>
#include <chrono>

// Forward declarations
class BaseMap;
class MapCanvas;

struct RenderSegment {
    int start_x, start_y;
    int end_x, end_y;
    int floor;
    bool is_visible;
    std::atomic<bool> is_dirty;
    std::shared_ptr<wxBitmap> buffer;
    
    // Cache management
    std::chrono::steady_clock::time_point last_used;
    bool in_cache;
    
    RenderSegment(int sx, int sy, int ex, int ey, int f) :
        start_x(sx), start_y(sy), end_x(ex), end_y(ey), floor(f),
        is_visible(false), is_dirty(true), in_cache(false) {
        last_used = std::chrono::steady_clock::now();
    }
};

struct RenderTask {
    std::shared_ptr<RenderSegment> segment;
    int priority;
    std::function<void(RenderSegment&)> render_func;
    
    bool operator<(const RenderTask& other) const {
        return priority < other.priority;
    }
};

class MapRenderPool {
public:
    MapRenderPool(size_t thread_count = std::thread::hardware_concurrency());
    ~MapRenderPool();

    void QueueRenderTask(const RenderTask& task);
    void WaitForCompletion();
    void Stop();
    
    // Segment management
    std::shared_ptr<RenderSegment> CreateSegment(int x, int y, int floor);
    void MarkSegmentDirty(int x, int y);
    
    // Thread safety helpers
    std::mutex& GetRenderMutex() { return render_mutex; }
    
    void UpdateSegmentPriorities(int center_x, int center_y);
    float CalculateSegmentPriority(const RenderSegment& segment, int center_x, int center_y);
    
private:
    void WorkerThread();
    
    std::vector<std::thread> workers;
    std::priority_queue<RenderTask> tasks;
    std::mutex queue_mutex;
    std::mutex render_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop_flag;
    std::atomic<size_t> active_tasks;
    
    // Segment management
    static constexpr int SEGMENT_SIZE = 96;
    std::map<uint64_t, std::shared_ptr<RenderSegment>> segments;
    std::mutex segments_mutex;
    
    // Add cache configuration
    static constexpr size_t MAX_CACHE_SIZE = 512;
    static constexpr size_t MIN_TASKS_PER_THREAD = 4;
    
    // Cache timeout needs to be defined in the cpp file since it's not an integral type
    static const std::chrono::seconds CACHE_TIMEOUT;
    
    void ManageCache();
    void UpdateSegmentUsage(std::shared_ptr<RenderSegment> segment);
    
    uint64_t GetSegmentKey(int x, int y) const {
        return (static_cast<uint64_t>(x) << 32) | static_cast<uint64_t>(y);
    }
    
    // Thread management
    std::atomic<size_t> pending_tasks{0};
    
    // Memory pooling
    struct MemoryPool {
        std::vector<std::vector<uint8_t>> buffers;
        std::mutex pool_mutex;
        
        std::vector<uint8_t>* acquire(size_t size);
        void release(std::vector<uint8_t>* buffer);
    } memory_pool;
};

#endif // RME_MAP_RENDER_POOL_H_ 