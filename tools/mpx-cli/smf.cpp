#include "smf.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mpxcli {

namespace {

struct Reader {
    const std::vector<uint8_t>& d;
    size_t p;
    size_t end;
    bool ok = true;

    uint8_t u8() {
        if (p >= end) {
            ok = false;
            return 0;
        }
        return d[p++];
    }
    uint32_t be(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v = (v << 8) | u8();
        return v;
    }
    uint32_t vlq() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            uint8_t b = u8();
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        return v;
    }
};

struct RawEvent {
    uint64_t tick;
    int order;  // stable sort key
    bool tempo;
    uint32_t usPerQuarter;
    TimedMidi msg;
};

} // namespace

bool readSmf(const std::string& path, std::vector<TimedMidi>& out, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> data;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);

    Reader r{data, 0, data.size()};
    if (r.be(4) != 0x4D546864) {  // "MThd"
        error = "not a MIDI file";
        return false;
    }
    uint32_t hlen = r.be(4);
    size_t hstart = r.p;
    uint32_t format = r.be(2);
    uint32_t ntracks = r.be(2);
    uint32_t division = r.be(2);
    r.p = hstart + hlen;
    if (format > 1) {
        error = "MIDI file format 2 is not supported";
        return false;
    }
    if (division & 0x8000) {
        error = "SMPTE time division is not supported";
        return false;
    }
    if (division == 0) division = 480;

    std::vector<RawEvent> events;
    int order = 0;
    for (uint32_t t = 0; t < ntracks && r.ok && r.p + 8 <= data.size(); ++t) {
        uint32_t id = r.be(4);
        uint32_t len = r.be(4);
        size_t start = r.p;
        size_t end = std::min(data.size(), start + len);
        if (id != 0x4D54726B) {  // "MTrk"
            r.p = end;
            continue;
        }
        Reader tr{data, start, end};
        uint64_t tick = 0;
        uint8_t running = 0;
        while (tr.ok && tr.p < end) {
            tick += tr.vlq();
            uint8_t status = tr.u8();
            if (status == 0xFF) {
                uint8_t type = tr.u8();
                uint32_t mlen = tr.vlq();
                if (type == 0x51 && mlen == 3) {
                    RawEvent e{};
                    e.tick = tick;
                    e.order = order++;
                    e.tempo = true;
                    e.usPerQuarter = tr.be(3);
                    events.push_back(e);
                } else {
                    tr.p += mlen;
                }
                if (type == 0x2F) break;
                continue;
            }
            if (status == 0xF0 || status == 0xF7) {
                tr.p += tr.vlq();
                continue;
            }
            uint8_t d1;
            if (status & 0x80) {
                running = status;
                d1 = tr.u8();
            } else {
                d1 = status;
                status = running;
                if (!status) {
                    error = "running status without status byte";
                    return false;
                }
            }
            RawEvent e{};
            e.tick = tick;
            e.order = order++;
            e.msg.data[0] = status;
            e.msg.data[1] = d1;
            uint8_t type = status & 0xF0;
            if (type == 0xC0 || type == 0xD0) {
                e.msg.len = 2;
            } else {
                e.msg.data[2] = tr.u8();
                e.msg.len = 3;
            }
            events.push_back(e);
        }
        r.p = end;
    }

    std::stable_sort(events.begin(), events.end(), [](const RawEvent& a, const RawEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });
    double usPerQuarter = 500000.0, seconds = 0.0;
    uint64_t lastTick = 0;
    out.clear();
    for (const RawEvent& e : events) {
        seconds += static_cast<double>(e.tick - lastTick) * usPerQuarter / 1e6 / division;
        lastTick = e.tick;
        if (e.tempo) {
            usPerQuarter = e.usPerQuarter;
            continue;
        }
        TimedMidi m = e.msg;
        m.time = seconds;
        out.push_back(m);
    }
    return true;
}

bool demoPattern(const std::string& name, double seconds, std::vector<TimedMidi>& out) {
    out.clear();
    auto add = [&](double t, uint8_t s, uint8_t a, uint8_t b) {
        TimedMidi m;
        m.time = t;
        m.data[0] = s;
        m.data[1] = a;
        m.data[2] = b;
        m.len = 3;
        out.push_back(m);
    };
    const double beat = 0.5;  // 120 BPM
    bool kick = name == "set" || name == "kick";
    bool melody = name == "set" || name == "notes";
    if (!kick && !melody) return false;
    for (double t = 0; t < seconds; t += beat) {
        if (kick) {
            add(t, 0x90, 36, 120);
            add(t + 0.1, 0x80, 36, 0);
        }
    }
    if (melody) {
        static const int scale[] = {0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24, 27, 29, 31, 34, 36, 39, 41, 43, 46, 48};
        int step = 0;
        for (double t = beat * 0.5; t < seconds; t += beat * 0.5, ++step) {
            int idx = step % 21;
            if ((step / 21) % 2) idx = 20 - idx;
            int note = 48 + scale[idx];
            int vel = 70 + (step * 13) % 57;
            add(t, 0x90, static_cast<uint8_t>(note), static_cast<uint8_t>(vel));
            add(t + beat * 0.4, 0x80, static_cast<uint8_t>(note), 0);
        }
    }
    if (name == "set") {
        for (double t = 0; t < seconds; t += 0.05) {
            double ph = t / 8.0;
            int v = static_cast<int>(127.0 * (0.5 - 0.5 * std::cos(ph * 6.283185307)));
            add(t, 0xB0, 1, static_cast<uint8_t>(v));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const TimedMidi& a, const TimedMidi& b) { return a.time < b.time; });
    return true;
}

} // namespace mpxcli
