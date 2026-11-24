#include "hlds/PathNodes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <tuple>

#include "HlMonsters.hpp"

namespace hlds
{
    using namespace hldata;

    namespace
    {
        struct Way
        {
            int from = 0;
            int to = 0;
            std::uint8_t flags = 0;
            double length = 0;
            std::uint16_t brush = kNoBrush;
        };

        double distance(const PathNode &a, const PathNode &b)
        {
            const double d[3] = {(b.pos[0] - a.pos[0]) / 4096.0, (b.pos[1] - a.pos[1]) / 4096.0,
                                 (b.pos[2] - a.pos[2]) / 4096.0};
            return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        }

        /// Whether something of that size walks from one to the other, by
        /// the console's own walk.
        bool walks(const MoveWorld &world, int hull, const PathNode &a, const PathNode &b)
        {
            return walkClear(world, nullptr, -1, hull, a.pos, b.pos, static_cast<int>(distance(a, b) / 16) + 4);
        }
        /// The fine grid the floors are found on.
        constexpr int kLaidCell = 48;

        /// Nodes over the floors walked to from where players start, and the
        /// ways between them: for a map with none of its own.
        ///
        /// First every floor that can be walked to is found, a fine grid's
        /// step at a time (so that a doorway is found wherever it is); then
        /// nodes are picked from them, evenly apart -- as far apart as the
        /// format's room needs -- and joined where a straight walk joins
        /// them. Where two parts are left that the floors joined (round a
        /// corner, through a narrow door), the way the floors went is
        /// followed and nodes put along it at its turns. Whether any were
        /// laid.
        bool layWalkNodes(const std::vector<Entity> &entities, const MoveWorld &world, CookedLevel &level,
                          std::vector<Way> &ways, const NavMesh *nav)
        {
            struct Cell
            {
                fx pos[3];
                std::vector<int> next;
            };
            std::vector<Cell> cells;
            std::map<std::tuple<int, int, int>, int> known;
            fx base[2] = {0, 0};
            auto keyOf = [&](const fx *pos)
            {
                const int x = static_cast<int>(std::lround((pos[0] - base[0]) / 4096.0 / kLaidCell));
                const int y = static_cast<int>(std::lround((pos[1] - base[1]) / 4096.0 / kLaidCell));
                const int z = static_cast<int>(std::floor(pos[2] / 4096.0 / 40));
                return std::make_tuple(x, y, z);
            };
            auto find = [&](const fx *pos)
            {
                auto [x, y, z] = keyOf(pos);
                for (int dz = -1; dz <= 1; ++dz)
                {
                    const auto it = known.find({x, y, z + dz});
                    if (it != known.end())
                        return it->second;
                }
                return -1;
            };
            auto add = [&](const fx *pos)
            {
                Cell cell;
                for (int a = 0; a < 3; ++a)
                    cell.pos[a] = pos[a];
                known[keyOf(pos)] = static_cast<int>(cells.size());
                cells.push_back(cell);
                return static_cast<int>(cells.size()) - 1;
            };
            auto walk2 = [&](const fx *x, const fx *y, int steps)
            {
                return walkClear(world, nullptr, -1, kWalkStanding, x, y, steps) &&
                       walkClear(world, nullptr, -1, kWalkStanding, y, x, steps);
            };
            for (const Entity &entity : entities)
            {
                const std::string cls(entity.className());
                if (cls != "info_player_start" && cls != "info_player_deathmatch")
                    continue;
                double origin[3] = {0, 0, 0};
                entity.vec3("origin", origin);
                ActorState probe;
                for (int a = 0; a < 3; ++a)
                    probe.pos[a] = fxFromDouble(origin[a]);
                if (cells.empty())
                {
                    base[0] = probe.pos[0];
                    base[1] = probe.pos[1];
                }
                // (Onto the grid, so that the starts' floors are one.)
                auto [x, y, z] = keyOf(probe.pos);
                (void)z;
                probe.pos[0] = base[0] + fxFromInt(x * kLaidCell);
                probe.pos[1] = base[1] + fxFromInt(y * kLaidCell);
                probe.pos[2] -= fxFromInt(36);
                if (!actorToFloor(world, probe, fxFromInt(8), fxFromInt(256)) || find(probe.pos) >= 0)
                    continue;
                add(probe.pos);
            }
            if (cells.empty())
                return false;
            static const int kDirs[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
            std::size_t flooded = 0;
            auto flood = [&]()
            {
            for (std::size_t at = flooded; at < cells.size() && cells.size() < 40000; ++at)
            {
                for (const auto &dir : kDirs)
                {
                    const Cell &from = cells[at];
                    ActorState probe;
                    probe.pos[0] = from.pos[0] + fxFromInt(dir[0] * kLaidCell);
                    probe.pos[1] = from.pos[1] + fxFromInt(dir[1] * kLaidCell);
                    probe.pos[2] = from.pos[2];
                    // (Up a step or two of a stair as steep as inferno's: 36.)
                    if (!actorToFloor(world, probe, fxFromInt(36), fxFromInt(64)))
                        continue;
                    const int there = find(probe.pos);
                    if (there >= 0 && there < static_cast<int>(at))
                        continue; // (joined from its side already)
                    // (Walked, both ways: not a drop, not a jump.)
                    if (!walk2(from.pos, probe.pos, 8))
                        continue;
                    const int to = there >= 0 ? there : add(probe.pos);
                    cells[at].next.push_back(to);
                    cells[to].next.push_back(static_cast<int>(at));
                }
            }
            flooded = cells.size();
            };
            flood();
            // A place of the bots' navigation none of those floors is in --
            // up a flight of stairs too narrow for the grid to find its way
            // up (inferno's, to the apartments): its floors found from the
            // middle of each of its areas as well, and joined to the rest
            // where they meet. (Not a place that has some of the floors: a
            // map whose every place was reached is laid as it was.)
            if (nav != nullptr && !nav->areas.empty() && nav->places.size() < 64)
            {
                std::uint64_t reached = 0;
                for (const Cell &cell : cells)
                {
                    const float x = static_cast<float>(cell.pos[0] / 4096.0), y = static_cast<float>(cell.pos[1] / 4096.0);
                    const float z = static_cast<float>(cell.pos[2] / 4096.0);
                    for (const NavArea &area : nav->areas)
                    {
                        float m[3];
                        area.middle(m);
                        if (area.place > 0 && x >= area.lo[0] && x <= area.hi[0] && y >= area.lo[1] && y <= area.hi[1] &&
                            std::fabs(z - m[2]) < 48)
                        {
                            reached |= std::uint64_t{1} << (area.place - 1);
                            break;
                        }
                    }
                }
                for (const NavArea &area : nav->areas)
                {
                    if (area.place == 0 || ((reached >> (area.place - 1)) & 1))
                        continue;
                    float m[3];
                    area.middle(m);
                    ActorState probe;
                    probe.pos[0] = fxFromDouble(m[0]);
                    probe.pos[1] = fxFromDouble(m[1]);
                    probe.pos[2] = fxFromDouble(m[2] + 8);
                    if (actorToFloor(world, probe, fxFromInt(24), fxFromInt(64)) && find(probe.pos) < 0)
                        add(probe.pos);
                }
                flood();
            }

            // How far each floor is from the floors' edge (a wall, a drop),
            // in cells: the middle of a corridor or a room furthest.
            std::vector<int> clear(cells.size(), -1);
            {
                std::vector<int> wave;
                for (int k = 0; k < static_cast<int>(cells.size()); ++k)
                {
                    if (cells[k].next.size() < 4)
                    {
                        clear[k] = 0;
                        wave.push_back(k);
                    }
                }
                for (std::size_t at = 0; at < wave.size(); ++at)
                {
                    for (int t : cells[wave[at]].next)
                    {
                        if (clear[t] >= 0)
                            continue;
                        clear[t] = clear[wave[at]] + 1;
                        wave.push_back(t);
                    }
                }
            }
            // The order nodes are picked in: those furthest from the edges
            // first (then as found, out from the starts) -- so that a way is
            // laid down the middle of a corridor, not along its wall, where a
            // walker catches on every corner.
            std::vector<int> order(cells.size());
            for (int k = 0; k < static_cast<int>(cells.size()); ++k)
                order[k] = k;
            std::stable_sort(order.begin(), order.end(), [&](int x, int y)
                             { return std::min(clear[x], 3) > std::min(clear[y], 3); });
            // Picked: in that order, each `apart` from any picked before.
            std::vector<int> picked;
            int apart = 160;
            for (;; apart += 16)
            {
                picked.clear();
                const fx flat = fxFromInt(apart);
                for (int k : order)
                {
                    const Cell &cell = cells[k];
                    bool close = false;
                    for (int p : picked)
                    {
                        const fx dx = cells[p].pos[0] - cell.pos[0], dy = cells[p].pos[1] - cell.pos[1],
                                 dz = cells[p].pos[2] - cell.pos[2];
                        if (std::abs(dx) < flat && std::abs(dy) < flat && std::abs(dz) < fxFromInt(64))
                        {
                            close = true;
                            break;
                        }
                    }
                    if (!close)
                        picked.push_back(k);
                }
                // (Room kept for the nodes the joining puts in.)
                if (picked.size() <= static_cast<std::size_t>(kMaxPathNodes) * 3 / 4)
                    break;
            }
            std::vector<int> nodeOf(cells.size(), -1);
            for (int p : picked)
            {
                nodeOf[p] = static_cast<int>(level.pathNodes.size());
                PathNode node{};
                for (int a = 0; a < 3; ++a)
                    node.pos[a] = cells[p].pos[a];
                level.pathNodes.push_back(node);
            }
            std::vector<int> group;
            auto top = [&](int n)
            {
                while (group[n] != n)
                    n = group[n] = group[group[n]];
                return n;
            };
            auto join = [&](int x, int y)
            {
                Way way;
                way.from = x;
                way.to = y;
                way.length = distance(level.pathNodes[x], level.pathNodes[y]);
                way.flags = kPathLinkSmall | kPathLinkHuman | kPathLinkLarge;
                ways.push_back(way);
                std::swap(way.from, way.to);
                ways.push_back(way);
                group.resize(level.pathNodes.size());
                group[top(x)] = top(y);
            };
            group.resize(level.pathNodes.size());
            for (std::size_t n = 0; n < group.size(); ++n)
                group[n] = static_cast<int>(n);
            const double reach = apart * 2.6;
            const int first = static_cast<int>(level.pathNodes.size());
            for (int x = 0; x < first; ++x)
            {
                for (int y = x + 1; y < first; ++y)
                {
                    const PathNode &nx = level.pathNodes[x], &ny = level.pathNodes[y];
                    const double length = distance(nx, ny);
                    if (length <= reach && walk2(nx.pos, ny.pos, static_cast<int>(length / 16) + 4))
                        join(x, y);
                }
            }

            // Each floor's nearest node, by the floors (and the way there).
            std::vector<int> owner(cells.size(), -1), back(cells.size(), -1);
            std::vector<int> queue;
            for (int p : picked)
            {
                owner[p] = nodeOf[p];
                queue.push_back(p);
            }
            for (std::size_t k = 0; k < queue.size(); ++k)
            {
                for (int t : cells[queue[k]].next)
                {
                    if (owner[t] >= 0)
                        continue;
                    owner[t] = owner[queue[k]];
                    back[t] = queue[k];
                    queue.push_back(t);
                }
            }
            // Where neighbouring floors are nearest two nodes the ways do not
            // join: the floors' way from the one node to the other, with a
            // node at each turn of it that a straight walk cannot cut.
            auto cellOfNode = [&](int n)
            {
                for (int p : picked)
                    if (nodeOf[p] == n)
                        return p;
                return -1;
            };
            for (int u = 0; u < static_cast<int>(cells.size()); ++u)
            {
                for (int v : cells[u].next)
                {
                    if (owner[u] < 0 || owner[v] < 0 || top(owner[u]) == top(owner[v]))
                        continue;
                    if (level.pathNodes.size() >= static_cast<std::size_t>(kMaxPathNodes))
                        return true;
                    std::vector<int> chain;
                    for (int k = u; k >= 0; k = back[k])
                        chain.insert(chain.begin(), k);
                    for (int k = v; k >= 0; k = back[k])
                        chain.push_back(k);
                    (void)cellOfNode;
                    int last = chain.front();
                    for (std::size_t k = 1; k < chain.size(); ++k)
                    {
                        const bool end = k + 1 == chain.size();
                        const fx *from = cells[last].pos;
                        if (!end)
                        {
                            const double length = std::hypot((cells[chain[k + 1]].pos[0] - from[0]) / 4096.0,
                                                             (cells[chain[k + 1]].pos[1] - from[1]) / 4096.0);
                            if (walk2(from, cells[chain[k + 1]].pos, static_cast<int>(length / 16) + 4))
                                continue;
                        }
                        const int here = chain[k];
                        if (nodeOf[here] < 0)
                        {
                            if (level.pathNodes.size() >= static_cast<std::size_t>(kMaxPathNodes))
                                return true;
                            nodeOf[here] = static_cast<int>(level.pathNodes.size());
                            PathNode node{};
                            for (int a = 0; a < 3; ++a)
                                node.pos[a] = cells[here].pos[a];
                            level.pathNodes.push_back(node);
                            group.push_back(nodeOf[here]);
                        }
                        join(nodeOf[last], nodeOf[here]);
                        last = here;
                    }
                }
            }
            return true;
        }
        /// The laid nodes joined where the bots' navigation says the floors
        /// join and they are not -- through a doorway the laying missed (a
        /// map's double doors, a tunnel's end): for each two areas that
        /// meet, the nodes nearest them, if the way between them now is far
        /// longer than the crow flies, are joined -- straight, if that walks;
        /// or through a node put where the two areas meet. How many joins.
        int bridgeWithNav(const NavMesh &nav, const MoveWorld &world, CookedLevel &level, std::vector<Way> &ways)
        {
            const int areaCount = static_cast<int>(nav.areas.size());
            std::map<std::uint32_t, int> byId;
            for (int i = 0; i < areaCount; ++i)
                byId[nav.areas[i].id] = i;
            auto nearestNode = [&](const float *at)
            {
                int best = -1;
                double bestCost = 0;
                for (std::size_t n = 0; n < level.pathNodes.size(); ++n)
                {
                    const PathNode &node = level.pathNodes[n];
                    const double dx = node.pos[0] / 4096.0 - at[0], dy = node.pos[1] / 4096.0 - at[1];
                    const double dz = (node.pos[2] / 4096.0 - at[2]) * 3;
                    const double cost = dx * dx + dy * dy + dz * dz;
                    if (best < 0 || cost < bestCost)
                    {
                        best = static_cast<int>(n);
                        bestCost = cost;
                    }
                }
                return best;
            };
            // The graph so far, both ways a link is.
            std::vector<std::vector<std::pair<int, double>>> next;
            auto rebuild = [&]()
            {
                next.assign(level.pathNodes.size(), {});
                for (const Way &way : ways)
                    if (way.flags & kPathLinkHuman)
                        next[way.from].push_back({way.to, way.length});
            };
            rebuild();
            auto along = [&](int from, int to)
            {
                std::vector<double> far(level.pathNodes.size(), 1e18);
                std::vector<char> done(level.pathNodes.size(), 0);
                far[from] = 0;
                for (;;)
                {
                    int at = -1;
                    for (std::size_t n = 0; n < far.size(); ++n)
                        if (!done[n] && far[n] < 1e17 && (at < 0 || far[n] < far[at]))
                            at = static_cast<int>(n);
                    if (at < 0 || at == to)
                        break;
                    done[at] = 1;
                    for (const auto &[to2, length] : next[at])
                        far[to2] = std::min(far[to2], far[at] + length);
                }
                return far[to];
            };
            auto join = [&](int a, int b)
            {
                Way way;
                way.from = a;
                way.to = b;
                way.length = distance(level.pathNodes[a], level.pathNodes[b]);
                way.flags = kPathLinkSmall | kPathLinkHuman | kPathLinkLarge;
                ways.push_back(way);
                std::swap(way.from, way.to);
                ways.push_back(way);
            };
            auto areaAt = [&](const PathNode &node)
            {
                const float x = static_cast<float>(node.pos[0] / 4096.0), y = static_cast<float>(node.pos[1] / 4096.0);
                const float z = static_cast<float>(node.pos[2] / 4096.0);
                int best = -1;
                float bestCost = 0;
                for (int i = 0; i < areaCount; ++i)
                {
                    const NavArea &area = nav.areas[i];
                    float m[3];
                    area.middle(m);
                    const float dx = std::max({area.lo[0] - x, 0.0f, x - area.hi[0]});
                    const float dy = std::max({area.lo[1] - y, 0.0f, y - area.hi[1]});
                    const float cost = dx * dx + dy * dy + (z - m[2]) * (z - m[2]);
                    if (best < 0 || cost < bestCost)
                    {
                        best = i;
                        bestCost = cost;
                    }
                }
                return best;
            };
            auto onFloor = [&](const float *at, PathNode &out)
            {
                ActorState probe;
                for (int c = 0; c < 3; ++c)
                    probe.pos[c] = fxFromDouble(at[c]);
                if (!actorToFloor(world, probe, fxFromInt(24), fxFromInt(96)))
                    return false;
                out = PathNode{};
                for (int c = 0; c < 3; ++c)
                    out.pos[c] = probe.pos[c];
                return true;
            };
            auto both = [&](const PathNode &x, const PathNode &y)
            { return walks(world, kWalkStanding, x, y) && walks(world, kWalkStanding, y, x); };
            auto navChain = [&](int fromNode, int toNode, std::vector<PathNode> &out)
            {
                out.clear();
                const int start = areaAt(level.pathNodes[fromNode]), goal = areaAt(level.pathNodes[toNode]);
                if (start < 0 || goal < 0)
                    return false;
                // The areas between, fewest first.
                std::vector<int> before(static_cast<std::size_t>(areaCount), -2);
                std::vector<int> open{start};
                before[start] = -1;
                for (std::size_t k = 0; k < open.size() && before[goal] == -2; ++k)
                {
                    const int at = open[k];
                    for (std::uint32_t id : nav.areas[at].next)
                    {
                        const auto found = byId.find(id);
                        if (found == byId.end() || before[found->second] != -2)
                            continue;
                        before[found->second] = at;
                        open.push_back(found->second);
                    }
                }
                if (before[goal] == -2)
                    return false;
                std::vector<int> areasBetween;
                for (int at = goal; at >= 0; at = before[at])
                    areasBetween.push_back(at);
                std::reverse(areasBetween.begin(), areasBetween.end());
                if (areasBetween.size() > 40)
                    return false;
                // Where each meets the next, on the floor.
                std::vector<PathNode> points{level.pathNodes[fromNode]};
                for (std::size_t k = 0; k + 1 < areasBetween.size(); ++k)
                {
                    const NavArea &x = nav.areas[areasBetween[k]], &y = nav.areas[areasBetween[k + 1]];
                    float mx[3], my[3], meet[3];
                    x.middle(mx);
                    y.middle(my);
                    for (int c = 0; c < 2; ++c)
                    {
                        const float lo = std::max(x.lo[c], y.lo[c]), hi = std::min(x.hi[c], y.hi[c]);
                        meet[c] = (lo + hi) * 0.5f;
                    }
                    meet[2] = (mx[2] + my[2]) * 0.5f;
                    PathNode point;
                    if (onFloor(meet, point))
                        points.push_back(point);
                }
                points.push_back(level.pathNodes[toNode]);
                // Kept: as few as a straight walk needs.
                std::size_t at = 0;
                while (at + 1 < points.size())
                {
                    std::size_t far = at;
                    for (std::size_t k = points.size() - 1; k > at; --k)
                        if (both(points[at], points[k]))
                        {
                            far = k;
                            break;
                        }
                    if (far == at)
                        return false;
                    if (far + 1 < points.size())
                        out.push_back(points[far]);
                    at = far;
                }
                return !out.empty();
            };
            int joins = 0;
            std::map<std::pair<int, int>, bool> tried;
            for (int i = 0; i < areaCount; ++i)
            {
                const NavArea &here = nav.areas[i];
                float mid[3];
                here.middle(mid);
                for (std::uint32_t id : here.next)
                {
                    const auto found = byId.find(id);
                    if (found == byId.end())
                        continue;
                    const NavArea &there = nav.areas[found->second];
                    // (Not up onto what is jumped to, nor down a fall.)
                    float theirs[3];
                    there.middle(theirs);
                    if (std::fabs(theirs[2] - mid[2]) > 40)
                        continue;
                    const int a = nearestNode(mid), b = nearestNode(theirs);
                    if (a < 0 || b < 0 || a == b || tried.count({std::min(a, b), std::max(a, b)}))
                        continue;
                    tried[{std::min(a, b), std::max(a, b)}] = true;
                    const double crow = distance(level.pathNodes[a], level.pathNodes[b]);
                    const double way = along(a, b);
                    if (std::getenv("HLDS_NAV_DEBUG") != nullptr && way > crow * 2.5 + 200)
                        std::fprintf(stderr, "nav join? areas %u-%u (place %d-%d) nodes %d-%d crow %.0f way %.0f walks %d/%d\n",
                                     here.id, there.id, here.place, there.place, a, b, crow, way,
                                     walks(world, kWalkStanding, level.pathNodes[a], level.pathNodes[b]) ? 1 : 0,
                                     walks(world, kWalkStanding, level.pathNodes[b], level.pathNodes[a]) ? 1 : 0);
                    if (crow > 700 || way < crow * 2.5 + 200)
                        continue;
                    if (walks(world, kWalkStanding, level.pathNodes[a], level.pathNodes[b]) &&
                        walks(world, kWalkStanding, level.pathNodes[b], level.pathNodes[a]))
                    {
                        join(a, b);
                        rebuild();
                        ++joins;
                        continue;
                    }
                    // (Else along the areas themselves, from the one at the
                    // first node to the one at the second: through where
                    // each meets the next -- a door's gap, however it
                    // winds -- each point kept that a straight walk cannot
                    // pass by.)
                    std::vector<PathNode> chain;
                    if (!navChain(a, b, chain) ||
                        level.pathNodes.size() + chain.size() > static_cast<std::size_t>(kMaxPathNodes))
                        continue;
                    int last = a;
                    for (const PathNode &point : chain)
                    {
                        const int d = static_cast<int>(level.pathNodes.size());
                        level.pathNodes.push_back(point);
                        join(last, d);
                        last = d;
                    }
                    join(last, b);
                    rebuild();
                    ++joins;
                }
            }
            return joins;
        }
    } // namespace

    PathReport cookPathNodes(const std::vector<Entity> &entities, CookedLevel &level, const NodGraph *graph,
                             const NavMesh *nav)
    {
        PathReport report;
        level.pathNodes.clear();
        level.pathLinks.clear();
        if (level.models.empty())
            return report;

        MoveWorld world;
        world.standing = Hull{level.clipNodes.data(), level.planes.data(), level.planeTypes.data(),
                              level.models[0].clipStanding};
        world.crouched = Hull{level.clipNodes.data(), level.planes.data(), level.planeTypes.data(),
                              level.models[0].clipCrouched};
        world.large = Hull{level.clipNodes.data(), level.planes.data(), level.planeTypes.data(),
                           level.models[0].clipLarge};

        std::vector<Way> ways;
        // The brush a map's model number is (a link names its door as the
        // game does: "*12").
        std::map<int, std::uint16_t> brushOfModel;
        for (std::size_t b = 0; b < level.brushes.size(); ++b)
            brushOfModel[level.brushes[b].model] = static_cast<std::uint16_t>(b);

        // A map with something that flies (an alien controller, placed or
        // made) has the nodes in the air too, and the ways that only what
        // flies takes -- after all the rest, where there is still room.
        bool fliers = false;
        for (const Entity &entity : entities)
        {
            const std::string cls(entity.className());
            fliers = fliers || cls == "monster_alien_controller" ||
                     (cls == "monstermaker" && std::string(entity.value("monstertype")) == "monster_alien_controller");
        }
        if (graph != nullptr && !graph->nodes.empty())
        {
            // The game's own: its nodes on the ground and in the water (and
            // in the air, for what flies: above), in its order; and its links
            // for what walks (and flies).
            report.fromGraph = true;
            std::vector<int> ours(graph->nodes.size(), -1);
            // (More than the format holds: those nothing walks to or from
            // go first.)
            std::vector<char> walked(graph->nodes.size(), 0);
            for (const NodLink &link : graph->links)
            {
                if (link.info & (kNodSmall | kNodHuman | kNodLarge))
                    walked[link.from] = walked[link.to] = 1;
            }
            for (int pass = 0; pass < 3; ++pass)
            {
                for (std::size_t n = 0; n < graph->nodes.size(); ++n)
                {
                    const NodNode &src = graph->nodes[n];
                    const bool air = (src.info & kNodAir) != 0;
                    if (ours[n] >= 0)
                        continue;
                    if (pass == 2 ? !(air && fliers)
                                  : (air || !(src.info & (kNodLand | kNodWater)) || (pass == 0) != (walked[n] != 0)))
                        continue;
                    if (level.pathNodes.size() >= static_cast<std::size_t>(kMaxPathNodes))
                        continue;
                    PathNode node{};
                    for (int a = 0; a < 3; ++a)
                        node.pos[a] = fxFromDouble(src.origin[a]);
                    node.flags = (src.info & kNodWater) ? kPathNodeWater : (air ? kPathNodeAir : 0);
                    ours[n] = static_cast<int>(level.pathNodes.size());
                    level.pathNodes.push_back(node);
                }
            }
            for (std::size_t n = 0; n < graph->nodes.size(); ++n)
                report.nodesLeftOut += ours[n] < 0 ? 1 : 0;

            for (const NodLink &link : graph->links)
            {
                if (ours[link.from] < 0 || ours[link.to] < 0 || link.from == link.to)
                    continue;
                Way way;
                way.from = ours[link.from];
                way.to = ours[link.to];
                way.length = link.length;
                if (link.info & kNodSmall)
                    way.flags |= kPathLinkSmall;
                if (link.info & kNodHuman)
                    way.flags |= kPathLinkHuman;
                if (link.info & kNodLarge)
                    way.flags |= kPathLinkLarge;
                if ((link.info & kNodFly) && fliers)
                    way.flags |= kPathLinkFly;
                if (way.flags == 0)
                    continue; // (for what flies only, and nothing here does)
                if (link.model != 0)
                {
                    const auto found = brushOfModel.find(link.model);
                    // (Through something the port does not carry as a brush
                    // of its own: then it is simply a way.)
                    if (found != brushOfModel.end())
                        way.brush = found->second;
                }
                // Tried with the console's own walk, as a count only. It is
                // the game's word that is kept: where the two disagree it is
                // nearly always over a floor or a step that is a brush
                // entity (a grate, a lift at rest), which this trial -- of
                // the world alone -- has not got under it, and the game had.
                // A link the console really cannot walk costs whoever takes
                // it a second's delay and another plan, not a wall.
                const PathNode &a = level.pathNodes[way.from];
                const PathNode &b = level.pathNodes[way.to];
                const bool wet = (a.flags | b.flags) & kPathNodeWater;
                const bool walked = way.flags & (kPathLinkSmall | kPathLinkHuman | kPathLinkLarge);
                if (!wet && walked && !walks(world, (way.flags & kPathLinkHuman) ? kWalkStanding : kWalkCrouched, a, b))
                    ++report.refused;
                ways.push_back(way);
            }
        }
        else
        {
            // No graph of the game's: the map's info_nodes, onto the floor
            // under them, and every pair of them that can be walked between
            // both ways.
            for (const Entity &entity : entities)
            {
                if (entity.className() != "info_node")
                    continue;
                double origin[3] = {0, 0, 0};
                entity.vec3("origin", origin);
                ActorState probe;
                for (int a = 0; a < 3; ++a)
                    probe.pos[a] = fxFromDouble(origin[a]);
                if (!actorToFloor(world, probe, fxFromInt(8), fxFromInt(256)))
                    continue; // in a wall, or over nothing
                if (level.pathNodes.size() >= static_cast<std::size_t>(kMaxPathNodes))
                {
                    ++report.nodesLeftOut;
                    continue;
                }
                PathNode node{};
                for (int a = 0; a < 3; ++a)
                    node.pos[a] = probe.pos[a];
                level.pathNodes.push_back(node);
            }
            // Counter-Strike's maps have no info_node (its bots found their
            // way their own way): nodes are laid out here, over the floors
            // that can be walked to from where players start.
            if (level.pathNodes.empty() && activeGame == kGameCStrike)
                report.generated = layWalkNodes(entities, world, level, ways, nav);
            // (Each laid node's place, from the bots' navigation: the area it
            // stands in, or the nearest. The nodes stay the port's own: its
            // walk is what they were laid for.)
            if (report.generated && nav != nullptr && !nav->areas.empty())
            {
                report.fromNav = true;
                report.navJoins = bridgeWithNav(*nav, world, level, ways);
                report.navAreas = static_cast<int>(nav->areas.size());
                for (PathNode &node : level.pathNodes)
                {
                    const float x = static_cast<float>(node.pos[0] / 4096.0), y = static_cast<float>(node.pos[1] / 4096.0);
                    const float z = static_cast<float>(node.pos[2] / 4096.0);
                    int best = -1;
                    float bestCost = 1e9f;
                    for (std::size_t i = 0; i < nav->areas.size(); ++i)
                    {
                        const NavArea &area = nav->areas[i];
                        float mid[3];
                        area.middle(mid);
                        const bool inside = x >= area.lo[0] - 8 && x <= area.hi[0] + 8 && y >= area.lo[1] - 8 &&
                                            y <= area.hi[1] + 8 && std::fabs(z - mid[2]) < 48;
                        const float dx = x - mid[0], dy = y - mid[1], dz = (z - mid[2]) * 2;
                        const float cost = (inside ? 0.0f : 1e6f) + dx * dx + dy * dy + dz * dz;
                        if (cost < bestCost)
                        {
                            bestCost = cost;
                            best = static_cast<int>(i);
                        }
                    }
                    const int place = best >= 0 && bestCost < 1e6f + 300.0f * 300.0f ? nav->areas[best].place : 0;
                    if (place > 0 && place <= 31)
                        node.flags = static_cast<std::uint8_t>((node.flags & 7) | (place << kPathNodePlaceShift));
                }
            }
            const int count = report.generated ? 0 : static_cast<int>(level.pathNodes.size());
            for (int a = 0; a < count; ++a)
            {
                for (int b = a + 1; b < count; ++b)
                {
                    const double length = distance(level.pathNodes[a], level.pathNodes[b]);
                    if (length > 1024 || !walks(world, kWalkStanding, level.pathNodes[a], level.pathNodes[b]) ||
                        !walks(world, kWalkStanding, level.pathNodes[b], level.pathNodes[a]))
                        continue;
                    Way way;
                    way.from = a;
                    way.to = b;
                    way.length = length;
                    // (For every size, as one table used to serve them all.)
                    way.flags = kPathLinkSmall | kPathLinkHuman | kPathLinkLarge;
                    ways.push_back(way);
                    std::swap(way.from, way.to);
                    ways.push_back(way);
                }
            }
        }

        // Ways down. From a node to one below it and not far off, where
        // there is no way already: out level over the edge with nothing in
        // the way, and down onto it. One way only.
        const int count = static_cast<int>(level.pathNodes.size());
        for (int a = 0; a < count; ++a)
        {
            for (int b = 0; b < count; ++b)
            {
                const PathNode &top = level.pathNodes[a];
                const PathNode &foot = level.pathNodes[b];
                const fx fall = top.pos[2] - foot.pos[2];
                if (a == b || fall < fxFromInt(40) || fall > fxFromInt(280) ||
                    ((top.flags | foot.flags) & kPathNodeWater))
                    continue;
                const fx d[3] = {foot.pos[0] - top.pos[0], foot.pos[1] - top.pos[1], 0};
                if (fxLength(d) > fxFromInt(224))
                    continue;
                bool known = false;
                for (const Way &way : ways)
                    known = known || (way.from == a && way.to == b);
                if (known)
                    continue;
                const fx lift = kStandingFeet + fxFromInt(2);
                const fx from[3] = {top.pos[0], top.pos[1], top.pos[2] + lift};
                const fx over[3] = {foot.pos[0], foot.pos[1], top.pos[2] + lift};
                const fx down[3] = {foot.pos[0], foot.pos[1], foot.pos[2] + lift - fxFromInt(1)};
                const Trace across = traceAll(world, false, from, over);
                if (across.startSolid || across.fraction < kFractionOne)
                    continue;
                const Trace descent = traceAll(world, false, over, down);
                if (descent.startSolid || descent.fraction < kFractionOne)
                    continue;
                Way way;
                way.from = a;
                way.to = b;
                way.length = distance(top, foot);
                way.flags = kPathLinkSmall | kPathLinkHuman | kPathLinkDrop;
                ways.push_back(way);
                ++report.dropLinks;
            }
        }

        // A laid way through a door that opens for whoever comes to it (it
        // has no name: no button or trigger of its own) names it, as the
        // game's graph names its doors: then those who walk it ask for it to
        // open (the logic's linksTick), not walk into it shut. (A door with a
        // name is left to its trigger.) Through one that swings, no way at
        // all: open, it lies across the ways either side of it, which were
        // laid with it shut -- nuke's squeaky door held its bots for good.
        if (report.generated)
        {
            struct DoorBox
            {
                std::uint16_t brush;
                double lo[3], hi[3];
                bool swings;
            };
            std::vector<DoorBox> doors;
            for (const Entity &entity : entities)
            {
                const std::string_view kind = entity.className();
                const std::string_view model = entity.value("model");
                if ((kind != "func_door" && kind != "func_door_rotating") || !entity.value("targetname").empty() ||
                    model.size() < 2 || model[0] != '*')
                    continue;
                const auto found = brushOfModel.find(std::atoi(std::string(model.substr(1)).c_str()));
                if (found == brushOfModel.end())
                    continue;
                const Brush &brush = level.brushes[found->second];
                DoorBox box{found->second, {}, {}, kind == "func_door_rotating"};
                for (int k = 0; k < 3; ++k)
                {
                    box.lo[k] = brush.mins[k] - 4.0;
                    box.hi[k] = brush.maxs[k] + 4.0;
                }
                doors.push_back(box);
            }
            std::vector<char> through(ways.size(), 0);
            for (std::size_t w = 0; w < ways.size(); ++w)
            {
                Way &way = ways[w];
                if (way.brush != kNoBrush || doors.empty())
                    continue;
                const PathNode &from = level.pathNodes[way.from];
                const PathNode &to = level.pathNodes[way.to];
                // (Waist high, from the one to the other: where it meets the box.)
                double p[3], d[3];
                for (int k = 0; k < 3; ++k)
                {
                    p[k] = from.pos[k] / 4096.0 + (k == 2 ? 36.0 : 0.0);
                    d[k] = to.pos[k] / 4096.0 - from.pos[k] / 4096.0;
                }
                for (const DoorBox &box : doors)
                {
                    double enter = 0, leave = 1;
                    for (int k = 0; k < 3 && enter <= leave; ++k)
                    {
                        if (std::fabs(d[k]) < 1e-9)
                        {
                            if (p[k] < box.lo[k] || p[k] > box.hi[k])
                                leave = -1;
                            continue;
                        }
                        double t0 = (box.lo[k] - p[k]) / d[k], t1 = (box.hi[k] - p[k]) / d[k];
                        if (t0 > t1)
                            std::swap(t0, t1);
                        enter = t0 > enter ? t0 : enter;
                        leave = t1 < leave ? t1 : leave;
                    }
                    if (enter <= leave)
                    {
                        if (box.swings)
                            through[w] = 1;
                        else
                            way.brush = box.brush;
                        break;
                    }
                }
            }
            std::size_t kept = 0;
            for (std::size_t w = 0; w < ways.size(); ++w)
                if (!through[w])
                    ways[kept++] = ways[w];
            ways.resize(kept);
        }

        // Grouped by where they start, the nearest first; a node has room
        // for 255.
        std::stable_sort(ways.begin(), ways.end(), [](const Way &x, const Way &y)
                         { return x.from != y.from ? x.from < y.from : x.length < y.length; });
        for (const Way &way : ways)
        {
            PathNode &node = level.pathNodes[way.from];
            if (node.linkCount == 0)
                node.firstLink = static_cast<std::uint16_t>(level.pathLinks.size());
            if (node.linkCount == 255 || level.pathLinks.size() >= 0xFFFF)
                continue;
            // (One link a pair: the game's graph has none twice, and neither
            // has the working out.)
            PathLink link{};
            link.to = static_cast<std::uint8_t>(way.to);
            link.flags = way.flags;
            link.length = static_cast<std::uint16_t>(std::clamp(std::lround(way.length), 1L, 65535L));
            link.brush = way.brush;
            level.pathLinks.push_back(link);
            ++node.linkCount;
            report.doorLinks += way.brush != kNoBrush ? 1 : 0;
        }
        report.nodes = count;
        report.links = static_cast<int>(level.pathLinks.size());
        return report;
    }
} // namespace hlds
