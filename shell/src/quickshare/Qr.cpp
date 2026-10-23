// QR codes after ISO/IEC 18004, the way Project Nayuki's generator lays
// them out: data and Reed–Solomon codewords interleaved in blocks, the
// function patterns, the codewords zig-zagged in, then the mask with the
// lowest penalty.
#include "quickshare/Qr.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>

namespace m3::quickshare::qr {

namespace {

// Error correction M, by version (index 0 unused)
constexpr int kEccPerBlock[41] = {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26,
                                  26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28};
constexpr int kBlocks[41] = {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16,
                             17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49};
constexpr int kFormatM = 0; // M's two format bits

int rawModules(int ver) {
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        const int align = ver / 7 + 2;
        result -= (25 * align - 10) * align - 55;
        if (ver >= 7) result -= 36;
    }
    return result;
}

int dataCodewords(int ver) { return rawModules(ver) / 8 - kEccPerBlock[ver] * kBlocks[ver]; }

uint8_t gfMul(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return static_cast<uint8_t>(z);
}

std::vector<uint8_t> rsDivisor(int degree) {
    std::vector<uint8_t> result(static_cast<size_t>(degree));
    result.back() = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (size_t j = 0; j < result.size(); ++j) {
            result[j] = gfMul(result[j], root);
            if (j + 1 < result.size()) result[j] ^= result[j + 1];
        }
        root = gfMul(root, 0x02);
    }
    return result;
}

std::vector<uint8_t> rsRemainder(const std::vector<uint8_t>& data, const std::vector<uint8_t>& divisor) {
    std::vector<uint8_t> result(divisor.size());
    for (uint8_t b : data) {
        const uint8_t factor = b ^ result.front();
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < result.size(); ++i) result[i] ^= gfMul(divisor[i], factor);
    }
    return result;
}

class Grid {
public:
    explicit Grid(int ver) : m_ver(ver), m_size(ver * 4 + 17), m_dark(static_cast<size_t>(m_size * m_size)), m_func(m_dark.size()) {}

    int size() const { return m_size; }
    bool dark(int x, int y) const { return m_dark[static_cast<size_t>(y * m_size + x)]; }

    void functionPatterns() {
        for (int i = 0; i < m_size; ++i) {
            set(6, i, i % 2 == 0);
            set(i, 6, i % 2 == 0);
        }
        finder(3, 3);
        finder(m_size - 4, 3);
        finder(3, m_size - 4);
        const auto pos = alignmentPositions();
        const int n = static_cast<int>(pos.size());
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                if (!((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))) alignment(pos[i], pos[j]);
        formatBits(0); // reserved now, written with the mask
        versionBits();
    }

    void codewords(const std::vector<uint8_t>& data) {
        size_t i = 0;
        for (int right = m_size - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;
            for (int vert = 0; vert < m_size; ++vert)
                for (int j = 0; j < 2; ++j) {
                    const int x = right - j;
                    const bool upward = ((right + 1) & 2) == 0;
                    const int y = upward ? m_size - 1 - vert : vert;
                    const size_t at = static_cast<size_t>(y * m_size + x);
                    if (!m_func[at] && i < data.size() * 8) {
                        m_dark[at] = (data[i >> 3] >> (7 - (i & 7))) & 1;
                        ++i;
                    }
                }
        }
    }

    void mask(int m) {
        for (int y = 0; y < m_size; ++y)
            for (int x = 0; x < m_size; ++x) {
                bool invert = false;
                switch (m) {
                case 0: invert = (x + y) % 2 == 0; break;
                case 1: invert = y % 2 == 0; break;
                case 2: invert = x % 3 == 0; break;
                case 3: invert = (x + y) % 3 == 0; break;
                case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                default: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                }
                const size_t at = static_cast<size_t>(y * m_size + x);
                if (invert && !m_func[at]) m_dark[at] = !m_dark[at];
            }
    }

    void formatBits(int maskId) {
        const int data = kFormatM << 3 | maskId;
        int rem = data;
        for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        const int bits = (data << 10 | rem) ^ 0x5412;
        const auto bit = [bits](int i) { return ((bits >> i) & 1) != 0; };
        for (int i = 0; i <= 5; ++i) set(8, i, bit(i));
        set(8, 7, bit(6));
        set(8, 8, bit(7));
        set(7, 8, bit(8));
        for (int i = 9; i < 15; ++i) set(14 - i, 8, bit(i));
        for (int i = 0; i < 8; ++i) set(m_size - 1 - i, 8, bit(i));
        for (int i = 8; i < 15; ++i) set(8, m_size - 15 + i, bit(i));
        set(8, m_size - 8, true); // always dark
    }

    // The standard's penalty, less its finder look-alike rule: runs, 2×2
    // boxes and the dark/light balance
    long penalty() const {
        long result = 0;
        const auto runs = [&](bool rows) {
            for (int a = 0; a < m_size; ++a) {
                bool colour = false;
                int run = 0;
                for (int b = 0; b < m_size; ++b) {
                    const bool d = rows ? dark(b, a) : dark(a, b);
                    if (b > 0 && d == colour) {
                        ++run;
                    } else {
                        colour = d;
                        run = 1;
                    }
                    if (run == 5) result += 3;
                    else if (run > 5) result += 1;
                }
            }
        };
        runs(true);
        runs(false);
        for (int y = 0; y + 1 < m_size; ++y)
            for (int x = 0; x + 1 < m_size; ++x) {
                const bool c = dark(x, y);
                if (c == dark(x + 1, y) && c == dark(x, y + 1) && c == dark(x + 1, y + 1)) result += 3;
            }
        const long total = static_cast<long>(m_size) * m_size;
        const long darkCount = std::count(m_dark.begin(), m_dark.end(), true);
        const long k = (std::labs(darkCount * 20 - total * 10) + total - 1) / total - 1;
        result += std::max(0L, k) * 10;
        return result;
    }

private:
    void set(int x, int y, bool d) {
        const size_t at = static_cast<size_t>(y * m_size + x);
        m_dark[at] = d;
        m_func[at] = true;
    }

    void finder(int x, int y) {
        for (int dy = -4; dy <= 4; ++dy)
            for (int dx = -4; dx <= 4; ++dx) {
                const int dist = std::max(std::abs(dx), std::abs(dy)), xx = x + dx, yy = y + dy;
                if (xx >= 0 && xx < m_size && yy >= 0 && yy < m_size) set(xx, yy, dist != 2 && dist != 4);
            }
    }

    void alignment(int x, int y) {
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) set(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }

    std::vector<int> alignmentPositions() const {
        if (m_ver == 1) return {};
        const int n = m_ver / 7 + 2;
        const int step = m_ver == 32 ? 26 : (m_ver * 4 + n * 2 + 1) / (n * 2 - 2) * 2;
        std::vector<int> result{6};
        for (int pos = m_size - 7; static_cast<int>(result.size()) < n; pos -= step) result.insert(result.begin() + 1, pos);
        return result;
    }

    void versionBits() {
        if (m_ver < 7) return;
        int rem = m_ver;
        for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        const long bits = static_cast<long>(m_ver) << 12 | rem;
        for (int i = 0; i < 18; ++i) {
            const bool d = ((bits >> i) & 1) != 0;
            const int a = m_size - 11 + i % 3, b = i / 3;
            set(a, b, d);
            set(b, a, d);
        }
    }

    int m_ver, m_size;
    std::vector<bool> m_dark, m_func;
};

} // namespace

std::vector<std::vector<bool>> encode(const std::string& text) {
    // The smallest version that holds it, in byte mode
    int ver = 1;
    for (; ver <= 40; ++ver) {
        const int countBits = ver < 10 ? 8 : 16;
        if (4 + countBits + static_cast<int>(text.size()) * 8 <= dataCodewords(ver) * 8 && text.size() < (1u << countBits)) break;
    }
    if (ver > 40) return {};

    // Mode, length, the bytes, a terminator, then padding
    std::vector<bool> bits;
    const auto put = [&bits](uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i) bits.push_back(((v >> i) & 1) != 0);
    };
    put(0x4, 4);
    put(static_cast<uint32_t>(text.size()), ver < 10 ? 8 : 16);
    for (unsigned char c : text) put(c, 8);
    const size_t capacity = static_cast<size_t>(dataCodewords(ver)) * 8;
    put(0, static_cast<int>(std::min<size_t>(4, capacity - bits.size())));
    put(0, static_cast<int>((8 - bits.size() % 8) % 8));
    for (uint8_t pad = 0xEC; bits.size() < capacity; pad ^= 0xEC ^ 0x11) put(pad, 8);
    std::vector<uint8_t> data(bits.size() / 8);
    for (size_t i = 0; i < bits.size(); ++i)
        if (bits[i]) data[i >> 3] |= static_cast<uint8_t>(1 << (7 - (i & 7)));

    // Split into blocks, each with its error correction, then interleave
    const int blocks = kBlocks[ver], eccLen = kEccPerBlock[ver];
    const int raw = rawModules(ver) / 8;
    const int shortBlocks = blocks - raw % blocks, shortLen = raw / blocks;
    const auto divisor = rsDivisor(eccLen);
    std::vector<std::vector<uint8_t>> parts;
    for (int i = 0, k = 0; i < blocks; ++i) {
        const int len = shortLen - eccLen + (i < shortBlocks ? 0 : 1);
        std::vector<uint8_t> block(data.begin() + k, data.begin() + k + len);
        k += len;
        const auto ecc = rsRemainder(block, divisor);
        if (i < shortBlocks) block.push_back(0); // a gap, skipped below
        block.insert(block.end(), ecc.begin(), ecc.end());
        parts.push_back(std::move(block));
    }
    std::vector<uint8_t> all;
    for (size_t i = 0; i < parts[0].size(); ++i)
        for (int j = 0; j < blocks; ++j)
            if (static_cast<int>(i) != shortLen - eccLen || j >= shortBlocks) all.push_back(parts[static_cast<size_t>(j)][i]);

    // Lay it out, and keep the mask that reads best
    Grid best(ver);
    long bestPenalty = LONG_MAX;
    for (int m = 0; m < 8; ++m) {
        Grid g(ver);
        g.functionPatterns();
        g.codewords(all);
        g.mask(m);
        g.formatBits(m);
        const long p = g.penalty();
        if (p < bestPenalty) {
            bestPenalty = p;
            best = g;
        }
    }
    std::vector<std::vector<bool>> out(static_cast<size_t>(best.size()), std::vector<bool>(static_cast<size_t>(best.size())));
    for (int y = 0; y < best.size(); ++y)
        for (int x = 0; x < best.size(); ++x) out[static_cast<size_t>(y)][static_cast<size_t>(x)] = best.dark(x, y);
    return out;
}

} // namespace m3::quickshare::qr
