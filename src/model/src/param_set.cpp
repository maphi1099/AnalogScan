#include "autoneg/model/param_set.hpp"

#include "autoneg/base/hash.hpp"

#include <algorithm>

namespace autoneg::model {

namespace {

template <class Values>
auto lowerBound(Values& values, std::string_view name)
{
    return std::ranges::lower_bound(values, name, std::less<>{}, &ParamSet::Entry::first);
}

void addPoint(base::Hasher& h, const NormPoint& p)
{
    h.add(p.x).add(p.y);
}

struct ValueHasher
{
    base::Hasher& h;

    void operator()(double v) const { h.add(v); }
    void operator()(int v) const { h.add(v); }
    void operator()(bool v) const { h.add(v); }
    void operator()(const std::string& v) const { h.add(std::string_view(v)); }
    void operator()(const base::Vec3& v) const { h.add(v.x).add(v.y).add(v.z); }
    void operator()(const NormRect& r) const { h.add(r.x).add(r.y).add(r.w).add(r.h); }
    void operator()(const Curve& c) const
    {
        h.add(static_cast<std::uint64_t>(c.points.size()));
        for (const NormPoint& p : c.points) {
            addPoint(h, p);
        }
    }
    void operator()(const SpotList& spots) const
    {
        h.add(static_cast<std::uint64_t>(spots.size()));
        for (const Spot& s : spots) {
            addPoint(h, s.center);
            addPoint(h, s.offset);
            h.add(s.radius);
        }
    }
};

} // namespace

ParamSet ParamSet::with(std::string_view name, ParamValue value) const
{
    ParamSet result = *this;
    auto it = lowerBound(result.values_, name);
    if (it != result.values_.end() && it->first == name) {
        it->second = std::move(value);
    } else {
        result.values_.emplace(it, std::string(name), std::move(value));
    }
    return result;
}

ParamSet ParamSet::without(std::string_view name) const
{
    ParamSet result = *this;
    auto it = lowerBound(result.values_, name);
    if (it != result.values_.end() && it->first == name) {
        result.values_.erase(it);
    }
    return result;
}

const ParamValue* ParamSet::find(std::string_view name) const noexcept
{
    auto it = lowerBound(values_, name);
    if (it != values_.end() && it->first == name) {
        return &it->second;
    }
    return nullptr;
}

std::uint64_t ParamSet::hash() const noexcept
{
    base::Hasher h;
    h.add(static_cast<std::uint64_t>(values_.size()));
    for (const auto& [name, value] : values_) {
        h.add(std::string_view(name));
        h.add(static_cast<std::uint64_t>(value.index()));
        std::visit(ValueHasher{h}, value);
    }
    return h.value();
}

} // namespace autoneg::model
