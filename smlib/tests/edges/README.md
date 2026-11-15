# Edge cases

Bodies whose edges SMLib once could not evaluate along their own parameter range. `tests/edges.rs`
reads each and evaluates every edge at both ends of `Edge::parameter_range`.

Each case is `<name>.smb` (a body in SMLib's native binary format, as `Brep::write_to_file` writes
it with `ascii` false).

- **edge-range-past-curve**: a C profile with rounded corners extruded, united with a hexagonal
  sleeve and a lug with rounded corners. The union split a curve so that one edge's interval began
  1.1e-16 before its curve's natural interval, and `SmApiEvaluateCurve` refused the edge's own
  start (`SM_ERR_INVALID_INPUT`).
