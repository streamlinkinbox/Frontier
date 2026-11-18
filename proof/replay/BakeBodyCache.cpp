// Offline companion to the pinned FVB1 vehicle. No runtime mesh rebake on
// motion. Every R8UI texel stores the authored ID of the triangle nearest its
// centre.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
struct V {
  float x, y, z;
  V operator+(V b) const { return {x + b.x, y + b.y, z + b.z}; }
  V operator-(V b) const { return {x - b.x, y - b.y, z - b.z}; }
  V operator*(float s) const { return {x * s, y * s, z * s}; }
};
float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float sq(V a) { return dot(a, a); }
struct Node {
  V lo;
  float child;
  V hi;
  float count;
};
struct Tri {
  V a;
  float id;
  V u;
  float n0;
  V v;
  float n1;
  std::array<float, 4> tail;
};
static_assert(sizeof(Node) == 32 && sizeof(Tri) == 64);
float edge(V p, V u) {
  return sq(p - u * std::clamp(dot(p, u) / std::max(sq(u), 1e-30f), 0.f, 1.f));
}
float distance2(V p, const Tri &t) {
  p = p - t.a;
  V a = t.u, b = t.v;
  float aa = sq(a), bb = sq(b), ab = dot(a, b), pa = dot(p, a), pb = dot(p, b);
  float det = aa * bb - ab * ab;
  if (det > 0) {
    float u = (bb * pa - ab * pb) / det, v = (aa * pb - ab * pa) / det;
    if (u >= 0 && v >= 0 && u + v <= 1)
      return sq(p - a * u - b * v);
  }
  return std::min({edge(p, a), edge(p, b), edge(p - a, b - a)});
}
float bound(V p, Node b) {
  V c{std::clamp(p.x, b.lo.x, b.hi.x), std::clamp(p.y, b.lo.y, b.hi.y),
      std::clamp(p.z, b.lo.z, b.hi.z)};
  return sq(c - p);
}
struct Body {
  std::vector<Tri> tris;
  std::vector<Node> nodes;
  void nearest(V p, int i, float &best, int &which) const {
    const auto &n = nodes.at(i);
    if (bound(p, n) > best)
      return;
    int a = int(n.child);
    if (n.count) {
      for (int j = a; j < a + int(n.count); ++j) {
        float d = distance2(p, tris.at(j));
        if (d < best || (d == best && j < which)) {
          best = d;
          which = j;
        }
      }
      return;
    }
    int b = a + 1;
    if (bound(p, nodes.at(a)) > bound(p, nodes.at(b)))
      std::swap(a, b);
    nearest(p, a, best, which);
    nearest(p, b, best, which);
  }
};
int main(int argc, char **argv) try {
  if (argc != 3)
    throw std::runtime_error("usage: bake VehicleBenchmarkAssets.bin body.r8");
  std::ifstream in(argv[1], std::ios::binary);
  in.exceptions(std::ios::badbit | std::ios::failbit);
  char magic[4];
  in.read(magic, 4);
  std::array<uint32_t, 4> counts;
  in.read((char *)counts.data(), 16);
  if (std::memcmp(magic, "FVB1", 4) ||
      counts != std::array<uint32_t, 4>{4, 5930, 9240, 5})
    throw std::runtime_error("unexpected asset revision");
  std::array<uint32_t, 16> meta;
  in.read((char *)meta.data(), 64);
  if (meta[0] != 0 || meta[1] != 1917 || meta[2] != 0 || meta[3] != 2960)
    throw std::runtime_error("unexpected body BLAS metadata");
  Body b;
  b.nodes.resize(meta.at(1));
  b.tris.resize(meta.at(3));
  in.read((char *)b.nodes.data(), b.nodes.size() * 32);
  in.seekg(84 + counts[1] * 32);
  in.read((char *)b.tris.data(), b.tris.size() * 64);
  in.seekg(84 + counts[1] * 32 + counts[2] * 64);
  std::array<uint32_t, 4> grid;
  V lo, hi;
  in.read((char *)grid.data(), 16);
  in.read((char *)&lo, 12);
  in.read((char *)&hi, 12);
  if (grid != std::array<uint32_t, 4>{88, 64, 36, 88 * 64 * 36 * 2})
    throw std::runtime_error("unexpected lattice");
  std::ofstream out(argv[2], std::ios::binary);
  out.exceptions(std::ios::badbit | std::ios::failbit);
  for (unsigned z = 0; z < grid[2]; ++z)
    for (unsigned y = 0; y < grid[1]; ++y)
      for (unsigned x = 0; x < grid[0]; ++x) {
        V p{lo.x + (hi.x - lo.x) * (x + .5f) / grid[0],
            lo.y + (hi.y - lo.y) * (y + .5f) / grid[1],
            lo.z + (hi.z - lo.z) * (z + .5f) / grid[2]};
        int i = -1;
        float best = INFINITY;
        b.nearest(p, 0, best, i);
        out.put(char(b.tris.at(i).id));
      }
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
