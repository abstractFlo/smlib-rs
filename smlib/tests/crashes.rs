// SPDX-License-Identifier: Apache-2.0
//! Inputs on which SMLib once crashed (see crashes/README.md). Each must end with a result or a
//! clean error; a crash fails the test run.

use serde_json::Value;
use smlib::{thread, Brep, CrossSection, EdgeHandle};
use std::path::PathBuf;

fn case_folder() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/crashes")
}

fn run_case(name: &str) {
    thread::start();
    let folder = case_folder();
    let body_file = folder.join(format!("{name}.txt"));
    let case: Value = serde_json::from_str(&std::fs::read_to_string(folder.join(format!("{name}.json"))).unwrap()).unwrap();
    let mut body = Brep::read_from_file_rebuilding(body_file.to_str().unwrap(), true, true).unwrap();
    let numbers = |key: &str| -> Vec<f64> { case[key].as_array().unwrap().iter().map(|value| value.as_f64().unwrap()).collect() };
    let edges: Vec<EdgeHandle> = {
        let every: Vec<EdgeHandle> = body.edges().iter().map(|edge| edge.handle()).collect();
        case["edges"].as_array().map(|indices| indices.iter().map(|index| every[index.as_u64().unwrap() as usize]).collect()).unwrap_or_default()
    };
    match case["op"].as_str().unwrap() {
        "fillet" => {
            let outcome = body.fillet_edges(&edges, case["radius"].as_f64().unwrap(), CrossSection::Circular, 1, 1.0);
            println!("{name}: {:?}", outcome.map_err(|error| error.message));
        }
        "fillet_per_edge" => {
            // the case files keep SMLib's SmFilletSurfaceGeneratorType: linear 1, circular 2, blend 3
            let sections: Vec<CrossSection> = numbers("xsects")
                .iter()
                .map(|&value| match value as i64 {
                    1 => CrossSection::Linear,
                    2 => CrossSection::Circular,
                    _ => CrossSection::Blend,
                })
                .collect();
            let outcome = body.fillet_edges_per_edge(&edges, &numbers("radii"), &sections, 1, 1.0);
            println!("{name}: {:?}", outcome.map_err(|error| error.message));
        }
        "volume" => {
            let volume = body.volume(1e-6).unwrap();
            let expected = case["expect"].as_f64().unwrap();
            assert!((volume - expected).abs() <= case["tolerance"].as_f64().unwrap(), "{name}: volume {volume}, expected {expected}");
        }
        other => panic!("{name}: unknown op {other}"),
    }
    drop(body);
    thread::bury();
}

#[test]
fn fillet_cube_top_and_corners() {
    run_case("fillet-cube-top-and-corners");
}

#[test]
fn fillet_flange() {
    run_case("fillet-flange");
}

#[test]
fn fillet_round_errors() {
    run_case("fillet-round-errors");
}

#[test]
fn fillet_seams_edge_in_one_face() {
    run_case("fillet-seams-edge-in-one-face");
}

#[test]
fn fillet_seams_stale_vertex_mate() {
    run_case("fillet-seams-stale-vertex-mate");
}

#[test]
fn unclamped_spline_prism() {
    run_case("unclamped-spline-prism");
}
