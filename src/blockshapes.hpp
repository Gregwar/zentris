#pragma once
// Block meshes that are not superellipsoids (MESH_CHAMFER and after): an outline in the board plane,
// extruded toward and away from the camera with a height profile (rims, puffy faces, domes).
#include <vector>

#include "theme.hpp"

// Whether a block mesh is built by buildBlockShape (otherwise it is a superellipsoid of Theme::meshExp).
inline bool isProfileMesh(int mesh) { return mesh >= MESH_CHAMFER; }

// Appends the mesh of a profile block shape, in the cube's space (-0.5 .. 0.5). Vertices: position 3,
// normal 3, edge 4 (face uv 2, distance to the outline scaled so 1 is the face center, 1 = that distance
// is given: the block shader then uses it instead of the uv square's).
void buildBlockShape(int mesh, std::vector<float>& verts, std::vector<unsigned>& idx);
