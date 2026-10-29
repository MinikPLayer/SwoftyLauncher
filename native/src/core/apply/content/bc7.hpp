#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

// BC7 encoder for logo slices: modes 5 and 6 only, block for block the same as the offline pipeline of Classic
// Seasons (addons/rnd-tyres-thermal/tools/bc7enc.py, then the mode 5 pass of classic-seasons/tools/logos.py). The
// sums add in numpy's order (one at a time, or eight lanes for sixteen values) and the file builds without fused
// multiply-add, so the output equals the Python bytes.

namespace swofty_core::apply::content::bc7 {

using Block = int64_t[16][4];

inline constexpr int W4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
inline constexpr int W2[4] = {0, 21, 43, 64};

inline double pairwise16(const double *a)
{
    double r[8];
    for (int j = 0; j < 8; j++)
        r[j] = a[j];
    for (int j = 0; j < 8; j++)
        r[j] += a[8 + j];
    return ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
}

inline int64_t rint_to(double v, int64_t lo, int64_t hi)
{
    double r = std::nearbyint(v);
    if (!(r > double(lo)))
        return lo;
    if (r > double(hi))
        return hi;
    return int64_t(r);
}

class Bits {
public:
    void put(uint64_t v, int n)
    {
        v &= (n >= 64) ? ~0ull : ((1ull << n) - 1);
        for (int i = 0; i < n; i++, pos_++)
            if ((v >> i) & 1)
                word_[pos_ >> 3] |= uint8_t(1u << (pos_ & 7));
    }
    int pos() const { return pos_; }
    const uint8_t *bytes() const { return word_; }

private:
    uint8_t word_[16] = {};
    int pos_ = 0;
};

inline int interp(int a, int b, int w) { return ((64 - w) * a + w * b + 32) >> 6; }

inline void endpoint(const int64_t c[4], int64_t q[4], int &p, int64_t val[4])
{
    int64_t best_err = -1;
    for (int pb = 0; pb < 2; pb++) {
        int64_t qq[4], vv[4], err = 0;
        for (int k = 0; k < 4; k++) {
            qq[k] = std::min<int64_t>(127, std::max<int64_t>(0, (c[k] - pb + 1) >> 1));
            vv[k] = (qq[k] << 1) | pb;
            err += (vv[k] - c[k]) * (vv[k] - c[k]);
        }
        if (best_err < 0 || err < best_err) {
            best_err = err;
            p = pb;
            for (int k = 0; k < 4; k++)
                q[k] = qq[k], val[k] = vv[k];
        }
    }
}

inline void pack6(const int64_t e0[4], const int64_t e1[4], int p0, int p1, const int ix[16], uint8_t out[16])
{
    Bits b;
    b.put(1ull << 6, 7);
    for (int c = 0; c < 4; c++) {
        b.put(uint64_t(e0[c]), 7);
        b.put(uint64_t(e1[c]), 7);
    }
    b.put(uint64_t(p0), 1);
    b.put(uint64_t(p1), 1);
    b.put(uint64_t(ix[0]), 3);
    for (int i = 1; i < 16; i++)
        b.put(uint64_t(ix[i]), 4);
    std::memcpy(out, b.bytes(), 16);
}

inline void flat_block(const Block px, uint8_t out[16])
{
    int lo = 0, hi = 0;
    for (int i = 1; i < 16; i++) {
        if (px[i][3] < px[lo][3])
            lo = i;
        if (px[i][3] > px[hi][3])
            hi = i;
    }
    int64_t q0[4], q1[4], v0[4], v1[4];
    int p0 = 0, p1 = 0;
    endpoint(px[lo], q0, p0, v0);
    endpoint(px[hi], q1, p1, v1);
    int pal[16][4];
    for (int i = 0; i < 16; i++)
        for (int c = 0; c < 4; c++)
            pal[i][c] = interp(int(v0[c]), int(v1[c]), W4[i]);
    int ix[16];
    for (int t = 0; t < 16; t++) {
        int64_t best = -1;
        int pick = 0;
        for (int i = 0; i < 16; i++) {
            int64_t e = 0;
            for (int c = 0; c < 4; c++) {
                int64_t d = pal[i][c] - px[t][c];
                e += d * d * (c == 3 ? 4 : 1);
            }
            if (best < 0 || e < best) {
                best = e;
                pick = i;
            }
        }
        ix[t] = pick;
    }
    if (ix[0] >= 8) {
        std::swap_ranges(q0, q0 + 4, q1);
        std::swap(p0, p1);
        for (int &i : ix)
            i = 15 - i;
    }
    pack6(q0, q1, p0, p1, ix, out);
}

struct Quant {
    int64_t q[4], v[4];
    int p;
};

inline Quant quantise(const double ep[4])
{
    int64_t c[4];
    for (int k = 0; k < 4; k++)
        c[k] = rint_to(ep[k], 0, 255);
    Quant best{};
    double best_err = 0;
    for (int pb = 0; pb < 2; pb++) {
        Quant cur{};
        cur.p = pb;
        double err = 0;
        for (int k = 0; k < 4; k++) {
            cur.q[k] = std::min<int64_t>(127, std::max<int64_t>(0, (c[k] - pb + 1) >> 1));
            cur.v[k] = (cur.q[k] << 1) | pb;
            double d = double(cur.v[k]) - ep[k];
            err += d * d;
        }
        if (pb == 0 || err < best_err) {
            best = cur;
            best_err = err;
        }
    }
    return best;
}

inline double assign(const Quant &a, const Quant &b, const Block px, const double wt[16][4], int idx[16])
{
    int pal[16][4];
    for (int i = 0; i < 16; i++)
        for (int c = 0; c < 4; c++)
            pal[i][c] = ((64 - W4[i]) * int(a.v[c]) + W4[i] * int(b.v[c]) + 32) >> 6;
    double per[16];
    for (int t = 0; t < 16; t++) {
        double best = 0;
        int pick = 0;
        for (int i = 0; i < 16; i++) {
            double e = 0;
            for (int c = 0; c < 4; c++) {
                double d = double(int64_t(pal[i][c]) - px[t][c]);
                e += (d * d) * wt[t][c];
            }
            if (i == 0 || e < best) {
                best = e;
                pick = i;
            }
        }
        idx[t] = pick;
        per[t] = best;
    }
    return pairwise16(per);
}

inline void fit(const double src[16][4], const double wt[16][4], const int idx[16], double e0[4], double e1[4])
{
    double f[16], g[16];
    for (int t = 0; t < 16; t++) {
        f[t] = double(W4[idx[t]]) / 64.0;
        g[t] = 1.0 - f[t];
    }
    for (int c = 0; c < 4; c++) {
        double a = 0, b = 0, cc = 0, x0 = 0, x1 = 0;
        for (int t = 0; t < 16; t++) {
            a += wt[t][c] * (g[t] * g[t]);
            b += wt[t][c] * (g[t] * f[t]);
            cc += wt[t][c] * (f[t] * f[t]);
            x0 += (wt[t][c] * g[t]) * src[t][c];
            x1 += (wt[t][c] * f[t]) * src[t][c];
        }
        double det = a * cc - b * b;
        bool ok = std::fabs(det) > 1e-9;
        double flat = (x0 + x1) / std::max(a + 2 * b + cc, 1e-9);
        double r0 = ok ? (cc * x0 - b * x1) / det : flat;
        double r1 = ok ? (a * x1 - b * x0) / det : flat;
        e0[c] = std::min(255.0, std::max(0.0, r0));
        e1[c] = std::min(255.0, std::max(0.0, r1));
    }
}

struct Cand {
    Quant a, b;
    int idx[16];
    double err;
};

inline Cand candidate(const Block px, const double src[16][4], const double wt[16][4], const double s0[4], const double s1[4])
{
    Cand cur{};
    cur.a = quantise(s0);
    cur.b = quantise(s1);
    cur.err = assign(cur.a, cur.b, px, wt, cur.idx);
    for (int it = 0; it < 4; it++) {
        double f0[4], f1[4];
        fit(src, wt, cur.idx, f0, f1);
        Quant r0 = quantise(f0), r1 = quantise(f1);
        int ridx[16];
        double rerr = assign(r0, r1, px, wt, ridx);
        if (rerr < cur.err) {
            cur.a = r0;
            cur.b = r1;
            cur.err = rerr;
            std::memcpy(cur.idx, ridx, sizeof ridx);
        }
    }
    return cur;
}

inline void normalise(double *v, int n)
{
    double s = 0;
    for (int i = 0; i < n; i++)
        s += v[i] * v[i];
    double norm = std::max(std::sqrt(s), 1e-12);
    for (int i = 0; i < n; i++)
        v[i] /= norm;
}

inline void colour_block(const Block px, uint8_t out[16])
{
    double src[16][4], wt[16][4], sw[16][4];
    for (int t = 0; t < 16; t++)
        for (int c = 0; c < 4; c++)
            src[t][c] = double(px[t][c]);
    for (int t = 0; t < 16; t++)
        for (int c = 0; c < 4; c++) {
            wt[t][c] = c < 3 ? src[t][3] / 255.0 : 4.0;
            sw[t][c] = std::sqrt(wt[t][c]);
        }
    double mean[4], swm[4];
    for (int c = 0; c < 4; c++) {
        double s = 0, w = 0, m = 0;
        for (int t = 0; t < 16; t++) {
            s += src[t][c] * wt[t][c];
            w += wt[t][c];
            m += sw[t][c];
        }
        mean[c] = s / w;
        swm[c] = m / 16.0;
    }
    double cov[4][4] = {};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0;
            for (int t = 0; t < 16; t++)
                s += ((src[t][i] - mean[i]) * sw[t][i]) * ((src[t][j] - mean[j]) * sw[t][j]);
            cov[i][j] = s;
        }
    double axis[4] = {0.5, 0.5, 0.5, 0.5};
    for (int it = 0; it < 16; it++) {
        double nx[4];
        for (int i = 0; i < 4; i++) {
            double s = 0;
            for (int j = 0; j < 4; j++)
                s += cov[i][j] * axis[j];
            nx[i] = s;
        }
        normalise(nx, 4);
        std::memcpy(axis, nx, sizeof nx);
    }
    for (int c = 0; c < 4; c++)
        axis[c] /= std::max(swm[c], 1e-3);
    normalise(axis, 4);
    double tmin = 0, tmax = 0;
    for (int t = 0; t < 16; t++) {
        double s = 0;
        for (int c = 0; c < 4; c++)
            s += (src[t][c] - mean[c]) * axis[c];
        if (t == 0 || s < tmin)
            tmin = s;
        if (t == 0 || s > tmax)
            tmax = s;
    }
    double seeds[3][2][4];
    int lo = 0, hi = 0;
    for (int t = 1; t < 16; t++) {
        if (src[t][3] < src[lo][3])
            lo = t;
        if (src[t][3] > src[hi][3])
            hi = t;
    }
    for (int c = 0; c < 4; c++) {
        double mn = src[0][c], mx = src[0][c];
        for (int t = 1; t < 16; t++) {
            mn = std::min(mn, src[t][c]);
            mx = std::max(mx, src[t][c]);
        }
        seeds[0][0][c] = mean[c] + tmin * axis[c];
        seeds[0][1][c] = mean[c] + tmax * axis[c];
        seeds[1][0][c] = mn;
        seeds[1][1][c] = mx;
        seeds[2][0][c] = src[lo][c];
        seeds[2][1][c] = src[hi][c];
    }
    Cand best{};
    for (int s = 0; s < 3; s++) {
        Cand c = candidate(px, src, wt, seeds[s][0], seeds[s][1]);
        if (s == 0 || c.err < best.err)
            best = c;
    }
    int ix[16];
    std::memcpy(ix, best.idx, sizeof ix);
    int64_t e0[4], e1[4];
    int p0 = best.a.p, p1 = best.b.p;
    for (int c = 0; c < 4; c++)
        e0[c] = best.a.q[c], e1[c] = best.b.q[c];
    if (ix[0] >= 8) {
        std::swap_ranges(e0, e0 + 4, e1);
        std::swap(p0, p1);
        for (int &i : ix)
            i = 15 - i;
    }
    pack6(e0, e1, p0, p1, ix, out);
}

inline void bleed(Block px)
{
    int64_t tot = 0, mean[3] = {0, 0, 0};
    for (int t = 0; t < 16; t++) {
        tot += px[t][3];
        for (int c = 0; c < 3; c++)
            mean[c] += px[t][c] * px[t][3];
    }
    if (tot <= 0)
        return;
    for (int c = 0; c < 3; c++)
        mean[c] = (mean[c] + tot / 2) / tot;
    for (int t = 0; t < 16; t++)
        if (px[t][3] == 0)
            for (int c = 0; c < 3; c++)
                px[t][c] = mean[c];
}

inline void gather(const uint8_t *rgba, int w, int bx, int by, Block px)
{
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            for (int c = 0; c < 4; c++)
                px[y * 4 + x][c] = rgba[(size_t(by * 4 + y) * w + size_t(bx * 4 + x)) * 4 + c];
}

inline void decode6(const uint8_t b[16], Block out)
{
    auto bit = [&](int pos) { return (b[pos >> 3] >> (pos & 7)) & 1; };
    auto get = [&](int pos, int n) {
        int v = 0;
        for (int i = 0; i < n; i++)
            v |= bit(pos + i) << i;
        return v;
    };
    int ep[2][4];
    int pos = 7;
    for (int c = 0; c < 4; c++)
        for (int e = 0; e < 2; e++, pos += 7)
            ep[e][c] = get(pos, 7);
    for (int e = 0; e < 2; e++, pos++)
        for (int c = 0; c < 4; c++)
            ep[e][c] = (ep[e][c] << 1) | bit(pos);
    for (int t = 0; t < 16; t++) {
        int n = t == 0 ? 3 : 4;
        int ix = get(pos, n);
        pos += n;
        for (int c = 0; c < 4; c++)
            out[t][c] = interp(ep[0][c], ep[1][c], W4[ix]);
    }
}

struct Mode5 {
    int64_t c0[3], c1[3];
    int64_t a0, a1;
    int cidx[16], aidx[16];
};

inline double block_error(const Block px, const Block dec)
{
    double per[16];
    for (int t = 0; t < 16; t++) {
        double rgb = 0;
        for (int c = 0; c < 3; c++) {
            double d = double(px[t][c] - dec[t][c]);
            rgb += d * d;
        }
        double da = double(px[t][3] - dec[t][3]);
        per[t] = rgb * double(px[t][3]) / 255 + 4 * (da * da);
    }
    return pairwise16(per);
}

inline double mode5_candidate(const Block px, const double src[16][3], const double wt[16], Mode5 &out)
{
    double mean[3];
    double wsum = pairwise16(wt);
    for (int c = 0; c < 3; c++) {
        double s = 0;
        for (int t = 0; t < 16; t++)
            s += src[t][c] * wt[t];
        mean[c] = s / std::max(wsum, 1e-9);
    }
    double cov[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            double s = 0;
            for (int t = 0; t < 16; t++)
                s += ((src[t][i] - mean[i]) * std::sqrt(wt[t])) * ((src[t][j] - mean[j]) * std::sqrt(wt[t]));
            cov[i][j] = s;
        }
    double axis[3] = {1, 1, 1};
    for (int it = 0; it < 8; it++) {
        double nx[3];
        for (int i = 0; i < 3; i++) {
            double s = 0;
            for (int j = 0; j < 3; j++)
                s += cov[i][j] * axis[j];
            nx[i] = s;
        }
        normalise(nx, 3);
        std::memcpy(axis, nx, sizeof nx);
    }
    double lo = INFINITY, hi = -INFINITY;
    for (int t = 0; t < 16; t++) {
        double s = 0;
        for (int c = 0; c < 3; c++)
            s += (src[t][c] - mean[c]) * axis[c];
        if (wt[t] > 0) {
            lo = std::min(lo, s);
            hi = std::max(hi, s);
        }
    }
    if (!std::isfinite(lo))
        lo = 0;
    if (!std::isfinite(hi))
        hi = 0;
    double ends[2][3];
    for (int c = 0; c < 3; c++) {
        ends[0][c] = mean[c] + lo * axis[c];
        ends[1][c] = mean[c] + hi * axis[c];
    }
    bool have = false;
    double best_err = 0;
    int64_t bq[2][3];
    int bidx[16];
    for (int round = 0; round < 3; round++) {
        int64_t q[2][3];
        int64_t v[2][3];
        for (int e = 0; e < 2; e++)
            for (int c = 0; c < 3; c++) {
                q[e][c] = rint_to(ends[e][c] / 255 * 127, 0, 127);
                v[e][c] = (q[e][c] << 1) | (q[e][c] >> 6);
            }
        int64_t pal[4][3];
        for (int i = 0; i < 4; i++)
            for (int c = 0; c < 3; c++)
                pal[i][c] = ((64 - W2[i]) * v[0][c] + W2[i] * v[1][c] + 32) >> 6;
        int idx[16];
        double per[16];
        for (int t = 0; t < 16; t++) {
            int64_t best = -1;
            int pick = 0;
            for (int i = 0; i < 4; i++) {
                int64_t e = 0;
                for (int c = 0; c < 3; c++) {
                    int64_t d = pal[i][c] - px[t][c];
                    e += d * d;
                }
                if (best < 0 || e < best) {
                    best = e;
                    pick = i;
                }
            }
            idx[t] = pick;
            per[t] = double(best) * wt[t];
        }
        double err = pairwise16(per);
        if (!have || err < best_err) {
            have = true;
            best_err = err;
            std::memcpy(bq, q, sizeof q);
            std::memcpy(bidx, idx, sizeof idx);
        }
        double f[16], g[16];
        for (int t = 0; t < 16; t++) {
            f[t] = double(W2[bidx[t]]) / 64;
            g[t] = 1 - f[t];
        }
        double pa[16], pb[16], pc[16];
        for (int t = 0; t < 16; t++) {
            pa[t] = (wt[t] * g[t]) * g[t];
            pb[t] = (wt[t] * g[t]) * f[t];
            pc[t] = (wt[t] * f[t]) * f[t];
        }
        double a = pairwise16(pa), b = pairwise16(pb), cc = pairwise16(pc);
        double det = a * cc - b * b;
        bool ok = std::fabs(det) > 1e-9;
        double den = std::max(a + 2 * b + cc, 1e-9);
        for (int c = 0; c < 3; c++) {
            double x0 = 0, x1 = 0;
            for (int t = 0; t < 16; t++) {
                x0 += (wt[t] * g[t]) * src[t][c];
                x1 += (wt[t] * f[t]) * src[t][c];
            }
            double flat = (x0 + x1) / den;
            ends[0][c] = ok ? (cc * x0 - b * x1) / det : flat;
            ends[1][c] = ok ? (a * x1 - b * x0) / det : flat;
        }
    }
    for (int c = 0; c < 3; c++)
        out.c0[c] = bq[0][c], out.c1[c] = bq[1][c];
    std::memcpy(out.cidx, bidx, sizeof bidx);
    int64_t a0 = px[0][3], a1 = px[0][3];
    for (int t = 1; t < 16; t++) {
        a0 = std::min(a0, px[t][3]);
        a1 = std::max(a1, px[t][3]);
    }
    out.a0 = a0;
    out.a1 = a1;
    int64_t apal[4];
    for (int i = 0; i < 4; i++)
        apal[i] = ((64 - W2[i]) * a0 + W2[i] * a1 + 32) >> 6;
    int64_t cpal[4][3];
    for (int i = 0; i < 4; i++)
        for (int c = 0; c < 3; c++) {
            int64_t e0 = (out.c0[c] << 1) | (out.c0[c] >> 6), e1 = (out.c1[c] << 1) | (out.c1[c] >> 6);
            cpal[i][c] = ((64 - W2[i]) * e0 + W2[i] * e1 + 32) >> 6;
        }
    Block dec;
    for (int t = 0; t < 16; t++) {
        int64_t best = -1;
        int pick = 0;
        for (int i = 0; i < 4; i++) {
            int64_t e = std::llabs(apal[i] - px[t][3]);
            if (best < 0 || e < best) {
                best = e;
                pick = i;
            }
        }
        out.aidx[t] = pick;
        for (int c = 0; c < 3; c++)
            dec[t][c] = cpal[out.cidx[t]][c];
        dec[t][3] = apal[pick];
    }
    return block_error(px, dec);
}

inline void pack5(Mode5 m, uint8_t out[16])
{
    if (m.cidx[0] >= 2) {
        for (int c = 0; c < 3; c++)
            std::swap(m.c0[c], m.c1[c]);
        for (int &i : m.cidx)
            i = 3 - i;
    }
    if (m.aidx[0] >= 2) {
        std::swap(m.a0, m.a1);
        for (int &i : m.aidx)
            i = 3 - i;
    }
    Bits b;
    b.put(1u << 5, 6);
    b.put(0, 2);
    for (int c = 0; c < 3; c++) {
        b.put(uint64_t(m.c0[c]), 7);
        b.put(uint64_t(m.c1[c]), 7);
    }
    b.put(uint64_t(m.a0), 8);
    b.put(uint64_t(m.a1), 8);
    b.put(uint64_t(m.cidx[0]), 1);
    for (int i = 1; i < 16; i++)
        b.put(uint64_t(m.cidx[i]), 2);
    b.put(uint64_t(m.aidx[0]), 1);
    for (int i = 1; i < 16; i++)
        b.put(uint64_t(m.aidx[i]), 2);
    std::memcpy(out, b.bytes(), 16);
}

inline std::string encode_rgba(const uint8_t *rgba, int w, int h)
{
    std::string out(size_t(w / 4) * size_t(h / 4) * 16, '\0');
    for (int by = 0; by < h / 4; by++)
        for (int bx = 0; bx < w / 4; bx++) {
            Block px;
            gather(rgba, w, bx, by, px);
            Block bled;
            std::memcpy(bled, px, sizeof px);
            bleed(bled);
            bool flat = true;
            for (int t = 1; t < 16 && flat; t++)
                for (int c = 0; c < 3; c++)
                    if (bled[t][c] != bled[0][c])
                        flat = false;
            uint8_t word[16];
            if (flat)
                flat_block(bled, word);
            else
                colour_block(bled, word);
            std::memcpy(&out[(size_t(by) * size_t(w / 4) + size_t(bx)) * 16], word, 16);
        }
    return out;
}

inline std::string encode_logo(const uint8_t *source, int w, int h)
{
    std::string clean(reinterpret_cast<const char *>(source), size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < clean.size(); i += 4)
        if (!clean[i + 3])
            clean[i] = clean[i + 1] = clean[i + 2] = 0;
    const uint8_t *rgba = reinterpret_cast<const uint8_t *>(clean.data());
    std::string out = encode_rgba(rgba, w, h);
    for (int by = 0; by < h / 4; by++)
        for (int bx = 0; bx < w / 4; bx++) {
            Block px;
            gather(rgba, w, bx, by, px);
            uint8_t *word = reinterpret_cast<uint8_t *>(&out[(size_t(by) * size_t(w / 4) + size_t(bx)) * 16]);
            Block dec;
            decode6(word, dec);
            double stay = block_error(px, dec);
            double src[16][3], wt[16];
            for (int t = 0; t < 16; t++) {
                for (int c = 0; c < 3; c++)
                    src[t][c] = double(px[t][c]);
                wt[t] = double(px[t][3]) / 255;
            }
            Mode5 m{};
            double err = mode5_candidate(px, src, wt, m);
            if (err < stay)
                pack5(m, word);
        }
    return out;
}

}
