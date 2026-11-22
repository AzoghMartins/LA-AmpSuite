#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace laas
{
/** Iterative in-place radix-2 complex FFT. Allocates only in prepare(). */
template <typename T>
class FFT
{
public:
    using Complex = std::complex<T>;

    void prepare (int size)
    {
        n = 1;
        log2n = 0;
        while (n < size)
        {
            n <<= 1;
            ++log2n;
        }
        twiddles.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
            twiddles[(size_t) i] = std::polar ((T) 1, (T) (-2.0 * 3.14159265358979323846 * i / n));
        bitrev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < log2n; ++b)
                if (i & (1 << b))
                    r |= 1 << (log2n - 1 - b);
            bitrev[(size_t) i] = r;
        }
    }

    int size() const noexcept { return n; }

    /** Forward transform (no scaling). */
    void forward (Complex* data) const noexcept { transform (data, false); }

    /** Inverse transform, scaled by 1/n. */
    void inverse (Complex* data) const noexcept
    {
        transform (data, true);
        const T s = (T) 1 / (T) n;
        for (int i = 0; i < n; ++i)
            data[i] *= s;
    }

private:
    void transform (Complex* d, bool inv) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = bitrev[(size_t) i];
            if (j > i)
                std::swap (d[i], d[j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    auto w = twiddles[(size_t) (j * step)];
                    if (inv)
                        w = std::conj (w);
                    const auto t = w * d[i + j + half];
                    const auto u = d[i + j];
                    d[i + j] = u + t;
                    d[i + j + half] = u - t;
                }
        }
    }

    int n = 0, log2n = 0;
    std::vector<Complex> twiddles;
    std::vector<int> bitrev;
};
} // namespace laas
