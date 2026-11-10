// SPDX-License-Identifier: Apache-2.0
//! Safe Rust types over SMLib, the NURBS B-rep kernel from NVIDIA's usd-brep: owned bodies and
//! curves, borrowed faces, edges and surfaces, with SMLib's thread rules in the types
//! (`docs/concurrency.md` in the repository):
//!
//! - A thread calls [`thread::start`] before its first call; its context then lives as long as
//!   the thread, so its bodies outlive any job run on it (a pool's long-lived workers suit it).
//! - A [`Brep`] or [`Curve`] is `Send`, not `Sync`: one thread at a time uses it. Readers in
//!   several threads share a body behind a `Mutex`, and copy it under it.
//! - A body is freed by the thread that built it: dropped elsewhere it waits in that thread's
//!   graveyard until the thread calls [`thread::bury`] (between jobs).
//!
//! # Safety
//!
//! Every `unsafe { ffi::cs_*(..) }` call here keeps one contract with the shim: a body, curve or
//! mesh pointer comes from the `NonNull` of a live owner borrowed for the call (`&self` or
//! `&mut self` where SMLib changes it), a face, edge, vertex or surface pointer from a handle whose
//! lifetime ties it to its body, out-parameters are locals, and arrays go in as a slice's pointer
//! with that slice's own length. An owner frees its pointer exactly once, in `Drop` on its own
//! thread (or through the graveyard), and `into_raw` forgets it when SMLib took it over. Unsafe
//! code that relies on more than that says so in a `SAFETY:` comment of its own.

use smlib_sys as ffi;

use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::marker::PhantomData;
use std::os::raw::c_int;
use std::ptr::{self, NonNull};
use std::sync::Mutex;
use std::thread::ThreadId;

pub type Point = [f64; 3];
pub type Vector = [f64; 3];
/// (low corner, high corner)
pub type Bounds = (Point, Point);

#[derive(Debug, Clone)]
pub struct Error {
    pub code: i32,
    pub message: String,
}

impl std::fmt::Display for Error {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl std::error::Error for Error {}

impl Error {
    pub fn new(message: impl Into<String>) -> Error {
        Error { code: -1, message: message.into() }
    }
}

pub type Result<T> = std::result::Result<T, Error>;

fn last_error(code: c_int) -> Error {
    // SAFETY: cs_error returns the calling thread's error text, NUL-terminated and alive until its
    // next call on this thread, which cannot happen before the copy is taken
    let message = unsafe { CStr::from_ptr(ffi::cs_error()) }.to_string_lossy().into_owned();
    Error { code, message }
}

fn check(status: c_int) -> Result<()> {
    if status == 0 {
        Ok(())
    } else {
        Err(last_error(status))
    }
}

fn bounds_of(values: [f64; 6]) -> Bounds {
    ([values[0], values[1], values[2]], [values[3], values[4], values[5]])
}

/// The two-call list pattern of the shim: the count first, then the items (the shim writes at
/// most `capacity` of them and answers how many there are).
fn list<T>(fetch: impl Fn(*mut *mut T, usize) -> usize) -> Vec<*mut T> {
    let count = fetch(ptr::null_mut(), 0);
    let mut items = vec![ptr::null_mut(); count];
    if count > 0 {
        let written = fetch(items.as_mut_ptr(), count);
        items.truncate(written);
    }
    items
}

enum Grave {
    Brep(*mut ffi::CsBrep),
    Curve(*mut ffi::CsCurve),
}

// SAFETY: a grave is only moved, never used, until bury() frees it on the thread it belongs to
unsafe impl Send for Grave {}

static GRAVEYARD: Mutex<Option<HashMap<ThreadId, Vec<Grave>>>> = Mutex::new(None);

fn send_to_grave(owner: ThreadId, grave: Grave) {
    let mut graveyard = GRAVEYARD.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
    graveyard.get_or_insert_with(HashMap::new).entry(owner).or_default().push(grave);
}

pub mod thread {
    use super::*;

    /// Makes this thread's SMLib context and error trail. Call once, before the first call.
    pub fn start() {
        unsafe { ffi::cs_thread_start() }
    }

    /// Ends this thread's context: only once every body it built is gone.
    pub fn end() {
        bury();
        unsafe { ffi::cs_thread_end() }
    }

    /// Frees what other threads dropped of the bodies this thread built.
    pub fn bury() -> usize {
        let graves = {
            let mut graveyard = GRAVEYARD.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
            graveyard.as_mut().and_then(|all| all.remove(&std::thread::current().id())).unwrap_or_default()
        };
        let count = graves.len();
        for grave in graves {
            match grave {
                Grave::Brep(raw) => unsafe { ffi::cs_brep_free(raw) },
                Grave::Curve(raw) => unsafe { ffi::cs_curve_free(raw) },
            }
        }
        count
    }
}

// ---------------------------------------------------------------------------------------------
// bodies

/// A B-rep body owned by the thread that built it.
pub struct Brep {
    raw: NonNull<ffi::CsBrep>,
    owner: ThreadId,
}

// SAFETY: one thread at a time uses a body (not Sync); Drop frees it on the thread that built it
// or hands it to that thread's graveyard
unsafe impl Send for Brep {}

impl Drop for Brep {
    fn drop(&mut self) {
        if std::thread::current().id() == self.owner {
            unsafe { ffi::cs_brep_free(self.raw.as_ptr()) }
        } else {
            send_to_grave(self.owner, Grave::Brep(self.raw.as_ptr()));
        }
    }
}

impl std::fmt::Debug for Brep {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(formatter, "<Brep faces={} edges={}>", self.face_count(), self.edge_count())
    }
}

fn made(status: c_int, raw: *mut ffi::CsBrep) -> Result<Brep> {
    check(status)?;
    Brep::from_raw(raw).ok_or_else(|| Error::new("the kernel returned no body"))
}

impl Brep {
    fn from_raw(raw: *mut ffi::CsBrep) -> Option<Brep> {
        NonNull::new(raw).map(|raw| Brep { raw, owner: std::thread::current().id() })
    }

    fn ptr(&self) -> *const ffi::CsBrep {
        self.raw.as_ptr()
    }

    fn mut_ptr(&mut self) -> *mut ffi::CsBrep {
        self.raw.as_ptr()
    }

    /// Gives up ownership: the kernel consumed the body.
    fn into_raw(self) -> *mut ffi::CsBrep {
        let raw = self.raw.as_ptr();
        std::mem::forget(self);
        raw
    }

    /// A pointer that names this body while it lives (for caches keyed by body).
    pub fn address(&self) -> usize {
        self.raw.as_ptr() as usize
    }

    /// A copy built by (and belonging to) the calling thread.
    pub fn copy(&self) -> Result<Brep> {
        let mut raw = ptr::null_mut();
        made(unsafe { ffi::cs_brep_copy(self.ptr(), &mut raw) }, raw)
    }

    pub fn faces(&self) -> Vec<Face<'_>> {
        list(|items, capacity| unsafe { ffi::cs_brep_faces(self.ptr(), items, capacity) })
            .into_iter()
            .map(|raw| Face { raw, _body: PhantomData })
            .collect()
    }

    pub fn edges(&self) -> Vec<Edge<'_>> {
        list(|items, capacity| unsafe { ffi::cs_brep_edges(self.ptr(), items, capacity) })
            .into_iter()
            .map(|raw| Edge { raw, _body: PhantomData })
            .collect()
    }

    pub fn vertices(&self) -> Vec<Vertex<'_>> {
        list(|items, capacity| unsafe { ffi::cs_brep_vertices(self.ptr(), items, capacity) })
            .into_iter()
            .map(|raw| Vertex { raw, _body: PhantomData })
            .collect()
    }

    pub fn face_count(&self) -> usize {
        unsafe { ffi::cs_brep_faces(self.ptr(), ptr::null_mut(), 0) }
    }

    pub fn edge_count(&self) -> usize {
        unsafe { ffi::cs_brep_edges(self.ptr(), ptr::null_mut(), 0) }
    }

    pub fn vertex_count(&self) -> usize {
        unsafe { ffi::cs_brep_vertices(self.ptr(), ptr::null_mut(), 0) }
    }

    pub fn bounding_box(&self, tight: bool) -> Result<Bounds> {
        let mut values = [0.0; 6];
        check(unsafe { ffi::cs_brep_bounding_box(self.ptr(), tight as c_int, values.as_mut_ptr()) })?;
        Ok(bounds_of(values))
    }

    pub fn volume(&self, accuracy: f64) -> Result<f64> {
        let mut volume = 0.0;
        check(unsafe { ffi::cs_brep_volume(self.ptr(), accuracy, &mut volume) })?;
        Ok(volume)
    }

    pub fn area(&self, accuracy: f64) -> Result<f64> {
        let mut area = 0.0;
        check(unsafe { ffi::cs_brep_area(self.ptr(), accuracy, &mut area) })?;
        Ok(area)
    }

    pub fn is_manifold_solid(&self) -> bool {
        let mut solid = 0;
        unsafe { ffi::cs_brep_is_manifold_solid(self.ptr(), &mut solid) == 0 && solid != 0 }
    }

    pub fn memory_used(&self) -> u64 {
        unsafe { ffi::cs_brep_memory_used(self.ptr()) }
    }

    pub fn mass_properties(&self, accuracy: f64, density: f64, origin: Option<Point>) -> Result<MassProperties> {
        let mut values = [0.0; 12];
        let origin_pointer = origin.as_ref().map(|point| point.as_ptr()).unwrap_or(ptr::null());
        check(unsafe { ffi::cs_brep_mass_properties(self.ptr(), accuracy, density, origin_pointer, values.as_mut_ptr()) })?;
        Ok(MassProperties {
            area: values[0],
            volume: values[1],
            mass: values[2],
            centroid: [values[3], values[4], values[5]],
            moments_of_inertia: [values[6], values[7], values[8]],
            products_of_inertia: [values[9], values[10], values[11]],
        })
    }

    pub fn material_census(&self) -> Result<(i64, i64)> {
        let (mut solids, mut voids) = (0, 0);
        check(unsafe { ffi::cs_brep_material_census(self.ptr(), &mut solids, &mut voids) })?;
        Ok((solids, voids))
    }

    pub fn write_to_file(&self, path: &str, ascii: bool) -> Result<()> {
        let path = CString::new(path).map_err(|_| Error::new("a path with a zero byte"))?;
        check(unsafe { ffi::cs_brep_write(self.ptr(), path.as_ptr(), ascii as c_int) })
    }

    /// The body's exact data as JSON (vertices, edges with their NURBS curves, faces with loops
    /// and surfaces, shells, regions), face-uses going by each surface's own normal.
    pub fn exact_json(&self) -> Result<String> {
        let mut text: *mut std::os::raw::c_char = ptr::null_mut();
        let mut length = 0usize;
        check(unsafe { ffi::cs_brep_exact(self.ptr(), &mut text, &mut length) })?;
        // SAFETY: on success the shim hands over `length` bytes at `text`, freed only below
        let bytes = unsafe { std::slice::from_raw_parts(text as *const u8, length) }.to_vec();
        unsafe { ffi::cs_text_free(text) };
        String::from_utf8(bytes).map_err(|_| Error::new("cs_brep_exact: not UTF-8"))
    }

    /// A native SMLib file read, its UV trim curves rebuilt from the 3D edges when asked.
    pub fn read_from_file_rebuilding(path: &str, ascii: bool, rebuild_uv_trims: bool) -> Result<Brep> {
        let path = CString::new(path).map_err(|_| Error::new("a path with a zero byte"))?;
        let mut raw = ptr::null_mut();
        made(unsafe { ffi::cs_brep_read_file(path.as_ptr(), ascii as c_int, rebuild_uv_trims as c_int, &mut raw) }, raw)
    }

    /// SMLib's healer over the body (closing small gaps, fixing tolerances).
    pub fn heal(&mut self) -> Result<()> {
        check(unsafe { ffi::cs_heal_brep(self.mut_ptr()) })
    }

    /// Open edges stitched together; whether that made a manifold solid.
    pub fn stitch_into_solid(&mut self) -> Result<bool> {
        let mut solid: c_int = 0;
        check(unsafe { ffi::cs_stitch_into_solid(self.mut_ptr(), &mut solid) })?;
        Ok(solid != 0)
    }

    pub fn read_from_file(path: &str, ascii: bool) -> Result<Brep> {
        let path = CString::new(path).map_err(|_| Error::new("a path with a zero byte"))?;
        let mut raw = ptr::null_mut();
        made(unsafe { ffi::cs_brep_read(path.as_ptr(), ascii as c_int, &mut raw) }, raw)
    }

    // ---- changes in place ----

    pub fn translate(&mut self, offset: Vector) -> Result<()> {
        check(unsafe { ffi::cs_translate(self.mut_ptr().cast(), 0, offset.as_ptr()) })
    }

    pub fn rotate(&mut self, pivot: Point, axis: Vector, angle_deg: f64) -> Result<()> {
        check(unsafe { ffi::cs_rotate(self.mut_ptr().cast(), 0, pivot.as_ptr(), axis.as_ptr(), angle_deg) })
    }

    pub fn scale(&mut self, factors: Vector) -> Result<()> {
        check(unsafe { ffi::cs_scale(self.mut_ptr().cast(), 0, factors.as_ptr()) })
    }

    pub fn scale_about(&mut self, pivot: Point, factors: Vector) -> Result<()> {
        check(unsafe { ffi::cs_scale_about(self.mut_ptr().cast(), 0, pivot.as_ptr(), factors.as_ptr()) })
    }

    /// Places the body as if its frame were (origin, x axis, y axis).
    pub fn transform(&mut self, origin: Point, x_axis: Vector, y_axis: Vector) -> Result<()> {
        check(unsafe { ffi::cs_transform(self.mut_ptr().cast(), 0, origin.as_ptr(), x_axis.as_ptr(), y_axis.as_ptr()) })
    }

    /// Rounds the edges (pointers of this body) in place.
    pub fn fillet_edges(&mut self, edges: &[EdgeHandle], radius: f64, cross_section: CrossSection, continuity: u32, thumbweight: f64) -> Result<()> {
        let raw: Vec<_> = edges.iter().map(|edge| edge.0).collect();
        check(unsafe { ffi::cs_fillet_edges(self.mut_ptr(), raw.as_ptr(), raw.len(), radius, cross_section as u32, continuity, thumbweight) })
    }

    pub fn fillet_edges_per_edge(&mut self, edges: &[EdgeHandle], radii: &[f64], cross_sections: &[CrossSection], continuity: u32, thumbweight: f64) -> Result<()> {
        let raw: Vec<_> = edges.iter().map(|edge| edge.0).collect();
        // SmFilletSurfaceGeneratorType counts from SM_FSG_UNKNOWN (0): linear 1, circular 2, blend 3,
        // one more than the xsect_type of fillet_edges
        let sections: Vec<i32> = cross_sections.iter().map(|section| *section as i32 + 1).collect();
        check(unsafe {
            ffi::cs_fillet_edges_per_edge(self.mut_ptr(), raw.as_ptr(), raw.len(), radii.as_ptr(), sections.as_ptr(), continuity, thumbweight)
        })
    }

    pub fn variable_radius_fillet(&mut self, edges: &[EdgeHandle], start_radius: f64, end_radius: f64, cross_section: CrossSection, continuity: u32) -> Result<()> {
        let raw: Vec<_> = edges.iter().map(|edge| edge.0).collect();
        check(unsafe { ffi::cs_variable_radius_fillet(self.mut_ptr(), raw.as_ptr(), raw.len(), start_radius, end_radius, cross_section as u32, continuity) })
    }

    pub fn chamfer_two_distances(&mut self, edges: &[EdgeHandle], reference_faces: &[FaceHandle], first: &[f64], second: &[f64]) -> Result<()> {
        let raw: Vec<_> = edges.iter().map(|edge| edge.0).collect();
        let faces: Vec<_> = reference_faces.iter().map(|face| face.0).collect();
        check(unsafe { ffi::cs_chamfer_two_distances(self.mut_ptr(), raw.as_ptr(), faces.as_ptr(), raw.len(), first.as_ptr(), second.as_ptr()) })
    }

    /// Keeps the side of the plane its normal points away from.
    pub fn cut(&mut self, point: Point, normal: Vector) -> Result<()> {
        check(unsafe { ffi::cs_cut(self.mut_ptr(), point.as_ptr(), normal.as_ptr()) })
    }

    /// Analytic faces as NURBS: a body then takes a mirror or an uneven scale.
    pub fn turn_to_nurbs(&mut self) -> Result<()> {
        check(unsafe { ffi::cs_turn_to_nurbs(self.mut_ptr()) })
    }

    /// The face pulled into a point, its neighbours meeting there.
    pub fn squeeze_face(&mut self, face: FaceHandle, tolerance: f64) -> Result<()> {
        check(unsafe { ffi::cs_squeeze_face(self.mut_ptr(), face.0, tolerance) })
    }

    // ---- new bodies from this one ----

    /// A shell: the body hollowed by `distance` (negative: inwards), open at `open_faces`.
    pub fn shell(&self, distance: f64, extended: bool, self_intersection: bool, create_solid: bool, open_faces: &[FaceHandle]) -> Result<Brep> {
        let faces: Vec<_> = open_faces.iter().map(|face| face.0).collect();
        let mut raw = ptr::null_mut();
        made(
            unsafe {
                ffi::cs_shell(self.ptr(), distance, extended as c_int, self_intersection as c_int, create_solid as c_int, faces.as_ptr(), faces.len(), &mut raw)
            },
            raw,
        )
    }

    pub fn offset(&self, distance: f64, extended: bool, self_intersection: bool) -> Result<Brep> {
        let mut raw = ptr::null_mut();
        made(unsafe { ffi::cs_offset(self.ptr(), distance, extended as c_int, self_intersection as c_int, &mut raw) }, raw)
    }

    pub fn tessellate(&self, quality: &Tessellation) -> Result<Mesh> {
        let mut raw = ptr::null_mut();
        let mut failed = 0usize;
        check(unsafe {
            ffi::cs_tessellate(
                self.ptr(),
                quality.chord,
                quality.curve_angle_deg,
                quality.surface_angle_deg,
                quality.max_edge,
                quality.max_aspect,
                quality.allow_partial as c_int,
                &mut raw,
                &mut failed,
            )
        })?;
        let raw = NonNull::new(raw).ok_or_else(|| Error::new("the kernel returned no mesh"))?;
        Ok(Mesh { raw, failed_faces: failed })
    }

    pub fn tessellate_boundaries(&self, angle_deg: f64, allow_partial: bool) -> Result<BoundaryLines<'_>> {
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::cs_tessellate_boundaries(self.ptr(), angle_deg, allow_partial as c_int, &mut raw) })?;
        let raw = NonNull::new(raw).ok_or_else(|| Error::new("the kernel returned no lines"))?;
        Ok(BoundaryLines { raw, _body: PhantomData })
    }

    pub fn closest_point(&self, point: Point) -> Result<(Point, f64)> {
        let mut closest = [0.0; 3];
        let mut distance = 0.0;
        check(unsafe { ffi::cs_brep_closest_point(self.ptr(), point.as_ptr(), closest.as_mut_ptr(), &mut distance) })?;
        Ok((closest, distance))
    }

    pub fn distance_to(&self, other: &Brep) -> Result<f64> {
        let mut distance = 0.0;
        check(unsafe { ffi::cs_brep_distance(self.ptr(), other.ptr(), &mut distance) })?;
        Ok(distance)
    }

    /// (0 disjoint, 1 touching, 2 intersecting, 3 this inside other, 4 other inside this; distance)
    pub fn relationship_to(&self, other: &Brep) -> Result<(i32, f64)> {
        let mut relationship = 0;
        let mut distance = 0.0;
        check(unsafe { ffi::cs_brep_relationship(self.ptr(), other.ptr(), &mut relationship, &mut distance) })?;
        Ok((relationship, distance))
    }

    /// Where a curve crosses the body (its curves of overlap left out).
    pub fn intersect_curve(&self, curve: &impl CurveLike) -> Result<Vec<Point>> {
        let mut count = 0usize;
        let mut points = vec![0.0; 3 * 64];
        check(unsafe { ffi::cs_intersect_curve_brep(curve.raw_curve(), self.ptr(), points.as_mut_ptr(), 64, &mut count) })?;
        if count > 64 {
            points = vec![0.0; 3 * count];
            check(unsafe { ffi::cs_intersect_curve_brep(curve.raw_curve(), self.ptr(), points.as_mut_ptr(), count, &mut count) })?;
        }
        Ok(points.chunks(3).take(count).map(|chunk| [chunk[0], chunk[1], chunk[2]]).collect())
    }

    pub fn intersect_with_plane(&self, point: Point, normal: Vector) -> Result<Vec<Curve>> {
        let mut count = 0usize;
        let mut first = vec![ptr::null_mut(); 256];
        check(unsafe { ffi::cs_intersect_brep_with_plane(self.ptr(), point.as_ptr(), normal.as_ptr(), first.as_mut_ptr(), first.len(), &mut count) })?;
        if count > first.len() {
            // the extra curves were freed by the shim: ask again with room for all
            for raw in first.drain(..) {
                unsafe { ffi::cs_curve_free(raw) };
            }
            let mut all = vec![ptr::null_mut(); count];
            check(unsafe { ffi::cs_intersect_brep_with_plane(self.ptr(), point.as_ptr(), normal.as_ptr(), all.as_mut_ptr(), all.len(), &mut count) })?;
            first = all;
        }
        first.truncate(count);
        Ok(first.into_iter().filter_map(Curve::from_raw).collect())
    }
}

#[derive(Debug, Clone, Copy)]
pub struct MassProperties {
    pub area: f64,
    pub volume: f64,
    pub mass: f64,
    pub centroid: Point,
    pub moments_of_inertia: Vector,
    pub products_of_inertia: Vector,
}

/// SmFilletSurfaceGeneratorType: 0 linear (a chamfer), 1 circular, 2 blend curve.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CrossSection {
    Linear = 0,
    Circular = 1,
    Blend = 2,
}

/// Tessellation quality (SmTessellationParams): 0 turns a criterion off.
#[derive(Debug, Clone, Copy)]
pub struct Tessellation {
    pub chord: f64,
    pub curve_angle_deg: f64,
    pub surface_angle_deg: f64,
    pub max_edge: f64,
    pub max_aspect: f64,
    pub allow_partial: bool,
}

impl Default for Tessellation {
    fn default() -> Self {
        // SmTessellationDefaults
        Tessellation { chord: 0.0, curve_angle_deg: 25.0, surface_angle_deg: 25.0, max_edge: 0.0, max_aspect: 0.0, allow_partial: false }
    }
}

// ---------------------------------------------------------------------------------------------
// solids

fn solid(call: impl FnOnce(*mut *mut ffi::CsBrep) -> c_int) -> Result<Brep> {
    let mut raw = ptr::null_mut();
    let status = call(&mut raw);
    made(status, raw)
}

pub fn create_box(origin: Point, length: f64, width: f64, height: f64) -> Result<Brep> {
    solid(|out| unsafe { ffi::cs_create_box(origin.as_ptr(), length, width, height, out) })
}

pub fn create_cylinder(origin: Point, radius: f64, height: f64) -> Result<Brep> {
    solid(|out| unsafe { ffi::cs_create_cylinder(origin.as_ptr(), radius, height, out) })
}

pub fn create_cone(origin: Point, radius_base: f64, radius_top: f64, height: f64) -> Result<Brep> {
    solid(|out| unsafe { ffi::cs_create_cone(origin.as_ptr(), radius_base, radius_top, height, out) })
}

pub fn create_sphere(origin: Point, radius: f64) -> Result<Brep> {
    solid(|out| unsafe { ffi::cs_create_sphere(origin.as_ptr(), radius, out) })
}

pub fn create_torus(origin: Point, radius_major: f64, radius_minor: f64) -> Result<Brep> {
    solid(|out| unsafe { ffi::cs_create_torus(origin.as_ptr(), radius_major, radius_minor, out) })
}

/// A hollow sector of a cylinder between two radii, from start to end degrees.
pub fn create_cylindrical_box(origin: Point, length: f64, inside_radius: f64, outside_radius: f64, start_deg: f64, end_deg: f64) -> Result<Brep> {
    solid(|out| unsafe { ffi::cs_create_cylindrical_box(origin.as_ptr(), length, inside_radius, outside_radius, start_deg, end_deg, out) })
}

pub fn create_partial_cylinder(origin: Point, radius: f64, height: f64, start_deg: f64, end_deg: f64) -> Result<Brep> {
    solid(|out| unsafe { ffi::cs_create_partial_cylinder(origin.as_ptr(), radius, height, start_deg, end_deg, out) })
}

pub fn create_partial_cone(origin: Point, radius_base: f64, radius_top: f64, height: f64, start_deg: f64, end_deg: f64) -> Result<Brep> {
    solid(|out| unsafe { ffi::cs_create_partial_cone(origin.as_ptr(), radius_base, radius_top, height, start_deg, end_deg, out) })
}

pub fn create_partial_sphere(origin: Point, radius: f64, start_deg: f64, end_deg: f64) -> Result<Brep> {
    solid(|out| unsafe { ffi::cs_create_partial_sphere(origin.as_ptr(), radius, start_deg, end_deg, out) })
}

pub fn create_partial_torus(origin: Point, radius_major: f64, radius_minor: f64, start_deg: f64, end_deg: f64) -> Result<Brep> {
    solid(|out| unsafe { ffi::cs_create_partial_torus(origin.as_ptr(), radius_major, radius_minor, start_deg, end_deg, out) })
}

fn curve_pointers(curves: &[Curve]) -> Vec<*mut ffi::CsCurve> {
    curves.iter().map(|curve| curve.raw.as_ptr()).collect()
}

/// The curves (copied, not used up) swept along `direction` by `distance`.
pub fn linear_sweep(curves: &[Curve], direction: Vector, distance: f64, cap: bool) -> Result<Brep> {
    let raw = curve_pointers(curves);
    solid(|out| unsafe { ffi::cs_linear_sweep(raw.as_ptr(), raw.len(), direction.as_ptr(), distance, cap as c_int, out) })
}

pub fn rotational_sweep(curves: &[Curve], base: Point, axis: Vector, angle_deg: f64, cap: bool) -> Result<Brep> {
    let raw = curve_pointers(curves);
    solid(|out| unsafe { ffi::cs_rotational_sweep(raw.as_ptr(), raw.len(), base.as_ptr(), axis.as_ptr(), angle_deg, cap as c_int, out) })
}

pub fn pipe_sweep(path: &Curve, radius: f64, cap: bool) -> Result<Brep> {
    solid(|out| unsafe { ffi::cs_pipe_sweep(path.raw.as_ptr(), radius, cap as c_int, out) })
}

pub fn create_planar_faces(curves: &[Curve]) -> Result<Brep> {
    let raw = curve_pointers(curves);
    solid(|out| unsafe { ffi::cs_create_planar_faces(raw.as_ptr(), raw.len(), out) })
}

/// cap_ends: 0 none, 1 start, 2 end, 3 both
pub fn taper_extrude(curves: &[Curve], height: f64, draft_deg: f64, cap_ends: i32, fillet_corner: bool) -> Result<Brep> {
    let raw = curve_pointers(curves);
    solid(|out| unsafe { ffi::cs_taper_extrude(raw.as_ptr(), raw.len(), height, draft_deg, cap_ends, fillet_corner as c_int, out) })
}

pub fn skin(curves_per_profile: &[u32], curves: &[Curve], degree: u32, cap: bool) -> Result<Brep> {
    let raw = curve_pointers(curves);
    solid(|out| unsafe { ffi::cs_skin(curves_per_profile.as_ptr(), curves_per_profile.len(), raw.as_ptr(), raw.len(), degree, cap as c_int, out) })
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BooleanOp {
    Union = 1,
    Intersection = 2,
    Difference = 3,
}

/// `first` becomes the result, `second` is used up; both are gone when it fails.
pub fn boolean(first: Brep, second: Brep, operation: BooleanOp) -> Result<Brep> {
    let mut raw = ptr::null_mut();
    let status = unsafe { ffi::cs_boolean(first.into_raw(), second.into_raw(), operation as c_int, &mut raw) };
    made(status, raw)
}

/// Which side of planar curves an offset region lies on (SMLib's offset types).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OffsetSide {
    Left = 1,
    Right = 2,
    Both = 3,
}

/// A flat region `distance` off closed planar curves, on one side or as a band on both.
pub fn create_offset_profile(curves: &[Curve], distance: f64, side: OffsetSide, round_corners: bool) -> Result<Brep> {
    let raw = curve_pointers(curves);
    solid(|out| unsafe { ffi::cs_create_offset_profile(raw.as_ptr(), raw.len(), distance, side as c_int, round_corners as c_int, out) })
}

/// A union that splits `first`'s faces where `second` touches them (imprinting); both used up.
pub fn boolean_imprint(first: Brep, second: Brep) -> Result<Brep> {
    let mut raw = ptr::null_mut();
    let status = unsafe { ffi::cs_boolean_imprint(first.into_raw(), second.into_raw(), &mut raw) };
    made(status, raw)
}

/// Several bodies in one call. When it fails on its input they come back; otherwise they are used up.
pub fn merge_breps(breps: Vec<Brep>, operation: BooleanOp) -> std::result::Result<Brep, (Error, Option<Vec<Brep>>)> {
    let raw: Vec<*mut ffi::CsBrep> = breps.iter().map(|body| body.raw.as_ptr()).collect();
    let mut result = ptr::null_mut();
    let mut consumed = 0;
    let status = unsafe { ffi::cs_merge_breps(raw.as_ptr(), raw.len(), operation as c_int, &mut result, &mut consumed) };
    if status == 0 {
        for body in breps {
            std::mem::forget(body);
        }
        return made(0, result).map_err(|error| (error, None));
    }
    let error = last_error(status);
    if consumed != 0 {
        for body in breps {
            std::mem::forget(body);
        }
        return Err((error, None));
    }
    Err((error, Some(breps)))
}

pub fn sweep_along_planar_path(profile: &[Curve], path: &[Curve], move_profile: bool, cap_ends: i32) -> Result<Brep> {
    let profile_raw = curve_pointers(profile);
    let path_raw = curve_pointers(path);
    solid(|out| unsafe { ffi::cs_sweep_along_planar_path(profile_raw.as_ptr(), profile_raw.len(), path_raw.as_ptr(), path_raw.len(), move_profile as c_int, cap_ends, out) })
}

pub fn boolean_union(first: Brep, second: Brep) -> Result<Brep> {
    boolean(first, second, BooleanOp::Union)
}

pub fn boolean_difference(first: Brep, second: Brep) -> Result<Brep> {
    boolean(first, second, BooleanOp::Difference)
}

pub fn boolean_intersection(first: Brep, second: Brep) -> Result<Brep> {
    boolean(first, second, BooleanOp::Intersection)
}

// ---------------------------------------------------------------------------------------------
// topology: borrowed from their body

/// A face's pointer without the body's lifetime, to hand a list of them back to the body
/// (fillets, shells): valid only while that body is unchanged.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct FaceHandle(*mut ffi::CsFace);

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct EdgeHandle(*mut ffi::CsEdge);

impl FaceHandle {
    /// The face's address: names it while its body lives unchanged.
    pub fn address(&self) -> usize {
        self.0 as usize
    }
}

impl EdgeHandle {
    pub fn address(&self) -> usize {
        self.0 as usize
    }
}

#[derive(Clone, Copy)]
pub struct Face<'a> {
    raw: *mut ffi::CsFace,
    _body: PhantomData<&'a Brep>,
}

impl PartialEq for Face<'_> {
    fn eq(&self, other: &Self) -> bool {
        self.raw == other.raw
    }
}
impl Eq for Face<'_> {}
impl std::hash::Hash for Face<'_> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.raw.hash(state)
    }
}
impl std::fmt::Debug for Face<'_> {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(formatter, "<Face {:p}>", self.raw)
    }
}

/// Where a uv point lies on a face, or a parameter on an edge.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Place {
    Inside,
    Outside,
    Boundary,
    Vertex,
}

impl<'a> Face<'a> {
    pub fn handle(&self) -> FaceHandle {
        FaceHandle(self.raw)
    }

    pub fn bounding_box(&self, tight: bool) -> Result<Bounds> {
        let mut values = [0.0; 6];
        check(unsafe { ffi::cs_face_bounding_box(self.raw, tight as c_int, values.as_mut_ptr()) })?;
        Ok(bounds_of(values))
    }

    pub fn area(&self, accuracy: f64) -> Result<f64> {
        let mut area = 0.0;
        check(unsafe { ffi::cs_face_area(self.raw, accuracy, &mut area) })?;
        Ok(area)
    }

    pub fn centroid(&self, accuracy: f64) -> Result<Point> {
        let mut centroid = [0.0; 3];
        check(unsafe { ffi::cs_face_centroid(self.raw, accuracy, centroid.as_mut_ptr()) })?;
        Ok(centroid)
    }

    pub fn surface(&self) -> Surface<'a> {
        Surface { raw: unsafe { ffi::cs_face_surface(self.raw) }, _body: PhantomData }
    }

    /// ((u min, v min), (u max, v max)) of the trimmed face
    pub fn uv_domain(&self) -> ([f64; 2], [f64; 2]) {
        let mut domain = [0.0; 4];
        unsafe { ffi::cs_face_uv_domain(self.raw, domain.as_mut_ptr()) };
        ([domain[0], domain[1]], [domain[2], domain[3]])
    }

    pub fn point_at_uv(&self, u: f64, v: f64) -> Result<Point> {
        let mut point = [0.0; 3];
        check(unsafe { ffi::cs_face_point_at_uv(self.raw, u, v, point.as_mut_ptr()) })?;
        Ok(point)
    }

    pub fn normal_at_uv(&self, u: f64, v: f64) -> Result<Vector> {
        let mut normal = [0.0; 3];
        check(unsafe { ffi::cs_face_normal_at_uv(self.raw, u, v, 0, normal.as_mut_ptr()) })?;
        Ok(normal)
    }

    /// The surface's normal turned to point out of the body.
    pub fn outward_normal_at_uv(&self, u: f64, v: f64) -> Result<Vector> {
        let mut normal = [0.0; 3];
        check(unsafe { ffi::cs_face_normal_at_uv(self.raw, u, v, 1, normal.as_mut_ptr()) })?;
        Ok(normal)
    }

    pub fn interior_point(&self) -> Result<Point> {
        let mut point = [0.0; 3];
        check(unsafe { ffi::cs_face_interior_point(self.raw, point.as_mut_ptr()) })?;
        Ok(point)
    }

    pub fn classify_uv(&self, u: f64, v: f64) -> Result<Place> {
        let mut place = 0;
        check(unsafe { ffi::cs_face_classify_uv(self.raw, u, v, &mut place) })?;
        Ok(match place {
            0 => Place::Inside,
            1 => Place::Outside,
            2 => Place::Boundary,
            _ => Place::Vertex,
        })
    }

    pub fn closest_point(&self, point: Point) -> Result<(Point, f64)> {
        let mut closest = [0.0; 3];
        let mut distance = 0.0;
        check(unsafe { ffi::cs_face_closest_point(self.raw, point.as_ptr(), closest.as_mut_ptr(), &mut distance) })?;
        Ok((closest, distance))
    }

    pub fn edges(&self) -> Vec<Edge<'a>> {
        list(|items, capacity| unsafe { ffi::cs_face_edges(self.raw, items, capacity) })
            .into_iter()
            .map(|raw| Edge { raw, _body: PhantomData })
            .collect()
    }

    pub fn vertices(&self) -> Vec<Vertex<'a>> {
        list(|items, capacity| unsafe { ffi::cs_face_vertices(self.raw, items, capacity) })
            .into_iter()
            .map(|raw| Vertex { raw, _body: PhantomData })
            .collect()
    }

    pub fn outer_loop_edges(&self) -> Vec<Edge<'a>> {
        list(|items, capacity| unsafe { ffi::cs_face_outer_loop_edges(self.raw, items, capacity) })
            .into_iter()
            .map(|raw| Edge { raw, _body: PhantomData })
            .collect()
    }

    /// Every loop of the face, the outer one first: (edge, runs with the loop)
    pub fn boundary_loops(&self) -> Result<Vec<Vec<(Edge<'a>, bool)>>> {
        let (mut loop_count, mut edge_count) = (0usize, 0usize);
        check(unsafe {
            ffi::cs_face_loops(self.raw, ptr::null_mut(), 0, &mut loop_count, ptr::null_mut(), ptr::null_mut(), 0, &mut edge_count)
        })?;
        let mut sizes = vec![0u32; loop_count];
        let mut edges = vec![ptr::null_mut(); edge_count];
        let mut forward = vec![0u8; edge_count];
        check(unsafe {
            ffi::cs_face_loops(
                self.raw,
                sizes.as_mut_ptr(),
                loop_count,
                &mut loop_count,
                edges.as_mut_ptr(),
                forward.as_mut_ptr(),
                edge_count,
                &mut edge_count,
            )
        })?;
        let mut loops = Vec::with_capacity(sizes.len());
        let mut next = 0usize;
        for size in sizes {
            let size = size as usize;
            if next + size > edges.len() {
                return Err(Error::new("a face's loops name more edges than it has"));
            }
            loops.push((next..next + size).map(|index| (Edge { raw: edges[index], _body: PhantomData }, forward[index] != 0)).collect());
            next += size;
        }
        Ok(loops)
    }

    pub fn user_ids(&self) -> (i64, i64) {
        let mut ids = [0i64; 2];
        unsafe { ffi::cs_face_user_ids(self.raw, ids.as_mut_ptr()) };
        (ids[0], ids[1])
    }

    /// The origin tag the face carries: set by set_tag on it or on the face it was split, copied or merged from.
    pub fn tag(&self) -> Option<i64> {
        let mut tag = 0i64;
        (unsafe { ffi::cs_face_tag(self.raw, &mut tag) } == 1).then_some(tag)
    }

    pub fn set_tag(&self, tag: i64) -> Result<()> {
        check(unsafe { ffi::cs_face_set_tag(self.raw, tag) })
    }
}

#[derive(Clone, Copy)]
pub struct Surface<'a> {
    raw: *const ffi::CsSurface,
    _body: PhantomData<&'a Brep>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SurfaceKind {
    Other,
    Plane,
    BSpline,
    Offset,
    Extrusion,
    Revolution,
}

impl Surface<'_> {
    pub fn is_planar(&self) -> bool {
        unsafe { ffi::cs_surface_is_planar(self.raw, 0.0) != 0 }
    }

    pub fn is_planar_within(&self, tolerance: f64) -> bool {
        unsafe { ffi::cs_surface_is_planar(self.raw, tolerance) != 0 }
    }

    pub fn kind(&self) -> SurfaceKind {
        match unsafe { ffi::cs_surface_kind(self.raw) } {
            1 => SurfaceKind::Plane,
            6 => SurfaceKind::BSpline,
            7 => SurfaceKind::Offset,
            8 => SurfaceKind::Extrusion,
            9 => SurfaceKind::Revolution,
            _ => SurfaceKind::Other,
        }
    }

    pub fn uv_domain(&self) -> ([f64; 2], [f64; 2]) {
        let mut domain = [0.0; 4];
        unsafe { ffi::cs_surface_uv_domain(self.raw, domain.as_mut_ptr()) };
        ([domain[0], domain[1]], [domain[2], domain[3]])
    }

    pub fn evaluate(&self, u: f64, v: f64) -> Result<Point> {
        let mut point = [0.0; 3];
        check(unsafe { ffi::cs_surface_point(self.raw, u, v, point.as_mut_ptr()) })?;
        Ok(point)
    }

    pub fn normal(&self, u: f64, v: f64) -> Result<Vector> {
        let mut normal = [0.0; 3];
        check(unsafe { ffi::cs_surface_normal(self.raw, u, v, normal.as_mut_ptr()) })?;
        Ok(normal)
    }

    /// (point, d/du, d/dv)
    pub fn derivatives(&self, u: f64, v: f64) -> Result<(Point, Vector, Vector)> {
        let mut values = [0.0; 9];
        check(unsafe { ffi::cs_surface_derivatives(self.raw, u, v, values.as_mut_ptr()) })?;
        Ok(([values[0], values[1], values[2]], [values[3], values[4], values[5]], [values[6], values[7], values[8]]))
    }

    /// (closest point, its first uv if the kernel gives one, distance)
    pub fn closest_point(&self, point: Point) -> Result<(Point, Option<[f64; 2]>, f64)> {
        let mut closest = [0.0; 3];
        let mut uv = [0.0; 2];
        let mut has_uv = 0;
        let mut distance = 0.0;
        check(unsafe { ffi::cs_surface_closest_point(self.raw, point.as_ptr(), closest.as_mut_ptr(), uv.as_mut_ptr(), &mut has_uv, &mut distance) })?;
        Ok((closest, if has_uv != 0 { Some(uv) } else { None }, distance))
    }

    pub fn is_periodic(&self) -> (bool, bool) {
        let (mut in_u, mut in_v) = (0, 0);
        unsafe { ffi::cs_surface_periodic(self.raw, &mut in_u, &mut in_v) };
        (in_u != 0, in_v != 0)
    }
}

#[derive(Clone, Copy)]
pub struct Edge<'a> {
    raw: *mut ffi::CsEdge,
    _body: PhantomData<&'a Brep>,
}

impl PartialEq for Edge<'_> {
    fn eq(&self, other: &Self) -> bool {
        self.raw == other.raw
    }
}
impl Eq for Edge<'_> {}
impl std::hash::Hash for Edge<'_> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.raw.hash(state)
    }
}
impl std::fmt::Debug for Edge<'_> {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(formatter, "<Edge {:p}>", self.raw)
    }
}

impl<'a> Edge<'a> {
    pub fn handle(&self) -> EdgeHandle {
        EdgeHandle(self.raw)
    }

    pub fn bounding_box(&self, tight: bool) -> Result<Bounds> {
        let mut values = [0.0; 6];
        check(unsafe { ffi::cs_edge_bounding_box(self.raw, tight as c_int, values.as_mut_ptr()) })?;
        Ok(bounds_of(values))
    }

    pub fn length(&self, accuracy: f64) -> Result<f64> {
        let mut length = 0.0;
        check(unsafe { ffi::cs_edge_length(self.raw, accuracy, &mut length) })?;
        Ok(length)
    }

    pub fn parameter_range(&self) -> Result<(f64, f64)> {
        let mut range = [0.0; 2];
        check(unsafe { ffi::cs_edge_parameter_range(self.raw, range.as_mut_ptr()) })?;
        Ok((range[0], range[1]))
    }

    pub fn point_at(&self, t: f64) -> Result<Point> {
        let mut point = [0.0; 3];
        check(unsafe { ffi::cs_edge_point_at(self.raw, t, point.as_mut_ptr()) })?;
        Ok(point)
    }

    /// Follows the curve's direction, not the edge's start to end.
    pub fn tangent_at(&self, t: f64) -> Result<Vector> {
        let mut tangent = [0.0; 3];
        check(unsafe { ffi::cs_edge_tangent_at(self.raw, t, tangent.as_mut_ptr()) })?;
        Ok(tangent)
    }

    /// (closest point, its parameter, distance)
    pub fn closest_point(&self, point: Point) -> Result<(Point, f64, f64)> {
        let mut closest = [0.0; 3];
        let (mut t, mut distance) = (0.0, 0.0);
        check(unsafe { ffi::cs_edge_closest_point(self.raw, point.as_ptr(), closest.as_mut_ptr(), &mut t, &mut distance) })?;
        Ok((closest, t, distance))
    }

    pub fn classify_parameter(&self, t: f64) -> Result<Place> {
        let mut place = 0;
        check(unsafe { ffi::cs_edge_classify_parameter(self.raw, t, &mut place) })?;
        Ok(match place {
            0 => Place::Inside,
            1 => Place::Vertex,
            _ => Place::Outside,
        })
    }

    pub fn faces(&self) -> Vec<Face<'a>> {
        list(|items, capacity| unsafe { ffi::cs_edge_faces(self.raw, items, capacity) })
            .into_iter()
            .map(|raw| Face { raw, _body: PhantomData })
            .collect()
    }

    pub fn face_count(&self) -> usize {
        unsafe { ffi::cs_edge_faces(self.raw, ptr::null_mut(), 0) }
    }

    pub fn vertices(&self) -> (Option<Vertex<'a>>, Option<Vertex<'a>>) {
        let (mut start, mut end) = (ptr::null_mut(), ptr::null_mut());
        unsafe { ffi::cs_edge_vertices(self.raw, &mut start, &mut end) };
        let wrap = |raw: *mut ffi::CsVertex| if raw.is_null() { None } else { Some(Vertex { raw, _body: PhantomData }) };
        (wrap(start), wrap(end))
    }

    pub fn start_vertex(&self) -> Option<Vertex<'a>> {
        self.vertices().0
    }

    pub fn end_vertex(&self) -> Option<Vertex<'a>> {
        self.vertices().1
    }

    pub fn curve(&self) -> Option<CurveRef<'a>> {
        let raw = unsafe { ffi::cs_edge_curve(self.raw) };
        if raw.is_null() {
            None
        } else {
            Some(CurveRef { raw, _body: PhantomData })
        }
    }
}

#[derive(Clone, Copy)]
pub struct Vertex<'a> {
    raw: *mut ffi::CsVertex,
    _body: PhantomData<&'a Brep>,
}

impl PartialEq for Vertex<'_> {
    fn eq(&self, other: &Self) -> bool {
        self.raw == other.raw
    }
}
impl Eq for Vertex<'_> {}
impl std::hash::Hash for Vertex<'_> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.raw.hash(state)
    }
}

impl<'a> Vertex<'a> {
    pub fn point(&self) -> Result<Point> {
        let mut point = [0.0; 3];
        check(unsafe { ffi::cs_vertex_point(self.raw, point.as_mut_ptr()) })?;
        Ok(point)
    }

    pub fn edges(&self) -> Vec<Edge<'a>> {
        list(|items, capacity| unsafe { ffi::cs_vertex_edges(self.raw, items, capacity) })
            .into_iter()
            .map(|raw| Edge { raw, _body: PhantomData })
            .collect()
    }

    pub fn faces(&self) -> Vec<Face<'a>> {
        list(|items, capacity| unsafe { ffi::cs_vertex_faces(self.raw, items, capacity) })
            .into_iter()
            .map(|raw| Face { raw, _body: PhantomData })
            .collect()
    }
}

// ---------------------------------------------------------------------------------------------
// curves

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CurveKind {
    Other,
    BSpline,
    Line,
    Circle,
    Ellipse,
}

/// A circle's (or arc's) frame: center, plane normal, the 0 degree direction, radius.
#[derive(Debug, Clone, Copy)]
pub struct CircleFrame {
    pub center: Point,
    pub normal: Vector,
    pub x_axis: Vector,
    pub radius: f64,
}

/// A b-spline's control points (x, y, z, weight), all its knots and its degree.
#[derive(Debug, Clone)]
pub struct Nurbs {
    pub points: Vec<[f64; 4]>,
    pub knots: Vec<f64>,
    pub degree: u32,
}

pub trait CurveLike {
    #[doc(hidden)]
    fn raw_curve(&self) -> *const ffi::CsCurve;

    fn kind(&self) -> CurveKind {
        match unsafe { ffi::cs_curve_kind(self.raw_curve()) } {
            1 => CurveKind::BSpline,
            2 => CurveKind::Line,
            3 => CurveKind::Circle,
            4 => CurveKind::Ellipse,
            _ => CurveKind::Other,
        }
    }

    fn circle_frame(&self) -> Result<CircleFrame> {
        let mut values = [0.0; 10];
        check(unsafe { ffi::cs_circle_frame(self.raw_curve(), values.as_mut_ptr()) })?;
        Ok(CircleFrame {
            center: [values[0], values[1], values[2]],
            normal: [values[3], values[4], values[5]],
            x_axis: [values[6], values[7], values[8]],
            radius: values[9],
        })
    }

    fn parameter_range(&self) -> (f64, f64) {
        let mut range = [0.0; 2];
        unsafe { ffi::cs_curve_parameter_range(self.raw_curve(), range.as_mut_ptr()) };
        (range[0], range[1])
    }

    fn evaluate(&self, t: f64) -> Result<Point> {
        let mut point = [0.0; 3];
        check(unsafe { ffi::cs_curve_point(self.raw_curve(), t, point.as_mut_ptr()) })?;
        Ok(point)
    }

    /// (point, first derivative, second derivative)
    fn derivatives(&self, t: f64) -> Result<(Point, Vector, Vector)> {
        let (mut point, mut first, mut second) = ([0.0; 3], [0.0; 3], [0.0; 3]);
        check(unsafe { ffi::cs_curve_derivatives(self.raw_curve(), t, point.as_mut_ptr(), first.as_mut_ptr(), second.as_mut_ptr()) })?;
        Ok((point, first, second))
    }

    fn start_point(&self) -> Result<Point> {
        self.evaluate(self.parameter_range().0)
    }

    fn end_point(&self) -> Result<Point> {
        self.evaluate(self.parameter_range().1)
    }

    fn length(&self, accuracy: f64) -> Result<f64> {
        let mut length = 0.0;
        check(unsafe { ffi::cs_curve_length(self.raw_curve(), accuracy, &mut length) })?;
        Ok(length)
    }

    fn is_closed(&self, tolerance: f64) -> bool {
        unsafe { ffi::cs_curve_is_closed(self.raw_curve(), tolerance) != 0 }
    }

    fn is_planar(&self) -> bool {
        unsafe { ffi::cs_curve_is_planar(self.raw_curve(), 0.0) != 0 }
    }

    fn nurbs(&self) -> Result<Nurbs> {
        let (mut point_count, mut knot_count, mut degree) = (0usize, 0usize, 0u32);
        check(unsafe {
            ffi::cs_curve_nurbs(self.raw_curve(), ptr::null_mut(), 0, &mut point_count, ptr::null_mut(), 0, &mut knot_count, &mut degree)
        })?;
        let mut points = vec![0.0; point_count * 4];
        let mut knots = vec![0.0; knot_count];
        check(unsafe {
            ffi::cs_curve_nurbs(
                self.raw_curve(),
                points.as_mut_ptr(),
                point_count,
                &mut point_count,
                knots.as_mut_ptr(),
                knot_count,
                &mut knot_count,
                &mut degree,
            )
        })?;
        Ok(Nurbs { points: points.chunks(4).map(|chunk| [chunk[0], chunk[1], chunk[2], chunk[3]]).collect(), knots, degree })
    }

    /// A curve of its own (owned by the calling thread) with the same shape.
    fn to_owned_curve(&self) -> Result<Curve> {
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::cs_curve_copy(self.raw_curve(), &mut raw) })?;
        Curve::from_raw(raw).ok_or_else(|| Error::new("the kernel returned no curve"))
    }
}

/// A curve owned by the thread that built it.
pub struct Curve {
    raw: NonNull<ffi::CsCurve>,
    owner: ThreadId,
}

// SAFETY: as for Brep: one thread at a time, freed by its owner thread
unsafe impl Send for Curve {}

impl Drop for Curve {
    fn drop(&mut self) {
        if std::thread::current().id() == self.owner {
            unsafe { ffi::cs_curve_free(self.raw.as_ptr()) }
        } else {
            send_to_grave(self.owner, Grave::Curve(self.raw.as_ptr()));
        }
    }
}

impl CurveLike for Curve {
    fn raw_curve(&self) -> *const ffi::CsCurve {
        self.raw.as_ptr()
    }
}

/// A body's curve, borrowed from its edge.
#[derive(Clone, Copy)]
pub struct CurveRef<'a> {
    raw: *const ffi::CsCurve,
    _body: PhantomData<&'a Brep>,
}

impl CurveLike for CurveRef<'_> {
    fn raw_curve(&self) -> *const ffi::CsCurve {
        self.raw
    }
}

fn made_curve(status: c_int, raw: *mut ffi::CsCurve) -> Result<Curve> {
    check(status)?;
    Curve::from_raw(raw).ok_or_else(|| Error::new("the kernel returned no curve"))
}

impl Curve {
    fn from_raw(raw: *mut ffi::CsCurve) -> Option<Curve> {
        NonNull::new(raw).map(|raw| Curve { raw, owner: std::thread::current().id() })
    }

    pub fn copy(&self) -> Result<Curve> {
        self.to_owned_curve()
    }

    pub fn translate(&mut self, offset: Vector) -> Result<()> {
        check(unsafe { ffi::cs_translate(self.raw.as_ptr().cast(), 1, offset.as_ptr()) })
    }

    pub fn rotate(&mut self, pivot: Point, axis: Vector, angle_deg: f64) -> Result<()> {
        check(unsafe { ffi::cs_rotate(self.raw.as_ptr().cast(), 1, pivot.as_ptr(), axis.as_ptr(), angle_deg) })
    }

    pub fn scale(&mut self, factors: Vector) -> Result<()> {
        check(unsafe { ffi::cs_scale(self.raw.as_ptr().cast(), 1, factors.as_ptr()) })
    }

    pub fn scale_about(&mut self, pivot: Point, factors: Vector) -> Result<()> {
        check(unsafe { ffi::cs_scale_about(self.raw.as_ptr().cast(), 1, pivot.as_ptr(), factors.as_ptr()) })
    }

    pub fn transform(&mut self, origin: Point, x_axis: Vector, y_axis: Vector) -> Result<()> {
        check(unsafe { ffi::cs_transform(self.raw.as_ptr().cast(), 1, origin.as_ptr(), x_axis.as_ptr(), y_axis.as_ptr()) })
    }
}

pub fn create_line_segment(start: Point, end: Point) -> Result<Curve> {
    let mut raw = ptr::null_mut();
    made_curve(unsafe { ffi::cs_create_line_segment(start.as_ptr(), end.as_ptr(), &mut raw) }, raw)
}

pub fn create_arc(center: Point, radius: f64, start_deg: f64, end_deg: f64) -> Result<Curve> {
    let mut raw = ptr::null_mut();
    made_curve(unsafe { ffi::cs_create_arc(center.as_ptr(), radius, start_deg, end_deg, &mut raw) }, raw)
}

pub fn create_circle(center: Point, radius: f64) -> Result<Curve> {
    let mut raw = ptr::null_mut();
    made_curve(unsafe { ffi::cs_create_circle(center.as_ptr(), radius, &mut raw) }, raw)
}

pub fn create_ellipse(center: Point, radius1: f64, radius2: f64) -> Result<Curve> {
    let mut raw = ptr::null_mut();
    made_curve(unsafe { ffi::cs_create_ellipse(center.as_ptr(), radius1, radius2, &mut raw) }, raw)
}

/// A b-spline from its control points, unique knots with their multiplicities, optional weights.
pub fn create_canonical_curve(points: &[Point], knots: &[f64], multiplicities: &[u32], weights: Option<&[f64]>, degree: u32) -> Result<Curve> {
    let flat: Vec<f64> = points.iter().flat_map(|point| point.iter().copied()).collect();
    let weight_pointer = weights.map(|values| values.as_ptr()).unwrap_or(ptr::null());
    let mut raw = ptr::null_mut();
    made_curve(
        unsafe {
            ffi::cs_create_canonical_curve(flat.as_ptr(), points.len(), knots.as_ptr(), multiplicities.as_ptr(), knots.len(), weight_pointer, degree, &mut raw)
        },
        raw,
    )
}

/// parameterization: 0 uniform, 1 chord length, 2 centripetal (SmCurveParameterizationType)
pub fn create_curve_interp(points: &[Point], parameterization: i32) -> Result<Curve> {
    let flat: Vec<f64> = points.iter().flat_map(|point| point.iter().copied()).collect();
    let mut raw = ptr::null_mut();
    made_curve(unsafe { ffi::cs_create_curve_interp(flat.as_ptr(), points.len(), parameterization, &mut raw) }, raw)
}

pub fn create_helix(origin: Point, height: f64, radius_start: f64, radius_end: f64, turns: f64, right_handed: bool, tolerance: f64) -> Result<Curve> {
    let mut raw = ptr::null_mut();
    made_curve(
        unsafe { ffi::cs_create_helix(origin.as_ptr(), height, radius_start, radius_end, turns, right_handed as c_int, tolerance, &mut raw) },
        raw,
    )
}

// ---------------------------------------------------------------------------------------------
// meshes

/// A tessellation's arrays (shim CsMesh): plain data, any thread may read it.
pub struct Mesh {
    raw: NonNull<ffi::CsMesh>,
    pub failed_faces: usize,
}

// SAFETY: a mesh is plain arrays owned by the shim's CsMesh, not tied to a thread's SMLib context,
// and only read after it is built
unsafe impl Send for Mesh {}
// SAFETY: see Send: nothing changes a mesh after it is built
unsafe impl Sync for Mesh {}

impl Drop for Mesh {
    fn drop(&mut self) {
        unsafe { ffi::cs_mesh_free(self.raw.as_ptr()) }
    }
}

impl Mesh {
    /// x, y, z per point
    pub fn points(&self) -> &[f32] {
        let mut data = ptr::null();
        let count = unsafe { ffi::cs_mesh_points(self.raw.as_ptr(), &mut data) };
        // SAFETY: `count` items at `data`, owned by the shim's struct that `self` keeps alive
        if count == 0 { &[] } else { unsafe { std::slice::from_raw_parts(data, count) } }
    }

    /// x, y, z per polygon corner: the normal of the polygon's own face there
    pub fn normals(&self) -> &[f32] {
        let mut data = ptr::null();
        let count = unsafe { ffi::cs_mesh_normals(self.raw.as_ptr(), &mut data) };
        // SAFETY: `count` items at `data`, owned by the shim's struct that `self` keeps alive
        if count == 0 { &[] } else { unsafe { std::slice::from_raw_parts(data, count) } }
    }

    /// [corner count, point indices..., corner count, ...]
    pub fn stream(&self) -> &[i32] {
        let mut data = ptr::null();
        let count = unsafe { ffi::cs_mesh_stream(self.raw.as_ptr(), &mut data) };
        // SAFETY: `count` items at `data`, owned by the shim's struct that `self` keeps alive
        if count == 0 { &[] } else { unsafe { std::slice::from_raw_parts(data, count) } }
    }

    /// each polygon's face (index in the body's faces), -1 where it has none
    pub fn face_ids(&self) -> &[i32] {
        let mut data = ptr::null();
        let count = unsafe { ffi::cs_mesh_face_ids(self.raw.as_ptr(), &mut data) };
        // SAFETY: `count` items at `data`, owned by the shim's struct that `self` keeps alive
        if count == 0 { &[] } else { unsafe { std::slice::from_raw_parts(data, count) } }
    }
}

/// The sampled edges of a body (SmApiTessellateBoundaries).
pub struct BoundaryLines<'a> {
    raw: NonNull<ffi::CsLines>,
    _body: PhantomData<&'a Brep>,
}

impl Drop for BoundaryLines<'_> {
    fn drop(&mut self) {
        unsafe { ffi::cs_lines_free(self.raw.as_ptr()) }
    }
}

impl<'a> BoundaryLines<'a> {
    /// x, y, z per sample
    pub fn points(&self) -> &[f64] {
        let mut data = ptr::null();
        let count = unsafe { ffi::cs_lines_points(self.raw.as_ptr(), &mut data) };
        // SAFETY: `count` items at `data`, owned by the shim's struct that `self` keeps alive
        if count == 0 { &[] } else { unsafe { std::slice::from_raw_parts(data, count) } }
    }

    /// samples per edge
    pub fn counts(&self) -> &[u32] {
        let mut data = ptr::null();
        let count = unsafe { ffi::cs_lines_counts(self.raw.as_ptr(), &mut data) };
        // SAFETY: `count` items at `data`, owned by the shim's struct that `self` keeps alive
        if count == 0 { &[] } else { unsafe { std::slice::from_raw_parts(data, count) } }
    }

    pub fn edges(&self) -> Vec<Edge<'a>> {
        let mut data = ptr::null();
        let count = unsafe { ffi::cs_lines_edges(self.raw.as_ptr(), &mut data) };
        if count == 0 {
            return Vec::new();
        }
        // SAFETY: as above
        unsafe { std::slice::from_raw_parts(data, count) }.iter().map(|&raw| Edge { raw, _body: PhantomData }).collect()
    }
}
