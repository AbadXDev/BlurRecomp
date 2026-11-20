#pragma once

// Blur Recomp: the template's patch sets are all GTA-specific and are
// NOT wired in:
// - gta5_patches.* never existed in the repo.
// - player_limit_patches.cpp calls GTA V guest functions by address
//   (sub_826A6CC8 etc.); meaningless and unsafe for Blur, so it is also
//   commented out of the build in BlurRecomp/CMakeLists.txt.
// Blur-specific patches will be declared here as they are developed.

inline void InitPatches()
{
}
