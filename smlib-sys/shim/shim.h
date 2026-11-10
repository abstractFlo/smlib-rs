/* SPDX-License-Identifier: Apache-2.0
 *
 * A C interface over SM_API and SMLib for Rust (smlib-sys/src/lib.rs declares it, the smlib crate
 * wraps it). The cs_ prefix stands for C shim.
 *
 * Every function catches what SMLib throws and returns a status: 0 on success, an SmStatus code
 * or -1 (a C++ exception) otherwise; cs_error() then says why, with SMLib's error trail, on the
 * calling thread. Objects belong to the thread whose context built them
 * (docs/concurrency.md); the Rust side keeps that rule.
 *
 * Lists are returned in two calls: with `capacity` 0 the count, then filled up to `capacity`.
 */
#ifndef SMLIB_SYS_SHIM_H
#define SMLIB_SYS_SHIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CsBrep CsBrep;
typedef struct CsFace CsFace;
typedef struct CsEdge CsEdge;
typedef struct CsVertex CsVertex;
typedef struct CsCurve CsCurve;
typedef struct CsSurface CsSurface;
typedef struct CsMesh CsMesh;
typedef struct CsLines CsLines;
typedef struct CsPoly CsPoly;

/* ---- threads and errors ---- */
void cs_thread_start(void);
void cs_thread_end(void);
const char* cs_error(void);

/* ---- breps ---- */
void cs_brep_free(CsBrep* brep);
int cs_brep_copy(const CsBrep* brep, CsBrep** result);
size_t cs_brep_faces(const CsBrep* brep, CsFace** faces, size_t capacity);
size_t cs_brep_edges(const CsBrep* brep, CsEdge** edges, size_t capacity);
size_t cs_brep_vertices(const CsBrep* brep, CsVertex** vertices, size_t capacity);
int cs_brep_bounding_box(const CsBrep* brep, int tight, double box[6]);
int cs_brep_volume(const CsBrep* brep, double accuracy, double* volume);
int cs_brep_area(const CsBrep* brep, double accuracy, double* area);
int cs_brep_is_manifold_solid(const CsBrep* brep, int* solid);
uint64_t cs_brep_memory_used(const CsBrep* brep);
/* area, volume, mass, centroid[3], moments[3], products[3] about origin (null: the centroid) */
int cs_brep_mass_properties(const CsBrep* brep, double accuracy, double density, const double* origin, double result[12]);
int cs_brep_material_census(const CsBrep* brep, int64_t* solids, int64_t* voids);
int cs_brep_write(const CsBrep* brep, const char* path, int ascii);
int cs_brep_read(const char* path, int ascii, CsBrep** result);

/* ---- faces ---- */
int cs_face_bounding_box(const CsFace* face, int tight, double box[6]);
int cs_face_area(const CsFace* face, double accuracy, double* area);
int cs_face_centroid(const CsFace* face, double accuracy, double centroid[3]);
const CsSurface* cs_face_surface(const CsFace* face);
void cs_face_uv_domain(const CsFace* face, double domain[4]);
int cs_face_point_at_uv(const CsFace* face, double u, double v, double point[3]);
int cs_face_normal_at_uv(const CsFace* face, double u, double v, int outward, double normal[3]);
int cs_face_interior_point(const CsFace* face, double point[3]);
/* 0 inside, 1 outside, 2 on the boundary, 3 at a vertex */
int cs_face_classify_uv(const CsFace* face, double u, double v, int* where);
int cs_face_closest_point(const CsFace* face, const double point[3], double closest[3], double* distance);
size_t cs_face_edges(const CsFace* face, CsEdge** edges, size_t capacity);
size_t cs_face_vertices(const CsFace* face, CsVertex** vertices, size_t capacity);
size_t cs_face_outer_loop_edges(const CsFace* face, CsEdge** edges, size_t capacity);
/* every loop, outer first: loop_sizes[i] edges each; forward[j] the edge runs with the loop */
int cs_face_loops(const CsFace* face, uint32_t* loop_sizes, size_t loop_capacity, size_t* loop_count,
                  CsEdge** edges, uint8_t* forward, size_t edge_capacity, size_t* edge_count);
void cs_face_user_ids(const CsFace* face, int64_t ids[2]);
/* The origin tag of a face: an SMLib attribute that splits, copies and merges carry along.
 * cs_face_tag gives 1 and the tag when the face has one, 0 when not. */
int cs_face_tag(const CsFace* face, int64_t* tag);
int cs_face_set_tag(const CsFace* face, int64_t tag);
const CsBrep* cs_face_brep(const CsFace* face);

/* ---- surfaces (borrowed from their face) ---- */
int cs_surface_is_planar(const CsSurface* surface, double tolerance);
void cs_surface_uv_domain(const CsSurface* surface, double domain[4]);
int cs_surface_point(const CsSurface* surface, double u, double v, double point[3]);
int cs_surface_normal(const CsSurface* surface, double u, double v, double normal[3]);
int cs_surface_derivatives(const CsSurface* surface, double u, double v, double result[9]);
/* the closest point, its distance and the first uv of it (has_uv 0 when the kernel gives none) */
int cs_surface_closest_point(const CsSurface* surface, const double point[3], double closest[3], double uv[2], int* has_uv, double* distance);
void cs_surface_periodic(const CsSurface* surface, int* in_u, int* in_v);
/* 0 other, 1 plane, 2 cylinder, 3 cone, 4 sphere, 5 torus, 6 b-spline, 7 offset, 8 extrusion, 9 revolution */
int cs_surface_kind(const CsSurface* surface);

/* ---- edges and vertices ---- */
int cs_edge_bounding_box(const CsEdge* edge, int tight, double box[6]);
int cs_edge_length(const CsEdge* edge, double accuracy, double* length);
int cs_edge_parameter_range(const CsEdge* edge, double range[2]);
int cs_edge_point_at(const CsEdge* edge, double t, double point[3]);
int cs_edge_tangent_at(const CsEdge* edge, double t, double tangent[3]);
int cs_edge_closest_point(const CsEdge* edge, const double point[3], double closest[3], double* t, double* distance);
/* 0 inside, 1 at an end, 2 outside */
int cs_edge_classify_parameter(const CsEdge* edge, double t, int* where);
size_t cs_edge_faces(const CsEdge* edge, CsFace** faces, size_t capacity);
void cs_edge_vertices(const CsEdge* edge, CsVertex** start, CsVertex** end);
const CsCurve* cs_edge_curve(const CsEdge* edge);
int cs_vertex_point(const CsVertex* vertex, double point[3]);
size_t cs_vertex_edges(const CsVertex* vertex, CsEdge** edges, size_t capacity);
size_t cs_vertex_faces(const CsVertex* vertex, CsFace** faces, size_t capacity);

/* ---- curves ---- */
void cs_curve_free(CsCurve* curve);
int cs_curve_copy(const CsCurve* curve, CsCurve** result);
/* 0 not a b-spline, 1 b-spline, 2 line, 3 circle (arc), 4 ellipse */
int cs_curve_kind(const CsCurve* curve);
/* center[3], normal[3], x axis[3], radius */
int cs_circle_frame(const CsCurve* curve, double frame[10]);
void cs_curve_parameter_range(const CsCurve* curve, double range[2]);
int cs_curve_point(const CsCurve* curve, double t, double point[3]);
int cs_curve_derivatives(const CsCurve* curve, double t, double point[3], double first[3], double second[3]);
int cs_curve_length(const CsCurve* curve, double accuracy, double* length);
int cs_curve_is_closed(const CsCurve* curve, double tolerance);
int cs_curve_is_planar(const CsCurve* curve, double tolerance);
/* a b-spline's control points (x, y, z, w each), knots and degree */
int cs_curve_nurbs(const CsCurve* curve, double* points, size_t point_capacity, size_t* point_count,
                   double* knots, size_t knot_capacity, size_t* knot_count, uint32_t* degree);

int cs_create_line_segment(const double start[3], const double end[3], CsCurve** result);
int cs_create_arc(const double center[3], double radius, double start_deg, double end_deg, CsCurve** result);
int cs_create_circle(const double center[3], double radius, CsCurve** result);
int cs_create_ellipse(const double center[3], double radius1, double radius2, CsCurve** result);
int cs_create_canonical_curve(const double* points, size_t point_count, const double* knots, const uint32_t* multiplicities,
                              size_t knot_count, const double* weights, uint32_t degree, CsCurve** result);
int cs_create_curve_interp(const double* points, size_t point_count, int parameterization, CsCurve** result);
int cs_create_helix(const double origin[3], double height, double radius_start, double radius_end, double turns,
                    int right_handed, double tolerance, CsCurve** result);

/* ---- solids ---- */
int cs_create_box(const double origin[3], double length, double width, double height, CsBrep** result);
int cs_create_cylinder(const double origin[3], double radius, double height, CsBrep** result);
int cs_create_cone(const double origin[3], double radius_base, double radius_top, double height, CsBrep** result);
int cs_create_sphere(const double origin[3], double radius, CsBrep** result);
int cs_create_torus(const double origin[3], double radius_major, double radius_minor, CsBrep** result);
int cs_create_partial_cylinder(const double origin[3], double radius, double height, double start_deg, double end_deg, CsBrep** result);
int cs_create_partial_cone(const double origin[3], double radius_base, double radius_top, double height, double start_deg, double end_deg, CsBrep** result);
int cs_create_partial_sphere(const double origin[3], double radius, double start_deg, double end_deg, CsBrep** result);
int cs_create_partial_torus(const double origin[3], double radius_major, double radius_minor, double start_deg, double end_deg, CsBrep** result);
int cs_linear_sweep(CsCurve* const* curves, size_t count, const double direction[3], double distance, int cap, CsBrep** result);
int cs_rotational_sweep(CsCurve* const* curves, size_t count, const double base[3], const double axis[3], double angle_deg, int cap, CsBrep** result);
int cs_pipe_sweep(const CsCurve* path, double radius, int cap, CsBrep** result);
int cs_create_planar_faces(CsCurve* const* curves, size_t count, CsBrep** result);
int cs_taper_extrude(CsCurve* const* curves, size_t count, double height, double draft_deg, int cap_ends, int fillet_corner, CsBrep** result);
int cs_skin(const uint32_t* curves_per_profile, size_t profile_count, CsCurve* const* curves, size_t curve_count, uint32_t degree, int cap, CsBrep** result);

/* ---- booleans: `first` is changed into the result, `second` used up (both freed on failure) ---- */
/* operation: 1 union, 2 intersection, 3 difference (SmBooleanOperationType) */
int cs_boolean(CsBrep* first, CsBrep* second, int operation, CsBrep** result);
/* a union imprinting the tool's outline into the faces it touches (SmApiBooleanWithOptions) */
int cs_boolean_imprint(CsBrep* first, CsBrep* second, CsBrep** result);
/* a flat region a distance off closed planar curves: 1 left, 2 right, 3 a band on both sides */
int cs_create_offset_profile(CsCurve* const* curves, size_t count, double distance, int offset_type, int round_corners, CsBrep** result);
int cs_create_cylindrical_box(const double origin[3], double length, double inside_radius, double outside_radius, double start_deg, double end_deg, CsBrep** result);
/* several bodies in one call; on success all are used up (*consumed 1), on bad input none */
int cs_merge_breps(CsBrep* const* breps, size_t count, int operation, CsBrep** result, int* consumed);

/* ---- rounds and shells ---- */
int cs_fillet_edges(CsBrep* brep, CsEdge* const* edges, size_t count, double radius, uint32_t cross_section, uint32_t continuity, double thumbweight);
int cs_fillet_edges_per_edge(CsBrep* brep, CsEdge* const* edges, size_t count, const double* radii, const int32_t* cross_sections, uint32_t continuity, double thumbweight);
int cs_variable_radius_fillet(CsBrep* brep, CsEdge* const* edges, size_t count, double start_radius, double end_radius, uint32_t cross_section, uint32_t continuity);
int cs_chamfer_two_distances(CsBrep* brep, CsEdge* const* edges, CsFace* const* reference_faces, size_t count, const double* first, const double* second);
int cs_shell(const CsBrep* brep, double distance, int extended, int self_intersection, int create_solid, CsFace* const* faces, size_t face_count, CsBrep** result);
int cs_offset(const CsBrep* brep, double distance, int extended, int self_intersection, CsBrep** result);
int cs_cut(CsBrep* brep, const double point[3], const double normal[3]);
/* analytic faces as NURBS, so a body takes a mirror or an uneven scale */
int cs_turn_to_nurbs(CsBrep* brep);
/* the body's exact data as JSON (vertices, edges with NURBS curves, faces with loops and their
   plane, cylinder, cone, sphere, torus or NURBS surface, shells, regions); free with cs_text_free */
int cs_brep_exact(const CsBrep* brep, char** text, size_t* length);
void cs_text_free(char* text);
/* SMLib's native .smb files (ASCII or binary); reading can rebuild the UV trim curves */
int cs_brep_read_file(const char* path, int ascii, int rebuildUvTrims, CsBrep** result);
int cs_heal_brep(CsBrep* brep);
int cs_stitch_into_solid(CsBrep* brep, int* solid);
/* a sliver face pulled into a point, its neighbours meeting there (smlib-rs) */
int cs_squeeze_face(CsBrep* brep, CsFace* face, double tolerance);
int cs_sweep_along_planar_path(CsCurve* const* profile, size_t profile_count, CsCurve* const* path, size_t path_count, int move_profile, int cap_ends, CsBrep** result);

/* ---- moves: a brep (kind 0) or a curve (kind 1) ---- */
int cs_translate(void* object, int kind, const double offset[3]);
int cs_rotate(void* object, int kind, const double point[3], const double axis[3], double angle_deg);
int cs_scale(void* object, int kind, const double factors[3]);
int cs_scale_about(void* object, int kind, const double point[3], const double factors[3]);
int cs_transform(void* object, int kind, const double origin[3], const double x_axis[3], const double y_axis[3]);

/* ---- tessellation ---- */
/* points (float x3), face-varying normals (float x3 per polygon corner), the polygon stream
 * [n, i0..in-1, ...], each polygon's face (index in the brep's faces, -1 none) */
int cs_tessellate(const CsBrep* brep, double chord, double curve_angle_deg, double surface_angle_deg, double max_edge,
                  double max_aspect, int allow_partial, CsMesh** result, size_t* failed_faces);
void cs_mesh_free(CsMesh* mesh);
size_t cs_mesh_points(const CsMesh* mesh, const float** points);
size_t cs_mesh_normals(const CsMesh* mesh, const float** normals);
size_t cs_mesh_stream(const CsMesh* mesh, const int32_t** stream);
size_t cs_mesh_face_ids(const CsMesh* mesh, const int32_t** face_ids);
/* each edge sampled: points (double x3), points per edge, the edges */
int cs_tessellate_boundaries(const CsBrep* brep, double angle_deg, int allow_partial, CsLines** result);
void cs_lines_free(CsLines* lines);
size_t cs_lines_points(const CsLines* lines, const double** points);
size_t cs_lines_counts(const CsLines* lines, const uint32_t** counts);
size_t cs_lines_edges(const CsLines* lines, CsEdge* const** edges);

/* ---- queries between bodies ---- */
int cs_brep_closest_point(const CsBrep* brep, const double point[3], double closest[3], double* distance);
int cs_brep_distance(const CsBrep* first, const CsBrep* second, double* distance);
/* 0 disjoint, 1 touching, 2 intersecting, 3 first inside second, 4 second inside first */
int cs_brep_relationship(const CsBrep* first, const CsBrep* second, int* relationship, double* distance);
/* where a curve crosses a body: the points (x, y, z each), up to capacity, and how many */
int cs_intersect_curve_brep(const CsCurve* curve, const CsBrep* brep, double* points, size_t capacity, size_t* count);
int cs_intersect_brep_with_plane(const CsBrep* brep, const double point[3], const double normal[3], CsCurve** curves, size_t capacity, size_t* count);

#ifdef __cplusplus
}
#endif

#endif
