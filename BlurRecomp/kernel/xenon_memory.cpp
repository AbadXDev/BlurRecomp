#include "xenon_memory.h"
#include <os/logger.h>
#include <cstring>

void InitializeXenonMemoryRegions(uint8_t* base) {
    LOG_WARNING("[Xenon] Initializing Xbox 360 memory regions per Xenon contract");
    
    // Zero ENTIRE PPC data region in one contiguous call (0x82000000-0x831F0000)
    // This covers all game data regions:
    // - Stream pool (0x82000000-0x82020000)
    // - XEX data (0x82020000-0x82120000)
    // - Code data (0x82120000-0x82A90000)
    // - Kernel runtime (0x82A90000-0x82AA0000)
    // - Game globals (0x82AA0000-0x83000000)
    // - Static data/BSS (0x83000000-0x831F0000)
    //
    // CRITICAL: Previous piecemeal approach had gaps that caused garbage pointer crashes.
    // The crash address 0x3f1aa1403e0 appeared repeatedly due to unzeroed memory.
    // This single contiguous memset eliminates all gaps.
    //
    // Size: 0x831F0000 - 0x82000000 = 0x11F0000 = 18,808,832 bytes (~17.9 MB)
    LOG_WARNING("[Xenon] Zeroing PPC image region: 0x82000000-0x82EF8000 (15.6 MB)");
    // Blur Recomp: the template zeroed up to 0x831F0000, the end of
    // the original game's much larger image. Only the pages of the
    // loaded image are committed at this point, so the memset ran
    // past Blur's image end (0x82EF8000) into protected pages and
    // died with SIGBUS/KERN_PROTECTION_FAILURE. Zero exactly Blur's
    // image range instead (PPC_IMAGE_BASE/PPC_IMAGE_SIZE from the
    // generated ppc_config.h). Heap regions are committed on demand
    // by the kernel allocator later.
    memset(base + 0x82000000, 0, 0xEF8000);
    
    LOG_WARNING("[Xenon] Memory region initialization complete");
    LOG_WARNING("[Xenon] Total zeroed: 15.6 MB contiguous (0x82000000-0x82EF8000)");
}
