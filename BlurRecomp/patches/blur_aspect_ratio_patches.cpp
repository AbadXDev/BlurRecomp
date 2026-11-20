// Blur Recomp: aspect-ratio support for Blur.
//
// ComputeOffsets() below is the framework's generic viewport math,
// carried over from the Marathon template's aspect_ratio_patches.cpp.
// The rest of that file hooks Sonic '06's CSD UI system by address and
// does not apply to Blur, so it is not built. Init() stays empty until
// Blur-specific aspect patches exist.

#include "aspect_ratio_patches.h"
#include <gpu/video.h>

#include <algorithm>
#include <cmath>

static float ComputeScale(float aspectRatio)
{
    return ((aspectRatio * 720.0f) / 1280.0f) / sqrt((aspectRatio * 720.0f) / 1280.0f);
}

void AspectRatioPatches::Init()
{
}

void AspectRatioPatches::ComputeOffsets()
{
    float width = Video::s_viewportWidth;
    float height = Video::s_viewportHeight;

    g_aspectRatio = width / height;
    g_aspectRatioGameplayScale = 1.0f;

    if (g_aspectRatio >= NARROW_ASPECT_RATIO)
    {
        g_aspectRatioOffsetX = (width - height * WIDE_ASPECT_RATIO) / 2.0f;
        g_aspectRatioOffsetY = 0.0f;
        g_aspectRatioScale = height / 720.0f;

        // keep same scale above Steam Deck aspect ratio
        if (g_aspectRatio < WIDE_ASPECT_RATIO)
        {
            // interpolate to original 4:3 scale
            float steamDeckScale = g_aspectRatio / WIDE_ASPECT_RATIO;
            float narrowScale = ComputeScale(NARROW_ASPECT_RATIO);
            float lerpFactor = std::clamp((g_aspectRatio - NARROW_ASPECT_RATIO) / (STEAM_DECK_ASPECT_RATIO - NARROW_ASPECT_RATIO), 0.0f, 1.0f);

            g_aspectRatioGameplayScale = narrowScale + (steamDeckScale - narrowScale) * lerpFactor;
        }
    }
    else
    {
        // 4:3 crop
        g_aspectRatioOffsetX = (width - width * NARROW_ASPECT_RATIO) / 2.0f;
        g_aspectRatioOffsetY = (height - width / NARROW_ASPECT_RATIO) / 2.0f;
        g_aspectRatioScale = width / 960.0f;
        g_aspectRatioGameplayScale = ComputeScale(NARROW_ASPECT_RATIO);
    }

    g_aspectRatioMultiplayerOffsetX = g_aspectRatioOffsetX / 2.0f;
    g_aspectRatioNarrowScale = std::clamp((g_aspectRatio - NARROW_ASPECT_RATIO) / (WIDE_ASPECT_RATIO - NARROW_ASPECT_RATIO), 0.0f, 1.0f);
    g_aspectRatioNarrowMargin = std::lerp(50.0f, 0.0f, g_aspectRatioNarrowScale);
    g_horzCentre = g_aspectRatioOffsetX + 640.0f * (1.0f - g_aspectRatioGameplayScale) * g_aspectRatioScale;
    g_vertCentre = g_aspectRatioOffsetY + 360.0f * (1.0f - g_aspectRatioGameplayScale) * g_aspectRatioScale;
    g_radarMapScale = 256 * g_aspectRatioScale * g_aspectRatioGameplayScale;
}
