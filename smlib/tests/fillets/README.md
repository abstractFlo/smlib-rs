# Fillet cases

Inputs SMLib once refused to round although a round of that size exists, each fixed in this
repository. `tests/fillets.rs` runs them; every one must give a closed solid.

Each case is `<name>.smb` (a body in SMLib's native binary format, as `Brep::write_to_file` writes
it with `ascii` false: read back from the text format the body rounds, its UV trim curves rebuilt)
and `<name>.json` (the operation, the indices of its edges in `Brep::edges()`, the radius).

- **fillet-guess-past-edge-end**: the top and bottom rims of a sleeve and a plate joined by a
  concave round, cut by an extruded rounded rectangle whose smooth seam crosses the round's rim
  (4 mm; the rounded rectangle's corner 16 mm). The guess for starting a rail at a corner lay just
  past the end of the neighbouring filleted edge, where a closest point on that edge gave no
  solution, and the fillet failed instead of dropping the guess on the surface
  (`SmConstantRadiusFS::LoadInitialValues`).
