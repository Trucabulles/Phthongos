#pragma once
#include "../document/Document.h"
#include <array>
#include <tuple>

namespace phthongos::midi {
struct Event {
    std::uint64_t tick = 0;
    std::uint8_t status = 0, meta = 0;
    std::vector<std::uint8_t> data;
    bool isMeta(int type) const { return status == 255 && meta == type; }
    bool channel() const { return status >= 128 && status < 240; }
    bool note() const { return channel() && ((status & 240) == 128 || (status & 240) == 144); }
};
struct Clock {
    struct Point { std::uint64_t tick; double seconds, step; };
    std::vector<Point> points;
    double seconds(std::uint64_t) const;
    std::uint64_t tick(double) const;
};
struct Route { std::size_t track; std::uint8_t channel, port; Part part; };
struct Parsed {
    std::uint16_t format = 0, division = 480;
    std::vector<std::vector<Event>> tracks;
    std::vector<Note> notes;
    std::vector<std::tuple<std::uint64_t,std::uint64_t,std::uint8_t>> noteTicks;
    std::map<PartId,Route> routes;
    Clock clock;
};
struct Source {
    bool createdEmpty = false;
    std::string path, name;
    std::vector<std::uint8_t> bytes;
    static Source blank(std::string name);
    Parsed parse(const std::vector<std::string>& gmNames) const;
    std::vector<std::uint8_t> exportNotes(const std::vector<Note>&,
                                         const std::vector<std::string>& gmNames,
                                         double fixedBpm = 0) const;
};
// Raw SMF codec retains non-note events, including unknown meta events and SysEx.
std::vector<std::uint8_t> encode(std::uint16_t format, std::uint16_t division,
                               const std::vector<std::vector<Event>>& tracks);
std::vector<std::uint8_t> exportParts(const std::vector<Note>&,const std::vector<std::string>& gmNames,double fixedBpm = 0);
} // namespace phthongos::midi
