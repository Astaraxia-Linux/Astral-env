#include "env/node.hpp"

namespace env {

void merge(NodeMap& dst, const NodeMap& src) {
    for (const auto& [key, src_val] : src) {
        auto it = dst.find(key);
        if (it == dst.end()) {
            dst[key] = src_val;
            continue;
        }
        Node& dst_val = it->second;
        if (dst_val.is_map() && src_val.is_map()) {
            merge(dst_val.map(), src_val.map());
        } else {
            dst_val = src_val;
        }
    }
}

} // namespace env
