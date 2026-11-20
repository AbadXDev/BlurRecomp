#pragma once

// Undefine this to generate a font atlas file in working directory.
// You also need to do this if you are testing localization, as only
// characters in the locale get added to the atlas.
// Blur Recomp: snapshot disabled during bring-up. The template's
// pre-baked atlas (res/font/im_font_atlas.bin) is a zstd snapshot of
// the atlas built from the original game's fonts; ours was a dummy
// file, and Load() turned it into a corrupt atlas that crashed
// ImGui::NewFrame in the installer wizard. With this undefined the
// runtime builds fonts instead (the header's own documented mode).
// Re-enable once a real atlas is generated from Blur's fonts.
// #define ENABLE_IM_FONT_ATLAS_SNAPSHOT

struct ImFontAtlasSnapshot
{
    std::vector<uint8_t> data;
    ankerl::unordered_dense::map<const void*, size_t> objects;
    std::vector<uint32_t> offsets;

    template<typename T1, typename T2>
    void SnapPointer(size_t offset, const T1& value, const T2& ptr, size_t count);

    template<typename T>
    void Traverse(size_t offset, const T& value);

    template<typename T>
    size_t Snap(const T& value);

    void Snap();

    static ImFontAtlas* Load();

    static void GenerateGlyphRanges();

    // When ENABLE_IM_FONT_ATLAS_SNAPSHOT is undefined, this creates the font runtime instead. 
    static ImFont* GetFont(const char* name);
};
