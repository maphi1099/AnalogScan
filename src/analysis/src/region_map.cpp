#include "autoneg/analysis/region_map.hpp"

#include "planes.hpp"

#include <cmath>

namespace autoneg::analysis {

RegionMap::RegionMap(int width, int height, std::vector<RegionClass> labels, double skewDeg,
                     std::vector<model::NormRect> rebate)
    : w_(width)
    , h_(height)
    , skewDeg_(skewDeg)
    , labels_(std::move(labels))
    , rebate_(std::move(rebate))
{}

RegionClass RegionMap::at(int x, int y) const
{
    if (x < 0 || y < 0 || x >= w_ || y >= h_ || labels_.empty()) {
        return RegionClass::Background;
    }
    return labels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)];
}

RegionClass RegionMap::classOf(model::NormPoint p) const
{
    const base::Point2 q = detail::sourceToDeskew({p.x * w_, p.y * h_}, skewDeg_, w_, h_);
    return at(static_cast<int>(std::floor(q.x)), static_cast<int>(std::floor(q.y)));
}

std::vector<double> RegionMap::shares() const
{
    std::vector<double> s(4, 0.0);
    for (const RegionClass c : labels_) {
        s[static_cast<std::size_t>(c)] += 1.0;
    }
    for (double& v : s) {
        v /= static_cast<double>(std::max<std::size_t>(labels_.size(), 1));
    }
    return s;
}

} // namespace autoneg::analysis
