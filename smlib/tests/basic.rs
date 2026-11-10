use smlib::*;

#[test]
fn box_rounded_and_meshed() {
    thread::start();
    let mut body = create_box([0.0, 0.0, 0.0], 20.0, 10.0, 5.0).unwrap();
    assert_eq!(body.face_count(), 6);
    assert!((body.volume(1e-6).unwrap() - 1000.0).abs() < 1e-6);
    let top: Vec<EdgeHandle> = body
        .edges()
        .iter()
        .filter(|edge| {
            let (start, end) = (edge.start_vertex().unwrap().point().unwrap(), edge.end_vertex().unwrap().point().unwrap());
            start[2] > 4.9 && end[2] > 4.9
        })
        .map(|edge| edge.handle())
        .collect();
    assert_eq!(top.len(), 4);
    body.fillet_edges(&top, 1.0, CrossSection::Circular, 1, 1.0).unwrap();
    assert!(body.is_manifold_solid());
    let mesh = body.tessellate(&Tessellation { chord: 0.05, curve_angle_deg: 15.0, surface_angle_deg: 15.0, allow_partial: true, ..Default::default() }).unwrap();
    assert!(!mesh.points().is_empty());
    assert_eq!(mesh.face_ids().iter().filter(|&&id| id < 0).count(), 0);
    let tool = create_cylinder([10.0, 5.0, -1.0], 2.0, 10.0).unwrap();
    let drilled = boolean_difference(body, tool).unwrap();
    assert!(drilled.is_manifold_solid());
    let lines = drilled.tessellate_boundaries(4.0, true).unwrap();
    assert_eq!(lines.counts().len(), lines.edges().len());
}

#[test]
fn copies_across_threads() {
    thread::start();
    let body = std::sync::Mutex::new(create_box([0.0, 0.0, 0.0], 10.0, 10.0, 10.0).unwrap());
    let volumes: Vec<f64> = std::thread::scope(|scope| {
        let workers: Vec<_> = (0..4)
            .map(|_| {
                scope.spawn(|| {
                    thread::start();
                    let copy = body.lock().unwrap().copy().unwrap();
                    let tool = create_box([2.0, 2.0, -1.0], 6.0, 6.0, 12.0).unwrap();
                    let volume = boolean_difference(copy, tool).unwrap().volume(1e-6).unwrap();
                    thread::bury();
                    volume
                })
            })
            .collect();
        workers.into_iter().map(|worker| worker.join().unwrap()).collect()
    });
    for volume in volumes {
        assert!((volume - 640.0).abs() < 1e-6, "{volume}");
    }
}

#[test]
fn exact_data_of_a_drilled_box() {
    thread::start();
    let body = create_box([0.0, 0.0, 0.0], 20.0, 10.0, 5.0).unwrap();
    let tool = create_cylinder([10.0, 5.0, -1.0], 2.0, 10.0).unwrap();
    let drilled = boolean_difference(body, tool).unwrap();
    let text = drilled.exact_json().unwrap();
    if std::env::var("SHOW_EXACT").is_ok() {
        println!("{text}");
    }
    assert!(text.contains("\"kind\":\"cylinder\""));
    assert!(text.contains("\"kind\":\"plane\""));
    assert_eq!(text.matches("\"loops\"").count(), drilled.face_count());
}
