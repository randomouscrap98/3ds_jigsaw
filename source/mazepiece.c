#include "mazepiece.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MP_RELAXED 16       // Most relaxed stub-fix rounds (see mp_fixstubs)

static const int mp_dx[4] = { 1, 0, -1, 0 }, mp_dy[4] = { 0, 1, 0, -1 };

// Growing-tree maze over a x b rooms (cell (2i, 2j)); carves corridors into wall[] (all 1 at start)
static int mp_maze(uint8_t * wall, uint16_t w, uint16_t h, float newest, Rng * r) {
  uint16_t a = (w + 1) / 2, b = (h + 1) / 2;
  uint32_t n = (uint32_t)a * b, nact = 0;
  uint32_t * act = malloc(sizeof(uint32_t) * n);
  uint8_t * seen = calloc(n, 1);
  if(!act || !seen) { free(act); free(seen); return 1; }
  uint32_t s = rng_below(r, n);
  act[nact++] = s;
  seen[s] = 1;
  wall[(s / a) * 2 * w + (s % a) * 2] = 0;
  while(nact) {
    uint32_t k = rng_float(r) < newest ? nact - 1 : rng_below(r, nact);
    uint32_t c = act[k];
    int ci = c % a, cj = c / a;
    int dirs[4], nd = 0;
    for(int d = 0; d < 4; d++) {
      int ni = ci + mp_dx[d], nj = cj + mp_dy[d];
      if(ni >= 0 && nj >= 0 && ni < a && nj < b && !seen[nj * a + ni]) dirs[nd++] = d;
    }
    if(!nd) {
      memmove(act + k, act + k + 1, sizeof(uint32_t) * (nact - k - 1));   // Keep order: newest = last
      nact--;
      continue;
    }
    int d = dirs[rng_below(r, nd)];
    int ni = ci + mp_dx[d], nj = cj + mp_dy[d];
    wall[(cj * 2 + mp_dy[d]) * w + ci * 2 + mp_dx[d]] = 0;
    wall[nj * 2 * w + ni * 2] = 0;
    seen[nj * a + ni] = 1;
    act[nact++] = nj * a + ni;
  }
  free(act);
  free(seen);
  return 0;
}

// Components smaller than minsize (wall stubs where the outer wall was cut off) join a
// bigger one of their kind: flip the connector between them. That splits the other kind's
// tree there, so a flip is only taken if every part left is still >= minsize (no cascade);
// the one leaving the biggest smallest part wins. When that leaves stubs with no such flip
// (dead-end corridors along the edge), one relaxed round takes the best flip anyway and
// strict rounds go on (fixing the small part it made); at most MP_RELAXED of those. Keeps
// both kinds forests. Per round: components found once (BFS: parents + subtree sizes), at
// most one flip per component.
static int mp_fixstubs(uint8_t * wall, uint16_t w, uint16_t h, uint32_t minsize, uint32_t * flips) {
  uint32_t n = (uint32_t)w * h;
  uint32_t * comp = malloc(sizeof(uint32_t) * n), * order = malloc(sizeof(uint32_t) * n);
  uint32_t * csize = malloc(sizeof(uint32_t) * n), * sub = malloc(sizeof(uint32_t) * n);
  int32_t * parent = malloc(sizeof(int32_t) * n);
  uint8_t * busy = malloc(n);
  if(!comp || !order || !csize || !sub || !parent || !busy) {
    free(comp); free(order); free(csize); free(sub); free(parent); free(busy);
    return 1;
  }
  *flips = 0;
  int relaxed = 0, relax = 0;
  for(int round = 0, again = 1; again && round < 4096; round++) {
    again = 0;
    int stubs = 0;
    // Components in BFS order (parents first)
    uint32_t nc = 0, no = 0;
    for(uint32_t i = 0; i < n; i++) comp[i] = UINT32_MAX;
    for(uint32_t s = 0; s < n; s++) {
      if(comp[s] != UINT32_MAX) continue;
      uint32_t head = no;
      comp[s] = nc;
      parent[s] = -1;
      order[no++] = s;
      while(head < no) {
        uint32_t v = order[head++];
        int x = v % w, y = v / w;
        for(int d = 0; d < 4; d++) {
          int nx = x + mp_dx[d], ny = y + mp_dy[d];
          if(nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
          uint32_t u = ny * w + nx;
          if(comp[u] != UINT32_MAX || wall[u] != wall[v]) continue;
          comp[u] = nc;
          parent[u] = v;
          order[no++] = u;
        }
      }
      csize[nc++] = 0;
    }
    for(uint32_t i = 0; i < n; i++) { sub[i] = 1; csize[comp[i]]++; }
    for(uint32_t i = n; i-- > 0;) if(parent[order[i]] >= 0) sub[parent[order[i]]] += sub[order[i]];
    memset(busy, 0, nc);
    // Each stub (by its root in BFS order) looks at the connectors around it
    for(uint32_t i = 0; i < n; i++) {
      uint32_t v = order[i], c = comp[v];
      if(parent[v] >= 0 || csize[c] >= minsize) continue;
      stubs++;
      if(busy[c]) continue;
      uint32_t best = UINT32_MAX, bestmin = 0;
      for(uint32_t j = i; j < i + csize[c]; j++) {
        int x = order[j] % w, y = order[j] / w;
        for(int d = 0; d < 4; d++) {
          int ux = x + mp_dx[d], uy = y + mp_dy[d], ox = ux + mp_dx[d], oy = uy + mp_dy[d];
          if(ox < 0 || oy < 0 || ox >= w || oy >= h) continue;
          uint32_t u = uy * w + ux, o = oy * w + ox;
          if(wall[u] == wall[v] || comp[o] == c || wall[o] != wall[v] || busy[comp[u]] || busy[comp[o]]) continue;
          // Parts of u's tree without u: each child's subtree, and the rest above it
          uint32_t m = parent[u] >= 0 ? csize[comp[u]] - sub[u] : UINT32_MAX;
          for(int e = 0; e < 4; e++) {
            int ax = ux + mp_dx[e], ay = uy + mp_dy[e];
            if(ax < 0 || ay < 0 || ax >= w || ay >= h) continue;
            uint32_t a = ay * w + ax;
            if(parent[a] == (int32_t)u && sub[a] < m) m = sub[a];
          }
          if(m < (relax ? 1 : minsize)) continue;
          if(best == UINT32_MAX || m > bestmin) { best = u; bestmin = m; }
        }
      }
      if(best == UINT32_MAX) continue;
      // Joins c to o's component and splits u's: none of the three may change again this round
      int bx = best % w, by = best / w;
      for(int d = 0; d < 4; d++) {
        int nx = bx + mp_dx[d], ny = by + mp_dy[d];
        if(nx >= 0 && ny >= 0 && nx < w && ny < h) busy[comp[ny * w + nx]] = 1;
      }
      busy[comp[best]] = 1;
      busy[c] = 1;
      wall[best] ^= 1;
      (*flips)++;
      again = 1;
    }
    relax = 0;
    if(!again && stubs && relaxed < MP_RELAXED) {
      relax = 1;
      relaxed++;
      again = 1;
    }
  }
  free(comp); free(order); free(csize); free(sub); free(parent); free(busy);
  return 0;
}

// Cut every same-kind component into connected pieces of >= thr cells (leftovers join a
// neighbour). Components (DFS preorder + parents) are found once by the caller.
typedef struct {
  uint32_t * order;     // Cells, component by component, each in DFS preorder
  int32_t * parent;
  uint32_t * cstart;    // Component c = order[cstart[c] .. cstart[c + 1])
  uint32_t ncomp;
} MpTrees;

static int mp_trees(MpTrees * t, const uint8_t * wall, uint16_t w, uint16_t h, Rng * r) {
  uint32_t n = (uint32_t)w * h;
  t->order = malloc(sizeof(uint32_t) * n);
  t->parent = malloc(sizeof(int32_t) * n);
  t->cstart = malloc(sizeof(uint32_t) * (n + 1));
  uint8_t * seen = calloc(n, 1);
  uint32_t * stack = malloc(sizeof(uint32_t) * n);
  if(!t->order || !t->parent || !t->cstart || !seen || !stack) { free(seen); free(stack); return 1; }
  uint32_t no = 0;
  t->ncomp = 0;
  // Roots: wall components from the edge (where they hang from), corridors from a random cell
  uint32_t first = rng_below(r, n);
  for(uint32_t pass = 0; pass < 3; pass++) {
    for(uint32_t i = 0; i < n; i++) {
      uint32_t c = pass == 0 ? (first + i) % n : i;
      int x = c % w, y = c / w;
      if(seen[c]) continue;
      if(pass == 0 && wall[c]) continue;
      if(pass == 1 && !(x == 0 || y == 0 || x == w - 1 || y == h - 1)) continue;
      t->cstart[t->ncomp++] = no;
      uint32_t ns = 0;
      stack[ns++] = c;
      seen[c] = 1;
      t->parent[c] = -1;
      while(ns) {
        uint32_t v = stack[--ns];
        t->order[no++] = v;
        int vx = v % w, vy = v / w, d0 = rng_below(r, 4);
        for(int k = 0; k < 4; k++) {
          int d = (d0 + k) & 3, nx = vx + mp_dx[d], ny = vy + mp_dy[d];
          if(nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
          uint32_t u = ny * w + nx;
          if(seen[u] || wall[u] != wall[v]) continue;
          seen[u] = 1;
          t->parent[u] = v;
          stack[ns++] = u;
        }
      }
      if(pass == 0) break;   // One corridor tree
    }
  }
  t->cstart[t->ncomp] = no;
  free(seen);
  free(stack);
  return 0;
}

static void mp_treesfree(MpTrees * t) {
  free(t->order); free(t->parent); free(t->cstart);
}

static uint32_t mp_cut(MazePieces * m, const MpTrees * t, uint32_t thr, uint32_t * res, uint8_t * cut) {
  uint32_t n = (uint32_t)m->w * m->h, count = 0;
  for(uint32_t i = 0; i < n; i++) m->label[i] = -1;
  m->tiny = 0;
  for(uint32_t c = 0; c < t->ncomp; c++) {
    const uint32_t * o = t->order + t->cstart[c];
    uint32_t len = t->cstart[c + 1] - t->cstart[c];
    for(uint32_t i = 0; i < len; i++) { res[o[i]] = 0; cut[o[i]] = 0; }
    // Reverse preorder: children before parents
    for(uint32_t i = len; i-- > 0;) {
      uint32_t v = o[i];
      res[v] += 1;
      if(res[v] >= thr) cut[v] = 1;
      else if(t->parent[v] >= 0) res[t->parent[v]] += res[v];
    }
    uint32_t root = o[0];
    int32_t merge = -1;
    for(uint32_t i = 0; i < len; i++) {
      uint32_t v = o[i];
      if(cut[v]) {
        m->label[v] = count++;
        if(t->parent[v] >= 0 && m->label[t->parent[v]] == -2 && merge < 0) merge = m->label[v];
      } else {
        m->label[v] = v == root ? -2 : m->label[t->parent[v]];
      }
    }
    if(cut[root]) continue;
    // Root leftover: own piece if big enough, else joins a piece cut next to it
    int32_t l = res[root] * 2 >= thr ? (int32_t)count++ : merge >= 0 ? merge : -3;
    if(l == -3) m->tiny++;
    for(uint32_t i = 0; i < len; i++) if(m->label[o[i]] == -2) m->label[o[i]] = l;
  }
  // Tiny components (wall stubs at the edge): join a neighbouring piece of either kind
  for(int pass = 0, left = 1; left && pass < 8; pass++) {
    left = 0;
    for(uint32_t c = 0; c < t->ncomp; c++) {
      const uint32_t * o = t->order + t->cstart[c];
      uint32_t len = t->cstart[c + 1] - t->cstart[c];
      if(m->label[o[0]] != -3) continue;
      int32_t l = -3;
      for(uint32_t i = 0; i < len && l < 0; i++) {
        int x = o[i] % m->w, y = o[i] / m->w;
        for(int d = 0; d < 4 && l < 0; d++) {
          int nx = x + mp_dx[d], ny = y + mp_dy[d];
          if(nx >= 0 && ny >= 0 && nx < m->w && ny < m->h && m->label[ny * m->w + nx] >= 0) l = m->label[ny * m->w + nx];
        }
      }
      if(l < 0) { left = 1; continue; }
      for(uint32_t i = 0; i < len; i++) m->label[o[i]] = l;
    }
  }
  return count;
}

int mazepiece_init(MazePieces * m, uint16_t cols, uint16_t rows, uint32_t count, uint32_t avg, float newest, uint32_t seed) {
  memset(m, 0, sizeof(MazePieces));
  float wf = sqrtf((float)count * avg * cols / rows);
  uint16_t a = (uint16_t)lroundf((wf + 1) / 2), b = (uint16_t)lroundf((wf * rows / cols + 1) / 2);
  if(a < 2 || b < 2) return 1;
  m->w = 2 * a - 1;
  m->h = 2 * b - 1;
  uint32_t n = (uint32_t)m->w * m->h;
  m->wall = malloc(n);
  m->label = malloc(sizeof(int32_t) * n);
  uint32_t * res = malloc(sizeof(uint32_t) * n);
  uint8_t * cut = malloc(n);
  MpTrees t;
  memset(&t, 0, sizeof t);
  Rng r;
  rng_init(&r, seed);
  int err = !m->wall || !m->label || !res || !cut;
  if(!err) {
    memset(m->wall, 1, n);
    err = mp_maze(m->wall, m->w, m->h, newest, &r) || mp_fixstubs(m->wall, m->w, m->h, avg / 2, &m->flips) ||
        mp_trees(&t, m->wall, m->w, m->h, &r);
  }
  if(!err) {
    // Threshold giving the piece count closest to the target
    uint32_t best = 2, bestd = UINT32_MAX;
    for(uint32_t thr = 2; thr <= 3 * avg; thr++) {
      uint32_t c = mp_cut(m, &t, thr, res, cut);
      uint32_t d = c > count ? c - count : count - c;
      if(d < bestd) { bestd = d; best = thr; }
    }
    m->thr = best;
    m->count = mp_cut(m, &t, best, res, cut);
  }
  mp_treesfree(&t);
  free(res);
  free(cut);
  if(err) mazepiece_free(m);
  return err;
}

void mazepiece_free(MazePieces * m) {
  free(m->wall);
  free(m->label);
  memset(m, 0, sizeof(MazePieces));
}
