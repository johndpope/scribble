﻿#include "rpsPCH.h"
#include "rpsInternal.h"

extern "C" {
#include "malloc.c"
};

namespace {

class rpsMemoryModule : public rpsIModule
{
public:
    static rpsMemoryModule* getInstance();

    rpsMemoryModule();
    ~rpsMemoryModule();
    virtual const char*     getModuleName() const;
    virtual rpsHookInfo*    getHooks() const;
    virtual void initialize();
    virtual void serialize(rpsArchive &ar);
    virtual void handleMessage(rpsMessage &m);

    void setMemorySize(size_t size) { m_size=size; }

    LPVOID rpsHeapAllocImpl(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes);
    LPVOID rpsHeapReAllocImpl(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes);
    BOOL rpsHeapFreeImpl(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);
    BOOL rpsHeapValidateImpl(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem);
    SIZE_T rpsHeapSizeImpl(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem);

private:
    char *m_mem;
    size_t m_size;
    size_t m_pos;
    rpsMutex m_mutex;
    mspace m_msp;
};

inline bool rpsIsValidMemory(void *p)
{
    MEMORY_BASIC_INFORMATION mi;
    if(::VirtualQuery(p, &mi, sizeof(mi))) {
        if( mi.State==MEM_COMMIT )
        {
            return true;
        }
    }
    return false;
}

inline bool rpsIsWritableMemory(void *p)
{
    MEMORY_BASIC_INFORMATION mi;
    if(::VirtualQuery(p, &mi, sizeof(mi))) {
        if( mi.State==MEM_COMMIT && 
            ((mi.Protect & PAGE_READWRITE)!=0 || (mi.Protect & PAGE_EXECUTE_READWRITE)!=0) )
        {
            return true;
        }
    }
    return false;
}

struct rpsMemoryPageInfo
{
    void *base;
    size_t size;
};

inline rpsArchive& operator&(rpsArchive &ar, rpsMemoryPageInfo &v)
{
    ar & (size_t&)v.base & v.size;
    if(ar.isWriter()) {
        ar.io(v.base, v.size);
    }
    else if(ar.isReader()) {
        if(rpsIsWritableMemory(v.base)) {
            ar.io(v.base, v.size);
        }
        else {
            ar.skip(v.size);
        }
    }
    return ar;
}


HeapAllocT      vaHeapAlloc;
HeapReAllocT    vaHeapReAlloc;
HeapFreeT       vaHeapFree;
HeapValidateT   vaHeapValidate;
HeapSizeT       vaHeapSize;
VirtualAllocT   vaVirtualAlloc;
VirtualFreeT    vaVirtualFree;
VirtualAllocExT vaVirtualAllocEx;
VirtualFreeExT  vaVirtualFreeEx;


LPVOID WINAPI rpsHeapAlloc( HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes )
{
    return rpsMemoryModule::getInstance()->rpsHeapAllocImpl(hHeap, dwFlags, dwBytes);
}

LPVOID WINAPI rpsHeapReAlloc( HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes )
{
    return rpsMemoryModule::getInstance()->rpsHeapReAllocImpl(hHeap, dwFlags, lpMem, dwBytes);
}

BOOL WINAPI rpsHeapFree( HANDLE hHeap, DWORD dwFlags, LPVOID lpMem )
{
    return rpsMemoryModule::getInstance()->rpsHeapFreeImpl(hHeap, dwFlags, lpMem);
}

BOOL WINAPI rpsHeapValidate( HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem )
{
    return rpsMemoryModule::getInstance()->rpsHeapValidateImpl(hHeap, dwFlags, lpMem);
}

SIZE_T WINAPI rpsHeapSize( HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem )
{
    return rpsMemoryModule::getInstance()->rpsHeapSizeImpl(hHeap, dwFlags, lpMem);
}


LPVOID WINAPI rpsVirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect)
{
    LPVOID ret = vaVirtualAlloc(lpAddress, dwSize, flAllocationType, flProtect);
    return ret;
}

BOOL WINAPI rpsVirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType)
{
    BOOL ret = vaVirtualFree(lpAddress, dwSize, dwFreeType);
    return ret;
}

LPVOID WINAPI rpsVirtualAllocEx(HANDLE hProcess, LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect)
{
    LPVOID ret = vaVirtualAllocEx(hProcess, lpAddress, dwSize, flAllocationType, flProtect);
    return ret;
}

BOOL WINAPI rpsVirtualFreeEx(HANDLE hProcess, LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType)
{
    BOOL ret = vaVirtualFreeEx(hProcess, lpAddress, dwSize, dwFreeType);
    return ret;
}

rpsHookInfo g_hookinfo[] = {
    rpsHookInfo("kernel32.dll", "HeapAlloc",   0, rpsHeapAlloc,   &(void*&)vaHeapAlloc),
    rpsHookInfo("kernel32.dll", "HeapReAlloc", 0, rpsHeapReAlloc, &(void*&)vaHeapReAlloc),
    rpsHookInfo("kernel32.dll", "HeapFree",    0, rpsHeapFree,    &(void*&)vaHeapFree),
    rpsHookInfo("kernel32.dll", "HeapValidate",0, rpsHeapValidate,&(void*&)vaHeapValidate),
    rpsHookInfo("kernel32.dll", "HeapSize",    0, rpsHeapSize,    &(void*&)vaHeapSize),

    rpsHookInfo("kernel32.dll", "VirtualAlloc",   0, rpsVirtualAlloc,   &(void*&)vaVirtualAlloc),
    rpsHookInfo("kernel32.dll", "VirtualFree",    0, rpsVirtualFree,    &(void*&)vaVirtualFree),
    rpsHookInfo("kernel32.dll", "VirtualAllocEx", 0, rpsVirtualAllocEx, &(void*&)vaVirtualAllocEx),
    rpsHookInfo("kernel32.dll", "VirtualFreeEx",  0, rpsVirtualFreeEx,  &(void*&)vaVirtualFreeEx),

    rpsHookInfo(nullptr, nullptr, 0, nullptr, nullptr),
};


const char*     rpsMemoryModule::getModuleName() const    { return "rpsMemoryModule"; }
rpsHookInfo*    rpsMemoryModule::getHooks() const         { return g_hookinfo; }

rpsMemoryModule* rpsMemoryModule::getInstance()
{
    static rpsMemoryModule *s_inst = new rpsMemoryModule();
    return s_inst;
}

rpsMemoryModule::rpsMemoryModule()
    : m_mem(nullptr)
    , m_size(0)
    , m_pos(0)
{
    // 適当
#if defined(_M_IX86)
    m_size = 0x40000000;
#elif defined(_M_X64)
    m_size = 0x100000000;
#endif 
}

rpsMemoryModule::~rpsMemoryModule()
{
    // 意図的に開放しない
}

void rpsMemoryModule::initialize()
{
    void *addr = (void*)nullptr;
    for(; !m_mem; m_size/=2) {
        m_mem = (char*)::VirtualAlloc(addr, m_size, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    }
    m_msp = create_mspace_with_base(m_mem, m_size, 1);
    m_pos = 1024;
}

void rpsMemoryModule::serialize(rpsArchive &ar)
{
    ar & m_size & m_pos;
    ar.io(m_mem, m_pos);

    // static 変数群の serialize
    // これらは module 内の書き込み可能領域にある
    std::vector<rpsMemoryPageInfo, rps_allocator<rpsMemoryPageInfo> > sinfo;
    if(ar.isWriter()) {
        HMODULE mod = ::GetModuleHandleA(nullptr);
        rpsEnumerateModulesDetailed([&](rpsModuleInfo &modinfo){
            // 単純化のためメインモジュールに限定
            if(modinfo.base!=mod) { return; }

            char *pos = (char*)modinfo.base;
            char *end = pos + modinfo.size;
            for(; pos<end; ) {
                MEMORY_BASIC_INFORMATION mi;
                if(::VirtualQuery(pos, &mi, sizeof(mi))) {
                    if( (mi.Protect & PAGE_READWRITE)!=0 ||
                        (mi.Protect & PAGE_EXECUTE_READWRITE)!=0 )
                    {
                        rpsMemoryPageInfo tmp = {mi.BaseAddress, mi.RegionSize};
                        sinfo.push_back(tmp);
                    }
                    pos = (char*)mi.BaseAddress + mi.RegionSize;
                }
                else {
                    break;
                }
            }
        });
        ar & sinfo;
    }
    else if(ar.isReader()) {
        ar & sinfo;
    }
}

void rpsMemoryModule::handleMessage( rpsMessage &m )
{
    if(strcmp(m.command, "setMemorySize")==0) {
        setMemorySize(m.value.cast<size_t>());
        return;
    }
}

LPVOID rpsMemoryModule::rpsHeapAllocImpl(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes)
{
    rpsMutex::ScopedLock lock(m_mutex);
    void *ret = mspace_malloc(m_msp, dwBytes);
    m_pos = std::max<size_t>((size_t)ret-(size_t)m_mem+dwBytes, m_pos);
    return ret;
}

LPVOID rpsMemoryModule::rpsHeapReAllocImpl(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes)
{
    rpsMutex::ScopedLock lock(m_mutex);
    void *ret = mspace_realloc(m_msp, lpMem, dwBytes);
    m_pos = std::max<size_t>((size_t)ret-(size_t)m_mem+dwBytes, m_pos);
    return ret;
}

BOOL rpsMemoryModule::rpsHeapFreeImpl(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem)
{
    rpsMutex::ScopedLock lock(m_mutex);
    mspace_free(m_msp, lpMem);
    return TRUE;
}

BOOL rpsMemoryModule::rpsHeapValidateImpl(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem)
{
    return TRUE;
}

SIZE_T rpsMemoryModule::rpsHeapSizeImpl(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem)
{
    return m_size;
}

} // namespace

rpsDLLExport rpsIModule* rpsCreateMemoryModule() { return rpsMemoryModule::getInstance(); }
