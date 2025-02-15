#include "gl_batch_renderer.h"
#include "graphics.h"

void GLBatchRenderer::begin() {
    vertices.clear();
    textures.clear();
    current_texture = 0;
}

void GLBatchRenderer::addTile(const Tile* tile, int x, int y) {
    if (vertices.size() >= MAX_BATCH_SIZE * 4) {
        flush();
    }
    
    // Add tile vertices to batch
    GLuint texture = tile->getTextureID();
    if (texture != current_texture) {
        if (current_texture != 0) {
            flush();
        }
        current_texture = texture;
    }
    
    // Add vertices for this tile
    BatchVertex v;
    v.color = tile->getColor();
    
    // Add four vertices for the quad
    for (int i = 0; i < 4; ++i) {
        vertices.push_back(v);
    }
}

void GLBatchRenderer::flush() {
    if (vertices.empty()) return;
    
    glBindTexture(GL_TEXTURE_2D, current_texture);
    glVertexPointer(2, GL_FLOAT, sizeof(BatchVertex), &vertices[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(BatchVertex), &vertices[0].tx);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(BatchVertex), &vertices[0].color);
    
    glDrawArrays(GL_QUADS, 0, vertices.size());
    
    vertices.clear();
    current_texture = 0;
} 