#pragma once

// Flush-to-zero / denormals-are-zero control for the audio thread. Architecture-specific,
// not platform-specific: the same code is used on macOS and Windows.
#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#include <pmmintrin.h>
#define RADIFF_HAVE_SSE_DENORMAL_CONTROL 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define RADIFF_HAVE_ARM64_DENORMAL_CONTROL 1
#endif

#include <cstdint>

namespace radiff::core::dsp {

/// RAII guard: enables FTZ/DAZ for the current thread while alive, restoring the
/// previous mode on destruction. Cheap enough to use once per process() call.
class ScopedFlushDenormals {
public:
    ScopedFlushDenormals() noexcept {
#if defined(RADIFF_HAVE_SSE_DENORMAL_CONTROL)
        saved_ = _mm_getcsr();
        _mm_setcsr(saved_ | 0x8040u); // FTZ (bit 15) | DAZ (bit 6)
#elif defined(RADIFF_HAVE_ARM64_DENORMAL_CONTROL) && !defined(_MSC_VER)
        std::uint64_t fpcr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        saved_ = fpcr;
        fpcr |= (1ull << 24); // FZ
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif
    }
    ~ScopedFlushDenormals() noexcept {
#if defined(RADIFF_HAVE_SSE_DENORMAL_CONTROL)
        _mm_setcsr(saved_);
#elif defined(RADIFF_HAVE_ARM64_DENORMAL_CONTROL) && !defined(_MSC_VER)
        __asm__ __volatile__("msr fpcr, %0" : : "r"(saved_));
#endif
    }
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

private:
#if defined(RADIFF_HAVE_SSE_DENORMAL_CONTROL)
    unsigned int saved_ = 0;
#else
    std::uint64_t saved_ = 0;
#endif
};

/// Adds a negligible value to kill denormals in recursive filters where hardware FTZ
/// cannot be relied upon (MSVC ARM64 builds).
[[nodiscard]] inline double undenormal(double v) noexcept { return v + 1e-30 - 1e-30; }

} // namespace radiff::core::dsp
