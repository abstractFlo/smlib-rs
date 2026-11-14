// SPDX-License-Identifier: Apache-2.0
//! Inputs SMLib once refused to round (see fillets/README.md). Each must give a closed solid.

use serde_json::Value;
use smlib::{thread, Brep, CrossSection, EdgeHandle};
use std::path::PathBuf;

fn run_case(name: &str) {
    thread::start();
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/fillets");
    let body_file = folder.join(format!("{name}.smb"));
    let case: Value = serde_json::from_str(&std::fs::read_to_string(folder.join(format!("{name}.json"))).unwrap()).unwrap();
    let mut body = Brep::read_from_file(body_file.to_str().unwrap(), false).unwrap();
    let faces_before = body.face_count();
    let every: Vec<EdgeHandle> = body.edges().iter().map(|edge| edge.handle()).collect();
    let edges: Vec<EdgeHandle> = case["edges"].as_array().unwrap().iter().map(|index| every[index.as_u64().unwrap() as usize]).collect();
    let outcome = body.fillet_edges(&edges, case["radius"].as_f64().unwrap(), CrossSection::Circular, 1, 1.0);
    assert!(outcome.is_ok(), "{name}: {:?}", outcome.map_err(|error| error.message));
    assert!(body.is_manifold_solid(), "{name}: the rounded body is not a closed solid");
    assert!(body.face_count() >= faces_before + edges.len(), "{name}: {} faces, fewer than one round per edge", body.face_count());
    drop(body);
    thread::bury();
}

#[test]
fn fillet_guess_past_edge_end() {
    run_case("fillet-guess-past-edge-end");
}
