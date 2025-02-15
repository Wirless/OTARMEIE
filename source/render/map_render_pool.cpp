#include "map_render_pool.h"
#include "basemap.h"
#include "map_display.h"
#include "main.h"
#include "gl_context_manager.h"
#include "performance_monitor.h"

const std::chrono::seconds MapRenderPool::CACHE_TIMEOUT(30);

MapRenderPool::MapRenderPool(size_t thread_count) : 
    stop_flag(false), 
    active_tasks(0),
    pending_tasks(0) {
    
    for(size_t i = 0; i < thread_count; ++i) {
        workers.emplace_back(&MapRenderPool::WorkerThread, this);
    }
}

MapRenderPool::~MapRenderPool() {
    Stop();
}

void MapRenderPool::Stop() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop_flag = true;
    }
    condition.notify_all();
    
    for(std::thread& worker : workers) {
        if(worker.joinable()) {
            worker.join();
        }
    }
}

void MapRenderPool::QueueRenderTask(const RenderTask& task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        tasks.push(task);
        ++active_tasks;
    }
    condition.notify_one();
}

void MapRenderPool::WorkerThread() {
    // Get shared OpenGL context for this thread
    wxGLContext* context = GLContextManager::getInstance().getSharedContext(std::this_thread::get_id());
    
    while(true) {
        RenderTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            condition.wait(lock, [this]() {
                return stop_flag || !tasks.empty();
            });
            
            if(stop_flag && tasks.empty()) {
                return;
            }
            
            task = tasks.top();
            tasks.pop();
        }
        
        // Execute the render task with performance monitoring
        if(task.segment && task.render_func) {
            std::unique_lock<std::mutex> render_lock(render_mutex);
            
            // Update segment usage
            UpdateSegmentUsage(task.segment);
            
            // Only render if dirty or not in cache
            if(task.segment->is_dirty || !task.segment->in_cache) {
                context->SetCurrent(*g_gui.GetCurrentMapTab()->GetCanvas());
                
                PerformanceMonitor::getInstance().beginSegmentRender(
                    GetSegmentKey(task.segment->start_x, task.segment->start_y)
                );
                
                task.render_func(*task.segment);
                
                PerformanceMonitor::getInstance().endSegmentRender(
                    GetSegmentKey(task.segment->start_x, task.segment->start_y)
                );
                
                task.segment->is_dirty = false;
            }
            
            // Manage cache periodically
            static int task_counter = 0;
            if(++task_counter % 100 == 0) {
                ManageCache();
            }
        }
        
        --active_tasks;
        ++pending_tasks;
        
        // Sleep if too many pending tasks
        if(pending_tasks > workers.size() * MIN_TASKS_PER_THREAD) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

std::shared_ptr<RenderSegment> MapRenderPool::CreateSegment(int x, int y, int floor) {
    uint64_t key = GetSegmentKey(x, y);
    std::unique_lock<std::mutex> lock(segments_mutex);
    
    auto it = segments.find(key);
    if(it != segments.end()) {
        return it->second;
    }
    
    auto segment = std::make_shared<RenderSegment>(
        x * SEGMENT_SIZE,
        y * SEGMENT_SIZE,
        (x + 1) * SEGMENT_SIZE,
        (y + 1) * SEGMENT_SIZE,
        floor
    );
    
    segments[key] = segment;
    return segment;
}

void MapRenderPool::MarkSegmentDirty(int x, int y) {
    uint64_t key = GetSegmentKey(x, y);
    std::unique_lock<std::mutex> lock(segments_mutex);
    
    auto it = segments.find(key);
    if(it != segments.end()) {
        it->second->is_dirty = true;
    }
}

void MapRenderPool::ManageCache() {
    std::unique_lock<std::mutex> lock(segments_mutex);
    
    // Count cached segments and find expired ones
    std::vector<uint64_t> to_remove;
    size_t cached_count = 0;
    auto now = std::chrono::steady_clock::now();
    
    for(const auto& pair : segments) {
        if(pair.second->in_cache) {
            cached_count++;
            auto age = now - pair.second->last_used;
            if(age > CACHE_TIMEOUT) {
                to_remove.push_back(pair.first);
            }
        }
    }
    
    // Remove expired segments from cache
    for(uint64_t key : to_remove) {
        auto it = segments.find(key);
        if(it != segments.end()) {
            it->second->in_cache = false;
            it->second->buffer.reset();
            cached_count--;
        }
    }
    
    // If still over limit, remove oldest segments
    if(cached_count > MAX_CACHE_SIZE) {
        std::vector<std::pair<uint64_t, std::chrono::steady_clock::time_point>> cached_segments;
        for(const auto& pair : segments) {
            if(pair.second->in_cache) {
                cached_segments.emplace_back(pair.first, pair.second->last_used);
            }
        }
        
        // Sort by last used time
        std::sort(cached_segments.begin(), cached_segments.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
            
        // Remove oldest segments until under limit
        for(size_t i = 0; cached_count > MAX_CACHE_SIZE && i < cached_segments.size(); ++i) {
            auto it = segments.find(cached_segments[i].first);
            if(it != segments.end()) {
                it->second->in_cache = false;
                it->second->buffer.reset();
                cached_count--;
            }
        }
    }
}

void MapRenderPool::UpdateSegmentUsage(std::shared_ptr<RenderSegment> segment) {
    segment->last_used = std::chrono::steady_clock::now();
    segment->in_cache = true;
}

void MapRenderPool::UpdateSegmentPriorities(int center_x, int center_y) {
    std::unique_lock<std::mutex> lock(queue_mutex);
    
    // Create new priority queue with updated priorities
    std::priority_queue<RenderTask> new_queue;
    
    while (!tasks.empty()) {
        RenderTask task = tasks.top();
        tasks.pop();
        
        if (task.segment) {
            task.priority = CalculateSegmentPriority(*task.segment, center_x, center_y);
            new_queue.push(task);
        }
    }
    
    tasks = std::move(new_queue);
}

float MapRenderPool::CalculateSegmentPriority(const RenderSegment& segment, int center_x, int center_y) {
    // Calculate distance from segment center to view center
    float dx = (segment.start_x + segment.end_x) / 2.0f - center_x;
    float dy = (segment.start_y + segment.end_y) / 2.0f - center_y;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    // Priority decreases with distance
    float priority = 1000.0f - distance;
    
    // Boost priority for visible segments
    if (segment.is_visible) {
        priority += 2000.0f;
    }
    
    return priority;
}

std::vector<uint8_t>* MapRenderPool::MemoryPool::acquire(size_t size) {
    std::lock_guard<std::mutex> lock(pool_mutex);
    
    for(auto& buffer : buffers) {
        if(buffer.capacity() >= size) {
            buffer.resize(size);
            return &buffer;
        }
    }
    
    buffers.emplace_back(size);
    return &buffers.back();
}

void MapRenderPool::MemoryPool::release(std::vector<uint8_t>* buffer) {
    std::lock_guard<std::mutex> lock(pool_mutex);
    buffer->clear();
} 