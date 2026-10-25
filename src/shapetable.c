#include <stdlib.h>
#include "shapetable.h"
#include "util.h"

#define BAND 16     /* half-width in m and r of the columns computed around a missing one */

static int gidx(const ShapeTable *t, int m, int r) { return m * (t->N + 2) + r; }
static int pidx(int m, int r) { return m * (m + 3) / 2 + r; }     /* r <= m + 1 */
static int plane_size(int N) { return (N + 1) * (N + 4) / 2; }
static int grid_size(int N) { return (N + 1) * (N + 2); }

static mpz_t *new_mpz(int n)
{
    mpz_t *v = xmalloc((size_t)(n > 0 ? n : 1) * sizeof *v);
    for (int i = 0; i < n; i++) mpz_init(v[i]);
    return v;
}

static void free_mpz(mpz_t *v, int n)
{
    if (!v) return;
    for (int i = 0; i < n; i++) mpz_clear(v[i]);
    free(v);
}

void stab_init(ShapeTable *t, int sigma)
{
    t->sigma = sigma;
    t->N = -1;
    t->bell = t->pw = t->off = NULL;
    t->R = NULL;
    t->col = NULL;
    t->want = NULL;
    t->plane[0] = t->plane[1] = NULL;
    t->have_off = 0;
    mpz_init(t->zero);
    mpz_init_set_ui(t->one, 1);
    mpz_init(t->t1);
    mpz_init(t->t2);
    mpz_init(t->t3);
    t->perm = NULL;
    if (sigma >= 0) {
        t->perm = new_mpz(sigma + 1);
        mpz_set_ui(t->perm[0], 1);
        for (int k = 1; k <= sigma; k++) mpz_mul_ui(t->perm[k], t->perm[k - 1], (unsigned long)(sigma - k + 1));
    }
}

mpz_srcptr stab_base(const ShapeTable *t, int k)
{
    if (t->sigma < 0) return t->one;
    return k <= t->sigma ? t->perm[k] : t->zero;
}

void stab_forget(ShapeTable *t)
{
    if (!t->col) return;
    for (int i = 0; i < grid_size(t->N); i++) {
        free_mpz(t->col[i].v, t->col[i].len);
        t->col[i].v = NULL;
        t->col[i].len = 0;
    }
}

static void free_sizes(ShapeTable *t)
{
    stab_forget(t);
    free(t->col);
    free(t->want);
    free_mpz(t->bell, t->N + 1);
    free_mpz(t->pw, t->N + 1);
    if (t->R) for (int k = 0; k <= t->N + 1; k++) free_mpz(t->R[k], t->N + 1);
    free(t->R);
    free_mpz(t->plane[0], plane_size(t->N));
    free_mpz(t->plane[1], plane_size(t->N));
    free_mpz(t->off, t->N + 2);
    t->col = NULL;
    t->want = NULL;
    t->bell = t->pw = t->off = NULL;
    t->R = NULL;
    t->plane[0] = t->plane[1] = NULL;
    t->have_off = 0;
}

/* Bell numbers 0..N by the Bell triangle */
static void bell_numbers(mpz_t *bell, int N)
{
    mpz_t *old = new_mpz(N + 1), *row = new_mpz(N + 1);
    mpz_set_ui(bell[0], 1);
    mpz_set_ui(old[0], 1);
    for (int n = 1; n <= N; n++) {
        mpz_set(row[0], old[n - 1]);
        for (int i = 1; i <= n; i++) mpz_add(row[i], row[i - 1], old[i - 1]);
        mpz_set(bell[n], row[0]);
        mpz_t *s = old; old = row; row = s;
    }
    free_mpz(old, N + 1);
    free_mpz(row, N + 1);
}

void stab_ensure(ShapeTable *t, int N)
{
    if (N <= t->N) return;
    int step = t->N / 4 < 8 ? 8 : t->N / 4;     /* grow in steps: a pass costs O(N^3) */
    if (N < t->N + step) N = t->N + step;
    if (t->N >= 0) free_sizes(t);
    t->N = N;
    if (t->sigma < 0) {
        t->bell = new_mpz(N + 1);
        bell_numbers(t->bell, N);
    } else {
        t->pw = new_mpz(N + 1);
        mpz_set_ui(t->pw[0], 1);
        for (int a = 1; a <= N; a++) mpz_mul_ui(t->pw[a], t->pw[a - 1], (unsigned long)t->sigma);
    }
    t->R = xcalloc((size_t)(N + 2), sizeof *t->R);
    t->col = xcalloc((size_t)grid_size(N), sizeof *t->col);
    t->want = xmalloc((size_t)grid_size(N) * sizeof *t->want);
    for (int i = 0; i < grid_size(N); i++) t->want[i] = -1;
    t->plane[0] = new_mpz(plane_size(N));
    t->plane[1] = new_mpz(plane_size(N));
    t->off = new_mpz(N + 2);
    t->have_off = 0;
}

/* the column R(., k) */
static const mpz_t *column_R(ShapeTable *t, int k)
{
    if (t->R[k]) return t->R[k];
    int N = t->N;
    mpz_t *v = t->R[k] = new_mpz(N + 1);
    if (t->sigma >= 0) {
        if (k <= t->sigma)
            for (int a = 0; a <= N; a++) mpz_mul(v[a], t->perm[k], t->pw[a]);
        return v;
    }
    /* R(a, k) = sum_i binom(a, i) k^i Bell(a - i) */
    mpz_t *kp = new_mpz(N + 1);
    mpz_set_ui(kp[0], 1);
    for (int i = 1; i <= N; i++) mpz_mul_ui(kp[i], kp[i - 1], (unsigned long)k);
    for (int a = 0; a <= N; a++) {
        mpz_set_ui(t->t1, 1);                                   /* binom(a, i) */
        for (int i = 0; i <= a; i++) {
            mpz_mul(t->t2, t->t1, kp[i]);
            mpz_addmul(v[a], t->t2, t->bell[a - i]);
            if (i < a) { mpz_mul_ui(t->t1, t->t1, (unsigned long)(a - i)); mpz_divexact_ui(t->t1, t->t1, (unsigned long)(i + 1)); }
        }
    }
    free_mpz(kp, N + 1);
    return v;
}

void stab_mark(ShapeTable *t, int m, int r, int maxb)
{
    if (m < 0 || m > t->N || r < 1 || r > m + 1 || maxb < 0) return;
    int g = gidx(t, m, r);
    if (t->col[g].len > maxb) return;
    if (t->want[g] < maxb) t->want[g] = maxb;
}

/*
 * One pass over the levels of NT.  Level b holds NT(b, m, r) for the
 * reachable states m_lo <= m <= min(N - b, m_hi + maxb - b) and
 * max(0, 2m + 1 - (N - b)) <= r <= m + 1; this region is closed under the
 * recurrence, so every value read was computed at the previous level.
 */
void stab_compute(ShapeTable *t)
{
    int N = t->N, maxb = -1, m_lo = N + 1, m_hi = -1;
    for (int m = 0; m <= N; m++)
        for (int r = 1; r <= m + 1; r++) {
            int g = gidx(t, m, r);
            if (t->want[g] < 0) continue;
            free_mpz(t->col[g].v, t->col[g].len);
            t->col[g].len = t->want[g] + 1;
            t->col[g].v = new_mpz(t->col[g].len);
            if (t->want[g] > maxb) maxb = t->want[g];
            if (m < m_lo) m_lo = m;
            if (m > m_hi) m_hi = m;
        }
    if (maxb < 0) return;
    mpz_t *cur = t->plane[0], *prev = t->plane[1];
    int hi = N < m_hi + maxb ? N : m_hi + maxb;
    for (int m = m_lo; m <= hi; m++) {
        int rlo = 2 * m + 1 - N > 0 ? 2 * m + 1 - N : 0;
        for (int r = rlo; r <= m + 1; r++) mpz_set_ui(cur[pidx(m, r)], r == 0);
    }
    for (int b = 1; b <= maxb; b++) {
        mpz_t *s = cur; cur = prev; prev = s;
        hi = N - b < m_hi + maxb - b ? N - b : m_hi + maxb - b;
        for (int m = m_lo; m <= hi; m++) {
            int rlo = 2 * m + 1 - (N - b) > 0 ? 2 * m + 1 - (N - b) : 0;
            for (int r = rlo; r <= m + 1; r++) {
                mpz_ptr c = cur[pidx(m, r)];
                if (r == 0) { mpz_set_ui(c, 0); continue; }
                mpz_mul_ui(c, prev[pidx(m, r)], (unsigned long)(m + 2));
                mpz_add(c, c, prev[pidx(m + 1, r + 1)]);
                mpz_add(c, c, prev[pidx(m, r - 1)]);
                int g = gidx(t, m, r);
                if (t->want[g] >= b) mpz_set(t->col[g].v[b], c);
            }
        }
    }
    for (int m = m_lo; m <= m_hi; m++)
        for (int r = 1; r <= m + 1; r++) t->want[gidx(t, m, r)] = -1;
}

void stab_count(ShapeTable *t, mpz_t out, int n, int m, int r, int k)
{
    if (n > t->N) die("internal: shape count for size %d beyond the table (%d)", n, t->N);
    if (r < 0 || r > m + 1 || m > t->N || k < 0 || k > t->N + 1) { mpz_set_ui(out, 0); return; }
    if (n == 0) { mpz_set(out, r == 0 ? stab_base(t, k) : t->zero); return; }
    if (r == 0) { mpz_set_ui(out, 0); return; }
    Col *col = &t->col[gidx(t, m, r)];
    if (col->len <= n) {
        stab_forget(t);
        for (int mm = m; mm <= m + BAND && mm <= t->N; mm++)
            for (int rr = r - BAND - 1 > 1 ? r - BAND - 1 : 1; rr <= r + BAND + 1 && rr <= mm + 1; rr++)
                stab_mark(t, mm, rr, n);
        stab_compute(t);
    }
    const mpz_t *Rk = column_R(t, k);
    mpz_set_ui(out, 0);
    mpz_set_ui(t->t1, 1);                                        /* binom(n - 1, a) */
    for (int a = 0; a < n; a++) {
        mpz_mul(t->t2, t->t1, Rk[a]);
        mpz_addmul(out, t->t2, col->v[n - a]);
        if (a < n - 1) { mpz_mul_ui(t->t1, t->t1, (unsigned long)(n - 1 - a)); mpz_divexact_ui(t->t1, t->t1, (unsigned long)(a + 1)); }
    }
}

mpz_srcptr stab_off(ShapeTable *t, int n)
{
    if (n < 0 || n > t->N + 1) die("internal: offset of size %d beyond the table (%d)", n, t->N);
    if (!t->have_off) {
        stab_mark(t, 0, 1, t->N);
        stab_compute(t);
        mpz_set_ui(t->off[0], 0);
        for (int s = 0; s <= t->N; s++) {
            stab_count(t, t->t3, s, 0, 1, 0);
            mpz_add(t->off[s + 1], t->off[s], t->t3);
        }
        t->have_off = 1;
    }
    return t->off[n];
}

void stab_total(ShapeTable *t, mpz_t out, int n)
{
    mpz_sub(out, stab_off(t, n + 1), stab_off(t, n));
}

void stab_free(ShapeTable *t)
{
    if (t->N >= 0) free_sizes(t);
    free_mpz(t->perm, t->sigma + 1);
    mpz_clear(t->zero);
    mpz_clear(t->one);
    mpz_clear(t->t1);
    mpz_clear(t->t2);
    mpz_clear(t->t3);
}
