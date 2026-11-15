// SPDX-License-Identifier: Apache-2.0
//! Bodies whose edges once did not evaluate along their own range (see edges/README.md).

use smlib::{thread, Brep};
use std::path::PathBuf;

fn every_edge_evaluates_at_its_ends(name: &str) {
    thread::start();
    let file = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join(format!("tests/edges/{name}.smb"));
    let body = Brep::read_from_file(file.to_str().unwrap(), false).unwrap();
    for (index, edge) in body.edges().iter().enumerate() {
        let (start, end) = edge.parameter_range().unwrap();
        for parameter in [start, end] {
            assert!(edge.point_at(parameter).is_ok(), "{name}: edge {index} does not evaluate at {parameter:?} of its range {start:?}..{end:?}");
            assert!(edge.tangent_at(parameter).is_ok(), "{name}: edge {index} has no tangent at {parameter:?} of its range {start:?}..{end:?}");
        }
    }
    drop(body);
    thread::bury();
}

#[test]
fn edge_range_past_curve() {
    every_edge_evaluates_at_its_ends("edge-range-past-curve");
}
