#include "lock/resolver.hpp"
#include "repo/bin_repo.hpp"
#include "util/parse.hpp"
#include <sstream>

namespace lock {

namespace {

int compare_versions(const std::string& a, const std::string& b) {
    std::istringstream iss_a(a);
    std::istringstream iss_b(b);
    std::string part_a, part_b;

    while (true) {
        bool has_a = static_cast<bool>(std::getline(iss_a, part_a, '.'));
        bool has_b = static_cast<bool>(std::getline(iss_b, part_b, '.'));

        if (!has_a && !has_b) return 0;
        if (!has_a) return -1;
        if (!has_b) return 1;

        try {
            int num_a = std::stoi(part_a);
            int num_b = std::stoi(part_b);
            if (num_a < num_b) return -1;
            if (num_a > num_b) return 1;
        } catch (...) {
            if (part_a < part_b) return -1;
            if (part_a > part_b) return 1;
        }
    }
}

bool satisfies_constraint(const std::string& version, const VersionConstraint& constraint) {
    if (constraint.op.empty()) return true;
    int cmp = compare_versions(version, constraint.version);
    if (constraint.op == "=")  return cmp == 0;
    if (constraint.op == ">=") return cmp >= 0;
    if (constraint.op == "<=") return cmp <= 0;
    if (constraint.op == ">")  return cmp > 0;
    if (constraint.op == "<")  return cmp < 0;
    return true;
}

// Find the best-version entry for `name` in bin/, or nullopt.
std::optional<repo::BinEntry> best_in_bin(
    const std::filesystem::path& repo_bin_dir,
    const std::string& name,
    const VersionConstraint& constraint
) {
    auto entries = repo::find_entries(repo_bin_dir, name);
    std::optional<repo::BinEntry> best;
    for (auto& e : entries) {
        if (!satisfies_constraint(e.version, constraint)) continue;
        if (!best || compare_versions(e.version, best->version) > 0) best = e;
    }
    return best;
}

} // anonymous namespace

VersionConstraint parse_constraint(std::string_view raw) {
    VersionConstraint c;
    std::string s = util::trim(std::string(raw));

    size_t op_pos = std::string::npos;
    std::string op;

    if      ((op_pos = s.find(">=")) != std::string::npos) op = ">=";
    else if ((op_pos = s.find("<=")) != std::string::npos) op = "<=";
    else if ((op_pos = s.find('>'))  != std::string::npos) op = ">";
    else if ((op_pos = s.find('<'))  != std::string::npos) op = "<";
    else if ((op_pos = s.find('='))  != std::string::npos) op = "=";

    if (op_pos != std::string::npos) {
        c.package = util::trim(s.substr(0, op_pos));
        c.op      = op;
        c.version = util::trim(s.substr(op_pos + op.length()));
    } else {
        c.package = s;
        c.op      = "";
        c.version = "";
    }

    return c;
}

ResolveResult resolve(
    const std::filesystem::path& repo_bin_dir,
    const VersionConstraint& constraint
) {
    const std::string& pkg = constraint.package;

    // ── Explicit binary request (-bin suffix) ────────────────────────────────
    // User wrote "curl-bin" → must come from AOHARU bin/.
    // Try both "curl-bin" (in case the repo uses that exact name) and
    // the stripped base name "curl". Hard error if neither is found.
    if (pkg.size() > 4 && pkg.substr(pkg.size() - 4) == "-bin") {
        std::string base = pkg.substr(0, pkg.size() - 4);

        auto best = best_in_bin(repo_bin_dir, pkg, constraint);
        if (!best) best = best_in_bin(repo_bin_dir, base, constraint);

        if (!best) {
            throw std::runtime_error(
                "'" + pkg + "': no entry found in AOHARU bin/ repo. "
                "Add " + base + ".stars to bin/ or use '" + base + "' for an astral source build.");
        }

        return ResolveResult{ ResolveKind::BinRepo, *best };
    }

    // ── Default: source build ─────────────────────────────────────────────────
    // Check bin/ first — a maintainer may have published the package there
    // (either prebuilt tarball or a source recipe with build_script).
    // If found, use it so the package goes into the isolated store.
    {
        auto best = best_in_bin(repo_bin_dir, pkg, constraint);
        if (best) return ResolveResult{ ResolveKind::BinRepo, *best };
    }

    // Not in bin/ → AstralSource: astral-env build will call astral -S <pkg>.
    return ResolveResult{ ResolveKind::AstralSource, std::nullopt };
}

} // namespace lock
