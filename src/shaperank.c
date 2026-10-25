#include <stdlib.h>
#include "shaperank.h"
#include "util.h"

void shape_rank(ShapeTable *t, mpz_t out, const Shape *s)
{
    int n = s->n, m = 0, r = 1, k = 0;
    stab_ensure(t, n);
    mpz_set(out, t->off[n]);
    for (int i = 0; i < n; i++) {
        int left = n - 1 - i;
        const Action *a = &s->a[i];
        if (r == 0) die("internal: shape continues after its last rule");
        mpz_srcptr same = stab_at(t, left, m, r, k);       /* old terminal, old nonterminal, | */
        switch (a->kind) {
        case A_T:
            if (a->id > k) die("internal: shape is not canonical");
            mpz_addmul_ui(out, same, (unsigned long)a->id);
            if (a->id == k) k++;
            break;
        case A_N:
            if (a->id > m + 1) die("internal: shape is not canonical");
            mpz_addmul_ui(out, same, (unsigned long)k);
            mpz_add(out, out, stab_at(t, left, m, r, k + 1));
            mpz_addmul_ui(out, same, (unsigned long)a->id);
            if (a->id == m + 1) { m++; r++; }
            break;
        case A_BAR:
        case A_END:
            mpz_addmul_ui(out, same, (unsigned long)(k + m + 1));
            mpz_add(out, out, stab_at(t, left, m, r, k + 1));
            mpz_add(out, out, stab_at(t, left, m + 1, r + 1, k));
            if (a->kind == A_END) { mpz_add(out, out, same); r--; }
            break;
        }
    }
    if (r != 0) die("internal: shape ends with %d rules missing", r);
}

/*
 * rem indexes into c consecutive blocks of w objects each.  Returns the
 * block and reduces rem to the offset inside it, or returns -1 after
 * subtracting all c blocks.
 */
static long pick(mpz_t rem, mpz_srcptr w, long c, mpz_t q, mpz_t rr)
{
    if (c <= 0 || mpz_sgn(w) == 0) return -1;
    mpz_fdiv_qr(q, rr, rem, w);
    if (mpz_cmp_ui(q, (unsigned long)c) < 0) {
        mpz_set(rem, rr);
        return (long)mpz_get_ui(q);
    }
    mpz_submul_ui(rem, w, (unsigned long)c);
    return -1;
}

void shape_unrank(ShapeTable *t, mpz_t rest, const mpz_t num, Shape *s)
{
    int n = 0;
    for (;;) {
        stab_ensure(t, n);
        if (mpz_cmp(num, t->off[n + 1]) < 0) break;
        n++;
    }
    mpz_t rem, q, rr;
    mpz_init(rem);
    mpz_init(q);
    mpz_init(rr);
    mpz_sub(rem, num, t->off[n]);
    shape_init(s);
    int m = 0, r = 1, k = 0;
    for (int i = 0; i < n; i++) {
        int left = n - 1 - i;
        mpz_srcptr same = stab_at(t, left, m, r, k);
        long x;
        if ((x = pick(rem, same, k, q, rr)) >= 0) { shape_push(s, A_T, (int)x); continue; }
        if (pick(rem, stab_at(t, left, m, r, k + 1), 1, q, rr) >= 0) { shape_push(s, A_T, k); k++; continue; }
        if ((x = pick(rem, same, m + 1, q, rr)) >= 0) { shape_push(s, A_N, (int)x); continue; }
        if (pick(rem, stab_at(t, left, m + 1, r + 1, k), 1, q, rr) >= 0) { shape_push(s, A_N, m + 1); m++; r++; continue; }
        if (pick(rem, same, 1, q, rr) >= 0) { shape_push(s, A_BAR, 0); continue; }
        if (pick(rem, stab_at(t, left, m, r - 1, k), 1, q, rr) >= 0) { shape_push(s, A_END, 0); r--; continue; }
        die("internal: unrank found no action at position %d", i);
    }
    if (r != 0 || mpz_cmp(rem, stab_base(t, k)) >= 0) die("internal: unrank left an incomplete shape");
    mpz_set(rest, rem);
    mpz_clear(rem);
    mpz_clear(q);
    mpz_clear(rr);
}

/* f = a (a-1) ... (a-b+1) */
static void falling(mpz_t f, int a, int b)
{
    mpz_set_ui(f, 1);
    for (int i = 0; i < b; i++) mpz_mul_ui(f, f, (unsigned long)(a - i));
}

void perm_rank(mpz_t out, int sigma, const int *p, int k)
{
    unsigned char *used = xcalloc((size_t)(sigma ? sigma : 1), 1);
    mpz_t f;
    mpz_init(f);
    mpz_set_ui(out, 0);
    for (int i = 0; i < k; i++) {
        if (p[i] < 0 || p[i] >= sigma || used[p[i]]) die("internal: not a permutation");
        int c = 0;
        for (int x = 0; x < p[i]; x++) c += !used[x];
        falling(f, sigma - 1 - i, k - 1 - i);
        mpz_addmul_ui(out, f, (unsigned long)c);
        used[p[i]] = 1;
    }
    mpz_clear(f);
    free(used);
}

void perm_unrank(int *p, int sigma, int k, const mpz_t num)
{
    if (k > sigma) die("internal: %d distinct terminals from an alphabet of %d", k, sigma);
    unsigned char *used = xcalloc((size_t)(sigma ? sigma : 1), 1);
    mpz_t f, rem, q;
    mpz_init(f);
    mpz_init_set(rem, num);
    mpz_init(q);
    for (int i = 0; i < k; i++) {
        falling(f, sigma - 1 - i, k - 1 - i);
        mpz_fdiv_qr(q, rem, rem, f);
        long c = (long)mpz_get_ui(q);
        int x = 0;                                  /* the c-th unused element */
        for (;;) {
            if (!used[x]) { if (c == 0) break; c--; }
            x++;
        }
        p[i] = x;
        used[x] = 1;
    }
    mpz_clear(f);
    mpz_clear(rem);
    mpz_clear(q);
    free(used);
}
