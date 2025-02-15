#ifndef RME_GL_CONTEXT_MANAGER_H_
#define RME_GL_CONTEXT_MANAGER_H_

#include <wx/glcanvas.h>
#include <map>
#include <mutex>
#include <thread>

class GLContextManager {
public:
    static GLContextManager& getInstance() {
        static GLContextManager instance;
        return instance;
    }

    wxGLContext* getSharedContext(std::thread::id thread_id) {
        std::lock_guard<std::mutex> lock(context_mutex);
        auto it = thread_contexts.find(thread_id);
        if (it == thread_contexts.end()) {
            auto context = createSharedContext();
            thread_contexts[thread_id] = context;
            return context;
        }
        return it->second;
    }

private:
    GLContextManager() = default;
    ~GLContextManager() {
        for (auto& pair : thread_contexts) {
            delete pair.second;
        }
    }

    wxGLContext* createSharedContext() {
        if (!main_context) {
            main_context = new wxGLContext(g_gui.GetCurrentMapTab()->GetCanvas());
        }
        return new wxGLContext(g_gui.GetCurrentMapTab()->GetCanvas(), main_context);
    }

    std::map<std::thread::id, wxGLContext*> thread_contexts;
    wxGLContext* main_context = nullptr;
    std::mutex context_mutex;
};

#endif 