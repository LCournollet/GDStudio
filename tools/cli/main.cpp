// gdds-cli: drives the decoration engine on GD level strings (no game needed).
//
//   gdds-cli testlevel <A..L> <out.txt>
//   gdds-cli analyze <level.txt>
//   gdds-cli decorate <in.txt> <out.txt> [options]
//       --preset "<name>"         built-in preset (default: Modern Glow)
//       --project <file.json>     load an existing project (layers, masks, records)
//       --save-project <file>     write the project after the operation
//       --layers id1,id2          only these layers (ids or kind names)
//       --mask x0,y0,x1,y1        operation limited to a rectangle mask
//       --mode generate|regenerate|remove
//       --seed <n>                global seed
//       --level-colors            also set the level's BG/ground colors from the palette
//   gdds-cli presets
//   gdds-cli catalog [query]

#include <gdds/Presets.hpp>
#include <gdds/Studio.hpp>
#include <gdds/TestLevels.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace gdds;

namespace {

std::string readFile(std::string const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

void writeFile(std::string const& path, std::string const& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path);
    out << data;
}

std::vector<std::string> splitList(std::string const& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::map<std::string, std::string> parseOptions(int argc, char** argv, int start) {
    std::map<std::string, std::string> opts;
    for (int i = start; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) != 0) continue;
        std::string key = a.substr(2);
        if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0) opts[key] = argv[++i];
        else opts[key] = "1";
    }
    return opts;
}

// Applies the palette's background colors to the level start colors.
void applyLevelColors(StringLevelBackend& backend, Project const& project) {
    auto channels = backend.readChannels();
    auto set = [&](int id, Color3 c) {
        for (auto& ch : channels)
            if (ch.id == id) { ch.r = c.r; ch.g = c.g; ch.b = c.b; return; }
        ColorChannel ch;
        ch.id = id; ch.r = c.r; ch.g = c.g; ch.b = c.b;
        channels.push_back(ch);
    };
    auto bg = resolveRole(project.palette, {}, ColorRole::Background).color;
    auto fg = resolveRole(project.palette, {}, ColorRole::Foreground).color;
    set(1000, bg.mix({0, 0, 0}, 0.35));
    set(1001, fg);
    set(1009, fg);
    set(1002, resolveRole(project.palette, {}, ColorRole::Secondary).color);
    backend.writeChannels(channels);
}

int cmdAnalyze(std::string const& path) {
    Level level = Level::parse(readFile(path));
    auto a = Analyzer().analyze(Analyzer::inputsFromLevel(level), &level.header);
    std::printf("%s\n", a->summary().c_str());
    for (auto& s : a->structures)
        std::printf("  structure %d: %s cells=%d objects=%zu bounds=[%.0f,%.0f..%.0f,%.0f] depth=%d repeat=%d\n", s.id,
                    structureClassName(s.cls), s.cellCount, s.objects.size(), s.bounds.x0, s.bounds.y0, s.bounds.x1,
                    s.bounds.y1, s.maxDepth, s.repeatCount);
    for (auto& sec : a->sections)
        std::printf("  section x=[%.0f..%.0f] %s speed=%d flipped=%d minClearance=%.0f hazards/block=%.2f tightness=%.2f\n",
                    sec.x0, sec.x1, gameModeName(sec.mode), sec.speed, int(sec.flipped), sec.minClearance,
                    sec.hazardDensity, sec.tightness);
    for (auto& h : a->hazardClusters)
        std::printf("  hazard cluster %d: %zu objects (spikes=%d saws=%d) support=%d\n", h.id, h.objects.size(), h.spikes,
                    h.saws, h.supportStructure);
    std::printf("  staircases=%zu corners: convex=%d concave=%d\n", a->staircases.size(),
                int(std::count_if(a->corners.begin(), a->corners.end(), [](auto& c) { return c.convex; })),
                int(std::count_if(a->corners.begin(), a->corners.end(), [](auto& c) { return !c.convex; })));
    return 0;
}

int cmdDecorate(std::string const& in, std::string const& out, std::map<std::string, std::string> const& opts) {
    Level level = Level::parse(readFile(in));
    StringLevelBackend backend(level, "cli");
    Studio studio(backend);
    std::optional<Project> project;
    if (auto it = opts.find("project"); it != opts.end()) {
        auto v = json::parse(readFile(it->second));
        if (!v) throw std::runtime_error("invalid project file");
        project = Project::fromJson(*v);
    }
    else {
        Project p;
        auto presetName = opts.count("preset") ? opts.at("preset") : std::string("Modern Glow");
        auto preset = PresetManager::builtin(presetName);
        if (!preset) throw std::runtime_error("unknown preset: " + presetName);
        PresetManager::apply(p, *preset);
        project = p;
    }
    auto rep = studio.attach(project);
    std::printf("reconcile: linked=%d modified=%d missing=%d recovered=%d\n", rep.linked, rep.modified, rep.missing,
                rep.recovered);
    if (opts.count("seed")) studio.project().settings.globalSeed = std::stoull(opts.at("seed"));

    GenerationRequest req;
    std::string mode = opts.count("mode") ? opts.at("mode") : "generate";
    req.mode = mode == "remove" ? OperationMode::Remove : mode == "regenerate" ? OperationMode::Regenerate : OperationMode::Generate;
    if (opts.count("layers")) {
        for (auto& name : splitList(opts.at("layers"))) {
            bool found = false;
            for (auto& l : studio.project().layers.all())
                if (l.id == name || layerKindName(l.kind) == name || l.name == name) {
                    req.layers.push_back(l.id);
                    found = true;
                }
            if (!found) throw std::runtime_error("unknown layer: " + name);
        }
    }
    if (opts.count("mask")) {
        auto parts = splitList(opts.at("mask"));
        if (parts.size() != 4) throw std::runtime_error("--mask x0,y0,x1,y1");
        Mask m;
        m.id = "cli-mask";
        m.name = "CLI mask";
        m.rect = {std::stod(parts[0]), std::stod(parts[1]), std::stod(parts[2]), std::stod(parts[3])};
        auto& masks = studio.project().masks;
        masks.erase(std::remove_if(masks.begin(), masks.end(), [](Mask const& x) { return x.id == "cli-mask"; }), masks.end());
        masks.push_back(m);
        req.masks.push_back(m.id);
    }
    if (opts.count("lock")) {
        for (auto& name : splitList(opts.at("lock")))
            for (auto& l : studio.project().layers.all())
                if (l.id == name || layerKindName(l.kind) == name) studio.project().layers.find(l.id)->locked = true;
    }

    auto outcome = studio.run(req);
    std::printf("%s\n", outcome.message.c_str());
    if (outcome.result) {
        auto& r = *outcome.result;
        for (auto& ls : r.layerStats) {
            auto l = studio.project().layers.find(ls.layerId);
            std::printf("  %-24s placed=%5d removed=%5d kept=%d rejected=%d  %.1f ms\n", l ? l->name.c_str() : ls.layerId.c_str(),
                        ls.placed, ls.removed, ls.keptModified, ls.rejected, ls.ms);
        }
        std::printf("  harmonizer: overlaps=%d densityTrimmed=%d repetitionsVaried=%d crowded=%d empty=%d\n",
                    r.harmony.overlapsRemoved, r.harmony.densityTrimmed, r.harmony.repetitionsVaried, r.harmony.crowdedZones,
                    r.harmony.emptyZones);
        std::printf("  validation: checked=%d rejected=%d\n", r.validation.checked, r.validation.rejected);
        for (auto& w : r.warnings) std::printf("  warning: %s\n", w.c_str());
        for (auto& i : outcome.issues)
            if (i.severity != IssueSeverity::Info) std::printf("  issue [%s]: %s\n", i.code.c_str(), i.message.c_str());
        std::printf("  generation: %.1f ms, analysis: %.1f ms\n", r.ms, studio.analysis().stats.analysisMs);
    }
    std::printf("  gameplay intact: %s\n", outcome.gameplayIntact ? "yes" : "NO");
    if (opts.count("level-colors")) applyLevelColors(backend, studio.project());
    writeFile(out, backend.level().serialize());
    if (opts.count("save-project")) writeFile(opts.at("save-project"), studio.project().toJson().dump());
    return outcome.ok ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::fprintf(stderr, "usage: gdds-cli testlevel|analyze|decorate|presets|catalog ...\n");
            return 1;
        }
        std::string cmd = argv[1];
        if (cmd == "testlevel" && argc >= 4) {
            auto t = makeTestLevel(argv[2]);
            writeFile(argv[3], t.level.serialize());
            std::printf("%s (%zu objects)\n%s\n", t.name.c_str(), t.level.objects.size(), t.description.c_str());
            return 0;
        }
        if (cmd == "analyze" && argc >= 3) return cmdAnalyze(argv[2]);
        if (cmd == "decorate" && argc >= 4) return cmdDecorate(argv[2], argv[3], parseOptions(argc, argv, 4));
        if (cmd == "presets") {
            for (auto& p : PresetManager::builtins()) std::printf("%-12s %s\n", p.name.c_str(), p.description.c_str());
            return 0;
        }
        if (cmd == "catalog") {
            auto& cat = DecoCatalog::builtin();
            auto list = cat.search(argc >= 3 ? argv[2] : "");
            for (auto d : list)
                if (argc >= 3 || !d->hasTag("library"))
                    std::printf("%5d %-14s %-12s %-26s %5.1fx%-5.1f %s\n", d->id, decoShapeName(d->shape), d->category.c_str(),
                                d->name.c_str(), d->visWidth(), d->visHeight(), d->frame.c_str());
            std::printf("%zu objects (%zu total in catalog)\n", list.size(), cat.all().size());
            return 0;
        }
        std::fprintf(stderr, "unknown command\n");
        return 1;
    }
    catch (std::exception const& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
