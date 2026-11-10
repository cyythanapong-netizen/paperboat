#pragma once

// Run record v1 (#277): what one run of a backend did, as plain JSON next to the project.
//
// `paperboat run --record <dir>` and `paperboat open ... --record <dir>` set PAPERBOAT_RECORD
// for the backend. The backend keeps every protocol message its run sends (signals, the
// Station's scene, the graph and variables it was given) and writes `<dir>/record.json` when
// the run finishes. A reader can replay it, diff two runs, or open it in a Studio.

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "paperboat/core/backend.hpp"

namespace paperboat::core {

/** A record that cannot be kept: a directory that cannot be made, a file that cannot be written. */
class RecordError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct RecordMeta {
  std::filesystem::path dir;
  std::string tool_version;      // paperboat's own version (PAPERBOAT_VERSION)
  std::string command;           // the command line that asked for the run (PAPERBOAT_COMMAND)
  std::string backend_version;   // the backend's version from backend.toml (PAPERBOAT_BACKEND_VERSION)
};

/** PAPERBOAT_RECORD and the version/command hints the CLI passes; null when no record was asked for. */
std::optional<RecordMeta> record_meta_from_env();

/**
 * One run's record. `begin` takes the graph.run request (flow, station, variables, settings);
 * `add` takes every message the run sends. `run.finished` writes the file, atomically: a reader
 * never sees a half-written record.
 */
class RunRecord {
 public:
  RunRecord(RecordMeta meta, std::string engine, json request, json toolkits = json::array());

  /** The graph.run request that started this run. */
  void begin(const json& request);
  /** A run message; `run.finished` writes the record. */
  void add(const json& message);

  bool finished() const { return finished_; }
  const std::filesystem::path& path() const { return path_; }
  /** The record as written. */
  json document() const;
  /** Write it now (also called by add on run.finished). Throws RecordError. */
  void write();

 private:
  RecordMeta meta_;
  std::string engine_;
  json request_ = json::object();
  json toolkits_ = json::array();

  std::vector<json> signals_;
  json scene_base_ = nullptr;
  json scene_frames_ = json::array();
  double scene_time_ = 0;
  bool have_scene_ = false;

  std::string run_id_;
  std::string status_;
  std::string started_at_;
  std::string finished_at_;
  bool started_ = false;
  bool finished_ = false;
  std::filesystem::path path_;
};

/** A wall-clock time as ISO 8601 UTC ("2026-11-10T14:48:54Z"). */
std::string now_iso8601();

}  // namespace paperboat::core
