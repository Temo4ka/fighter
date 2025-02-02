#include "../include/graphics.hpp"

void GraphicsModule::draw() {
    for (Drawable *drawable : drawingQueue) {
        drawable->draw();
    }
}

void GraphicsModule:insertObject(Drawable *obj, size_t ind) { 
    size_t position = std::min(drawingQueue.size(), ind);
    drawingQueue.insert(drawingQueue.begin() + position, obj);
}

void GraphicsModule:eraseObject(Drawable *obj) { 
    for (auto iter = drawingQueue.begin(); iter != drawingQueue.end(); iter++)
        if (*iter == obj) {
            drawingQueue.erase(iter);
        }
}

void GraphicsModule:eraseObject(size_t ind) {
    size_t position = std::min(drawingQueue.size(), ind);
    drawingQueue.erase(drawingQueue.begin() + position);
}