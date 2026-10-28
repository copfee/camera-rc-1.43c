#pragma once
inline void vTaskDelay(unsigned) {}
inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, unsigned, void*, unsigned, void*, int) { return 1; }

