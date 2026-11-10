// SPDX-License-Identifier: Apache-2.0
//! A box with its top edges rounded and a hole drilled through it, measured and meshed.
//!
//!     cargo run --release --example drilled_box

use smlib::{boolean_difference, create_box, create_cylinder, thread, CrossSection, EdgeHandle, Tessellation};

fn main() -> smlib::Result<()> {
    // every thread that calls SMLib starts its context first
    thread::start();

    let mut body = create_box([0.0, 0.0, 0.0], 40.0, 20.0, 10.0)?;
    let top_edges: Vec<EdgeHandle> = body
        .edges()
        .iter()
        .filter(|edge| {
            let start = edge.start_vertex().and_then(|vertex| vertex.point().ok());
            let end = edge.end_vertex().and_then(|vertex| vertex.point().ok());
            matches!((start, end), (Some(start), Some(end)) if start[2] > 9.9 && end[2] > 9.9)
        })
        .map(|edge| edge.handle())
        .collect();
    body.fillet_edges(&top_edges, 2.0, CrossSection::Circular, 1, 1.0)?;

    let drill = create_cylinder([20.0, 10.0, -1.0], 3.0, 12.0)?;
    let part = boolean_difference(body, drill)?;

    let mesh = part.tessellate(&Tessellation { chord: 0.05, curve_angle_deg: 15.0, surface_angle_deg: 15.0, ..Default::default() })?;
    println!("faces:     {}", part.face_count());
    println!("volume:    {:.3} mm³", part.volume(1e-6)?);
    println!("area:      {:.3} mm²", part.area(1e-6)?);
    println!("mesh:      {} points, {} polygons", mesh.points().len() / 3, mesh.face_ids().len());
    println!("solid:     {}", part.is_manifold_solid());
    Ok(())
}
