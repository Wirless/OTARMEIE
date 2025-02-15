#ifndef RME_PERFORMANCE_MONITOR_H_
#define RME_PERFORMANCE_MONITOR_H_

#include <chrono>
#include <atomic>
#include <array>
#include <mutex>

class PerformanceMonitor {
public:
    static PerformanceMonitor& getInstance() {
        static PerformanceMonitor instance;
        return instance;
    }

    void beginSegmentRender(int segment_id) {
        auto& metric = metrics[current_index];
        metric.segment_id = segment_id;
        metric.start_time = std::chrono::high_resolution_clock::now();
    }

    void endSegmentRender(int segment_id) {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto& metric = metrics[current_index];
        if (metric.segment_id == segment_id) {
            metric.duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end_time - metric.start_time
            ).count();
            current_index = (current_index + 1) % MAX_METRICS;
        }
    }

    float getAverageRenderTime() const {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        int64_t total = 0;
        int count = 0;
        for (const auto& metric : metrics) {
            if (metric.duration > 0) {
                total += metric.duration;
                count++;
            }
        }
        return count > 0 ? static_cast<float>(total) / count : 0.0f;
    }

private:
    static const size_t MAX_METRICS = 1000;
    
    struct RenderMetric {
        int segment_id = -1;
        std::chrono::high_resolution_clock::time_point start_time;
        int64_t duration = 0;
    };

    std::array<RenderMetric, MAX_METRICS> metrics;
    std::atomic<size_t> current_index{0};
    mutable std::mutex metrics_mutex;
};

#endif 