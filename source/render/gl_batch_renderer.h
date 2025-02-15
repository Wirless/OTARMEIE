#ifndef RME_GL_BATCH_RENDERER_H_
#define RME_GL_BATCH_RENDERER_H_

#include <vector>
#include <wx/glcanvas.h>
#include "tile.h"

struct BatchVertex {
    float x, y;
    float tx, ty;
    uint32_t color;
};

class GLBatchRenderer {
public:
    static const size_t MAX_BATCH_SIZE = 1024;
    
    void begin();
    void addTile(const Tile* tile, int x, int y);
    void flush();
    
private:
    std::vector<BatchVertex> vertices;
    std::vector<GLuint> textures;
    GLuint current_texture = 0;
};

#endif 