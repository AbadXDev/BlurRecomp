#include <stdafx.h>
#include <unordered_set>
#include <mutex>
#include <cstdio>
#include "xdm.h"
#include "freelist.h"

Mutex g_kernelLock;

void DestroyKernelObject(KernelObject* obj)
{
    obj->~KernelObject();
    g_userHeap.Free(obj);
}

// Blur Recomp (2026-11-17) v9.46: registro de handles de kernel vivos.
// NtClose/DestroyKernelObject sobre un handle ya destruido corrompia el
// heap o1heap (doble free). Un kernel real devuelve INVALID_HANDLE.
static std::unordered_set<uint32_t> g_liveKernelHandles;
static std::mutex g_liveKernelHandlesMutex;

uint32_t GetKernelHandle(KernelObject* obj)
{
    assert(obj != GetInvalidKernelObject());
    const uint32_t handle = g_memory.MapVirtual(obj);
    std::lock_guard lock(g_liveKernelHandlesMutex);
    g_liveKernelHandles.insert(handle);
    return handle;
}

void DestroyKernelObject(uint32_t handle)
{
    {
        std::lock_guard lock(g_liveKernelHandlesMutex);
        auto it = g_liveKernelHandles.find(handle);
        if (it == g_liveKernelHandles.end())
        {
            static int s_staleCount = 0;
            if (++s_staleCount <= 10)
                std::fprintf(stderr, "[DestroyKernelObject] handle 0x%08X no vivo; ignorado\n", handle);
            return;
        }
        g_liveKernelHandles.erase(it);
    }
    // Blur Recomp (2026-11-17) v9.50: destruccion DIFERIDA. El objeto
    // no se destruye ni se libera: otro hilo del invitado puede estar
    // usandolo/esperando en el (el kernel real lo mantiene por
    // refcount). Liberarlo al instante corrompia o1heap de forma
    // intermitente (carrera al cerrar durante la carga de atvi.bik).
    static int s_deferCount = 0;
    if (++s_deferCount <= 20)
        std::fprintf(stderr, "[DEFER-CLOSE] handle=0x%08X (objeto conservado)\n", handle);
    return;
}

bool IsKernelObject(uint32_t handle)
{
    return (handle & 0x80000000) != 0;
}

bool IsKernelObject(void* obj)
{
    return IsKernelObject(g_memory.MapVirtual(obj));
}

bool IsInvalidKernelObject(void* obj)
{
    return obj == GetInvalidKernelObject();
}
