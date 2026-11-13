// SPDX-License-Identifier: MIT
// Read-only check of one GPU scanout copy (GpuScanoutClient.h): does the scanout hold what the GPU's kernel was asked to store?
// The contract is scanout_opaque's in shaders/scanout_convert.ll as the scaled build has it (patch-gpu-scanout-scale.py):
//     shown[y * shownPitch + x] = source[floor(y * sh / dh) * sourcePitch + floor(x * sw / dw)] | 0xff000000
// for every x < dw, y < dh, with 32-bit unsigned products (both extents are at most 16384, so x * sw < 2^28). Equal extents are the
// same formula. Nothing is written, rendered or copied: aligned 32-bit volatile loads only (the scanout is device memory, where wider
// or unaligned loads can fault), and every index is proved inside its buffer before the first load.
// No Metal and no Darwin header: the host oracle (test_scanout_verify.cpp) compiles this file as it is.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace gpuscanout {

struct VerifyView {
    const volatile uint32_t *words;   // first row; 4-byte aligned
    uint64_t bytes;                   // readable bytes from `words`
    uint64_t stride;                  // bytes per row
    uint32_t width, height;           // pixels
};

enum VerifyStatus : uint32_t {
    kVerifyCompared = 0,      // every row below `rows` was compared
    kVerifyNoBuffer = 1,      // a null or unaligned base
    kVerifyBadExtent = 2,     // a zero extent or one above 16384
    kVerifyBadStride = 3,     // a row pitch that is not whole words, or shorter than a row
    kVerifyShortBuffer = 4,   // the last row does not fit in the readable bytes
};

struct VerifyResult {
    uint32_t status;          // VerifyStatus; nothing was read unless it is kVerifyCompared
    uint32_t rows;            // scanout rows compared
    uint32_t colourRows;      // rows with a pixel whose B, G or R differs from the mapped source pixel
    uint32_t alphaRows;       // rows with a pixel whose alpha is not 0xff (the kernel stores 0xff whatever the source holds)
    uint32_t firstRow, lastRow;   // the first and last differing row (colour or alpha); meaningful when colourRows + alphaRows > 0
    uint32_t firstColumn;     // the first differing column of firstRow
    bool uniform;             // every compared scanout pixel had the same B, G and R: an agreeing blank frame proves little
    bool alphaIsSource;       // every non-0xff alpha equalled the source pixel's (a plain blit, not the kernel, made the copy)
};

inline const uint32_t kVerifyMaxExtent = 16384;   // the presenter's own bound (validExtent / validScanout)

// Whether `view` is a buffer the comparison may read: status kVerifyCompared, or why not.
inline uint32_t verifyViewStatus(const VerifyView &view)
{
    if (!view.words || ((uintptr_t)view.words & 3u)) return kVerifyNoBuffer;
    if (!view.width || !view.height || view.width > kVerifyMaxExtent || view.height > kVerifyMaxExtent) return kVerifyBadExtent;
    const uint64_t row = (uint64_t)view.width * 4u;
    // Whole words per row; bounded so that stride * height cannot wrap (16384 rows of at most 2^32 bytes).
    if ((view.stride & 3u) || view.stride < row || view.stride > UINT32_MAX) return kVerifyBadStride;
    // The last row ends at stride * (height - 1) + width * 4: at most 2^46, no overflow.
    if (view.stride * (uint64_t)(view.height - 1) + row > view.bytes) return kVerifyShortBuffer;
    return kVerifyCompared;
}

// Compares the scanout's first `rowLimit` rows (0: all of them) with the source under the contract above.
inline VerifyResult verifyScaledOpaque(const VerifyView &source, const VerifyView &shown, uint32_t rowLimit)
{
    VerifyResult result = {};
    result.uniform = true;
    result.alphaIsSource = true;
    result.status = verifyViewStatus(source);
    if (result.status == kVerifyCompared) result.status = verifyViewStatus(shown);
    if (result.status != kVerifyCompared) {result.uniform = false; result.alphaIsSource = false; return result;}
    const uint32_t sw = source.width, sh = source.height, dw = shown.width, dh = shown.height;
    const uint32_t rows = rowLimit && rowLimit < dh ? rowLimit : dh;
    const size_t sourcePitch = (size_t)(source.stride / 4u), shownPitch = (size_t)(shown.stride / 4u);
    uint32_t first = 0;
    bool haveFirst = false;
    for (uint32_t y = 0; y < rows; ++y) {
        // floor(y * sh / dh) < sh because y < dh; the product is below 2^28.
        const uint32_t sy = (uint32_t)(y * sh) / dh;
        const volatile uint32_t *a = shown.words + (size_t)y * shownPitch;
        const volatile uint32_t *b = source.words + (size_t)sy * sourcePitch;
        bool colour = false, alpha = false;
        for (uint32_t x = 0; x < dw; ++x) {
            const uint32_t sx = (uint32_t)(x * sw) / dw;   // < sw because x < dw
            const uint32_t have = a[x], from = b[sx];
            if (!haveFirst) {first = have & 0x00ffffffu; haveFirst = true;}
            else if ((have & 0x00ffffffu) != first) result.uniform = false;
            const bool differs = ((have ^ from) & 0x00ffffffu) != 0;
            const bool translucent = (have >> 24) != 0xffu;
            if (translucent && (have >> 24) != (from >> 24)) result.alphaIsSource = false;
            if ((differs || translucent) && !colour && !alpha) {
                if (!result.colourRows && !result.alphaRows) {result.firstRow = y; result.firstColumn = x;}
                result.lastRow = y;
            }
            colour = colour || differs;
            alpha = alpha || translucent;
        }
        if (colour) ++result.colourRows;
        if (alpha) ++result.alphaRows;
    }
    result.rows = rows;
    if (!result.alphaRows) result.alphaIsSource = false;   // no translucent pixel: the kernel's alpha, not a blit's
    return result;
}

}  // namespace gpuscanout
