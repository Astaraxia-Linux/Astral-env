#include "repo/registry.hpp"
#include "env/node.hpp"

#include <algorithm>

namespace repo {

std::string expand_url(const std::string& url) {
    // github::Owner/Repo
    if (url.rfind("github::", 0) == 0) {
        std::string slug = url.substr(8);
        return "https://raw.githubusercontent.com/" + slug + "/refs/heads/main";
    }
    // codeberg::Owner/Repo
    if (url.rfind("codeberg::", 0) == 0) {
        std::string slug = url.substr(10);
        return "https://codeberg.org/" + slug + "/raw/branch/main";
    }
    // bare https:// — strip trailing slash
    std::string out = url;
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

std::vector<RepoEntry> parse_repos(const env::NodeMap& root) {
    std::vector<RepoEntry> repos;

    env::Node r(root);
    auto repo_node = r.get_path("System.Packages.Repository");

    if (repo_node && (*repo_node)->is_map()) {
        for (const auto& [name, vn] : (*repo_node)->map()) {
            if (name.rfind("__item_", 0) == 0) continue;
            RepoEntry e;
            e.name = name;
            if (vn.is_map()) {
                if (auto u = vn.get("url"); u && (*u)->is_str())
                    e.raw_url = expand_url((*u)->str());
            } else if (vn.is_str()) {
                e.raw_url = expand_url(vn.str());
            }
            if (!e.raw_url.empty()) repos.push_back(std::move(e));
        }
    }

    // Default: AOHARU
    if (repos.empty()) {
        repos.push_back({
            "AOHARU",
            "https://raw.githubusercontent.com/Astaraxia-Linux/AOHARU/refs/heads/main",
            "", true
        });
    }

    for (auto& r : repos) r.index_url = r.raw_url + "/astral.index";
    return repos;
}

std::string index_url(const RepoEntry& r) {
    return r.raw_url + "/astral.index";
}

std::string recipe_url(const RepoEntry& r, const std::string& cat,
                        const std::string& shard, const std::string& pkg,
                        const std::string& filename) {
    return r.raw_url + "/recipes/" + cat + "/" + shard + "/" + pkg + "/" + filename;
}

std::string bin_url(const RepoEntry& r, const std::string& arch,
                    const std::string& cat, const std::string& shard,
                    const std::string& filename) {
    return r.raw_url + "/bin/" + arch + "/" + cat + "/" + shard + "/" + filename;
}

std::string font_url(const RepoEntry& r, const std::string& family,
                     const std::string& filename) {
    return r.raw_url + "/font/" + family + "/" + filename;
}

} // namespace repo
