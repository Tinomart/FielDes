/*
libfive: a CAD kernel for modeling with implicit functions
Copyright (C) 2017  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#ifdef __cplusplus
#include <cstdint>
#include "libfive/tree/tree.hpp"
#include "libfive/eval/evaluator.hpp"
extern "C" {
#else
#include <stdint.h>
#include <stdbool.h>
#endif

/*
 *  libfive_interval is a range used in interval arithmetic
 *  It usually represents either a spatial region (along a single axis)
 *  or a range that is guaranteed to contain a value.
 */
typedef struct libfive_interval  { float lower; float upper; } libfive_interval;

/*
 *  libfive_region2:  A 2D region
 */
typedef struct libfive_region2   { libfive_interval X, Y; } libfive_region2;

/*
 *  libfive_region3:  A 3D region
 */
typedef struct libfive_region3   { libfive_interval X, Y, Z; } libfive_region3;

/*
 *  libfive_vec2:  A 2D point or vector
 */
typedef struct libfive_vec2      { float x, y; } libfive_vec2;

/*
 *  libfive_vec3:  A 3D point or vector
 */
typedef struct libfive_vec3      { float x, y, z; } libfive_vec3;

/*
 *  libfive_vec4:  A 4D point or vector
 */
typedef struct libfive_vec4      { float x, y, z, w; } libfive_vec4;

/*
 *  libfive_tri:    A triangle, with corners stored as indices
 *  into a separate vertex array
 */
typedef struct libfive_tri       { uint32_t a, b, c; } libfive_tri;

/*
 *  libfive_contour is a single 2D contour, consisting of a sequence of
 *  2D points plus a count of how many points are stored
 */
typedef struct libfive_contour {
    libfive_vec2* pts;
    uint32_t count;
} libfive_contour;

/*
 *  libfive_contour is a set of 2D contours, consisting of multiple
 *  libfive_contour objects and a count of how many are stored
 */
typedef struct libfive_contours {
    libfive_contour* cs;
    uint32_t count;
} libfive_contours;

/*
 *  libfive_contour3 is a single 2D contour, consisting of a sequence of
 *  3D points plus a count of how many points are stored
 */
typedef struct libfive_contour3 {
    libfive_vec3* pts;
    uint32_t count;
} libfive_contour3;

/*
 *  libfive_contours3 is a set of 2D contours, consisting of multiple
 *  libfive_contour3 objects and a count of how many are stored
 */
typedef struct libfive_contours3 {
    libfive_contour3* cs;
    uint32_t count;
} libfive_contours3;

/*
 *  libfive_mesh is an indexed 3D mesh.
 *  There are vert_count vertices, and tri_count triangles.
 */
typedef struct libfive_mesh {
    libfive_vec3* verts;
    libfive_tri* tris;
    uint32_t tri_count;
    uint32_t vert_count;
} libfive_mesh;

/*
 *  libfive_mesh_coords is an indexed 3D mesh, similar to
 *  libfive_mesh, with sets of vertex indices separated by -1 instead
 *  of using triangle structs. There are vert_count vertices, and
 *  coord_index_count coordinate indices (including the -1s), for
 *  coord_index_count / 4 total triangles.
 */
typedef struct libfive_mesh_coords {
    libfive_vec3* verts;
    uint32_t vert_count;
    int32_t* coord_indices;
    uint32_t coord_index_count;
} libfive_mesh_coords;

/*
 *  libfive_pixels is a bitmap representing occupancy
 *  There are width * height pixels, in row-major order
 */
typedef struct libfive_pixels {
    bool* pixels;
    uint32_t width;
    uint32_t height;
} libfive_pixels;

////////////////////////////////////////////////////////////////////////////////

/*
 *  Frees an libfive_contours data structure
 */
void libfive_contours_delete(libfive_contours* cs);

/*
 *  Frees an libfive_contours data structure
 */
void libfive_contours3_delete(libfive_contours3* cs);

/*
 *  Frees an libfive_mesh data structure
 */
void libfive_mesh_delete(libfive_mesh* m);

/*
 *  Frees an libfive_mesh_coords data structure
 */
void libfive_mesh_coords_delete(libfive_mesh_coords* m);

/*
 *  Frees an libfive_pixels data structure
 */
void libfive_pixels_delete(libfive_pixels* ps);

/*
 *  Takes a string description of an op-code ('min', 'max', etc) and
 *  returns the libfive::Opcode value, or -1 if no such value exists.
 */
int libfive_opcode_enum(const char* op);

/*
 *  Returns the number of arguments for the given opcode
 *  (either 0, 1, 2, or -1 if the opcode is invalid)
 */
int libfive_opcode_args(int op);

////////////////////////////////////////////////////////////////////////////////

/*  libfive_vars is a general-purpose struct for mapping a set of
 *  free variables to their values.  The variables are opaque
 *  pointers, i.e. values returned with libfive_tree_id. */
typedef struct libfive_vars {
    void* const* vars;
    float* values;
    uint32_t size;
} libfive_vars;
void libfive_vars_delete(libfive_vars* j);

////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
typedef const libfive::Tree::Data* libfive_tree;
typedef libfive::Evaluator *libfive_evaluator;
#else
typedef struct libfive_tree_ libfive_tree_;
typedef struct libfive_tree_* libfive_tree;

typedef struct libfive_evaluator_ libfive_evaluator_;
typedef struct libfive_evaluator_ *libfive_evaluator;
#endif

/*
 *  Constructs a new tree that returns the X coordinate
 */
libfive_tree libfive_tree_x();

/*
 *  Constructs a new tree that returns the Y coordinate
 */
libfive_tree libfive_tree_y();

/*
 *  Constructs a new tree that returns the Z coordinate
 */
libfive_tree libfive_tree_z();

/*
 *  Constructs a new tree that contains a free variable
 */
libfive_tree libfive_tree_var();

/*
 *  Returns true if the given tree is a free variable
 */
bool libfive_tree_is_var(libfive_tree t);

/*
 *  Constructs a new tree that contains the given constant value
 */
libfive_tree libfive_tree_const(float f);

/*
 *  If t is a constant value, returns that value and sets *success to true.
 *  Otherwise, sets success to false and returns 0.
 */
float libfive_tree_get_const(libfive_tree t, bool* success);

/*
 *  Constructs a tree with the given no-argument opcode
 *  Returns NULL if the opcode is invalid.
 */
libfive_tree libfive_tree_nullary(int op);

/*
 *  Constructs a tree with the given one-argument opcode
 *  Returns NULL if the opcode or argument is invalid
 */
libfive_tree libfive_tree_unary(int op, libfive_tree a);

/*
 *  Constructs a tree with the given two-argument opcode
 *  Returns NULL if the opcode or arguments are invalid
 */
libfive_tree libfive_tree_binary(int op, libfive_tree a, libfive_tree b);

/*
 *  Returns a unique ID for the given tree.  There is no global deduplication;
 *  e.g. multiple calls to libfive_tree_const(1.0) will return trees with
 *  different IDs.
 *
 *  This is primarily used to uniquely identify free variables, i.e. trees
 *  returned from libfive_tree_var().
 */
const void* libfive_tree_id(libfive_tree t);

/*
 *  Evaluates the given math tree at the given position.
 *  TODO:  Free variables are treated as zero
 */
float libfive_tree_eval_f(libfive_tree t, libfive_vec3 p);

/*
 *  Evaluates the given math tree over a spatial region, returning an interval
 *  that is guaranteed to contain the result
 *  TODO:  Free variables are treated as zero
 */
libfive_interval libfive_tree_eval_r(libfive_tree t, libfive_region3 r);

/*
 *  Samples the tree at the cell centres of an nx x ny x nz grid over the box
 *  [lower, upper] and reports: out[0] number of samples < 0 (inside),
 *  out[1..3] sums of their x, y, z, out[4] / out[5] min / max value,
 *  out[6..8] sums of x^2, y^2, z^2 of the inside samples.  Returns false on
 *  bad arguments.
 */
bool libfive_tree_grid_stats(libfive_tree t, const float* lower3, const float* upper3,
                             int nx, int ny, int nz, double* out9);

/*
 *  Estimates the bounding box of a shape's interior (where the tree is
 *  <= 0) within the search region, by branch-and-bound on interval
 *  arithmetic.  The box is conservative (never smaller than the shape),
 *  within about 0.1% of the shape's size.  Returns false if the shape has
 *  no interior in the region or the search needs more than max_evals
 *  interval evaluations or max_seconds (if > 0) of time; a search cut
 *  short while refining returns the coarser box found so far.  Sides that reach the search region's faces (the
 *  shape may continue past them, e.g. a half-space) are flagged in `open`
 *  (bit 2*axis for the lower side, 2*axis+1 for the upper), if not NULL.
 */
bool libfive_tree_bounds(libfive_tree t, libfive_region3 search, int max_evals,
                         float max_seconds, libfive_region3* out, int* open);

/*
 *  The same for a shape with variables: `vars` (count of them) are the variable trees and `values`
 *  the numbers they stand for.
 */
bool libfive_tree_bounds_vars(libfive_tree t, libfive_region3 search, int max_evals, float max_seconds,
                              libfive_region3* out, int* open,
                              const libfive_tree* vars, const float* values, int count);

/*
 *  The lowest value a tree can take anywhere in a box (interval arithmetic:
 *  a lower bound, never too high), with the numbers `values` for the
 *  variables `vars` (count of each).  A field with a positive lower bound
 *  is nowhere inside (negative) in the box.
 */
float libfive_tree_interval_lower(libfive_tree t, libfive_region3 box,
                                  const libfive_tree* vars, const float* values, int count);

/*
 *  Evaluates the partial derivatives of a math tree at a specific point,
 *  with respect to x, y, z.
 */
libfive_vec3 libfive_tree_eval_d(libfive_tree t, libfive_vec3 p);

/*  A second, independently owned handle to the same tree  */
libfive_tree libfive_tree_copy(libfive_tree t);

/*
 *  A script run's progress, for a GUI (see run_progress.hpp): the runner
 *  says how many steps (top-level statements) the script has and which one
 *  runs; an operation in a step (a loop, progress()) counts how far it is.
 *  Operations nest: one begun inside another counts within the share the
 *  outer one gave it with libfive_run_task_span.
 */
void libfive_run_begin(int steps);
void libfive_run_step(int index, const char* label);
/*  The (1-based) line the step just begun starts on  */
void libfive_run_step_line(int line);
void libfive_run_end(void);
void libfive_run_task_begin(const char* name);
void libfive_run_task_set(double fraction, const char* detail);
void libfive_run_task_span(double a, double b);
void libfive_run_task_end(void);

/*
 *  Static linear-elastic FEA of a shape on a voxel grid (see fea.hpp):
 *  create a problem over a region with an element size and a material
 *  (Young's modulus E, Poisson's ratio nu), add supports / loads given as
 *  region shapes, prepare (voxelize), solve, then read result fields as
 *  trees.  Functions returning int give 1 on success; the message says why
 *  not (or summarises the solve).
 */
typedef struct libfive_fea_ libfive_fea;
libfive_fea* libfive_fea_new(libfive_tree shape, libfive_region3 region,
                             float element_size, float E, float nu);
/*  The element the voxel grid is made of (Element in fea.hpp), before
 *  libfive_fea_prepare: 0 tetrahedra, six to a cell (the default), 1 hexahedra
 *  with incompatible modes, 2 plain hexahedra  */
void libfive_fea_set_element(libfive_fea* f, int element);
void libfive_fea_add_support(libfive_fea* f, libfive_tree region, int x, int y, int z);
void libfive_fea_add_force(libfive_fea* f, libfive_tree region, float fx, float fy, float fz);
/*  The same in a load case (0, 1, ...): topology optimization makes the part
 *  stiff for each case on its own; a static analysis applies every load  */
void libfive_fea_add_force_case(libfive_fea* f, libfive_tree region, float fx, float fy, float fz,
                                int load_case);
void libfive_fea_set_gravity(libfive_fea* f, float gx, float gy, float gz, float density);
/*  Thermal expansion: the part at the temperature field (a tree), alpha in
 *  1/K, stress-free at the reference temperature  */
void libfive_fea_set_thermal(libfive_fea* f, libfive_tree temperature, float alpha, float reference);
/*  Modal analysis (see fea.hpp): the `count` lowest natural frequencies and
 *  mode shapes, held by the supports (no loads needed); density in t/mm^3.
 *  1 on success.  Then: libfive_fea_mode_count, _mode_frequency (Hz),
 *  _mode_field (fields 1 |u|, 2-4 ux uy uz, largest movement 1), and the
 *  field's range.  */
int libfive_fea_modal(libfive_fea* f, int count, float density, int max_iterations, float tolerance);
int libfive_fea_mode_count(libfive_fea* f);
double libfive_fea_mode_frequency(libfive_fea* f, int i);
libfive_tree libfive_fea_mode_field(libfive_fea* f, int i, int field);
float libfive_fea_mode_field_min(libfive_fea* f, int i, int field);
float libfive_fea_mode_field_max(libfive_fea* f, int i, int field);
int libfive_fea_prepare(libfive_fea* f);
uint64_t libfive_fea_hash(libfive_fea* f);
/*  The solved problem kept in a file (the result cache), and read back into a handle that answers every query
 *  as the solved one did; set_salt before solving makes the results' field keys a function of the salt (the
 *  whole problem as asked), the same in every session.  save: 1 on success; load: NULL when the file is not one  */
int libfive_fea_save(libfive_fea* f, const char* path);
libfive_fea* libfive_fea_load(const char* path);
void libfive_fea_set_salt(libfive_fea* f, uint64_t salt);
int libfive_fea_solve(libfive_fea* f, int max_iterations, float tolerance);
const char* libfive_fea_message(libfive_fea* f);
/*  Fields: 0 von Mises, 1 |u|, 2-4 ux uy uz, 5-10 sxx syy szz sxy syz szx,
 *  11 max principal, 12 min principal, 13 strain energy density  */
libfive_tree libfive_fea_field(libfive_fea* f, int field);
float libfive_fea_field_min(libfive_fea* f, int field);
float libfive_fea_field_max(libfive_fea* f, int field);
/*  0 elements, 1 nodes, 2 dofs, 3 iterations, 4 residual, 5 seconds,
 *  6 volume, 7 compliance, 8-10 support reaction xyz, 11-13 total load xyz,
 *  14 fixed nodes, 15 loaded nodes, 16-18 grid elements along x y z  */
double libfive_fea_stat(libfive_fea* f, int which);
/*  What the analysis is made of (as libfive_fea_set_element), and how many
 *  elements a cell of the grid is: 6 tetrahedra, or 1  */
int libfive_fea_element(libfive_fea* f);
int libfive_fea_elements_per_cell(libfive_fea* f);
/*  After a solve: field `field` (as libfive_fea_field) in each element the
 *  solver used, as that element has it (a tetrahedron's own stress, not the
 *  nodal averages): the cells with a fraction > 0 in order (as
 *  libfive_fea_elements), libfive_fea_elements_per_cell values each.  Returns
 *  the number of values; fills `out` (up to `capacity`) when it is not null.  */
int64_t libfive_fea_element_values(libfive_fea* f, int field, float* out, int64_t capacity);
/*  The smallest and largest of those values (1 on success)  */
int libfive_fea_element_range(libfive_fea* f, int field, float* lo, float* hi);
/*  The element grid: its lower corner, element size and element counts;
 *  0 before prepare.  libfive_fea_elements fills `out` (ex * ey * ez
 *  floats, x fastest) with each element's fill fraction, 0 = none.  */
int libfive_fea_grid(libfive_fea* f, double* lower3, double* h, int* dims3);
void libfive_fea_elements(libfive_fea* f, float* out);
void libfive_fea_delete(libfive_fea* f);

/*
 *  A tetrahedral mesh of the inside of a shape whose boundary is the shape's
 *  surface (see tetmesh.hpp).  libfive_tetmesh_new returns null when the mesh
 *  can't be made (libfive_tetmesh_last_message says why).
 */
typedef struct libfive_tetmesh_ libfive_tetmesh;
libfive_tetmesh* libfive_tetmesh_new(libfive_tree shape, libfive_region3 region, float h);
const char* libfive_tetmesh_last_message(void);
/*  vertices, tetrahedra, boundary triangles  */
void libfive_tetmesh_counts(libfive_tetmesh* m, int64_t* out3);
void libfive_tetmesh_vertices(libfive_tetmesh* m, double* out);     // 3 per vertex
void libfive_tetmesh_tets(libfive_tetmesh* m, int32_t* out);        // 4 per tetrahedron
void libfive_tetmesh_faces(libfive_tetmesh* m, int32_t* out);       // 3 per boundary triangle, normals out
/*  volume, min dihedral (degrees), max dihedral, mean of each tetrahedron's minimum dihedral,
 *  smallest volume (of h^3 / 12), inverted tetrahedra, faces shared by more than two
 *  tetrahedra, open boundary edges, boundary area, dropped degenerate tetrahedra  */
void libfive_tetmesh_quality(libfive_tetmesh* m, double* out13);   // (then: folded, sharp-feature vertices, snaps reverted)
void libfive_tetmesh_delete(libfive_tetmesh* m);

/*
 *  Static linear-elastic FEA on a body-fitted tetrahedral mesh (see tetfea.hpp):
 *  the same calls as libfive_fea_*, for the analysis whose elements are
 *  tetrahedra that follow the part's surface.  The fields are numbered as
 *  libfive_fea_field.
 */
typedef struct libfive_tetfea_ libfive_tetfea;
libfive_tetfea* libfive_tetfea_new(libfive_tree shape, libfive_region3 region,
                                   float element_size, float E, float nu);
void libfive_tetfea_add_support(libfive_tetfea* f, libfive_tree region, int x, int y, int z);
void libfive_tetfea_add_force(libfive_tetfea* f, libfive_tree region, float fx, float fy, float fz);
void libfive_tetfea_set_gravity(libfive_tetfea* f, float gx, float gy, float gz, float density);
void libfive_tetfea_set_thermal(libfive_tetfea* f, libfive_tree temperature, float alpha, float reference);
/*  Material properties as fields (see TetProblem::setStiffnessField): Young's modulus in MPa (the E given to _new is then
 *  only what the stiffness matrix is made with), the density, the thermal expansion coefficient; and a load whose total is
 *  spread over its region by a profile (a field, not negative) instead of evenly  */
void libfive_tetfea_set_stiffness_field(libfive_tetfea* f, libfive_tree E);
void libfive_tetfea_set_density_field(libfive_tetfea* f, libfive_tree density);
void libfive_tetfea_set_expansion_field(libfive_tetfea* f, libfive_tree alpha);
void libfive_tetfea_add_force_profile(libfive_tetfea* f, libfive_tree region, float fx, float fy, float fz,
                                      int load_case, libfive_tree profile);
/*  Meshes the part and resolves supports and loads; 1 on success (else the message says why)  */
int libfive_tetfea_prepare(libfive_tetfea* f);
uint64_t libfive_tetfea_hash(libfive_tetfea* f);
/*  The solved problem kept in a file (the result cache), and read back into a handle that answers every query
 *  as the solved one did; set_salt before solving makes the results' field keys a function of the salt (the
 *  whole problem as asked), the same in every session.  save: 1 on success; load: NULL when the file is not one  */
int libfive_tetfea_save(libfive_tetfea* f, const char* path);
libfive_tetfea* libfive_tetfea_load(const char* path);
void libfive_tetfea_set_salt(libfive_tetfea* f, uint64_t salt);
/*  Before libfive_tetfea_optimize: the design space (the shape the problem was made of) is the part grown outwards, and
 *  `origin` is the part: the volume fraction is then of the part's volume and material may go outside it  */
void libfive_tetfea_set_origin(libfive_tetfea* f, libfive_tree origin);
/*  Before libfive_tetfea_optimize: how crisp the design is -- the density is projected towards 0 and 1 with a step that grows
 *  steeper up to this number (default 16; 1 or less: not projected, grey regions stay)  */
void libfive_tetfea_set_sharpness(libfive_tetfea* f, double sharpness);
/*  A support that holds the part only in one load case (0, 1, ...) -- optimising against several sets of boundary conditions, each with its
 *  own supports.  Supports added with libfive_tetfea_add_support hold it in every case  */
void libfive_tetfea_add_support_case(libfive_tetfea* f, libfive_tree region, int x, int y, int z, int load_case);
/*  Before libfive_tetfea_optimize: keep the design symmetric about the plane axis (0, 1, 2 = x, y, z) = at, or (at_centre) through the middle
 *  of the part's extent along that axis; or (set_symmetry_auto) about every plane the whole problem is symmetric about  */
void libfive_tetfea_add_mirror(libfive_tetfea* f, int axis, double at, int at_centre);
void libfive_tetfea_set_symmetry_auto(libfive_tetfea* f, int on);
/*  After libfive_tetfea_optimize: 0 if the design was not kept symmetric about a plane perpendicular to the axis, 1 if it was (asked for), 2 if it
 *  was found to be symmetric and kept so; *at is the plane's coordinate  */
int libfive_tetfea_mirror(libfive_tetfea* f, int axis, double* at);
int libfive_tetfea_solve(libfive_tetfea* f, int max_iterations, float tolerance);
const char* libfive_tetfea_message(libfive_tetfea* f);
libfive_tree libfive_tetfea_field(libfive_tetfea* f, int field);
float libfive_tetfea_field_min(libfive_tetfea* f, int field);
float libfive_tetfea_field_max(libfive_tetfea* f, int field);
/*  0 elements (tetrahedra), 1 nodes, 2 dofs, 3 iterations, 4 residual, 5 seconds, 6 volume,
 *  7 compliance, 8-10 support reaction xyz, 11-13 total load xyz, 14 fixed nodes,
 *  15 loaded nodes, 19 loose tetrahedra left out  */
double libfive_tetfea_stat(libfive_tetfea* f, int which);
/*  vertices, tetrahedra, boundary triangles of the (prepared) mesh  */
void libfive_tetfea_counts(libfive_tetfea* f, int64_t* out3);
/*  The mesh: 3 floats per vertex, 4 ints per tetrahedron, 3 per boundary triangle
 *  (normals out), and the tetrahedron each of those belongs to  */
void libfive_tetfea_mesh(libfive_tetfea* f, float* vertices, int32_t* tets, int32_t* faces, int32_t* face_tet);
/*  The solved displacements at the vertices: 3 floats each (interleaved)  */
void libfive_tetfea_node_displacements(libfive_tetfea* f, float* out);
/*  A field in each tetrahedron as it has it (its own stress; the displacement at its centre),
 *  and the range of those; as libfive_fea_element_values / _range  */
int64_t libfive_tetfea_element_values(libfive_tetfea* f, int field, float* out, int64_t capacity);
int libfive_tetfea_element_range(libfive_tetfea* f, int field, float* lo, float* hi);
/*  Modal analysis on the mesh (see tetfea.hpp; as libfive_fea_modal): the `count` lowest natural
 *  frequencies and mode shapes, held by the supports (no loads needed); density in t/mm^3.
 *  1 on success; the mode fields are numbered as libfive_fea_field (1 |u|, 2-4 ux uy uz).  */
/*  A load in a load case (0, 1, ...): topology optimization makes the part stiff for each case on its
 *  own; a static analysis applies every load together  */
void libfive_tetfea_add_force_case(libfive_tetfea* f, libfive_tree region, float fx, float fy, float fz,
                                   int load_case);
/*  Topology optimization on the mesh (as libfive_fea_optimize): density per tetrahedron, SIMP with a
 *  volume-weighted filter and optimality-criteria updates.  Then libfive_tetfea_density (a field) and
 *  libfive_tetfea_history (the compliance per iteration).  */
int libfive_tetfea_optimize(libfive_tetfea* f, float volume_fraction, float penalty, float filter_radius,
                            int iterations, const libfive_tree* keep, int keep_count,
                            const libfive_tree* avoid, int avoid_count, int solver_iterations, float tolerance,
                            int extrude);
libfive_tree libfive_tetfea_density(libfive_tetfea* f);
int libfive_tetfea_history(libfive_tetfea* f, double* out, int max);
/*  The density field after iteration k (0-based, one per compliance value; NULL past the end)  */
libfive_tree libfive_tetfea_density_at(libfive_tetfea* f, int k);
/*  How many separate pieces the optimised design is in when it is cut at the density `threshold`: the part is where the
 *  density at the nodes is above it, and a link less than `margin` above it is too thin to count (specks under 2 % of
 *  the body are not counted); 0 if there is no result  */
int libfive_tetfea_pieces(libfive_tetfea* f, double threshold, double margin);
int libfive_tetfea_modal(libfive_tetfea* f, int count, float density, int max_iterations, float tolerance);
int libfive_tetfea_mode_count(libfive_tetfea* f);
double libfive_tetfea_mode_frequency(libfive_tetfea* f, int i);
libfive_tree libfive_tetfea_mode_field(libfive_tetfea* f, int i, int field);
float libfive_tetfea_mode_field_min(libfive_tetfea* f, int i, int field);
float libfive_tetfea_mode_field_max(libfive_tetfea* f, int i, int field);
void libfive_tetfea_delete(libfive_tetfea* f);

/*
 *  Steady-state heat conduction on a body-fitted tetrahedral mesh (see tetthermal.hpp): the same
 *  calls as libfive_thermal_*.  Fields: 0 temperature, 1 heat flux, 2-4 qx qy qz; stats: 0 elements,
 *  1 nodes, 2 iterations, 3 residual, 4 seconds, 5 heat in, 6 heat out through fixed temperatures,
 *  7 heat out by convection.
 */
typedef struct libfive_tetthermal_ libfive_tetthermal;
libfive_tetthermal* libfive_tetthermal_new(libfive_tree shape, libfive_region3 region, float element_size,
                                           float conductivity);
void libfive_tetthermal_add_temperature(libfive_tetthermal* f, libfive_tree region, float value);
void libfive_tetthermal_add_heat(libfive_tetthermal* f, libfive_tree region, float watts);
void libfive_tetthermal_add_generation(libfive_tetthermal* f, libfive_tree region, float watts);
void libfive_tetthermal_add_convection(libfive_tetthermal* f, libfive_tree region, float coefficient, float ambient);
/*  The same with fields (see TetThermalProblem::setConductivityField; a field that is NULL is the number)  */
void libfive_tetthermal_set_conductivity_field(libfive_tetthermal* f, libfive_tree k);
void libfive_tetthermal_add_temperature_field(libfive_tetthermal* f, libfive_tree region, float value, libfive_tree field);
void libfive_tetthermal_add_heat_profile(libfive_tetthermal* f, libfive_tree region, float watts, libfive_tree profile);
void libfive_tetthermal_add_generation_profile(libfive_tetthermal* f, libfive_tree region, float watts, libfive_tree profile);
void libfive_tetthermal_add_convection_fields(libfive_tetthermal* f, libfive_tree region, float coefficient,
                                              libfive_tree coefficient_field, float ambient, libfive_tree ambient_field);
int libfive_tetthermal_prepare(libfive_tetthermal* f);
uint64_t libfive_tetthermal_hash(libfive_tetthermal* f);
/*  The solved problem kept in a file (the result cache), and read back into a handle that answers every query
 *  as the solved one did; set_salt before solving makes the results' field keys a function of the salt (the
 *  whole problem as asked), the same in every session.  save: 1 on success; load: NULL when the file is not one  */
int libfive_tetthermal_save(libfive_tetthermal* f, const char* path);
libfive_tetthermal* libfive_tetthermal_load(const char* path);
void libfive_tetthermal_set_salt(libfive_tetthermal* f, uint64_t salt);
int libfive_tetthermal_solve(libfive_tetthermal* f, int max_iterations, float tolerance);
const char* libfive_tetthermal_message(libfive_tetthermal* f);
libfive_tree libfive_tetthermal_field(libfive_tetthermal* f, int field);
float libfive_tetthermal_field_min(libfive_tetthermal* f, int field);
float libfive_tetthermal_field_max(libfive_tetthermal* f, int field);
double libfive_tetthermal_stat(libfive_tetthermal* f, int which);
void libfive_tetthermal_delete(libfive_tetthermal* f);

/*
 *  Steady incompressible laminar flow on a body-fitted tetrahedral mesh of the fluid domain (see
 *  tetflow.hpp): inlets, outlets, walls and slip planes given as regions.  Fields: 0 speed,
 *  1-3 vx vy vz, 4 pressure, 5 total pressure, 6 shear rate, 7 vorticity.  Stats: 0 elements,
 *  1 nodes, 2 unknowns, 3 nonlinear iterations, 4 residual, 5 seconds, 6 inlet flow, 7 outlet
 *  flow, 8 imbalance, 9 pressure drop, 10 max speed, 11 dissipation (N mm / s), 12 Reynolds
 *  number, 13 cell Reynolds number, 14 hydraulic diameter, 15 elements across it, 16-18 wall
 *  force xyz, 19 converged, 20 the linear solver (0 direct, 1 iterative), 21 net flow in through the walls.
 */
typedef struct libfive_tetflow_ libfive_tetflow;
libfive_tetflow* libfive_tetflow_new(libfive_tree domain, libfive_region3 region, float element_size,
                                     float density, float viscosity);
/*  direction (zero: along the inward normal), the mean speed (<= 0: the direction's length), a flow
 *  rate (> 0 overrides the speed), profile 0 uniform / 1 developed  */
void libfive_tetflow_add_inlet(libfive_tetflow* f, libfive_tree region, float dx, float dy, float dz, float speed,
                               float flow_rate, int profile);
void libfive_tetflow_add_outlet(libfive_tetflow* f, libfive_tree region, float pressure);
void libfive_tetflow_add_wall(libfive_tetflow* f, libfive_tree region, float vx, float vy, float vz);
void libfive_tetflow_add_slip(libfive_tetflow* f, libfive_tree region);
void libfive_tetflow_set_gravity(libfive_tetflow* f, float gx, float gy, float gz);
void libfive_tetflow_set_options(libfive_tetflow* f, int stokes, int nonlinear_iterations, float relaxation,
                                 int direct_limit, int linear_iterations, float linear_tolerance, int newton);
int libfive_tetflow_prepare(libfive_tetflow* f);
uint64_t libfive_tetflow_hash(libfive_tetflow* f);
/*  The solved problem kept in a file (the result cache), and read back into a handle that answers every query
 *  as the solved one did; set_salt before solving makes the results' field keys a function of the salt (the
 *  whole problem as asked), the same in every session.  save: 1 on success; load: NULL when the file is not one  */
int libfive_tetflow_save(libfive_tetflow* f, const char* path);
libfive_tetflow* libfive_tetflow_load(const char* path);
void libfive_tetflow_set_salt(libfive_tetflow* f, uint64_t salt);
int libfive_tetflow_solve(libfive_tetflow* f, float tolerance);
const char* libfive_tetflow_message(libfive_tetflow* f);
const char* libfive_tetflow_warning(libfive_tetflow* f);
libfive_tree libfive_tetflow_field(libfive_tetflow* f, int field);
float libfive_tetflow_field_min(libfive_tetflow* f, int field);
float libfive_tetflow_field_max(libfive_tetflow* f, int field);
double libfive_tetflow_stat(libfive_tetflow* f, int which);
/*  kind 0: the flow through each inlet, 1: through each outlet, 2: the force on each wall item
 *  (3 values each); fills up to max values, returns how many there are  */
int libfive_tetflow_items(libfive_tetflow* f, int kind, double* out, int max);
/*  The flow in time (see tetflow.hpp): from the Stokes flow at t = 0, `steps` steps of dt seconds, every
 *  store_every-th kept (and the last).  Then libfive_tetflow_step_count, _step_time, _step_field (as
 *  libfive_tetflow_field, for step k), _step_field_min / _max, _step_stat (as libfive_tetflow_stat).  The
 *  flow of the last step is also libfive_tetflow_field / _stat.  */
int libfive_tetflow_solve_transient(libfive_tetflow* f, float dt, int steps, int store_every, float tolerance);
int libfive_tetflow_step_count(libfive_tetflow* f);
double libfive_tetflow_step_time(libfive_tetflow* f, int k);
/*  The solver iteration a step is of (0 = the Stokes start; a steady solve keeps every iteration as a step), or
 *  -1 for a stored time of a flow in time  */
int libfive_tetflow_step_iteration(libfive_tetflow* f, int k);
libfive_tree libfive_tetflow_step_field(libfive_tetflow* f, int k, int field);
float libfive_tetflow_step_field_min(libfive_tetflow* f, int k, int field);
float libfive_tetflow_step_field_max(libfive_tetflow* f, int k, int field);
double libfive_tetflow_step_stat(libfive_tetflow* f, int k, int which);
/*  Streamlines of the flow (step k, or the last / the steady flow for k < 0) from `n` seed points (3 doubles
 *  each): each line's points as x y z speed time (5 doubles) written one line after another into `out` (up to
 *  `capacity` doubles), `counts[i]` the number of points of line i; returns the number of doubles the lines
 *  need (call again with a larger buffer when it is more than the capacity).  */
int libfive_tetflow_streamlines(libfive_tetflow* f, int k, const double* seeds, int n, double max_time, int max_points,
                                int backward, double* out, int capacity, int* counts);
/*  Up to n points over the inlets to seed streamlines from (3 doubles each); returns how many  */
int libfive_tetflow_inlet_seeds(libfive_tetflow* f, int n, double* out);
/*  Shape optimisation of a body in the flow (see tetflow.hpp): `body` (solid where negative) is optimised
 *  inside `region` (NULL: anywhere in the fluid domain) for the objective w_drag * drag - w_lift * lift, minimised
 *  (drag along the flow direction d -- zero: the inlets' mean -- lift along l -- zero: perpendicular to d in the
 *  domain's long plane), with the solid volume between v_min and v_max times the body's own; elements in `keep`
 *  stay solid, in `avoid` fluid; extrude -1, or 0 / 1 / 2 for a design constant along x / y / z; darcy sets the
 *  solid's friction (mu / (darcy D^2)).  1 on success; then libfive_tetflow_level is the body as a level set (mm, > 0
 *  inside it, its boundary the zero level), libfive_tetflow_level_at the same after iteration k, libfive_tetflow_history fills up to max
 *  values of the drag (kind 0) or the lift (kind 1, N), one per iteration and the final body's last; the flow of
 *  every iteration is a step (libfive_tetflow_step_*, its iteration the step's), the last around the final body,
 *  which is also libfive_tetflow_field.  libfive_tetflow_direction gives d (kind 0) or l (kind 1).  */
int libfive_tetflow_optimize(libfive_tetflow* f, libfive_tree body, libfive_tree region, float w_drag, float w_lift,
                             float dx, float dy, float dz, float lx, float ly, float lz, float v_min, float v_max,
                             float filter_radius, int iterations, const libfive_tree* keep, int keep_count,
                             const libfive_tree* avoid, int avoid_count, int extrude, float darcy);
/*  Before libfive_tetflow_optimize: keep the design symmetric about the plane axis (0, 1, 2 = x, y, z) = at, or (at_centre) through the middle of
 *  the domain's extent along that axis; or (set_symmetry_auto) about every plane the whole problem is symmetric about  */
void libfive_tetflow_add_mirror(libfive_tetflow* f, int axis, double at, int at_centre);
void libfive_tetflow_set_symmetry_auto(libfive_tetflow* f, int on);
/*  After libfive_tetflow_optimize: 0 if the design was not kept symmetric about a plane perpendicular to the axis, 1 if it was (asked for), 2 if it
 *  was found to be symmetric and kept so; *at is the plane's coordinate  */
int libfive_tetflow_mirror(libfive_tetflow* f, int axis, double* at);
libfive_tree libfive_tetflow_level(libfive_tetflow* f);
int libfive_tetflow_history(libfive_tetflow* f, int kind, double* out, int max);
void libfive_tetflow_direction(libfive_tetflow* f, int kind, double* out3);
/*  The design field after iteration k (0-based, one per power value; NULL past the end)  */
libfive_tree libfive_tetflow_level_at(libfive_tetflow* f, int k);
void libfive_tetflow_delete(libfive_tetflow* f);

/*
 *  Steady-state heat conduction on the same voxel grid (see thermal.hpp):
 *  fixed temperatures, heat inputs (W) and convection (W / (mm^2 K) to an
 *  ambient temperature) given as regions.  Fields: 0 temperature, 1 heat
 *  flux magnitude, 2-4 its x / y / z parts.  Stats: 0 elements, 1 nodes,
 *  2 iterations, 3 residual, 4 seconds, 5 heat in, 6 heat out through the
 *  fixed temperatures, 7 heat out by convection.
 */
typedef struct libfive_thermal_ libfive_thermal;
libfive_thermal* libfive_thermal_new(libfive_tree shape, libfive_region3 region,
                                     float element_size, float conductivity);
/*  The element of the grid (as libfive_fea_set_element): 0 tetrahedra, six to a
 *  cell (the default), 1 or 2 hexahedra; before preparing  */
void libfive_thermal_set_element(libfive_thermal* t, int element);
void libfive_thermal_add_temperature(libfive_thermal* t, libfive_tree region, float value);
void libfive_thermal_add_heat(libfive_thermal* t, libfive_tree region, float power);
void libfive_thermal_add_convection(libfive_thermal* t, libfive_tree region, float coefficient,
                                    float ambient);
/*  A total power (W) generated in the part's volume inside a region  */
void libfive_thermal_add_generation(libfive_thermal* t, libfive_tree region, float power);
int libfive_thermal_solve(libfive_thermal* t, int max_iterations, float tolerance);
/*  Voxelizes and resolves the boundary conditions (1 on success); the hash
 *  of the prepared problem, for caching  */
int libfive_thermal_prepare(libfive_thermal* t);
uint64_t libfive_thermal_hash(libfive_thermal* t);
/*  The solved problem kept in a file (the result cache), and read back into a handle that answers every query
 *  as the solved one did; set_salt before solving makes the results' field keys a function of the salt (the
 *  whole problem as asked), the same in every session.  save: 1 on success; load: NULL when the file is not one  */
int libfive_thermal_save(libfive_thermal* f, const char* path);
libfive_thermal* libfive_thermal_load(const char* path);
void libfive_thermal_set_salt(libfive_thermal* f, uint64_t salt);
/*  Thermal topology optimization (see thermal.hpp; extrude: -1, or 0 / 1 / 2
 *  for a design constant along x / y / z): 1 on success;
 *  libfive_thermal_density is then the density field (0..1) and
 *  libfive_thermal_history fills up to max values of the heat-weighted
 *  mean temperature of the heat inputs, one per iteration  */
int libfive_thermal_optimize(libfive_thermal* t, float volume_fraction, float penalty,
                             float filter_radius, int iterations, float move,
                             const libfive_tree* keep, int keep_count,
                             const libfive_tree* avoid, int avoid_count,
                             int solver_iterations, float tolerance, int extrude);
libfive_tree libfive_thermal_density(libfive_thermal* t);
int libfive_thermal_history(libfive_thermal* t, double* out, int max);
const char* libfive_thermal_message(libfive_thermal* t);
libfive_tree libfive_thermal_field(libfive_thermal* t, int field);
float libfive_thermal_field_min(libfive_thermal* t, int field);
float libfive_thermal_field_max(libfive_thermal* t, int field);
double libfive_thermal_stat(libfive_thermal* t, int which);
void libfive_thermal_delete(libfive_thermal* t);
/*  Topology optimization of a prepared problem (see fea.hpp): returns 1 on
 *  success; libfive_fea_density is then the density field (0..1) and
 *  libfive_fea_history fills up to max compliance values (one per
 *  iteration), returning how many there are.  */
int libfive_fea_optimize(libfive_fea* f, float volume_fraction, float penalty,
                         float filter_radius, int iterations, float move,
                         const libfive_tree* keep, int keep_count,
                         const libfive_tree* avoid, int avoid_count,
                         int solver_iterations, float tolerance, int extrude);
libfive_tree libfive_fea_density(libfive_fea* f);
int libfive_fea_history(libfive_fea* f, double* out, int max);

/*
 *  Evaluates a math tree at n points (xyz holds 3n floats) into out (n
 *  floats), with one evaluator per thread -- much faster than n calls
 *  to libfive_tree_eval_f for a large tree.
 */
void libfive_tree_eval_points(libfive_tree t, const float* xyz, int n, float* out);

/*
 *  Meshes a tree with variables (see libfive_tree_render_mesh): `vars` (count of them) are the variable
 *  trees and `values` the numbers they stand for.
 */
libfive_mesh* libfive_tree_render_mesh_vars(libfive_tree tree, libfive_region3 R, float res,
                                            const libfive_tree* vars, const float* values, int count);

/*
 *  The same for a shape with variables: `vars` (count of them) are the variable trees and `values` the
 *  numbers they stand for (without them every variable reads as 0).
 */
void libfive_tree_eval_points_vars(libfive_tree t, const float* xyz, int n, float* out,
                                   const libfive_tree* vars, const float* values, int count);

/*
 *  Deletes a tree.  If binding in a higher-level language, call this in
 *  a destructor / finalizer to avoid leaking memory
 */
void libfive_tree_delete(libfive_tree ptr);

/*
 *  Graph (beam) lattices.  A graph is nodes (3 floats each) and beams (2 node
 *  indices each); the generators return one (free it with
 *  libfive_graph_delete), or NULL with libfive_lattice_last_error() set.
 *    volume:  Poisson-disk points `spacing` apart in the body (within the
 *             region), mode 0 = Delaunay edges, 1 = Voronoi cell edges,
 *             `relax` regularizing iterations
 *    surface: points on the body's surface, mode 0 = triangles, 1 = the
 *             dual (Voronoi / hexagon-like cells)
 *    points:  Delaunay (0) or Voronoi (1) graph of the given points
 *  libfive_beam_lattice makes round beams along a graph (radius per node,
 *  varying linearly along each beam; blend > 0 rounds the joints).
 */
typedef struct libfive_graph {
    float* nodes;
    int node_count;
    int* beams;
    int beam_count;
} libfive_graph;
libfive_graph* libfive_lattice_volume_graph(libfive_tree body, libfive_region3 region,
                                            float spacing, int mode, int relax, unsigned seed);
libfive_graph* libfive_lattice_surface_graph(libfive_tree body, libfive_region3 region,
                                             float spacing, int mode, unsigned seed);
libfive_graph* libfive_lattice_points_graph(const float* points, int count, int mode);
void libfive_graph_delete(libfive_graph* g);
const char* libfive_lattice_last_error(void);
/*  What the last conformal layout leaves in doubt (a map whose topology is not certain, holes that were closed); empty when nothing */
const char* libfive_lattice_last_warning(void);
libfive_tree libfive_beam_lattice(const float* nodes, int node_count, const int* beams,
                                  int beam_count, const float* radii, float blend);

/*
 *  Analysis fields of a shape: mode 0..2 = x/y/z of the unit normal, 3 = the
 *  gradient's length, 4..6 = raw gradient; the local wall thickness (capped
 *  at max_thickness); the mean curvature (central differences of step h).
 */
libfive_tree libfive_field_gradient(libfive_tree t, int mode);
libfive_tree libfive_field_thickness(libfive_tree t, float max_thickness);
libfive_tree libfive_field_curvature(libfive_tree t, float h);
/*  A field through n scattered samples (xyz: 3n floats, values: n floats),
 *  inverse-distance weighted over the k nearest; and seeded Perlin noise  */
libfive_tree libfive_field_points(const float* xyz, const float* values, int n, int k,
                                  float power);
libfive_tree libfive_field_noise(float scale, int octaves, unsigned seed, float gain,
                                 float lacunarity);

/*  Debug: print the native stack of the first access violation (Windows)  */
void libfive_debug_crash_handler(void);

/*  Serializes the given tree to a file, return true on success.
 *  The file format is not archival, and may change without notice */
bool libfive_tree_save(libfive_tree ptr, const char* filename);

/*  Can the whole tree be kept in a file and read back?  (Every oracle in it knows how to save itself.)  libfive_tree_save
 *  refuses a tree that cannot.  */
bool libfive_tree_can_save(libfive_tree ptr);

/*
 *  Which of the coordinates a tree depends on: bit 0 for x, bit 1 for y, bit 2 for z (a tree that holds an oracle,
 *  which is a field of all of space, has all three).  A shape of no z is a 2D profile.
 */
int libfive_tree_axes(libfive_tree ptr);

/*  Deserializes a tree from a file. */
libfive_tree libfive_tree_load(const char* filename);

/*  One part (placed solid) of a STEP file, reconstructed as its own tree
 *  with its own tight bounding box (see libfive_import_step_parts_reconstructed).
 *  `tree` is owned by the caller -- free it with libfive_tree_delete; it is
 *  not freed by libfive_step_parts_delete. */
typedef struct libfive_step_part {
    libfive_tree tree;
    libfive_region3 bounds;
    /*  NULL unless this solid could not be reconstructed -- then `tree` is
     *  a meaningless placeholder and `error` says why (owned by this
     *  struct; freed by libfive_step_parts_delete) */
    char* error;
    /*  Which occurrence this is: its assembly path (e.g.
     *  "Drive:1/Motor:1/M3x10-Screw:2") or the solid's name; owned by
     *  this struct, NULL if there is none  */
    char* name;
    /*  Where B-spline faces were approximated: the fit's relative error
     *  (deviation / face size) on and near each approximated face, 0
     *  elsewhere; NULL if nothing was noticeably approximated.  Owned by
     *  the caller, like `tree`.  */
    libfive_tree marker;
    /*  Which solid of the file and which of its instances this part is
     *  (for libfive_step_exact_surface)  */
    int32_t solid;
    int32_t instance;
    /*  Hints for choosing a render resolution, in the delivered units: the
     *  part's smallest significant feature (its thinnest side, or the
     *  smallest radius among faces that matter), and its surface area,
     *  planar faces and all others; 0 if unknown  */
    double detail;
    double area_flat;
    double area_curved;
} libfive_step_part;

typedef struct libfive_step_parts {
    libfive_step_part* parts;
    uint32_t count;
} libfive_step_parts;

/*  Imports a STEP (ISO 10303-21) file with a small built-in reader (no
 *  external CAD kernel): every solid is reconstructed as plain CSG of its
 *  surfaces (see step_reconstruct.hpp) -- analytic faces exactly, B-spline
 *  faces as fitted closed-form surfaces (see step_fit.hpp and
 *  libfive_import_step_fit_report) -- one tree per placed solid.  A solid
 *  that can't be reconstructed doesn't fail the call: check each part's
 *  `error`.  NULL only if the file itself couldn't be read; then
 *  libfive_import_step_last_message() says why (on success it returns a
 *  short summary).  Free the result with libfive_step_parts_delete(). */
libfive_step_parts* libfive_import_step_parts_reconstructed(const char* filename);

/*  The same, rebuilding only the `count` solids listed in `solids` (NULL: all): the others come back as parts whose
 *  `error` says they were left out -- for a caller that imports them another way (the tessellation).  */
libfive_step_parts* libfive_import_step_parts_reconstructed_only(const char* filename, const int32_t* solids,
                                                                 uint32_t count);
void libfive_step_parts_delete(libfive_step_parts* parts);

/*  The version of the import algorithm (kImportVersion in step_reconstruct.hpp):
 *  the cache of imported parts stays valid for as long as it is unchanged. */
int libfive_step_import_version(void);
const char* libfive_import_step_last_message(void);

/*  The B-spline faces the last import (on this thread) approximated: one
 *  line per part that has any, "part faces worst_mm worst_face"  */
const char* libfive_import_step_fit_report(void);

/*  An imported part's exact surface, meshed directly from the STEP file (for the exact regions of
 *  fieldes.stdlib.cad_import.exclude, which makes it a distance field with libfive_mesh_from_arrays): the part's
 *  `solid` / `instance`, placed by the row-major 4x4 matrix `m` (NULL: none) after its own placement.
 *  `turn_samples`: points per full turn of a circle (the quality).  NULL on failure;
 *  libfive_import_step_last_message() then says why.  Free with libfive_mesh_delete.  */
libfive_mesh* libfive_step_exact_surface(const char* filename, int solid, int instance,
                                         const double* m, int turn_samples);

/*  The tessellated import: a STEP file as the exact surface itself, nothing reconstructed or fitted.
 *  Every solid is tessellated once, in its own coordinates (the file's units), directly from its trimmed
 *  faces (the free-form faces refined inside their outlines, on all the threads there are); each placed
 *  occurrence of a solid is described by its placement.  The caller makes the mesh a signed distance field
 *  (libfive_mesh_from_arrays) and places it: p = linear * q + offset takes the solid's coordinates q to
 *  millimetres p.  Instances are in the order of libfive_import_step_parts_reconstructed's parts. */
typedef struct libfive_step_brep_solid {
    libfive_mesh* mesh;      /*  NULL if the solid could not be tessellated (then `error` says why)  */
    char* error;
    int32_t faces;
    int32_t bspline_faces;
    int32_t surface;         /*  1: a surface body (an open shell: a sheet, no inside), not a solid  */
} libfive_step_brep_solid;

typedef struct libfive_step_brep_instance {
    int32_t solid;
    int32_t instance;
    double linear[9];        /*  row-major 3 x 3  */
    double offset[3];
    char* name;              /*  the occurrence's assembly path or the solid's name; NULL if none  */
    libfive_region3 bounds;  /*  placed, in millimetres  */
    double detail;           /*  as in libfive_step_part, in millimetres  */
    double area_flat;
    double area_curved;
    double area_bspline;     /*  (of area_curved) the free-form faces: what the share of free-form surface is from  */
} libfive_step_brep_instance;

typedef struct libfive_step_brep {
    libfive_step_brep_solid* solids;
    uint32_t solid_count;
    libfive_step_brep_instance* instances;
    uint32_t instance_count;
} libfive_step_brep;

/*  NULL if the file could not be read (libfive_import_step_last_message() says why).  Free with
 *  libfive_step_brep_delete.  `turn_samples`: points per full turn of a circle (the quality; 64).  */
libfive_step_brep* libfive_step_brep_read(const char* filename, int turn_samples);

/*  The same, tessellating only the `count` solids of `solids` (NULL: all): the others have `error` set ("left out"),
 *  their instances are there, with no areas.  (The areas of libfive_step_brep_read(file, 12) -- the free-form share of
 *  each part -- are a cheap survey of a file.)  */
libfive_step_brep* libfive_step_brep_read_only(const char* filename, int turn_samples, const int32_t* solids,
                                               uint32_t count);
void libfive_step_brep_delete(libfive_step_brep* b);

/*  The version of the tessellation (kTessellationVersion): meshes kept from before it changed are stale.  */
int libfive_step_tessellation_version(void);

/*  `lift` (mm): the base of the layers is this far from the surface along their way (negative: on the other side of it, so that cells
 *  of their own proportions reach through the surface and the caller cuts them there).  0: the layers start on the surface.
 *
 *  `layout`: 0 the surface is a closed BODY (laid out as one closed mesh of quads that covers all of it), 1 a SHEET (a surface with no body, cut by
 *  the box `lo3`..`hi3`), 2 a PATCH of a surface -- `region` is a field that is below 0 within the walk's cover of the patch (a selection's
 *  `.patch`): the cells cover that part of the surface only.  It is told, not guessed.
 *
 *  A strut lattice's cells laid on the surface of `body` (where its field is zero), as a graph of beams -- from the
 *  field alone: its value and gradient, no mesh.  `vars`/`values` (`var_count` of them) are the script's variables.
 *  `lo3`..`hi3` is the box to look for the surface in, `unit_beams` are `unit_beam_count` beams of 6 floats (two ends in
 *  unit-cube coordinates), carried to every cell.  The cells are one closed mesh of quads over the surface, one quad to a
 *  cell (laid out by a global solve: see surface_cells.cpp); `direction3` is the way their rows run where the surface gives
 *  none (zero: along x).  `grid_offset` (0 to 3) is which of four fixed grids the surface is sampled on: the layout is made once,
 *  from that grid, and is the same every time; a part that does not close with one grid may with another (the error says so).
 *  No cell is left out for the way it is distorted.  `height_dir` -1: the layers fill the body (`depth` deep, or as deep as the body is when
 *  `depth` <= 0), +1: they stand out of it.  `layers` <= 0: as many as fit; `radius` <= 0: 12 % of the smaller of the
 *  cell and the layer.  Returns the graph (free it with libfive_graph_delete), or NULL with libfive_lattice_last_error()
 *  set.  `info` (10 doubles, may be NULL) gets the cells made, 0, the nodes, the beams, the direction used, the radius
 *  used, the layers and how deep they are.  */
libfive_graph* libfive_surface_cells(libfive_tree body, const libfive_tree* vars, const float* values, int var_count,
                                     const double* lo3, const double* hi3, const double* direction3, int grid_offset,
                                     int layout, libfive_tree region,
                                     double cell, const float* unit_beams, int unit_beam_count,
                                     double depth, int layers, double radius, double height_dir, double lift, double* info);

/*  A patch of a surface, found by walking over it from a seed: the flood fill of a CAD program, from the field alone (its value
 *  and gradient: no mesh).  `seed3` is a point on or near the surface; the walk spreads over the surface in steps of `spacing`
 *  while it stays within `angle_degrees` of the seed's own direction (mode 0) or turns less than `angle_degrees` for every 10 mm of the walk (mode 1), and within
 *  `max_radius` (<= 0: no limit) of the seed; `lo3`..`hi3` is the box of the body.  Returns the samples of the patch's surface
 *  (about `spacing` apart, with the normals) or NULL with libfive_lattice_last_error() set; free it with
 *  libfive_surface_patch_delete.  libfive_points_distance makes the field of a set of points: the distance to the nearest.  */
typedef struct libfive_surface_patch {
    float* points;          /*  3 floats a sample  */
    float* normals;
    uint32_t count;
    double seed_point[3];   /*  where the seed is on the surface  */
    double seed_distance;
    double spacing;
    int32_t stopped;        /*  1: the walk was cut short (too many samples)  */
} libfive_surface_patch;

libfive_surface_patch* libfive_surface_select(libfive_tree body, const libfive_tree* vars, const float* values, int var_count,
                                              const double* lo3, const double* hi3, const double* seed3,
                                              double angle_degrees, int mode, double max_radius, double spacing);
void libfive_surface_patch_delete(libfive_surface_patch* p);
libfive_tree libfive_points_distance(const float* xyz, uint32_t count);

/*  The surface the samples of a patch make, as a field: the height above it along the normals (`normals`, 3 floats a sample), zero on it,
 *  positive on the side they face, blended over `sigma`.  A surface and nothing else: no body behind it, so nothing of a thickness.  */
libfive_tree libfive_points_surface(const float* xyz, const float* normals, uint32_t count, double sigma);

/*  The same cells with a periodic surface (a TPMS) laid on them that follows the surface: returns the field (a tree) or NULL
 *  with libfive_lattice_last_error() set.  `kind` 0 gyroid, 1 Schwarz P, 2 diamond, 3 Neovius, 4 Lidinoid, 5 split P, 6 IWP,
 *  7 FRD, 8 Fischer-Koch S; `style` 0 a sheet `thickness` mm thick, 1 / 2 the solid on one side / the other, grown by
 *  `offset`; `skin` mm: a solid skin against the faces of the layers.  The other arguments and `info` are those of
 *  libfive_surface_cells (info[3], the beams, is 0).  */
libfive_tree libfive_surface_tpms(libfive_tree body, const libfive_tree* vars, const float* values, int var_count,
                                  const double* lo3, const double* hi3, const double* direction3, int grid_offset,
                                  int layout, libfive_tree region,
                                  double cell, double depth, int layers, double height_dir, double lift, int kind, double thickness,
                                  int style, double offset, double skin, double* info);

/*  Triangle-mesh import (.stl binary / ASCII, .obj): the returned tree is
 *  the exact signed distance to the triangles (negative inside), after
 *  welding, cleaning and re-orienting them; every coordinate is multiplied
 *  by `scale`.  Returns NULL on failure; libfive_import_mesh_last_message()
 *  then says why (and after a success, summarises the mesh).  `info` may
 *  be NULL.  */
typedef struct libfive_mesh_import_info {
    uint32_t triangles;
    uint32_t vertices;
    uint32_t components;          /*  connected shells  */
    uint32_t boundary_edges;      /*  edges of a single triangle (holes)  */
    uint32_t nonmanifold_edges;   /*  edges of more than two triangles  */
    uint32_t reoriented;          /*  triangles whose winding was flipped  */
    uint32_t dropped;             /*  degenerate / duplicate triangles  */
    uint8_t watertight;
    uint8_t winding_sign;         /*  winding number used where the sign is unclear  */
    libfive_region3 bounds;
} libfive_mesh_import_info;

libfive_tree libfive_import_mesh(const char* filename, float scale,
                                 libfive_mesh_import_info* info);

/*  The unsigned distance to the triangles with selected[i] != 0 (an open patch of a surface has no
 *  inside).  NULL on failure.  */
libfive_tree libfive_mesh_patch(const float* xyz, uint32_t vertex_count, const uint32_t* tri,
                                uint32_t tri_count, const uint8_t* selected, libfive_mesh_import_info* info);

/*  The same from triangles in memory: `xyz` holds 3 * vertex_count floats,
 *  `tri` 3 * tri_count vertex indices (0-based).  */
libfive_tree libfive_mesh_from_arrays(const float* xyz, uint32_t vertex_count,
                                      const uint32_t* tri, uint32_t tri_count,
                                      float scale, libfive_mesh_import_info* info);
const char* libfive_import_mesh_last_message(void);

/*  Executes the remapping operation returning a tree
 *  q(x, y, z) = p(x'(x, y, z), y'(x, y, z), z'(x, y, z)) */
libfive_tree libfive_tree_remap(libfive_tree p,
        libfive_tree x, libfive_tree y, libfive_tree z);

/*
 *  Returns an optimized version of the given tree
 */
libfive_tree libfive_tree_optimized(libfive_tree t);

/*
 *  Returns a C string representing the tree in Scheme style
 *  (e.g. "(+ 1 2 x y)" )
 *
 *  The caller is responsible for freeing the string with libfive_free()
 */
char* libfive_tree_print(libfive_tree t);

/*
 *  An exact key of the tree's structure (two separately built trees that are
 *  the same expression have the same key; see libfive/tree/content_key.hpp),
 *  as text.  Unlike the printed tree it writes every constant in full and
 *  tells oracles and free variables apart.  Free with libfive_free_str.
 */
char* libfive_tree_content_key(libfive_tree t);

/*
 *  The key the render cache keeps a shape's mesh by (libfive::treePersistentKey of the tree as built, not optimized: free
 *  variables count by their value, `values` of the `count` variables `vars`; an oracle by what it holds), as text,
 *  or NULL when the tree holds something no other run could recognise (a solved analysis, a variable with no
 *  value).  Free with libfive_free_str.
 */
char* libfive_tree_persistent_key(libfive_tree t, const libfive_tree* vars, const float* values, int count);

/*
 *  The numbers of a tree that place its surfaces (a plane's offset, a radius,
 *  a box's faces: see libfive/tree/expose.hpp), which Studio can make
 *  draggable.  libfive_tree_expose_count says how many there are,
 *  libfive_tree_expose_values writes their values (that many floats), and
 *  libfive_tree_expose returns the tree with each of them replaced by the
 *  given tree (usually a free variable), in the same order -- or NULL when
 *  `count` is not the number there are.
 */
int libfive_tree_expose_count(libfive_tree t);
void libfive_tree_expose_values(libfive_tree t, float* out);
libfive_tree libfive_tree_expose(libfive_tree t, libfive_tree* with, int count);

/*
 *  Frees a string allocated by libfive (probably by libfive_tree_print
 */
void libfive_free_str(char* ptr);

////////////////////////////////////////////////////////////////////////////////

/*
 *  Renders a tree to a set of contours
 *
 *  R is a region that will be subdivided into an octree.  For clean
 *  triangles, it should be near-cubical, but that isn't a hard requirement
 *
 *  res should be approximately half the model's smallest feature size;
 *  subdivision halts when all sides of the region are below it.
 *
 *  The returned struct must be freed with libfive_contours_delete
 */
libfive_contours* libfive_tree_render_slice(libfive_tree tree,
                                            libfive_region2 R,
                                            float z, float res);
/*
 *  Renders a tree to a set of contours, similar to libfive_tree_render_slice,
 *  except the contours are 3D points (see the libfive_contour3 struct) above.
 */
libfive_contours3* libfive_tree_render_slice3(libfive_tree tree,
                                              libfive_region2 R,
                                              float z, float res);

/*
 *  Renders and saves a slice to a file
 *
 *  See argument details in libfive_tree_render_slice
 */
void libfive_tree_save_slice(libfive_tree tree, libfive_region2 R,
                             float z, float res, const char* f);

/*
 *  Renders a tree to a set of triangles
 *
 *  R is a region that will be subdivided into an octree.  For clean
 *  triangles, it should be near-cubical, but that isn't a hard requirement
 *
 *  res should be approximately half the model's smallest feature size;
 *  subdivision halts when all sides of the region are below it.
 *
 *  The returned struct must be freed with libfive_mesh_delete
 */
libfive_mesh* libfive_tree_render_mesh(libfive_tree tree,
                                       libfive_region3 R, float res);
/*
 * Same as libfive_tree_render_mesh, but forces single-threaded meshing to
 * play nicely with FFI.
 */
libfive_mesh* libfive_tree_render_mesh_st(libfive_tree tree,
                                       libfive_region3 R, float res);
/*
 * Debug/diagnostic only: same as libfive_tree_render_mesh, but lets the
 * caller pick the meshing algorithm (0=dual contouring, 1=iso simplex,
 * 2=hybrid) and max_err (cell-merging tolerance; -1 disables merging).
 */
libfive_mesh* libfive_tree_render_mesh_algo(libfive_tree tree,
                                       libfive_region3 R, float res,
                                       int algo, double max_err, int workers);
/*
 *  Renders to an alternate mesh format, see description of
 *  libfive_mesh_coords above.  The returned struct must be freed with
 *  libfive_mesh_coords_delete.
 */
libfive_mesh_coords* libfive_tree_render_mesh_coords(libfive_tree tree,
                                                     libfive_region3 R,
                                                     float res);

/*
 *  Renders and saves a mesh to a file
 *
 *  Returns true on success, false otherwise
 *  See argument details in libfive_tree_render_mesh
 */
bool libfive_tree_save_mesh(libfive_tree tree, libfive_region3 R,
                            float res, const char* f);

/*
 *  Renders and saves a mesh to a file
 *
 *  Returns true on success, false otherwise
 *  Second argument is an evaluator
 *  See other argument details in libfive_tree_render_mesh
 */
bool libfive_evaluator_save_mesh(libfive_evaluator evaluator, libfive_region3 R,
                                 const char *f);

/*
 *  Renders and saves multiple meshes mesh to a file
 *
 *  Returns true on success, false otherwise
 *
 *  Arguments are equivalent to Studio's resolution and quality
 *  settings.  In particular quality is a value q such that we
 *  collapse cells when the QEF error is below 10**(-q)
 *
 *  trees is a null-terminated list (since libfive_tree is a pointer
 *  under the hood).
 */
bool libfive_tree_save_meshes(
        libfive_tree trees[], libfive_region3 R,
        float res, float quality, const char* f);

/*
 *  Renders a 2D slice of pixels at the given Z height
 *
 *  The returned struct must be freed with libfive_pixels_delete
 */
libfive_pixels* libfive_tree_render_pixels(libfive_tree tree,
                                           libfive_region2 R,
                                           float z, float res);

/*
 *  Constructs a new evaluator
 */
libfive_evaluator libfive_tree_evaluator(libfive_tree tree, libfive_vars vars);

/*
 *  Updates the variables of the evaluator
 */
bool libfive_evaluator_update_vars(libfive_evaluator eval_tree, libfive_vars vars);

/*
 *  Deletes (first) evaluator.  TODO: if settings.workers > 1
 */
void libfive_evaluator_delete(libfive_evaluator ptr);

/*
 *  Returns the human-readable tag associated with this build,
 *  or the empty string if there is no such tag
 */
const char* libfive_git_version(void);

/*
 *  Returns the 7-character git hash associated with this build,
 *  with a trailing '+' if there are local (uncommitted) modifications
 */
const char* libfive_git_revision(void);

/*
 *  Returns the name of the branch associated with this build
 */
const char* libfive_git_branch(void);

#ifdef __cplusplus
}
#endif
