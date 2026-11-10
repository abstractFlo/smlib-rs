// SPDX-License-Identifier: Apache-2.0
//! Builds SMLib and SM_API for the target, with a C interface over them (`shim/shim.h`), and links
//! them as one static library. This crate declares that interface as it is; everything here is
//! unsafe. The `smlib` crate wraps it in safe types and documents the rules it keeps.
#![allow(non_camel_case_types, dead_code)]
#![no_std]

use core::ffi::{c_char, c_int, c_void};

#[repr(C)]
pub struct CsBrep {
    _private: [u8; 0],
}
#[repr(C)]
pub struct CsFace {
    _private: [u8; 0],
}
#[repr(C)]
pub struct CsEdge {
    _private: [u8; 0],
}
#[repr(C)]
pub struct CsVertex {
    _private: [u8; 0],
}
#[repr(C)]
pub struct CsCurve {
    _private: [u8; 0],
}
#[repr(C)]
pub struct CsSurface {
    _private: [u8; 0],
}
#[repr(C)]
pub struct CsMesh {
    _private: [u8; 0],
}
#[repr(C)]
pub struct CsLines {
    _private: [u8; 0],
}

extern "C" {
    pub fn cs_thread_start();
    pub fn cs_thread_end();
    pub fn cs_error() -> *const c_char;

    pub fn cs_brep_free(brep: *mut CsBrep);
    pub fn cs_brep_copy(brep: *const CsBrep, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_brep_faces(brep: *const CsBrep, faces: *mut *mut CsFace, capacity: usize) -> usize;
    pub fn cs_brep_edges(brep: *const CsBrep, edges: *mut *mut CsEdge, capacity: usize) -> usize;
    pub fn cs_brep_vertices(brep: *const CsBrep, vertices: *mut *mut CsVertex, capacity: usize) -> usize;
    pub fn cs_brep_bounding_box(brep: *const CsBrep, tight: c_int, bounds: *mut f64) -> c_int;
    pub fn cs_brep_volume(brep: *const CsBrep, accuracy: f64, volume: *mut f64) -> c_int;
    pub fn cs_brep_area(brep: *const CsBrep, accuracy: f64, area: *mut f64) -> c_int;
    pub fn cs_brep_is_manifold_solid(brep: *const CsBrep, solid: *mut c_int) -> c_int;
    pub fn cs_brep_memory_used(brep: *const CsBrep) -> u64;
    pub fn cs_brep_mass_properties(brep: *const CsBrep, accuracy: f64, density: f64, origin: *const f64, result: *mut f64) -> c_int;
    pub fn cs_brep_material_census(brep: *const CsBrep, solids: *mut i64, voids: *mut i64) -> c_int;
    pub fn cs_brep_write(brep: *const CsBrep, path: *const c_char, ascii: c_int) -> c_int;
    pub fn cs_brep_read(path: *const c_char, ascii: c_int, result: *mut *mut CsBrep) -> c_int;

    pub fn cs_face_bounding_box(face: *const CsFace, tight: c_int, bounds: *mut f64) -> c_int;
    pub fn cs_face_area(face: *const CsFace, accuracy: f64, area: *mut f64) -> c_int;
    pub fn cs_face_centroid(face: *const CsFace, accuracy: f64, centroid: *mut f64) -> c_int;
    pub fn cs_face_surface(face: *const CsFace) -> *const CsSurface;
    pub fn cs_face_uv_domain(face: *const CsFace, domain: *mut f64);
    pub fn cs_face_point_at_uv(face: *const CsFace, u: f64, v: f64, point: *mut f64) -> c_int;
    pub fn cs_face_normal_at_uv(face: *const CsFace, u: f64, v: f64, outward: c_int, normal: *mut f64) -> c_int;
    pub fn cs_face_interior_point(face: *const CsFace, point: *mut f64) -> c_int;
    pub fn cs_face_classify_uv(face: *const CsFace, u: f64, v: f64, place: *mut c_int) -> c_int;
    pub fn cs_face_closest_point(face: *const CsFace, point: *const f64, closest: *mut f64, distance: *mut f64) -> c_int;
    pub fn cs_face_edges(face: *const CsFace, edges: *mut *mut CsEdge, capacity: usize) -> usize;
    pub fn cs_face_vertices(face: *const CsFace, vertices: *mut *mut CsVertex, capacity: usize) -> usize;
    pub fn cs_face_outer_loop_edges(face: *const CsFace, edges: *mut *mut CsEdge, capacity: usize) -> usize;
    pub fn cs_face_loops(
        face: *const CsFace,
        loop_sizes: *mut u32,
        loop_capacity: usize,
        loop_count: *mut usize,
        edges: *mut *mut CsEdge,
        forward: *mut u8,
        edge_capacity: usize,
        edge_count: *mut usize,
    ) -> c_int;
    pub fn cs_face_user_ids(face: *const CsFace, ids: *mut i64);
    pub fn cs_face_tag(face: *const CsFace, tag: *mut i64) -> c_int;
    pub fn cs_face_set_tag(face: *const CsFace, tag: i64) -> c_int;
    pub fn cs_face_brep(face: *const CsFace) -> *const CsBrep;

    pub fn cs_surface_is_planar(surface: *const CsSurface, tolerance: f64) -> c_int;
    pub fn cs_surface_uv_domain(surface: *const CsSurface, domain: *mut f64);
    pub fn cs_surface_point(surface: *const CsSurface, u: f64, v: f64, point: *mut f64) -> c_int;
    pub fn cs_surface_normal(surface: *const CsSurface, u: f64, v: f64, normal: *mut f64) -> c_int;
    pub fn cs_surface_derivatives(surface: *const CsSurface, u: f64, v: f64, result: *mut f64) -> c_int;
    pub fn cs_surface_closest_point(
        surface: *const CsSurface,
        point: *const f64,
        closest: *mut f64,
        uv: *mut f64,
        has_uv: *mut c_int,
        distance: *mut f64,
    ) -> c_int;
    pub fn cs_surface_periodic(surface: *const CsSurface, in_u: *mut c_int, in_v: *mut c_int);
    pub fn cs_surface_kind(surface: *const CsSurface) -> c_int;

    pub fn cs_edge_bounding_box(edge: *const CsEdge, tight: c_int, bounds: *mut f64) -> c_int;
    pub fn cs_edge_length(edge: *const CsEdge, accuracy: f64, length: *mut f64) -> c_int;
    pub fn cs_edge_parameter_range(edge: *const CsEdge, range: *mut f64) -> c_int;
    pub fn cs_edge_point_at(edge: *const CsEdge, t: f64, point: *mut f64) -> c_int;
    pub fn cs_edge_tangent_at(edge: *const CsEdge, t: f64, tangent: *mut f64) -> c_int;
    pub fn cs_edge_closest_point(edge: *const CsEdge, point: *const f64, closest: *mut f64, t: *mut f64, distance: *mut f64) -> c_int;
    pub fn cs_edge_classify_parameter(edge: *const CsEdge, t: f64, place: *mut c_int) -> c_int;
    pub fn cs_edge_faces(edge: *const CsEdge, faces: *mut *mut CsFace, capacity: usize) -> usize;
    pub fn cs_edge_vertices(edge: *const CsEdge, start: *mut *mut CsVertex, end: *mut *mut CsVertex);
    pub fn cs_edge_curve(edge: *const CsEdge) -> *const CsCurve;
    pub fn cs_vertex_point(vertex: *const CsVertex, point: *mut f64) -> c_int;
    pub fn cs_vertex_edges(vertex: *const CsVertex, edges: *mut *mut CsEdge, capacity: usize) -> usize;
    pub fn cs_vertex_faces(vertex: *const CsVertex, faces: *mut *mut CsFace, capacity: usize) -> usize;

    pub fn cs_curve_free(curve: *mut CsCurve);
    pub fn cs_curve_copy(curve: *const CsCurve, result: *mut *mut CsCurve) -> c_int;
    pub fn cs_curve_kind(curve: *const CsCurve) -> c_int;
    pub fn cs_circle_frame(curve: *const CsCurve, frame: *mut f64) -> c_int;
    pub fn cs_curve_parameter_range(curve: *const CsCurve, range: *mut f64);
    pub fn cs_curve_point(curve: *const CsCurve, t: f64, point: *mut f64) -> c_int;
    pub fn cs_curve_derivatives(curve: *const CsCurve, t: f64, point: *mut f64, first: *mut f64, second: *mut f64) -> c_int;
    pub fn cs_curve_length(curve: *const CsCurve, accuracy: f64, length: *mut f64) -> c_int;
    pub fn cs_curve_is_closed(curve: *const CsCurve, tolerance: f64) -> c_int;
    pub fn cs_curve_is_planar(curve: *const CsCurve, tolerance: f64) -> c_int;
    pub fn cs_curve_nurbs(
        curve: *const CsCurve,
        points: *mut f64,
        point_capacity: usize,
        point_count: *mut usize,
        knots: *mut f64,
        knot_capacity: usize,
        knot_count: *mut usize,
        degree: *mut u32,
    ) -> c_int;

    pub fn cs_create_line_segment(start: *const f64, end: *const f64, result: *mut *mut CsCurve) -> c_int;
    pub fn cs_create_arc(center: *const f64, radius: f64, start_deg: f64, end_deg: f64, result: *mut *mut CsCurve) -> c_int;
    pub fn cs_create_circle(center: *const f64, radius: f64, result: *mut *mut CsCurve) -> c_int;
    pub fn cs_create_ellipse(center: *const f64, radius1: f64, radius2: f64, result: *mut *mut CsCurve) -> c_int;
    pub fn cs_create_canonical_curve(
        points: *const f64,
        point_count: usize,
        knots: *const f64,
        multiplicities: *const u32,
        knot_count: usize,
        weights: *const f64,
        degree: u32,
        result: *mut *mut CsCurve,
    ) -> c_int;
    pub fn cs_create_curve_interp(points: *const f64, point_count: usize, parameterization: c_int, result: *mut *mut CsCurve) -> c_int;
    pub fn cs_create_helix(
        origin: *const f64,
        height: f64,
        radius_start: f64,
        radius_end: f64,
        turns: f64,
        right_handed: c_int,
        tolerance: f64,
        result: *mut *mut CsCurve,
    ) -> c_int;

    pub fn cs_create_box(origin: *const f64, length: f64, width: f64, height: f64, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_create_cylinder(origin: *const f64, radius: f64, height: f64, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_create_cone(origin: *const f64, radius_base: f64, radius_top: f64, height: f64, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_create_sphere(origin: *const f64, radius: f64, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_create_torus(origin: *const f64, radius_major: f64, radius_minor: f64, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_create_partial_cylinder(origin: *const f64, radius: f64, height: f64, start_deg: f64, end_deg: f64, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_create_partial_cone(
        origin: *const f64,
        radius_base: f64,
        radius_top: f64,
        height: f64,
        start_deg: f64,
        end_deg: f64,
        result: *mut *mut CsBrep,
    ) -> c_int;
    pub fn cs_create_partial_sphere(origin: *const f64, radius: f64, start_deg: f64, end_deg: f64, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_create_partial_torus(origin: *const f64, radius_major: f64, radius_minor: f64, start_deg: f64, end_deg: f64, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_linear_sweep(curves: *const *mut CsCurve, count: usize, direction: *const f64, distance: f64, cap: c_int, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_rotational_sweep(
        curves: *const *mut CsCurve,
        count: usize,
        base: *const f64,
        axis: *const f64,
        angle_deg: f64,
        cap: c_int,
        result: *mut *mut CsBrep,
    ) -> c_int;
    pub fn cs_pipe_sweep(path: *const CsCurve, radius: f64, cap: c_int, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_create_planar_faces(curves: *const *mut CsCurve, count: usize, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_taper_extrude(
        curves: *const *mut CsCurve,
        count: usize,
        height: f64,
        draft_deg: f64,
        cap_ends: c_int,
        fillet_corner: c_int,
        result: *mut *mut CsBrep,
    ) -> c_int;
    pub fn cs_skin(
        curves_per_profile: *const u32,
        profile_count: usize,
        curves: *const *mut CsCurve,
        curve_count: usize,
        degree: u32,
        cap: c_int,
        result: *mut *mut CsBrep,
    ) -> c_int;

    pub fn cs_boolean(first: *mut CsBrep, second: *mut CsBrep, operation: c_int, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_create_offset_profile(curves: *const *mut CsCurve, count: usize, distance: f64, offset_type: c_int, round_corners: c_int, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_boolean_imprint(first: *mut CsBrep, second: *mut CsBrep, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_create_cylindrical_box(origin: *const f64, length: f64, inside_radius: f64, outside_radius: f64, start_deg: f64, end_deg: f64, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_merge_breps(breps: *const *mut CsBrep, count: usize, operation: c_int, result: *mut *mut CsBrep, consumed: *mut c_int) -> c_int;
    pub fn cs_turn_to_nurbs(brep: *mut CsBrep) -> c_int;
    pub fn cs_brep_exact(brep: *const CsBrep, text: *mut *mut c_char, length: *mut usize) -> c_int;
    pub fn cs_text_free(text: *mut c_char);
    pub fn cs_brep_read_file(path: *const c_char, ascii: c_int, rebuild_uv_trims: c_int, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_heal_brep(brep: *mut CsBrep) -> c_int;
    pub fn cs_stitch_into_solid(brep: *mut CsBrep, solid: *mut c_int) -> c_int;
    pub fn cs_squeeze_face(brep: *mut CsBrep, face: *mut CsFace, tolerance: f64) -> c_int;
    pub fn cs_sweep_along_planar_path(
        profile: *const *mut CsCurve,
        profile_count: usize,
        path: *const *mut CsCurve,
        path_count: usize,
        move_profile: c_int,
        cap_ends: c_int,
        result: *mut *mut CsBrep,
    ) -> c_int;

    pub fn cs_fillet_edges(brep: *mut CsBrep, edges: *const *mut CsEdge, count: usize, radius: f64, cross_section: u32, continuity: u32, thumbweight: f64) -> c_int;
    pub fn cs_fillet_edges_per_edge(
        brep: *mut CsBrep,
        edges: *const *mut CsEdge,
        count: usize,
        radii: *const f64,
        cross_sections: *const i32,
        continuity: u32,
        thumbweight: f64,
    ) -> c_int;
    pub fn cs_variable_radius_fillet(
        brep: *mut CsBrep,
        edges: *const *mut CsEdge,
        count: usize,
        start_radius: f64,
        end_radius: f64,
        cross_section: u32,
        continuity: u32,
    ) -> c_int;
    pub fn cs_chamfer_two_distances(
        brep: *mut CsBrep,
        edges: *const *mut CsEdge,
        reference_faces: *const *mut CsFace,
        count: usize,
        first: *const f64,
        second: *const f64,
    ) -> c_int;
    pub fn cs_shell(
        brep: *const CsBrep,
        distance: f64,
        extended: c_int,
        self_intersection: c_int,
        create_solid: c_int,
        faces: *const *mut CsFace,
        face_count: usize,
        result: *mut *mut CsBrep,
    ) -> c_int;
    pub fn cs_offset(brep: *const CsBrep, distance: f64, extended: c_int, self_intersection: c_int, result: *mut *mut CsBrep) -> c_int;
    pub fn cs_cut(brep: *mut CsBrep, point: *const f64, normal: *const f64) -> c_int;

    pub fn cs_translate(object: *mut c_void, kind: c_int, offset: *const f64) -> c_int;
    pub fn cs_rotate(object: *mut c_void, kind: c_int, point: *const f64, axis: *const f64, angle_deg: f64) -> c_int;
    pub fn cs_scale(object: *mut c_void, kind: c_int, factors: *const f64) -> c_int;
    pub fn cs_scale_about(object: *mut c_void, kind: c_int, point: *const f64, factors: *const f64) -> c_int;
    pub fn cs_transform(object: *mut c_void, kind: c_int, origin: *const f64, x_axis: *const f64, y_axis: *const f64) -> c_int;

    pub fn cs_tessellate(
        brep: *const CsBrep,
        chord: f64,
        curve_angle_deg: f64,
        surface_angle_deg: f64,
        max_edge: f64,
        max_aspect: f64,
        allow_partial: c_int,
        result: *mut *mut CsMesh,
        failed_faces: *mut usize,
    ) -> c_int;
    pub fn cs_mesh_free(mesh: *mut CsMesh);
    pub fn cs_mesh_points(mesh: *const CsMesh, points: *mut *const f32) -> usize;
    pub fn cs_mesh_normals(mesh: *const CsMesh, normals: *mut *const f32) -> usize;
    pub fn cs_mesh_stream(mesh: *const CsMesh, stream: *mut *const i32) -> usize;
    pub fn cs_mesh_face_ids(mesh: *const CsMesh, face_ids: *mut *const i32) -> usize;
    pub fn cs_tessellate_boundaries(brep: *const CsBrep, angle_deg: f64, allow_partial: c_int, result: *mut *mut CsLines) -> c_int;
    pub fn cs_lines_free(lines: *mut CsLines);
    pub fn cs_lines_points(lines: *const CsLines, points: *mut *const f64) -> usize;
    pub fn cs_lines_counts(lines: *const CsLines, counts: *mut *const u32) -> usize;
    pub fn cs_lines_edges(lines: *const CsLines, edges: *mut *const *mut CsEdge) -> usize;

    pub fn cs_brep_closest_point(brep: *const CsBrep, point: *const f64, closest: *mut f64, distance: *mut f64) -> c_int;
    pub fn cs_brep_distance(first: *const CsBrep, second: *const CsBrep, distance: *mut f64) -> c_int;
    pub fn cs_brep_relationship(first: *const CsBrep, second: *const CsBrep, relationship: *mut c_int, distance: *mut f64) -> c_int;
    pub fn cs_intersect_curve_brep(curve: *const CsCurve, brep: *const CsBrep, points: *mut f64, capacity: usize, count: *mut usize) -> c_int;
    pub fn cs_intersect_brep_with_plane(
        brep: *const CsBrep,
        point: *const f64,
        normal: *const f64,
        curves: *mut *mut CsCurve,
        capacity: usize,
        count: *mut usize,
    ) -> c_int;
}
