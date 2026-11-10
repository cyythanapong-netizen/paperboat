#include "paperboat/core/record.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <system_error>

#ifndef PAPERBOAT_VERSION
#define PAPERBOAT_VERSION "0.0.0-dev"
#endif

namespace paperboat::core {
namespace {

std::string env(const char* name) {
  const char* value = std::getenv(name);
  return value ? value : "";
}

json request_part(const json& request, const char* key, const json& fallback) {
  const auto it = request.find(key);
  return it == request.end() ? fallback : *it;
}

}  // namespace

std::string now_iso8601() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &seconds);
#else
  gmtime_r(&seconds, &utc);
#endif
  char text[32];
  std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &utc);
  return text;
}

std::optional<RecordMeta> record_meta_from_env() {
  const std::string dir = env("PAPERBOAT_RECORD");
  if (dir.empty()) return std::nullopt;
  RecordMeta meta;
  meta.dir = dir;
  meta.tool_version = env("PAPERBOAT_VERSION");
  meta.command = env("PAPERBOAT_COMMAND");
  meta.backend_version = env("PAPERBOAT_BACKEND_VERSION");
  return meta;
}

RunRecord::RunRecord(RecordMeta meta, std::string engine, json request, json toolkits)
    : meta_(std::move(meta)), engine_(std::move(engine)), request_(std::move(request)), toolkits_(std::move(toolkits)) {
  path_ = meta_.dir / "record.json";
}

void RunRecord::begin(const json& request) { request_ = request; }

void RunRecord::add(const json& message) {
  if (finished_) return;
  const std::string type = message.value("type", "");
  if (type == "run.started") {
    started_ = true;
    run_id_ = message.value("run", "");
    started_at_ = now_iso8601();
    return;
  }
  if (!started_) return;  // nothing before the run belongs to it
  // The run clock, as the messages that carry one say it (signals, monitor states).
  if (const auto t = message.find("t"); t != message.end() && t->is_number()) scene_time_ = std::max(scene_time_, t->get<double>());
  if (type == "signal") {
    signals_.push_back(message);
  } else if (type == "scene") {
    if (!have_scene_ && message.contains("entities")) {
      // The record keeps the scene by id, as a replay addresses entities (#284).
      scene_base_ = json::object();
      for (const auto& entity : message.at("entities"))
        if (entity.contains("id")) scene_base_[entity.at("id").get<std::string>()] = entity;
      have_scene_ = true;
    }
  } else if (type == "scene.patch") {
    if (message.contains("entities")) scene_frames_.push_back({{"t", scene_time_}, {"entities", message.at("entities")}});
  } else if (type == "run.finished") {
    status_ = message.value("status", "");
    finished_at_ = now_iso8601();
    finished_ = true;
    write();
  }
}

json RunRecord::document() const {
  json run = {{"id", run_id_}, {"status", status_}};
  if (!started_at_.empty()) run["started"] = started_at_;
  if (!finished_at_.empty()) run["finished"] = finished_at_;

  json backend = {{"name", engine_}};
  if (!meta_.backend_version.empty()) backend["version"] = meta_.backend_version;

  json tool_versions = {{"paperboat", meta_.tool_version.empty() ? std::string(PAPERBOAT_VERSION) : meta_.tool_version}};
  if (!meta_.backend_version.empty()) tool_versions["backend"] = meta_.backend_version;

  json doc = {
      {"paperboat", 0},
      {"run", std::move(run)},
      {"backend", std::move(backend)},
      {"tool_versions", std::move(tool_versions)},
      {"flow", request_part(request_, "graph", json::object())},
      {"variables", request_part(request_, "variables", json::array())},
      {"settings", request_part(request_, "settings", json::object())},
      {"signals", signals_},
  };
  if (!meta_.command.empty()) doc["command"] = meta_.command;
  if (request_.contains("station")) doc["station"] = request_.at("station");
  if (toolkits_.is_array() && !toolkits_.empty()) doc["toolkits"] = toolkits_;
  if (have_scene_) doc["scene"] = {{"base", scene_base_}, {"frames", scene_frames_}};
  return doc;
}

void RunRecord::write() {
  std::error_code ec;
  std::filesystem::create_directories(meta_.dir, ec);
  if (ec) throw RecordError("cannot create record directory \"" + meta_.dir.string() + "\": " + ec.message());

  const std::filesystem::path tmp = path_.string() + ".tmp";
  std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
  if (!out) throw RecordError("cannot write run record \"" + tmp.string() + "\"");
  out << document().dump(2) << '\n';
  out.close();
  if (!out) throw RecordError("cannot write run record \"" + tmp.string() + "\"");

  // Atomic where the platform allows it; Windows will not replace, so drop the old one first.
  std::filesystem::rename(tmp, path_, ec);
  if (ec) {
    std::filesystem::remove(path_, ec);
    ec.clear();
    std::filesystem::rename(tmp, path_, ec);
    if (ec) throw RecordError("cannot replace run record \"" + path_.string() + "\": " + ec.message());
  }
}

}  // namespace paperboat::core
