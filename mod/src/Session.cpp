#include "Session.hpp"

#include "ui/StudioPopup.hpp"
#include "ui/Toolbar.hpp"

#include "Files.hpp"
#include <gdds/ObjectInfo.hpp>

#include <algorithm>
#include <unordered_set>

using namespace geode::prelude;

namespace {

std::unique_ptr<StudioSession> g_session;
bool g_catalogVerified = false;
std::vector<int> g_disabledIds;

std::string sanitize(std::string const& s) {
    std::string out;
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += c;
        else if (c == ' ') out += '_';
        if (out.size() >= 40) break;
    }
    return out.empty() ? std::string("level") : out;
}

std::filesystem::path projectsDir() { return files::ensureDir(files::saveDir() / "projects"); }

std::optional<gdds::json::Value> readJsonFile(std::filesystem::path const& p) {
    auto text = files::readText(p);
    if (!text) return std::nullopt;
    return gdds::json::parse(*text);
}

// Picks the project file of this level. Several levels can share a name, so
// candidates are scored by how many of their generated objects are present.
std::pair<std::optional<gdds::Project>, std::filesystem::path> findProject(std::string const& levelName,
                                                                          std::vector<gdds::LevelObject> const& objects) {
    auto dir = projectsDir();
    std::string prefix = sanitize(levelName) + "-";
    std::unordered_set<uint64_t> live;
    std::unordered_set<int> liveGroups;
    for (auto& o : objects) {
        live.insert(o.fingerprint());
        for (int g : o.groups) liveGroups.insert(g);
    }
    std::optional<gdds::Project> best;
    std::filesystem::path bestPath;
    double bestScore = -1;
    int slots = 0;
    for (auto& f : files::listFiles(dir)) {
        auto name = files::fileName(f);
        if (name.rfind(prefix, 0) != 0 || f.extension() != ".json") continue;
        ++slots;
        auto v = readJsonFile(f);
        if (!v) continue;
        auto p = gdds::Project::fromJson(*v);
        if (p.levelName != levelName) continue;
        double score = 0;
        if (!p.records.empty()) {
            int found = 0;
            for (auto& r : p.records) found += live.count(r.fingerprint) ? 1 : 0;
            score = double(found) / double(p.records.size());
        }
        for (auto& [g, l] : p.tagGroups())
            if (liveGroups.count(g)) score += 0.25;
        if (p.records.empty() && p.tagGroups().empty()) score = 0.01;  // untouched project
        if (score > bestScore) {
            bestScore = score;
            best = std::move(p);
            bestPath = f;
        }
    }
    if (best && bestScore >= 0.3) return {std::move(best), bestPath};
    if (best && bestScore > 0 && best->records.empty()) return {std::move(best), bestPath};
    auto path = dir / (prefix + std::to_string(slots + 1) + ".json");
    return {std::nullopt, path};
}

}  // namespace

StudioSession* StudioSession::current() {
    // Only drop the session when ANOTHER editor is open: while the editor is
    // being built, GameManager may not point to it yet.
    auto lel = LevelEditorLayer::get();
    if (g_session && lel && lel != g_session->m_lel) g_session.reset();
    return g_session.get();
}

StudioSession* StudioSession::begin(LevelEditorLayer* lel, EditorUI* ui) {
    g_session.reset(new StudioSession(lel, ui));
    return g_session.get();
}

void StudioSession::end() {
    if (g_session && g_session->m_ready) g_session->save();
    g_session.reset();
}

StudioSession::StudioSession(LevelEditorLayer* lel, EditorUI* ui) : m_lel(lel), m_catalog(gdds::DecoCatalog::builtin()) {
    for (int id : g_disabledIds) m_catalog.disable(id);
    m_backend = std::make_unique<EditorBackend>(lel, ui);
    m_studio = std::make_unique<gdds::Studio>(*m_backend, m_catalog);
    m_maskTool = std::make_unique<MaskTool>(this);
}

void StudioSession::verifyCatalog() {
    if (g_catalogVerified || !Mod::get()->getSettingValue<bool>("verify-catalog")) return;
    g_catalogVerified = true;
    // Instantiate every curated object once and check the game reports it as a
    // pure decoration. If most checks fail at once, the check itself is not
    // reliable on this game version (layout mismatch): do not trust it.
    std::vector<int> failed;
    int checked = 0;
    for (auto& def : m_catalog.all()) {
        if (def.hasTag("library")) continue;
        auto obj = GameObject::createWithKey(def.id);
        ++checked;
        if (!obj || static_cast<int>(obj->m_objectType) != static_cast<int>(GameObjectType::Decoration)) failed.push_back(def.id);
    }
    if (checked > 0 && failed.size() * 3 > size_t(checked)) {
        log::warn("Catalog verification looks unreliable on this game version ({} of {} failed): ignored", failed.size(),
                  checked);
        return;
    }
    for (int id : failed) {
        log::warn("Object {} does not behave as a decoration in this game version: disabled", id);
        g_disabledIds.push_back(id);
        m_catalog.disable(id);
    }
    log::info("Catalog verification: {} objects checked, {} disabled", checked, failed.size());
}

void StudioSession::ensureReady() {
    if (m_ready) return;
    verifyCatalog();
    auto objects = m_backend->readObjects();
    auto [found, path] = findProject(m_backend->levelName(), objects);
    m_projectPath = path;
    if (found) {
        m_reconcile = m_studio->attach(std::move(found));
    }
    else {
        m_reconcile = m_studio->attach(std::nullopt);
        m_studio->project().projectId = path.stem().string();
    }
    m_studio->project().settings.maxObjectsPerOperation = int(Mod::get()->getSettingValue<int64_t>("max-objects"));
    m_ready = true;
    if (selectedLayer.empty()) {
        for (auto& l : project().layers.all())
            if (l.kind != gdds::LayerKind::Group) { selectedLayer = l.id; break; }
    }
    if (m_reconcile.modified || m_reconcile.missing || m_reconcile.recovered)
        status(fmt::format("Project loaded: {} linked, {} hand-modified, {} removed by hand, {} recovered", m_reconcile.linked,
                           m_reconcile.modified, m_reconcile.missing, m_reconcile.recovered));
    else status(fmt::format("Project ready ({} generated objects)", m_reconcile.linked));
}

void StudioSession::save() {
    if (!m_ready || m_projectPath.empty()) return;
    auto text = project().toJson().dump(1);
    std::string error;
    if (!files::writeTextSafe(m_projectPath, text, &error)) log::error("Could not save the studio project: {}", error);
}

void StudioSession::status(std::string message, bool error) {
    m_status = std::move(message);
    if (error) log::warn("{}", m_status);
    else log::info("{}", m_status);
}

void StudioSession::report(gdds::OperationOutcome const& out) {
    status(out.message, !out.ok);
    Notification::create(out.message, out.ok ? NotificationIcon::Success : NotificationIcon::Warning,
                         out.ok ? NOTIFICATION_DEFAULT_TIME : NOTIFICATION_LONG_TIME)
        ->show();
    if (!out.gameplayIntact)
        FLAlertLayer::create("Gameplay check",
                             "The operation was <cr>rolled back</c>: the gameplay comparison detected a difference. "
                             "Nothing generated was kept.",
                             "OK")
            ->show();
}

StudioSession::~StudioSession() {
    if (m_job) {
        m_job->cancel = true;
        if (m_job->thread.joinable()) m_job->thread.join();
        m_job.reset();
    }
}

bool StudioSession::startJob(gdds::GenerationRequest const& request, bool preview) {
    ensureReady();
    if (m_job) {
        status("Another generation is still running", true);
        return false;
    }
    auto job = std::make_unique<Job>();
    // Main thread: snapshot the project and make sure the analysis exists.
    job->prepared = m_studio->prepare(request);
    job->preview = preview;
    job->started = std::chrono::steady_clock::now();
    Job* raw = job.get();
    job->thread = std::thread([raw] {
        // Worker thread: pure computation on the snapshot, no game object touched.
        raw->result = gdds::Studio::compute(raw->prepared, &raw->cancel);
        raw->done = true;
    });
    m_job = std::move(job);
    status(preview ? "Computing preview..." : "Generating...");
    return true;
}

void StudioSession::pollJob() {
    if (!m_job || !m_job->done) return;
    if (m_job->thread.joinable()) m_job->thread.join();
    auto job = std::move(m_job);
    // Main thread: apply to the level.
    auto out = m_studio->finish(job->prepared, std::move(job->result), job->preview);
    handleOutcome(out, job->prepared.request, job->preview);
}

void StudioSession::cancelJob() {
    if (m_job) m_job->cancel = true;
}

void StudioSession::abortJob() {
    if (!m_job) return;
    m_job->cancel = true;
    if (m_job->thread.joinable()) m_job->thread.join();
    m_job.reset();
    status("Generation aborted");
}

double StudioSession::jobSeconds() const {
    if (!m_job) return 0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_job->started).count();
}

void StudioSession::handleOutcome(gdds::OperationOutcome const& out, gdds::GenerationRequest const& request, bool preview) {
    if (preview) {
        status(out.message, !out.ok);
        if (out.ok)
            Notification::create("Preview ready: use Apply or Cancel in the editor toolbar", NotificationIcon::Info)->show();
        return;
    }
    if (!out.needsConfirmation.empty()) {
        // "Ask" policy: let the user decide, then run again with the answer.
        auto req = request;
        auto count = out.needsConfirmation.size();
        geode::createQuickPopup("Hand-modified objects",
                                fmt::format("{} generated objects in this area were <cy>modified by hand</c>.\nKeep them or "
                                            "replace them with new decoration?",
                                            count),
                                "Keep", "Replace", [req](auto, bool replace) mutable {
                                    if (auto s = StudioSession::current()) {
                                        req.modifiedPolicy = replace ? gdds::ModifiedPolicy::Replace : gdds::ModifiedPolicy::Keep;
                                        s->run(req);
                                    }
                                });
        status(out.message);
        return;
    }
    report(out);
    if (out.ok) save();
}

bool StudioSession::run(gdds::GenerationRequest const& request) { return startJob(request, false); }

bool StudioSession::preview(gdds::GenerationRequest const& request) { return startJob(request, true); }

void StudioSession::beginConfigEdit() {
    if (!m_configBefore) m_configBefore = project().configJson();
}

void StudioSession::commitConfigEdit(std::string const& description) {
    if (!m_configBefore) return;
    m_studio->pushConfigChange(description, *m_configBefore);
    m_configBefore.reset();
    save();
}

std::vector<int> StudioSession::favorites() const {
    return Mod::get()->getSavedValue<std::vector<int>>("favorites", {});
}

void StudioSession::toggleFavorite(int id) {
    auto favs = favorites();
    auto it = std::find(favs.begin(), favs.end(), id);
    if (it == favs.end()) favs.push_back(id);
    else favs.erase(it);
    Mod::get()->setSavedValue("favorites", favs);
}

std::vector<std::pair<std::string, std::vector<int>>> StudioSession::collections() const {
    std::vector<std::pair<std::string, std::vector<int>>> out;
    auto v = readJsonFile(files::saveDir() / "collections.json");
    if (!v) return out;
    for (auto& [name, ids] : v->members()) {
        std::vector<int> list;
        for (auto& i : ids.items()) list.push_back(i.asInt());
        out.emplace_back(name, list);
    }
    return out;
}

void StudioSession::saveCollection(std::string const& name, std::vector<int> const& ids) {
    auto path = files::saveDir() / "collections.json";
    auto v = readJsonFile(path).value_or(gdds::json::Value::object());
    gdds::json::Value arr = gdds::json::Value::array();
    for (int id : ids) arr.push(id);
    v[name] = arr;
    (void)files::writeTextSafe(path, v.dump());
}

std::filesystem::path StudioSession::presetsDir() const {
    return files::ensureDir(files::configDir() / "presets");
}

std::vector<std::pair<gdds::Preset, std::filesystem::path>> StudioSession::userPresets() const {
    std::vector<std::pair<gdds::Preset, std::filesystem::path>> out;
    for (auto& f : files::listFiles(presetsDir())) {
        if (f.extension() != ".json") continue;
        auto v = readJsonFile(f);
        if (!v) continue;
        if (auto p = gdds::Preset::fromJson(*v)) out.emplace_back(std::move(*p), f);
    }
    return out;
}
