use smlib::*;
use std::collections::HashMap;

const TOLERANCE: f64 = 1e-5;

fn line(start: Point, end: Point) -> Nurbs {
    Nurbs { points: vec![[start[0], start[1], start[2], 1.0], [end[0], end[1], end[2], 1.0]], knots: vec![0.0, 0.0, 1.0, 1.0], degree: 1 }
}

/// A plane through `origin` spanned by `along_u` and `along_v`, its normal their cross product.
fn plane(origin: Point, along_u: Point, along_v: Point) -> NurbsSurface {
    let corner = |u: f64, v: f64| {
        let point = [0, 1, 2].map(|axis| origin[axis] + u * (along_u[axis] - origin[axis]) + v * (along_v[axis] - origin[axis]));
        [point[0], point[1], point[2], 1.0]
    };
    NurbsSurface {
        points: vec![corner(0.0, 0.0), corner(0.0, 1.0), corner(1.0, 0.0), corner(1.0, 1.0)],
        count_u: 2,
        count_v: 2,
        knots_u: vec![0.0, 0.0, 1.0, 1.0],
        knots_v: vec![0.0, 0.0, 1.0, 1.0],
        degree_u: 1,
        degree_v: 1,
    }
}

/// A cube of `size` at the origin, its faces looking out, each face's loop running
/// counter-clockwise about its normal; one solid: the outside region with its shell, the
/// material region with its own.
fn cube_rows(size: f64) -> BrepRows {
    let corner = |index: usize| [(index & 1) as f64 * size, ((index >> 1) & 1) as f64 * size, ((index >> 2) & 1) as f64 * size];
    let mut rows = BrepRows { tolerance: TOLERANCE, ..Default::default() };
    rows.vertices = (0..8).map(|index| VertexRow { point: corner(index), tolerance: TOLERANCE }).collect();
    // (origin, end of u, end of v); the fourth corner follows
    let faces = [(0, 2, 1), (4, 5, 6), (0, 1, 4), (2, 6, 3), (0, 4, 2), (1, 3, 5)];
    let mut edge_of: HashMap<(usize, usize), u32> = HashMap::new();
    let mut uses_of_edge: Vec<Vec<usize>> = Vec::new();
    for (face_index, &(origin, end_u, end_v)) in faces.iter().enumerate() {
        let opposite = end_u + end_v - origin;
        rows.surfaces.push(plane(corner(origin), corner(end_u), corner(end_v)));
        rows.faces.push(FaceRow {
            surface: face_index as u32,
            first_loop: rows.loops.len() as u32,
            loop_count: 1,
            tolerance: TOLERANCE,
            u_range: (0.0, 1.0),
            v_range: (0.0, 1.0),
            rectangular_trim: None,
        });
        rows.loops.push(LoopRow::Edgeuses { count: 4, first: rows.edgeuses.len() as u32 });
        let around = [origin, end_u, opposite, end_v];
        for step in 0..4 {
            let (start, end) = (around[step], around[(step + 1) % 4]);
            let key = (start.min(end), start.max(end));
            let edge = *edge_of.entry(key).or_insert_with(|| {
                rows.curves.push(line(corner(start), corner(end)));
                rows.edges.push(EdgeRow { curve: rows.curves.len() as u32 - 1, start: start as u32, end: end as u32, primary_edgeuse: 0, tolerance: TOLERANCE, interval: (0.0, 1.0) });
                uses_of_edge.push(Vec::new());
                rows.edges.len() as u32 - 1
            });
            uses_of_edge[edge as usize].push(rows.edgeuses.len());
            rows.edgeuses.push(EdgeuseRow { edge, in_loop: face_index as u32, along_edge: rows.edges[edge as usize].start as usize == start, next_radial: 0, mate_next_radial: 0 });
        }
    }
    for (edge, uses) in uses_of_edge.iter().enumerate() {
        let (first, second) = (uses[0], uses[1]);
        rows.edges[edge].primary_edgeuse = first as u32;
        (rows.edgeuses[first].next_radial, rows.edgeuses[first].mate_next_radial) = (-(first as i64 + 1), -(second as i64 + 1));
        (rows.edgeuses[second].next_radial, rows.edgeuses[second].mate_next_radial) = (first as i64, second as i64);
    }
    rows.regions = vec![RegionRow { shell_count: 1, first_shell: 0, is_void: true }, RegionRow { shell_count: 1, first_shell: 1, is_void: false }];
    rows.shells = vec![ShellRow { faceuse_count: 6, first_faceuse: 0 }, ShellRow { faceuse_count: 6, first_faceuse: 6 }];
    rows.faceuses = (0..6).map(|face| FaceuseRow { face, along_normal: true }).chain((0..6).map(|face| FaceuseRow { face, along_normal: false })).collect();
    rows
}

#[test]
fn a_cube_from_its_rows() {
    thread::start();
    let body = Brep::from_rows(&cube_rows(10.0)).unwrap();
    assert_eq!(body.face_count(), 6);
    assert_eq!(body.edge_count(), 12);
    assert!(body.is_manifold_solid());
    assert!((body.volume(1e-6).unwrap() - 1000.0).abs() < 1e-6);
    let mesh = body.tessellate(&Tessellation { chord: 0.05, curve_angle_deg: 15.0, surface_angle_deg: 15.0, allow_partial: true, ..Default::default() }).unwrap();
    assert!(!mesh.points().is_empty());
}

#[test]
fn rows_pointing_past_the_others_are_refused() {
    thread::start();
    let mut rows = cube_rows(10.0);
    rows.edges[3].curve = 99;
    let error = Brep::from_rows(&rows).unwrap_err();
    assert!(error.message.contains("an edge names no curve"), "{}", error.message);
    let mut rows = cube_rows(10.0);
    rows.curves[0].knots.pop();
    assert!(Brep::from_rows(&rows).unwrap_err().message.contains("do not add up"));
    let mut rows = cube_rows(10.0);
    rows.edgeuses[5].next_radial = -100;
    assert!(Brep::from_rows(&rows).is_err());
}
