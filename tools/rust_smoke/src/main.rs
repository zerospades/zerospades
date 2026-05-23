//! rust_smoke — cross-language schema-lock gate for the ZeroSpades golden-spec fixtures.
//!
//! Standalone Cargo project (NOT part of the CMake build — its independence from the
//! C++ toolchain is the portability proof, per CONTEXT.md D-20). Iterates the sample
//! fixtures and deserializes each into `Fixture` via `serde_json::from_str`, reporting
//! OK/FAIL per file and exiting non-zero if any fixture fails to parse.
//!
//! Deserialization approach — ADJACENTLY-TAGGED enum (verified empirically 2026-05-23,
//! RESEARCH "Open Questions (RESOLVED)" #2):
//!   The fixture envelope places `kind` and `expected` as *sibling* top-level keys.
//!   `#[serde(tag = "kind", content = "expected")]` on the `Expected` enum, combined with
//!   `#[serde(flatten)]` on the `Fixture::expected` field, maps that shape directly:
//!   `kind` selects the variant, `content = "expected"` points at the payload object.
//!   This supersedes the originally-sketched internally-tagged `#[serde(tag = "kind")]`
//!   (which would have required `kind` *inside* `expected`).

use serde::Deserialize;
use std::path::Path;

#[derive(Debug, Deserialize)]
#[allow(dead_code)]
struct Vec3 {
    x: f64,
    y: f64,
    z: f64,
}

#[derive(Debug, Deserialize)]
#[allow(dead_code)]
struct MapSpec {
    generator: String,
    ground_z: Option<i32>,
    content_hash: Option<String>,
}

/// `expected` payload, discriminated by the sibling `kind` envelope key.
/// Adjacently tagged: `kind` = discriminator, `expected` = content object.
#[derive(Debug, Deserialize)]
#[serde(tag = "kind", content = "expected", rename_all = "snake_case")]
#[allow(dead_code)]
enum Expected {
    WorldSnapshot {
        tick: i64,
        world_time_s: Option<f64>,
        map_content_hash: Option<String>,
        players: Vec<serde_json::Value>,
        grenades: Option<Vec<serde_json::Value>>,
        game_mode: Option<serde_json::Value>,
    },
    StepTrace {
        ticks: Vec<serde_json::Value>,
    },
    ValueLookup {
        value: serde_json::Value,
    },
    PacketRoundtrip {
        bytes_hex: String,
        decoded: serde_json::Value,
    },
}

/// Top-level fixture envelope (D-01). `kind` + `expected` are flattened into the
/// adjacently-tagged `Expected` enum. Unknown keys (e.g. `subsystem`) are ignored by
/// serde's default behavior, which is intentional — the envelope tolerates extra metadata.
#[derive(Debug, Deserialize)]
#[allow(dead_code)]
struct Fixture {
    version: String,
    id: String,
    behavior: String,
    seed: u64,
    protocol_version: String,
    map: MapSpec,
    inputs: Vec<serde_json::Value>,
    config: Option<serde_json::Value>,
    tolerances: Option<serde_json::Value>,
    #[serde(flatten)]
    expected: Expected,
}

fn main() {
    // cargo runs from the Cargo.toml directory (tools/rust_smoke/), so the fixtures
    // live two levels up. Resolve robustly relative to the manifest dir at compile time
    // when available, falling back to the runtime-relative path.
    let fixture_dir = manifest_relative_fixtures().unwrap_or_else(|| Path::new("../../fixtures").to_path_buf());

    let mut ok = 0usize;
    let mut fail = 0usize;

    // read_dir failure is an infrastructure error (missing fixtures/ dir), not fixture
    // content — surface it loudly. This is the only sanctioned panic path (T-02-10: accept).
    let entries = std::fs::read_dir(&fixture_dir)
        .unwrap_or_else(|e| panic!("fixtures dir not found at {}: {e}", fixture_dir.display()));

    for entry in entries {
        // Each DirEntry is itself a Result; a mid-iteration OS error (permission
        // denied on a specific entry, transient filesystem error during traversal)
        // must be reported as a FAIL and skipped, never panic. This upholds the
        // "report fail, never crash" contract for per-file errors (WR-02); only the
        // read_dir() call above is a sanctioned panic path (T-02-10).
        let entry = match entry {
            Ok(e) => e,
            Err(e) => {
                eprintln!("FAIL <dir entry>: read error: {e}");
                fail += 1;
                continue;
            }
        };
        let path = entry.path();

        // Skip non-.json files.
        if path.extension().and_then(|e| e.to_str()) != Some("json") {
            continue;
        }
        // Skip non-fixture JSON: the registry and the JSON Schema document are not
        // Fixture envelopes and would (correctly) fail deserialization.
        match path.file_name().and_then(|n| n.to_str()) {
            Some("INDEX.json") | Some("fixture_schema.json") => continue,
            _ => {}
        }

        let name = path
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("<non-utf8>")
            .to_string();

        // Read errors are treated as a FAIL (no panic on per-file IO) so one unreadable
        // file does not abort the whole gate run.
        let text = match std::fs::read_to_string(&path) {
            Ok(t) => t,
            Err(e) => {
                eprintln!("FAIL {name}: read error: {e}");
                fail += 1;
                continue;
            }
        };

        // T-02-09 (mitigate): from_str returns Result — malformed fixture JSON yields Err,
        // never a panic. No .unwrap() on external fixture content.
        match serde_json::from_str::<Fixture>(&text) {
            Ok(f) => {
                println!("OK   {name}  id={}", f.id);
                ok += 1;
            }
            Err(e) => {
                eprintln!("FAIL {name}: {e}");
                fail += 1;
            }
        }
    }

    println!("\n{ok} passed, {fail} failed.");
    if fail > 0 {
        std::process::exit(1);
    }
    // Implicit exit 0 on success — no panic path on the public parsing route.
}

/// Resolve `fixtures/` relative to the crate manifest dir (set by cargo at build time),
/// so `cargo run` works regardless of the caller's current working directory.
fn manifest_relative_fixtures() -> Option<std::path::PathBuf> {
    let manifest = option_env!("CARGO_MANIFEST_DIR")?;
    // tools/rust_smoke/ -> repo root is two parents up; fixtures/ sits at the root.
    let dir = Path::new(manifest).join("..").join("..").join("fixtures");
    if dir.is_dir() {
        Some(dir)
    } else {
        None
    }
}
