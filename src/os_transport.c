#include "os_transport.h"
#include <stdio.h>
#include <string.h>

// 全局初始化状态
static int g_inited = 0;

int os_transport_init(uint32_t config) {
    if (g_inited) {
        fprintf(stderr, "os_transport: 已初始化\n");
        return -1;
    }
    g_inited = 1;
    printf("os_transport: 初始化成功（配置=0x%08x）\n", config);
    return 0;
}

ssize_t os_transport_send(const void *buf, size_t len) {
    if (!g_inited) { fprintf(stderr, "os_transport: 未初始化\n"); return -1; }
    if (!buf || len == 0) { fprintf(stderr, "os_transport: 无效参数\n"); return -1; }
    printf("os_transport: 发送 %zu 字节\n", len);
    return (ssize_t)len;
}

ssize_t os_transport_recv(void *buf, size_t len) {
    if (!g_inited) { fprintf(stderr, "os_transport: 未初始化\n"); return -1; }
    if (!buf || len == 0) { fprintf(stderr, "os_transport: 无效参数\n"); return -1; }
    memset(buf, 0xAA, len);
    printf("os_transport: 接收 %zu 字节\n", len);
    return (ssize_t)len;
}

void os_transport_destroy(void) {
    if (!g_inited) return;
    g_inited = 0;
    printf("os_transport: 资源销毁成功\n");
}