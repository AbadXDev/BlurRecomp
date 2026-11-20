#include <cstdio>
#include <stdafx.h>
#ifdef __x86_64__
#include <cpuid.h>
#endif
#include <cpu/guest_thread.h>
#include <gpu/video.h>
#include <kernel/function.h>
#include <kernel/memory.h>
#include <kernel/heap.h>
#include <kernel/xam.h>
#include <kernel/save_system.h>
#include <kernel/io/file_system.h>
#include <kernel/vfs.h>
#include <file.h>
#include <vector>
#include <image.h>
#include <apu/audio.h>
#include <hid/hid.h>
#include <user/config.h>
#include <user/paths.h>
#include <user/registry.h>
#include <kernel/xdbf.h>
#include <install/installer.h>
#include <install/update_checker.h>
#include <os/logger.h>
#include <os/process.h>
#include <os/registry.h>
#include <ui/game_window.h>
#include <ui/installer_wizard.h>
#include <mod/mod_loader.h>
#include <preload_executable.h>
#include <iostream>
#include <app.h>

#ifdef _WIN32
#include <timeapi.h>
#endif

#if defined(_WIN32) && defined(BLUR_RECOMP_D3D12)
static std::array<std::string_view, 3> g_D3D12RequiredModules =
{
    "D3D12/D3D12Core.dll",
    "dxcompiler.dll",
    "dxil.dll"
};
#endif

const size_t XMAIOBegin = 0x7FEA0000;
const size_t XMAIOEnd = XMAIOBegin + 0x0000FFFF;

Memory g_memory;
Heap g_userHeap;
XDBFWrapper g_xdbfWrapper;
std::unordered_map<uint16_t, GuestTexture*> g_xdbfTextureCache;

void HostStartup()
{
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif

    hid::Init();
}

// Forward declarations from imports.cpp
void InitKernelMainThread();

// Xenon memory initialization
#include <kernel/xenon_memory.h>

// Name inspired from nt's entry point
void KiSystemStartup()
{
    // Initialize main thread ID for SDL event safety
    InitKernelMainThread();
    
    if (g_memory.base == nullptr)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), Localise("System_MemoryAllocationFailed").c_str(), GameWindow::s_pWindow);
        std::_Exit(1);
    }

    g_userHeap.Init();
    
    // CRITICAL: Initialize Xbox 360 Xenon memory regions per memory contract
    // This zeros all regions the game assumes are pre-allocated and zeroed on boot.
    // Must happen BEFORE any game code executes to prevent corruption.
    InitializeXenonMemoryRegions(g_memory.base);

    // Initialize save system early - creates directories and registers save content
    SaveSystem::Initialize();

    const auto gameContent = XamMakeContent(XCONTENTTYPE_RESERVED, "Game");
    const std::string gamePath = (const char*)(GetGamePath() / "game").u8string().c_str();

    BuildPathCache(gamePath);

    // Also cache extracted assets living under "RPF DUMP" (nested RPFS, audio packs, etc.).
    // This improves hit rate when the title requests files that are not present in game/.
    const std::string rpfDumpPath = (const char*)(GetGamePath() / "RPF DUMP").u8string().c_str();
    if (std::filesystem::exists(rpfDumpPath))
        BuildPathCache(rpfDumpPath);

    XamRegisterContent(gameContent, gamePath);

    // Register and mount update content (CRITICAL for file override)
    const auto updateContent = XamMakeContent(XCONTENTTYPE_RESERVED, "Update");
    const std::filesystem::path updateRoot = GetGamePath() / "update";
    const std::string updatePath = (const char*)updateRoot.u8string().c_str();

    if (std::filesystem::exists(updateRoot))
    {
        XamRegisterContent(updateContent, updatePath);
        XamContentCreateEx(0, "update", &updateContent, OPEN_EXISTING, nullptr, nullptr, 0, 0, nullptr);
        
        // Create root mappings for update paths (case variations)
        XamRootCreate("update", updatePath);
        XamRootCreate("Update", updatePath);
        
        LOGF_IMPL(Utility, "Main", "Registered update: -> {}", updatePath);
    }
    else
    {
        LOGF_IMPL(Utility, "Main", "No update directory found (this is normal if no Title Update installed)");
    }

    // Note: Save system initialization already handled by SaveSystem::Initialize() above
    // This section is kept for backwards compatibility with old save file format
    const auto saveFilePath = GetSaveFilePath(true);
    bool saveFileExists = std::filesystem::exists(saveFilePath);

    if (!saveFileExists)
    {
        // Copy base save data to modded save as fallback.
        std::error_code ec;
        std::filesystem::create_directories(saveFilePath.parent_path(), ec);

        if (!ec)
        {
            std::filesystem::copy_file(GetSaveFilePath(false), saveFilePath, ec);
            saveFileExists = !ec;
        }
    }

    if (saveFileExists)
    {
        std::u8string savePathU8 = saveFilePath.parent_path().u8string();
        XamRegisterContent(XamMakeContent(XCONTENTTYPE_SAVEDATA, "GTA5SaveData.bin"), (const char*)(savePathU8.c_str()));
    }

    // Mount game
    XamContentCreateEx(0, "game", &gameContent, OPEN_EXISTING, nullptr, nullptr, 0, 0, nullptr);

    // OS mounts game data to D:
    XamContentCreateEx(0, "D", &gameContent, OPEN_EXISTING, nullptr, nullptr, 0, 0, nullptr);

    // GTA V uses "common:" and "platform:" root paths
    // All game files are in: GetGamePath() / "game" / (common/, xbox360/, audio/)
    // Structure: ~/Library/Application Support/SanRecomp/game/common/, etc.
    const auto gameRoot = GetGamePath() / "game";
    const std::string commonPath = (const char*)(gameRoot / "common").u8string().c_str();
    const std::string platformPath = (const char*)(gameRoot / "xbox360").u8string().c_str();
    const std::string audioPath = (const char*)(gameRoot / "audio").u8string().c_str();
    
    // Register main root paths
    XamRootCreate("common", commonPath);
    XamRootCreate("platform", platformPath);
    XamRootCreate("audio", audioPath);
    
    // Also register alternate names the game might use
    XamRootCreate("xbox360", platformPath);  // Some code uses xbox360: instead of platform:
    
    LOGF_IMPL(Utility, "Main", "Game root: {}", gameRoot.string());
    LOGF_IMPL(Utility, "Main", "Registered common: -> {}", commonPath);
    LOGF_IMPL(Utility, "Main", "Registered platform: -> {}", platformPath);
    LOGF_IMPL(Utility, "Main", "Registered audio: -> {}", audioPath);

    // Initialize Virtual File System for direct file serving
    // This bypasses complex RPF offset-based reading that causes stream issues
    VFS::Initialize(gameRoot);
    LOGF_IMPL(Utility, "Main", "VFS initialized with root: {}", gameRoot.string());

    std::error_code ec;
    for (auto& file : std::filesystem::directory_iterator(GetGamePath() / "dlc", ec))
    {
        if (file.is_directory())
        {
            std::u8string fileNameU8 = file.path().filename().u8string();
            std::u8string filePathU8 = file.path().u8string();
            const char* fileName = (const char*)(fileNameU8.c_str());
            const char* filePath = (const char*)(filePathU8.c_str());
            
            // Register DLC content
            XamRegisterContent(XamMakeContent(XCONTENTTYPE_DLC, fileName), filePath);
            
            // Mount DLC to virtual path
            auto dlcContent = XamMakeContent(XCONTENTTYPE_DLC, fileName);
            XamContentCreateEx(0, fileName, &dlcContent, OPEN_EXISTING, nullptr, nullptr, 0, 0, nullptr);
            
            // Create root mapping for DLC-specific paths
            XamRootCreate(fileName, filePath);
            
            LOGF_IMPL(Utility, "Main", "Registered DLC: {} -> {}", fileName, filePath);
        }
    }

    XAudioInitializeSystem();
}


// Blur Recomp v9.111 [TRAP]: vigia de escritura sobre el agujero de .text.
#ifndef _WIN32
#include <csignal>
#include <sys/ucontext.h>
#include <sys/mman.h>
#include <unistd.h>
#include <execinfo.h>
static volatile int s_trapHits = 0;
static uint8_t* s_trapMemBase = nullptr;
static void BlurTrapHandler(int sig, siginfo_t* info, void* uc_)
{
    uint8_t* addr = (uint8_t*)info->si_addr;
    uint8_t* lo = s_trapMemBase + 0x82600000;
    uint8_t* hi = s_trapMemBase + 0x82920000;
    if (s_trapMemBase != nullptr && addr >= lo && addr < hi)
    {
        if (s_trapHits < 8)
        {
            s_trapHits++;
            ucontext_t* uc = (ucontext_t*)uc_;
            unsigned long long pc = 0, lr = 0;
#if defined(__arm64__)
            pc = (unsigned long long)uc->uc_mcontext->__ss.__pc;
            lr = (unsigned long long)uc->uc_mcontext->__ss.__lr;
#endif
            char buf[320];
            PPCContext* c_ = GetPPCContext();
            unsigned g3 = 0, g4 = 0, g5 = 0, glr = 0;
            if (c_ != nullptr)
            {
                g3 = c_->r3.u32;
                g4 = c_->r4.u32;
                g5 = c_->r5.u32;
                glr = c_->lr;
            }
            int n = snprintf(buf, sizeof(buf),
                "[TRAP] write #%d guest=0x%08llX host_pc=0x%llX host_lr=0x%llX r3=0x%08X r4=0x%08X r5=0x%08X glr=0x%08X\n",
                s_trapHits,
                (unsigned long long)(addr - s_trapMemBase),
                pc, lr, g3, g4, g5, glr);
            if (n > 0)
                (void)!write(1, buf, (size_t)n);
        }
        uintptr_t page = (uintptr_t)addr & ~(uintptr_t)0x3FFF;
        mprotect((void*)page, 0x4000, PROT_READ | PROT_WRITE);
        return;
    }
    { // v9.116: primero la linea minima con datos del ucontext (sin
        // tocar TLS: GetPPCContext en un hilo sin contexto invitado era
        // el sospechoso de las muertes silenciosas sin [CRASH]); despues,
        // si se puede, el contexto invitado y la pila del host.
        ucontext_t* uc = (ucontext_t*)uc_;
        unsigned long long pc = 0, lr = 0;
#if defined(__arm64__)
        pc = (unsigned long long)uc->uc_mcontext->__ss.__pc;
        lr = (unsigned long long)uc->uc_mcontext->__ss.__lr;
#endif
        char buf[512];
        long long goff = s_trapMemBase != nullptr ? (long long)(addr - s_trapMemBase) : -1;
        int n = snprintf(buf, sizeof(buf),
            "[CRASH] sig=%d addr=%p goff=0x%llX host_pc=0x%llX host_lr=0x%llX\n",
            sig, (void*)addr, goff, pc, lr);
        if (n > 0)
            (void)!write(1, buf, (size_t)n);
#if defined(__arm64__)
        {
            const auto& ss = uc->uc_mcontext->__ss;
            int m1 = snprintf(buf, sizeof(buf),
                "[CRASHX] x0=0x%llX x1=0x%llX x2=0x%llX x3=0x%llX x4=0x%llX x5=0x%llX x6=0x%llX x7=0x%llX x8=0x%llX x9=0x%llX\n",
                (unsigned long long)ss.__x[0], (unsigned long long)ss.__x[1],
                (unsigned long long)ss.__x[2], (unsigned long long)ss.__x[3],
                (unsigned long long)ss.__x[4], (unsigned long long)ss.__x[5],
                (unsigned long long)ss.__x[6], (unsigned long long)ss.__x[7],
                (unsigned long long)ss.__x[8], (unsigned long long)ss.__x[9]);
            if (m1 > 0)
                (void)!write(1, buf, (size_t)m1);
            int m2 = snprintf(buf, sizeof(buf),
                "[CRASHX] x10=0x%llX x11=0x%llX x12=0x%llX x13=0x%llX x14=0x%llX x15=0x%llX x16=0x%llX x17=0x%llX x18=0x%llX x19=0x%llX\n",
                (unsigned long long)ss.__x[10], (unsigned long long)ss.__x[11],
                (unsigned long long)ss.__x[12], (unsigned long long)ss.__x[13],
                (unsigned long long)ss.__x[14], (unsigned long long)ss.__x[15],
                (unsigned long long)ss.__x[16], (unsigned long long)ss.__x[17],
                (unsigned long long)ss.__x[18], (unsigned long long)ss.__x[19]);
            if (m2 > 0)
                (void)!write(1, buf, (size_t)m2);
            int m3 = snprintf(buf, sizeof(buf),
                "[CRASHX] x20=0x%llX x21=0x%llX x22=0x%llX x23=0x%llX x24=0x%llX x25=0x%llX x26=0x%llX x27=0x%llX x28=0x%llX fp=0x%llX sp=0x%llX\n",
                (unsigned long long)ss.__x[20], (unsigned long long)ss.__x[21],
                (unsigned long long)ss.__x[22], (unsigned long long)ss.__x[23],
                (unsigned long long)ss.__x[24], (unsigned long long)ss.__x[25],
                (unsigned long long)ss.__x[26], (unsigned long long)ss.__x[27],
                (unsigned long long)ss.__x[28],
                (unsigned long long)ss.__fp, (unsigned long long)ss.__sp);
            if (m3 > 0)
                (void)!write(1, buf, (size_t)m3);
        }
#endif
        PPCContext* c_ = GetPPCContext();
        if (c_ != nullptr)
        {
            int n2 = snprintf(buf, sizeof(buf),
                "[CRASH2] r3=0x%08X r4=0x%08X r5=0x%08X glr=0x%08X\n",
                c_->r3.u32, c_->r4.u32, c_->r5.u32, c_->lr);
            if (n2 > 0)
                (void)!write(1, buf, (size_t)n2);
        }
        {
            void* bt[16];
            int nb = backtrace(bt, 16);
            int off = snprintf(buf, sizeof(buf), "[CRASHBT]");
            for (int i = 0; i < nb && off > 0 && off < (int)sizeof(buf) - 22; i++)
                off += snprintf(buf + off, sizeof(buf) - (size_t)off, " %p", bt[i]);
            if (off > 0 && off < (int)sizeof(buf) - 1)
            {
                buf[off++] = '\n';
                buf[off] = '\0';
                (void)!write(1, buf, (size_t)off);
            }
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

uint32_t LdrLoadModule(const std::filesystem::path &path)
{
    const auto loadResult = LoadFile(path);
    if (loadResult.empty())
    {
        assert("Failed to load module" && false);
        return 0;
    }

    const auto image = Image::ParseImage(loadResult.data(), loadResult.size());

    memcpy(g_memory.Translate(image.base), image.data.get(), image.size);
    { const uint8_t* d_ = image.data.get(); size_t off_ = 0x826F9378ull - image.base; const uint8_t* m_ = (const uint8_t*)g_memory.Translate(0x826F9378); const uint8_t* t_ = (const uint8_t*)g_memory.Translate(0x826F951C); printf("[IMGCHK] base=0x%08X size=0x%zX buf_cod=%02X %02X %02X %02X mem_cod=%02X %02X %02X %02X mem_tab=%02X %02X %02X %02X\n", image.base, image.size, off_ + 4 <= image.size ? d_[off_] : 0, off_ + 4 <= image.size ? d_[off_ + 1] : 0, off_ + 4 <= image.size ? d_[off_ + 2] : 0, off_ + 4 <= image.size ? d_[off_ + 3] : 0, m_[0], m_[1], m_[2], m_[3], t_[0], t_[1], t_[2], t_[3]); fflush(stdout); }

#ifndef _WIN32
    { // Blur Recomp v9.111 [TRAP]: instalar tras la copia de la imagen.
        s_trapMemBase = (uint8_t*)g_memory.Translate(0);
        static uint8_t s_altStack[256 * 1024];
        stack_t ss {};
        ss.ss_sp = s_altStack;
        ss.ss_size = sizeof(s_altStack);
        ss.ss_flags = 0;
        sigaltstack(&ss, nullptr);
        struct sigaction sa {};
        sa.sa_sigaction = BlurTrapHandler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGBUS, &sa, nullptr);
        sigaction(SIGTRAP, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        int rc = mprotect((void*)g_memory.Translate(0x82600000), 0x320000, PROT_READ);
        printf("[TRAP] instalado rc=%d handler=%p loader=%p\n", rc,
            (void*)&BlurTrapHandler, (void*)&LdrLoadModule);
        fflush(stdout);
        { // Blur Recomp v9.118 [WRAP]: en hardware PPC las EA son de
          // 32 bits y un acceso que cruza el final de la ventana (p.
          // ej. el lhz a 0xFFFFFFFF con el que las tablas hash del
          // juego desreferencian su entrada vacia antes de
          // descartarla) envuelve al byte 0. Aqui cruzaba al primer
          // byte tras el mmap de 4 GiB y caia (addr=base+0x100000000,
          // cazado con [CRASHX]: x13=0xFFFFFFFF). Una pagina cero tras
          // la ventana absorbe el cruce con la semantica practica
          // equivalente (la comparacion de clave no casa).
            void* hint_ = (void*)((uintptr_t)s_trapMemBase + 0x100000000ull);
            void* wp_ = mmap(hint_, 0x10000, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (wp_ != hint_)
            {
                if (wp_ != MAP_FAILED)
                    munmap(wp_, 0x10000);
                wp_ = MAP_FAILED;
            }
            printf("[WRAP] pagina post-ventana %p: %s\n", hint_,
                   wp_ == MAP_FAILED ? "FALLO" : "OK");
            fflush(stdout);
        }
    }
#endif
    g_xdbfWrapper = XDBFWrapper(static_cast<uint8_t*>(g_memory.Translate(image.resource_offset)), image.resource_size);

    // Blur Recomp: the template had a "GTA V Memory Layout Collision
    // Fix" here that zeroed 0x82003880-0x82003900 and wrote a fake
    // stream struct at 0x82003890, because in GTA V that address
    // aliases the "common.rpf" string. In Blur those addresses are
    // ordinary image data, so the hack would corrupt the game.
    // Removed; if Blur shows an equivalent collision it will get
    // its own fix from its own evidence.
    
    // Blur Recomp: the template zeroed "worker globals" at
    // 0x830F5000-0x830F8000 here. That range belongs to the original
    // game's memory layout; in Blur it is past the end of the image
    // and not committed, so the memset died with
    // SIGBUS/KERN_PROTECTION_FAILURE, and Translate() never returns
    // null for an in-range address so the guard did not help. If
    // Blur turns out to need an equivalent region zeroed, its own
    // addresses will come from Blur crashes, not from GTA's layout.

    return image.entry_point;
}

#ifdef __x86_64__
__attribute__((constructor(101), target("no-avx,no-avx2"), noinline))
void init()
{
    uint32_t eax, ebx, ecx, edx;

    // Execute CPUID for processor info and feature bits.
    __get_cpuid(1, &eax, &ebx, &ecx, &edx);

    // Check for AVX support.
    if ((ecx & (1 << 28)) == 0)
    {
        printf("[*] CPU does not support the AVX instruction set.\n");

#ifdef _WIN32
        MessageBoxA(nullptr, "Your CPU does not meet the minimum system requirements.", "Blur Recompiled", MB_ICONERROR);
#endif

        std::_Exit(1);
    }
}
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    timeBeginPeriod(1);
#endif

    os::process::CheckConsole();

    if (!os::registry::Init())
        LOGN_WARNING("OS does not support registry.");

    os::logger::Init();

    PreloadContext preloadContext;
    preloadContext.PreloadExecutable();

    bool forceInstaller = false;
    bool forceDLCInstaller = false;
    bool useDefaultWorkingDirectory = false;
    bool forceInstallationCheck = false;
    bool graphicsApiRetry = false;
    const char *sdlVideoDriver = nullptr;

    for (uint32_t i = 1; i < argc; i++)
    {
        forceInstaller = forceInstaller || (strcmp(argv[i], "--install") == 0);
        forceDLCInstaller = forceDLCInstaller || (strcmp(argv[i], "--install-dlc") == 0);
        useDefaultWorkingDirectory = useDefaultWorkingDirectory || (strcmp(argv[i], "--use-cwd") == 0);
        forceInstallationCheck = forceInstallationCheck || (strcmp(argv[i], "--install-check") == 0);
        graphicsApiRetry = graphicsApiRetry || (strcmp(argv[i], "--graphics-api-retry") == 0);
        App::s_isSkipLogos = App::s_isSkipLogos || (strcmp(argv[i], "--skip-logos") == 0);

        if (strcmp(argv[i], "--sdl-video-driver") == 0)
        {
            if ((i + 1) < argc)
                sdlVideoDriver = argv[++i];
            else
                LOGN_WARNING("No argument was specified for --sdl-video-driver. Option will be ignored.");
        }
    }

    if (!useDefaultWorkingDirectory)
    {
        // Set the current working directory to the executable's path.
        std::error_code ec;
        std::filesystem::current_path(os::process::GetExecutablePath().parent_path(), ec);
    }

    Config::Load();

    if (forceInstallationCheck)
    {
        // Create the console to show progress to the user, otherwise it will seem as if the game didn't boot at all.
        os::process::ShowConsole();

        Journal journal;
        double lastProgressMiB = 0.0;
        double lastTotalMib = 0.0;
        Installer::checkInstallIntegrity(GAME_INSTALL_DIRECTORY, journal, [&]()
        {
            constexpr double MiBDivisor = 1024.0 * 1024.0;
            constexpr double MiBProgressThreshold = 128.0;
            double progressMiB = double(journal.progressCounter) / MiBDivisor;
            double totalMiB = double(journal.progressTotal) / MiBDivisor;
            if (journal.progressCounter > 0)
            {
                if ((progressMiB - lastProgressMiB) > MiBProgressThreshold)
                {
                    fprintf(stdout, "Checking files: %0.2f MiB / %0.2f MiB\n", progressMiB, totalMiB);
                    lastProgressMiB = progressMiB;
                }
            }
            else
            {
                if ((totalMiB - lastTotalMib) > MiBProgressThreshold)
                {
                    fprintf(stdout, "Scanning files: %0.2f MiB\n", totalMiB);
                    lastTotalMib = totalMiB;
                }
            }

            return true;
        });

        char resultText[512];
        uint32_t messageBoxStyle;
        if (journal.lastResult == Journal::Result::Success)
        {
            snprintf(resultText, sizeof(resultText), "%s", Localise("IntegrityCheck_Success").c_str());
            fprintf(stdout, "%s\n", resultText);
            messageBoxStyle = SDL_MESSAGEBOX_INFORMATION;
        }
        else
        {
            snprintf(resultText, sizeof(resultText), Localise("IntegrityCheck_Failed").c_str(), journal.lastErrorMessage.c_str());
            fprintf(stderr, "%s\n", resultText);
            messageBoxStyle = SDL_MESSAGEBOX_ERROR;
        }

        SDL_ShowSimpleMessageBox(messageBoxStyle, GameWindow::GetTitle(), resultText, GameWindow::s_pWindow);
        std::_Exit(int(journal.lastResult));
    }

#if defined(_WIN32) && defined(BLUR_RECOMP_D3D12)
    for (auto& dll : g_D3D12RequiredModules)
    {
        if (!std::filesystem::exists(g_executableRoot / dll))
        {
            char text[512];
            snprintf(text, sizeof(text), Localise("System_Win32_MissingDLLs").c_str(), dll.data());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), text, GameWindow::s_pWindow);
            std::_Exit(1);
        }
    }
#endif

    // Check the time since the last time an update was checked. Store the new time if the difference is more than six hours.
    constexpr double TimeBetweenUpdateChecksInSeconds = 6 * 60 * 60;
    time_t timeNow = std::time(nullptr);
    double timeDifferenceSeconds = difftime(timeNow, Config::LastChecked);
    if (timeDifferenceSeconds > TimeBetweenUpdateChecksInSeconds)
    {
        UpdateChecker::initialize();
        UpdateChecker::start();
        Config::LastChecked = timeNow;
        Config::Save();
    }

    if (Config::ShowConsole)
        os::process::ShowConsole();
    LOGN_WARNING("Host Startup");
    HostStartup();

    std::filesystem::path modulePath;
    bool isGameInstalled = Installer::checkGameInstall(GetGamePath(), modulePath);
    bool runInstallerWizard = forceInstaller || forceDLCInstaller || !isGameInstalled;
    
    // TEMPORARY: Force installer UI to always show for preview
    // TODO: Remove this line after UI preview is done
    // runInstallerWizard = true;  // DISABLED - respect actual install check
    
     if (runInstallerWizard)
     {
         if (!Video::CreateHostDevice(sdlVideoDriver, graphicsApiRetry))
         {
             SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), Localise("Video_BackendError").c_str(), GameWindow::s_pWindow);
             std::_Exit(1);
         }

         if (!InstallerWizard::Run(GetGamePath(), isGameInstalled && forceDLCInstaller))
         {
             std::_Exit(0);
         }
     }

    // ModLoader::Init();

    printf("[Main] Calling KiSystemStartup...\n"); fflush(stdout);
    KiSystemStartup();
    printf("[Main] KiSystemStartup done\n"); fflush(stdout);

    printf("[Main] Loading module: %s\n", modulePath.string().c_str()); fflush(stdout);
    uint32_t entry = LdrLoadModule(modulePath);
    printf("[Main] Module loaded, entry=0x%08X\n", entry); fflush(stdout);

    if (!runInstallerWizard)
    {
        printf("[Main] Creating video device...\n"); fflush(stdout);
        if (!Video::CreateHostDevice(sdlVideoDriver, graphicsApiRetry))
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), Localise("Video_BackendError").c_str(), GameWindow::s_pWindow);
            std::_Exit(1);
        }
        printf("[Main] Video device created\n"); fflush(stdout);
    }
    LOGN_WARNING("Start Guest Thread");
    LOGN_WARNING(modulePath.string());
    // Video::StartPipelinePrecompilation();

    GuestThread::Start({ entry, 0, 0, 0 });

    return 0;
}

GUEST_FUNCTION_STUB(__imp__vsprintf);
GUEST_FUNCTION_STUB(__imp___vsnprintf);
GUEST_FUNCTION_STUB(__imp__sprintf);
GUEST_FUNCTION_STUB(__imp___snprintf);
GUEST_FUNCTION_STUB(__imp___snwprintf);
GUEST_FUNCTION_STUB(__imp__vswprintf);
GUEST_FUNCTION_STUB(__imp___vscwprintf);
GUEST_FUNCTION_STUB(__imp__swprintf);
