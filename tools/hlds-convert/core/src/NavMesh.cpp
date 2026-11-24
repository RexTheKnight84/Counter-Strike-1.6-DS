#include "hlds/NavMesh.hpp"

#include <cstring>

namespace hlds
{
    namespace
    {
        struct Reader
        {
            const std::vector<std::uint8_t> &bytes;
            std::size_t at = 0;
            bool bad = false;
            template <typename T> T get()
            {
                T value{};
                if (at + sizeof(T) > bytes.size())
                {
                    bad = true;
                    return value;
                }
                std::memcpy(&value, bytes.data() + at, sizeof(T));
                at += sizeof(T);
                return value;
            }
            void skip(std::size_t count)
            {
                if (at + count > bytes.size())
                    bad = true;
                else
                    at += count;
            }
        };
    } // namespace

    bool parseNavMesh(const std::vector<std::uint8_t> &bytes, NavMesh &out, std::string &error)
    {
        out = NavMesh();
        Reader r{bytes};
        if (r.get<std::uint32_t>() != kNavMagic)
        {
            error = "not a .nav (its magic)";
            return false;
        }
        out.version = r.get<std::uint32_t>();
        if (out.version < 1 || out.version > 9)
        {
            error = "a .nav of version " + std::to_string(out.version) + " (the port reads 1 to 9)";
            return false;
        }
        if (out.version >= 4)
            r.get<std::uint32_t>(); // (the map's size)
        if (out.version >= 5)
        {
            const std::uint16_t count = r.get<std::uint16_t>();
            for (int p = 0; p < count && !r.bad; ++p)
            {
                const std::uint16_t length = r.get<std::uint16_t>();
                if (r.at + length > bytes.size())
                {
                    r.bad = true;
                    break;
                }
                std::string name(reinterpret_cast<const char *>(bytes.data() + r.at), length);
                name = name.c_str(); // (to its 0)
                out.places.push_back(name);
                r.skip(length);
            }
        }
        const std::uint32_t areaCount = r.get<std::uint32_t>();
        if (r.bad || areaCount > 100000)
        {
            error = "a .nav cut short";
            return false;
        }
        out.areas.reserve(areaCount);
        for (std::uint32_t i = 0; i < areaCount && !r.bad; ++i)
        {
            NavArea area;
            area.id = r.get<std::uint32_t>();
            area.flags = out.version <= 8 ? r.get<std::uint8_t>() : r.get<std::uint16_t>();
            for (int k = 0; k < 3; ++k)
                area.lo[k] = r.get<float>();
            for (int k = 0; k < 3; ++k)
                area.hi[k] = r.get<float>();
            area.neZ = r.get<float>();
            area.swZ = r.get<float>();
            for (int side = 0; side < 4 && !r.bad; ++side)
            {
                const std::uint32_t count = r.get<std::uint32_t>();
                if (count > 10000)
                {
                    r.bad = true;
                    break;
                }
                for (std::uint32_t c = 0; c < count; ++c)
                    area.next.push_back(r.get<std::uint32_t>());
            }
            const std::uint8_t spots = r.get<std::uint8_t>();
            for (int h = 0; h < spots && !r.bad; ++h)
            {
                if (out.version != 1)
                    r.get<std::uint32_t>();
                std::array<float, 3> at{};
                for (int k = 0; k < 3; ++k)
                    at[k] = r.get<float>();
                if (out.version != 1)
                    r.get<std::uint8_t>();
                area.hiding.push_back(at);
            }
            if (out.version >= 2)
            {
                const std::uint8_t approaches = r.get<std::uint8_t>();
                r.skip(static_cast<std::size_t>(approaches) * (4 + 4 + 1 + 4 + 1));
            }
            const std::uint32_t paths = r.get<std::uint32_t>();
            for (std::uint32_t p = 0; p < paths && !r.bad; ++p)
            {
                if (out.version < 3)
                {
                    r.skip(4 + 4);
                    const std::uint8_t count = r.get<std::uint8_t>();
                    r.skip(static_cast<std::size_t>(count) * (4 + 1));
                    continue;
                }
                r.skip(4 + 1 + 4 + 1);
                const std::uint8_t count = r.get<std::uint8_t>();
                r.skip(static_cast<std::size_t>(count) * (4 + 1));
            }
            if (out.version >= 5)
                area.place = r.get<std::uint16_t>();
            if (!r.bad)
                out.areas.push_back(std::move(area));
        }
        if (r.bad)
        {
            error = "a .nav cut short, or of another kind";
            out.areas.clear();
            return false;
        }
        return true;
    }
} // namespace hlds
