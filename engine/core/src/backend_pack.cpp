#include "backend_internal.hpp"

namespace paperboat::core {
using namespace detail;

namespace {

// Run scripts/package_backend.py. With `progress`, its stderr progress lines are forwarded as
// they come; without, it runs as before (captured, 30 s timeout).
ProcessResult run_packer(const std::vector<std::string>& args, const fs::path& cwd,
                         const std::vector<std::pair<std::string, std::string>>& env,
                         const PackProgress& progress) {
  if (!progress) return ProcessRunner::run_sync(args, cwd, env, std::chrono::milliseconds(30000));
  ProcessResult r;
  r.exit_code = ProcessRunner::run_stream(args, cwd, env, [&](std::string_view stream, std::string_view line) {
    std::string& text = stream == "stdout" ? r.stdout_text : r.stderr_text;
    if (stream == "stderr") {
      try {
        const auto j = json::parse(line);
        progress(j.at("files").get<int>(), j.at("total").get<int>(), j.at("bytes").get<uint64_t>());
        return;
      } catch (const std::exception&) {}  // not a progress line: keep it as error text
    }
    text.append(line);
    text += '\n';
  });
  return r;
}

}  // namespace

PackResult BackendManager::pack_backend(const fs::path& path, const fs::path& out_path, bool /*fast*/,
                                        const PackProgress& progress) {
  PackResult res;
  const fs::path dir = resolve_backend_dir(path);
  if (!fs::is_directory(dir)) {
    res.errors.push_back("directory not found: " + path.string());
    return res;
  }

  // Run fast check first
  auto check_res = check_backend(dir, /*fast=*/true);
  if (!check_res.ok) {
    res.errors.push_back("cannot pack: backend verification failed");
    for (const auto& err : check_res.errors) res.errors.push_back("  " + err);
    return res;
  }

  BackendManifest manifest = load_manifest(dir);
  res.name = manifest.name.empty() ? dir.filename().string() : manifest.name;
  res.version = manifest.version.empty() ? "0.1.0" : manifest.version;

  fs::path package_out = out_path.empty() ? out_path : fs::absolute(out_path);  // the packer runs in the repo root
  if (package_out.empty()) {
    package_out = fs::current_path() / (res.name + "-" + res.version + ".ppk");
  }

  const fs::path repo_root = toolkit::find_repo_root(dir);
  auto py_env = prepare_python_env(repo_root, dir);
  fs::path script = repo_root / "scripts" / "package_backend.py";

  auto pack_proc = run_packer(
      {py_env.python_bin.string(), script.string(), "pack", "--dir", dir.string(), "--out", package_out.string()},
      repo_root, py_env.env, progress);

  if (!pack_proc.ok()) {
    res.errors.push_back("packing script failed: " + output_of(pack_proc));
    return res;
  }

  try {
    auto j = json::parse(pack_proc.stdout_text);
    if (!j.value("ok", false)) {
      res.errors.push_back(j.value("error", "unknown packing error"));
      return res;
    }
    res.ok = true;
    res.package_path = package_out;
    res.name = j.value("name", res.name);
    res.version = j.value("version", res.version);
    res.file_count = j.value("file_count", 0);
    res.total_bytes = j.value("total_bytes", static_cast<uint64_t>(0));
    res.sha256 = j.value("sha256", std::string{});
  } catch (const std::exception& e) {
    res.errors.push_back("failed to parse pack output: " + std::string(e.what()));
    return res;
  }

  return res;
}

UnpackResult BackendManager::unpack_backend(const fs::path& archive_in, const fs::path& dest_dir, bool verify,
                                            const PackProgress& progress) {
  UnpackResult res;
  const fs::path archive_path = fs::absolute(archive_in);  // the packer runs in the repo root
  if (!fs::is_regular_file(archive_path)) {
    res.errors.push_back("package archive not found: " + archive_path.string());
    return res;
  }

  const fs::path repo_root = toolkit::find_repo_root();
  auto py_env = prepare_python_env(repo_root);
  fs::path script = repo_root / "scripts" / "package_backend.py";

  fs::path target_dir = dest_dir.empty() ? dest_dir : fs::absolute(dest_dir);
  if (target_dir.empty()) {
    std::string stem = archive_path.stem().string();
    target_dir = fs::current_path() / stem;
  }

  std::vector<std::string> args = {
      py_env.python_bin.string(), script.string(), "unpack",
      "--pkg", archive_path.string(), "--dir", target_dir.string()
  };
  if (verify) args.push_back("--verify");

  auto unpack_proc = run_packer(args, repo_root, py_env.env, progress);
  if (!unpack_proc.ok()) {
    res.errors.push_back("unpack failed: " + output_of(unpack_proc));
    return res;
  }

  try {
    auto j = json::parse(unpack_proc.stdout_text);
    if (!j.value("ok", false)) {
      res.errors.push_back(j.value("error", "unknown unpack error"));
      return res;
    }
    res.ok = true;
    res.destination_dir = target_dir;
    res.name = j.value("name", "");
    res.version = j.value("version", "");
    res.file_count = j.value("file_count", 0);
  } catch (const std::exception& e) {
    res.errors.push_back("failed to parse unpack output: " + std::string(e.what()));
    return res;
  }

  if (verify) {
    auto check_res = check_backend(target_dir, /*fast=*/true);
    if (!check_res.ok) {
      res.errors.push_back("post-unpack verification failed:");
      for (const auto& err : check_res.errors) res.errors.push_back("  " + err);
      res.ok = false;
      return res;
    }
  }

  return res;
}

}  // namespace paperboat::core
