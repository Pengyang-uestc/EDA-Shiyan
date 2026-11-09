# A Board Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver A's Board data model, geometry primitives, and validated `EDA-BRD 1` read/write support for B/C integration.

**Architecture:** Keep the new Board model and geometry in `core/`, and expose serialization beside the existing footprint APIs. Parse all records into a temporary Board, validate references after parsing, and replace caller output only after complete success; resolve library footprints through a synchronous callback returning an owned `Result<Footprint>` value.

**Tech Stack:** C++17, Qt 6.11.1 Core/Gui, CMake, existing `Status`/`Result` conventions.

**Spec:** `docs/superpowers/specs/2026-11-02-a-board-core-design.md` plus `docs/DECISIONS.md` D4.1/D4.3/D4.5/D5/D6.

## Global Constraints

- Model and UI units are `mil`; type is `double`.
- Qt baseline is 6.11.1; project compiles as C++17.
- Text files use UTF-8 and LF; parsers reject unknown records and report line numbers.
- Public APIs use `Status`/`Result`, preserve caller models on failure, and do not leak exceptions.
- Board serialization follows `EDA-BRD 1`, including its exact record fields and post-parse reference checks.
- Board IDs are positive, monotonic allocations; `kInvalidId == 0`; explicit undo reinsertion preserves IDs.
- Manufacturing rules and UI/service code remain outside this A core.

## Review Focus

1. Self-touching, repeated, collinear, clockwise, and near-zero-area polygons: reject invalid polygons and normalize a valid loaded outline to CCW.
2. Forward references and arbitrary record order: validate successfully after the full parse, or fail with the source line for the broken reference.
3. Duplicate IDs/names, missing nets/layers/groups/instances/pins, and invalid via layer types: reject the whole document without partial output.
4. Escaped quoted strings, comment lines, invalid UTF-8, malformed numbers, missing `end`, and trailing data: follow D4.1 exactly.
5. `quint64` exhaustion and save failures: do not wrap IDs to zero or replace an existing file with partial data.

## File Map

- Create `core/geometry.h`, `core/geometry.cpp`: reusable mil-space geometry helpers.
- Create `core/board.h`, `core/board.cpp`: Board value model, structural validation, ID allocation and collection operations.
- Create `core/board_io.cpp`; modify `core/io.h`, `core/io_internal.h`, `core/api.cpp`: public Board file APIs and strict parser/writer.
- Modify `core/types.h`, `CMakeLists.txt`: ID aliases and build sources.
- Modify `docs/A-使用与对接.md`, `docs/interface-changelog.md`: integration contract and ownership boundaries.

## Tasks

### Task 1: Geometry and Board data model

**Interfaces:** Define `LayerGroup`, `BoardLayerType`, `BoardLayer`, `BoardSide`, `Instance`, `Track`, `Via`, `CopperPour`, `PinRef`, `Net`, and `Board` in namespace `eda`. Add `TrackId`, `ViaId`, and `PourId`. Board exposes `allocateTrackId/allocateViaId/allocatePourId`, `insertTrack/insertVia/insertCopperPour`, and matching remove methods. Geometry exposes polygon/segment helpers plus `footprintToBoard`, `boardToFootprint`, and `mapFootprintLayerToBoard` per D4.6.

- [x] Implement the types and APIs in the mapped core files.
- [x] Keep data validation structural only; do not add DRC or manufacturing thresholds.
- [x] Verify exact signatures, ownership, C++17/Qt6 types, and build integration by compiling the core and demo.

Ruling: Add the D4.6 transform helpers to A's `core/geometry` even though the initial task breakdown omitted them — they are shared model geometry with a fixed transform order and do not depend on B/C interfaces — cost if wrong: B/C may need an adapter if they expected the transform in another module.

Ruling: Parse the project's netlist as the `net` records inside `EDA-BRD 1`; the frozen decisions define no separate netlist file grammar — this avoids inventing an incompatible format — cost if wrong: a separately specified external netlist import would need its own follow-up parser.

### Task 2: `EDA-BRD 1` serialization

**Interfaces:** Add `FootprintResolver = std::function<Result<Footprint>(const QString&)>`, `validateBoard`, `encodeBoard`, `decodeBoard`, `saveBoard`, and `loadBoard`. Decode/load require the resolver to validate footprint IDs and referenced pin numbers. Save uses `QSaveFile`; parse builds a temporary object and only publishes it on success.

- [x] Implement the encoder for all D4.3 records with 3-decimal mil output and quoted-string escaping.
- [x] Implement UTF-8 line parser, field-count/number checks, duplicate tracking, line-numbered errors, and post-parse cross-reference checks.
- [x] Normalize loaded board outline winding to CCW; reject self-intersection and zero-area outline/pour polygons.
- [x] Compile the library, demo, and existing test target (do not execute tests unless requested).

### Task 3: Integration documentation and final review

- [x] Document Board/resolver APIs, board/network semantics, error behavior, and A/B/C ownership.
- [x] Review the complete diff against D4/D5/D6 and confirm no app/render/DRC dependency was introduced.
- [x] Run a fresh configure/build and review command output; report any verification that cannot run with the installed toolchain.

Independent static review found two Important and three Minor findings: unsafe chained string replacement, lossy small dimensions, short-edge geometry tolerance, permissive identifier tokenization, and incomplete missing-footprint reporting. All five were fixed in one pass. The final MinGW build compiled `eda_core`, `eda_core_demo`, and the existing `test_core` target with no warnings; tests were not executed.
