#include "app.h"
#include "gfx.h"
#include "audiofx.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <ctime>

App* g_app = nullptr;

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------
void Settings::load() {
    std::string s;
    if (!readFile(joinPath(appDataDir(), "settings.json"), s)) return;
    Json j = Json::parse(s);
    autosaveMin = (int)j.num("autosaveMin", autosaveMin);
    undoLevels = (int)j.num("undoLevels", undoLevels);
    showFacts = j.boolean("showFacts", showFacts);
    confirmDelete = j.boolean("confirmDelete", confirmDelete);
    uiScale = (float)j.num("uiScale", uiScale);
    fontSize = (float)j.num("fontSize", fontSize);
    highlightRows = j.boolean("highlightRows", highlightRows);
    previewRes = (int)j.num("previewRes", previewRes);
    loop = j.boolean("loop", loop);
    cacheMB = (int)j.num("cacheMB", cacheMB);
    stillDuration = j.num("stillDuration", stillDuration);
    seqFps = j.num("seqFps", seqFps);
    importSequences = j.boolean("importSequences", importSequences);
    skipFrames = j.boolean("skipFrames", skipFrames);
    snapping = j.boolean("snapping", snapping);
    timeDisplay = (int)j.num("timeDisplay", timeDisplay);
    exportDir = j.str("exportDir", exportDir);
    exportPreset = (int)j.num("exportPreset", exportPreset);
    aiProvider = (int)j.num("aiProvider", aiProvider);
    removeBgKey = j.str("removeBgKey");
    photoroomKey = j.str("photoroomKey");
    customKey = j.str("customKey");
    rembgUrl = j.str("rembgUrl", rembgUrl);
    customUrl = j.str("customUrl");
    lastCompPreset = (int)j.num("lastCompPreset", 0);
    language = (int)j.num("language", -1);
    freezeDuration = clampv(j.num("freezeDuration", 3.0), 0.1, 600.0);
    disabledPlugins.clear();
    if (auto* d = j.get("disabledPlugins")) for (auto& e : d->a) disabledPlugins.push_back(e.s);
    recent.clear();
    if (auto* r = j.get("recent")) for (auto& e : r->a) recent.push_back(e.s);
    aiGpu = j.boolean("aiGpu", aiGpu);
    mediaGrid = j.boolean("mediaGrid", mediaGrid);
    curvePresets.clear();
    if (auto* cp = j.get("curvePresets"))
        for (auto& e : cp->a) {
            CurvePreset p;
            p.name = e.str("name");
            p.type = (int)e.num("type", 1);
            if (auto* b = e.get("bz")) for (size_t i = 0; i < b->a.size() && i < 4; i++) p.bz[i] = (float)b->a[i].n;
            if (!p.name.empty()) curvePresets.push_back(p);
        }
    if (j.num("version", 1) < 3) { fontSize = 16.0f; uiScale = 1.0f; }  // older builds defaulted to a tiny 13px UI
    if (j.num("version", 1) < 4) importSequences = false;  // was on by default and turned phone photos into "videos"
    if (j.num("version", 1) < 5) aiProvider = 0;  // the built-in network became provider 0 (free, offline)
    aiProvider = clampv(aiProvider, 0, kAiProviderCount - 1);
    uiScale = clampv(uiScale, 0.75f, 2.0f);
    fontSize = clampv(fontSize, 11.f, 22.f);
}
void Settings::save() const {
    Json j = Json::object();
    j["version"] = 5;
    j["autosaveMin"] = autosaveMin;
    j["undoLevels"] = undoLevels;
    j["showFacts"] = showFacts;
    j["confirmDelete"] = confirmDelete;
    j["uiScale"] = uiScale;
    j["fontSize"] = fontSize;
    j["highlightRows"] = highlightRows;
    j["previewRes"] = previewRes;
    j["loop"] = loop;
    j["cacheMB"] = cacheMB;
    j["stillDuration"] = stillDuration;
    j["seqFps"] = seqFps;
    j["importSequences"] = importSequences;
    j["skipFrames"] = skipFrames;
    j["snapping"] = snapping;
    j["timeDisplay"] = timeDisplay;
    j["exportDir"] = exportDir;
    j["exportPreset"] = exportPreset;
    j["aiProvider"] = aiProvider;
    j["removeBgKey"] = removeBgKey;
    j["photoroomKey"] = photoroomKey;
    j["customKey"] = customKey;
    j["rembgUrl"] = rembgUrl;
    j["customUrl"] = customUrl;
    j["lastCompPreset"] = lastCompPreset;
    j["language"] = language;
    j["freezeDuration"] = freezeDuration;
    Json d = Json::array();
    for (auto& s : disabledPlugins) d.push(Json(s));
    j["disabledPlugins"] = d;
    Json r = Json::array();
    for (auto& s : recent) r.push(Json(s));
    j["recent"] = r;
    j["aiGpu"] = aiGpu;
    j["mediaGrid"] = mediaGrid;
    Json cp = Json::array();
    for (auto& p : curvePresets) {
        Json e = Json::object();
        e["name"] = p.name;
        e["type"] = p.type;
        Json b = Json::array();
        for (int i = 0; i < 4; i++) b.push(Json(p.bz[i]));
        e["bz"] = b;
        cp.push(e);
    }
    j["curvePresets"] = cp;
    writeFile(joinPath(appDataDir(), "settings.json"), j.dump());
}
void Settings::addRecent(const std::string& p) {
    recent.erase(std::remove_if(recent.begin(), recent.end(), [&](const std::string& s) { return toLower(s) == toLower(p); }), recent.end());
    recent.insert(recent.begin(), p);
    if (recent.size() > 16) recent.resize(16);
    save();
}

// ---------------------------------------------------------------------------
Composition* App::comp() { return project ? project->findComp(activeComp) : nullptr; }
Layer* App::selectedLayer() {
    Composition* c = comp();
    if (!c) return nullptr;
    for (auto& l : c->layers) if (selLayers.count(l->id)) return l.get();
    return nullptr;
}
std::vector<Layer*> App::selectedLayers() {
    std::vector<Layer*> r;
    Composition* c = comp();
    if (!c) return r;
    for (auto& l : c->layers) if (selLayers.count(l->id)) r.push_back(l.get());
    return r;
}
void App::notify(const std::string& s, bool error) {
    double now = nowSec();
    // the same message again just refreshes the one on screen
    for (auto& t : toasts)
        if (t.text == s && t.error == error) { t.until = now + (error ? 7.0 : 4.0); return; }
    toasts.push_back({s, now + (error ? 7.0 : 4.0), error, now, newId()});
    logf("%s%s", error ? "ERROR: " : "", s.c_str());
}

// ---------------------------------------------------------------------------
// project lifecycle
// ---------------------------------------------------------------------------
void App::newProject() {
    project = std::make_unique<Project>();
    project->createdAt = (double)::time(nullptr);
    activeComp = 0;
    openComps.clear();
    selLayers.clear();
    selItems.clear();
    renderer.clearCaches();
    resetUndo();
    dirty = false;
    screen = Screen::Editor;
}

void App::attachMedia(Project& p, Project* old) {
    for (auto& f : p.footage) {
        Footage* of = old ? old->findFootage(f->id) : nullptr;
        if (of && of->path == f->path && of->kind == f->kind) {
            f->media = of->media;
            f->audio = of->audio;
            f->thumb = of->thumb;
            f->hasVideo = of->hasVideo;
            f->hasAudio = of->hasAudio;
            f->codec = of->codec;
            f->audioCodec = of->audioCodec;
            f->bytes = of->bytes;
            f->missing = of->missing;
            continue;
        }
        std::string probePath = f->kind == FootageKind::Sequence ? sequenceFrameName(*f, 0) : f->path;
        if (!fileExists(probePath)) { f->missing = true; continue; }
        MediaInfo mi;
        if (f->kind == FootageKind::Sequence) {
            f->hasVideo = true;
        } else if (probeMedia(f->path, mi)) {
            f->hasVideo = mi.hasVideo;
            f->hasAudio = mi.hasAudio;
            f->codec = mi.codec;
            f->audioCodec = mi.audioCodec;
            f->sampleRate = mi.sampleRate;
            f->channels = mi.channels;
        }
        f->bytes = fileSize(probePath);
        if (f->hasVideo) f->media = MediaSource::open(*f);
        if (f->hasAudio) f->audio = AudioData::decodeAsync(f->path);
        if (f->media) {
            int64_t ti = f->media->still ? 0 : std::min<int64_t>(f->media->frameCount / 2, (int64_t)f->fps);
            if (auto fr = f->media->decodeSync(ti, 256)) f->thumb = gfx::createTexture(fr->w, fr->h, DXGI_FORMAT_R8G8B8A8_UNORM, false, fr->rgba.data(), fr->w * 4);
        }
    }
}

bool App::openProject(const std::string& path) {
    std::string s;
    if (!readFile(path, s)) { notify("Cannot read " + path, true); return false; }
    bool ok = false;
    Json j = Json::parse(s, &ok);
    if (!ok) { notify("Project file is damaged: " + fileName(path), true); return false; }
    auto p = std::make_unique<Project>();
    std::string err;
    if (!p->load(j, err)) { notify(err, true); return false; }
    p->path = path;
    for (auto& c : p->comps)
        for (auto& l : c->layers)
            for (auto& e : l->effects) effects.sync(e);
    attachMedia(*p, nullptr);
    project = std::move(p);
    renderer.clearCaches();
    openComps.clear();
    selLayers.clear();
    selItems.clear();
    activeComp = 0;
    if (!project->comps.empty()) openComp(project->comps.front()->id);
    resetUndo();
    dirty = false;
    settings.addRecent(path);
    screen = Screen::Editor;
    int missing = 0;
    for (auto& f : project->footage) if (f->missing) missing++;
    if (missing) notify(strf("%d footage file(s) are missing", missing), true);
    else notify("Opened " + fileName(path));
    return true;
}

bool App::saveProject(bool saveAs) {
    if (!project) return false;
    std::string path = project->path;
    if (saveAs || path.empty()) {
        path = dlgSaveFile(hwnd, L"GeSe Project (*.gese)\0*.gese\0", "gese", project->path.empty() ? "Untitled Project" : fileStem(project->path),
                           "Save Project As");
        if (path.empty()) return false;
    }
    std::string data = project->save().dump();
    if (!writeFile(path, data)) { notify("Could not save " + path, true); return false; }
    project->path = path;
    dirty = false;
    settings.addRecent(path);
    notify("Saved " + fileName(path));
    return true;
}

void App::closeProject() {
    if (playing) togglePlay();
    project.reset();
    openComps.clear();
    selLayers.clear();
    activeComp = 0;
    renderer.clearCaches();
}

void App::requestAction(int action) {
    if (project && dirty) {
        unsavedAction = action;
        dlgUnsaved = true;
        return;
    }
    doAction(action);
}

void App::doAction(int action) {
    switch (action) {
    case 1: newProject(); break;
    case 2: {
        std::string p = pendingOpenPath;
        pendingOpenPath.clear();
        if (p.empty()) p = dlgOpenFile(hwnd, L"GeSe Project (*.gese)\0*.gese\0All files\0*.*\0", "Open Project");
        if (!p.empty()) openProject(p);
        break;
    }
    case 3: quitConfirmed = true; break;
    case 4: closeProject(); screen = Screen::Home; break;
    case 5: newProject(); dlgNewComp = true; break;
    }
}

// ---------------------------------------------------------------------------
// undo
// ---------------------------------------------------------------------------
void App::resetUndo() {
    undoStack.clear();
    redoStack.clear();
    lastSnapshot = project ? project->save().dump(-1) : "";
    changed = false;
}
void App::commitNow() {
    if (!project) return;
    changed = false;
    std::string s = project->save().dump(-1);
    if (s == lastSnapshot) return;
    undoStack.push_back(lastSnapshot);
    while ((int)undoStack.size() > std::max(10, settings.undoLevels)) undoStack.pop_front();
    redoStack.clear();
    lastSnapshot = s;
    dirty = true;
    buildAudio();
}
void App::commitIfIdle() {
    if (!changed) return;
    if (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1) || ImGui::IsAnyItemActive()) return;
    commitNow();
}
void App::restoreSnapshot(const std::string& s) {
    bool ok = false;
    Json j = Json::parse(s, &ok);
    if (!ok) return;
    auto p = std::make_unique<Project>();
    std::string err;
    if (!p->load(j, err)) return;
    p->path = project->path;
    // keep the playheads where they are
    for (auto& c : p->comps)
        if (Composition* oc = project->findComp(c->id)) c->time = oc->time;
    attachMedia(*p, project.get());
    project = std::move(p);
    std::set<uint64_t> keep;
    if (Composition* c = comp())
        for (auto& l : c->layers) if (selLayers.count(l->id)) keep.insert(l->id);
    selLayers = keep;
    openComps.erase(std::remove_if(openComps.begin(), openComps.end(), [&](uint64_t id) { return !project->findComp(id); }), openComps.end());
    if (!project->findComp(activeComp)) activeComp = openComps.empty() ? 0 : openComps.front();
    needRender = true;
    dirty = true;
    buildAudio();
}
void App::undo() {
    if (!project) return;
    commitNow();  // the latest change may not be in the history yet (e.g. the very first one)
    if (undoStack.empty()) return;
    redoStack.push_back(lastSnapshot);
    lastSnapshot = undoStack.back();
    undoStack.pop_back();
    restoreSnapshot(lastSnapshot);
}
void App::redo() {
    if (!project || redoStack.empty()) return;
    undoStack.push_back(lastSnapshot);
    lastSnapshot = redoStack.back();
    redoStack.pop_back();
    restoreSnapshot(lastSnapshot);
}

// ---------------------------------------------------------------------------
// compositions & import
// ---------------------------------------------------------------------------
Composition* App::newComp(const std::string& name, int w, int h, double fps, double dur) {
    auto c = std::make_shared<Composition>();
    c->id = newId();
    c->name = name.empty() ? project->uniqueCompName("Comp") : name;
    c->w = w;
    c->h = h;
    c->fps = fps;
    c->duration = dur;
    c->workStart = 0;
    c->workEnd = dur;
    project->comps.push_back(c);
    openComp(c->id);
    markChanged();
    return c.get();
}

void App::openComp(uint64_t id) {
    if (!project || !project->findComp(id)) return;
    if (std::find(openComps.begin(), openComps.end(), id) == openComps.end()) openComps.push_back(id);
    if (activeComp != id) {
        if (playing) togglePlay();
        activeComp = id;
        selLayers.clear();
        viewZoom = 0;
        viewPan = ImVec2(0, 0);
        buildAudio();
    }
    needRender = true;
}

void App::closeComp(uint64_t id) {
    openComps.erase(std::remove(openComps.begin(), openComps.end(), id), openComps.end());
    if (activeComp == id) {
        if (playing) togglePlay();
        activeComp = openComps.empty() ? 0 : openComps.back();
        selLayers.clear();
        buildAudio();
    }
}

FootagePtr App::importFile(const std::string& path) {
    std::string ext = fileExt(path);
    if (ext == ".gese") { pendingOpenPath = path; requestAction(2); return nullptr; }
    if (ext == ".gsfx") {
        std::string dst = joinPath(effects.userPluginDir(), fileName(path));
        CopyFileW(widen(path).c_str(), widen(dst).c_str(), FALSE);
        effects.reload();
        notify("Plugin installed: " + fileName(path));
        return nullptr;
    }
    auto f = std::make_shared<Footage>();
    f->id = newId();
    f->path = path;
    f->name = fileName(path);
    f->bytes = fileSize(path);
    bool seq = false;
    if (isImageExt(ext) && ext != ".gif" && settings.importSequences) {
        f->fps = settings.seqFps;
        if (detectSequence(path, *f)) {
            std::vector<uint8_t> px;
            int w, h;
            if (gfx::loadImageFile(sequenceFrameName(*f, 0), px, w, h)) {
                f->w = w;
                f->h = h;
                f->hasVideo = true;
                f->codec = toLower(ext.substr(1)) + " sequence";
                seq = true;
            }
        }
    }
    if (!seq) {
        MediaInfo mi;
        if (!probeMedia(path, mi)) { notify("Unsupported or damaged file: " + fileName(path), true); return nullptr; }
        f->hasVideo = mi.hasVideo;
        f->hasAudio = mi.hasAudio;
        f->w = mi.w;
        f->h = mi.h;
        f->fps = mi.fps;
        f->duration = mi.duration;
        f->codec = mi.codec;
        f->audioCodec = mi.audioCodec;
        f->sampleRate = mi.sampleRate;
        f->channels = mi.channels;
        f->kind = mi.still ? FootageKind::Image : (mi.hasVideo ? FootageKind::Video : FootageKind::Audio);
        if (mi.still) f->duration = settings.stillDuration;
    }
    if (f->hasVideo) f->media = MediaSource::open(*f);
    if (f->hasAudio) f->audio = AudioData::decodeAsync(path);
    if (f->media) {
        int64_t ti = f->media->still ? 0 : std::min<int64_t>(f->media->frameCount / 2, (int64_t)f->fps);
        if (auto fr = f->media->decodeSync(ti, 256)) f->thumb = gfx::createTexture(fr->w, fr->h, DXGI_FORMAT_R8G8B8A8_UNORM, false, fr->rgba.data(), fr->w * 4);
    }
    project->footage.push_back(f);
    markChanged();
    return f;
}

static bool isDirectory(const std::string& p) {
    DWORD a = GetFileAttributesW(widen(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// a folder becomes a project folder (subfolders too) with every media file inside
std::vector<FootagePtr> App::importFolder(const std::string& dir, uint64_t parent, const std::string& name) {
    std::vector<FootagePtr> r;
    if (!project || !isDirectory(dir)) return r;
    Folder fo;
    fo.id = newId();
    fo.name = name.empty() ? fileName(dir) : name;
    fo.parent = parent;
    project->folders.push_back(fo);
    std::vector<std::string> files, dirs;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(widen(joinPath(dir, "*")).c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = narrow(fd.cFileName);
            if (n == "." || n == ".." || (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) dirs.push_back(joinPath(dir, n));
            else if (isMediaExt(toLower(fileExt(n)))) files.push_back(joinPath(dir, n));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(files.begin(), files.end());
    std::sort(dirs.begin(), dirs.end());
    for (auto& f : files)
        if (auto fp = importFile(f)) { fp->folder = fo.id; r.push_back(fp); }
    for (auto& d : dirs) {
        auto sub = importFolder(d, fo.id);
        r.insert(r.end(), sub.begin(), sub.end());
    }
    if (r.empty() && parent == 0) project->folders.erase(std::remove_if(project->folders.begin(), project->folders.end(), [&](const Folder& x) { return x.id == fo.id; }), project->folders.end());
    for (auto& f : project->folders) if (f.id == fo.id) f.open = true;
    markChanged();
    return r;
}

// .zip: unpacked next to the project (Windows' own tar.exe reads zip), then imported as a folder
std::vector<FootagePtr> App::importZip(const std::string& zip) {
    std::string base = project && !project->path.empty() ? joinPath(parentDir(project->path), "Imported") : joinPath(documentsDir(), "GeSe Imported");
    std::string stem = fileStem(zip);
    std::string dest = joinPath(base, stem);
    for (int i = 2; isDirectory(dest); i++) dest = joinPath(base, strf("%s_%d", stem.c_str(), i));
    makeDirs(dest);
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(sys) + L"\\tar.exe\" -xf \"" + widen(zip) + L"\" -C \"" + widen(dest) + L"\"";
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    DWORD code = 1;
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10 * 60 * 1000);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (code != 0) { notify("Could not unpack " + fileName(zip), true); return {}; }
    auto r = importFolder(dest, 0, stem);
    if (r.empty()) notify("No media files in " + fileName(zip), true);
    return r;
}

std::vector<FootagePtr> App::importFiles(const std::vector<std::string>& paths) {
    std::vector<FootagePtr> r;
    if (!project) return r;
    std::set<std::string> seqSeen;
    for (auto& p : paths) {
        if (isDirectory(p) || toLower(fileExt(p)) == ".zip") {
            auto sub = isDirectory(p) ? importFolder(p, 0) : importZip(p);
            r.insert(r.end(), sub.begin(), sub.end());
            continue;
        }
        // when several frames of one sequence are dropped, import the sequence once
        Footage probe;
        probe.fps = settings.seqFps;
        if (settings.importSequences && isImageExt(fileExt(p)) && detectSequence(p, probe)) {
            std::string key = toLower(probe.seqDir + "|" + probe.seqPrefix + "|" + probe.seqSuffix);
            if (seqSeen.count(key)) continue;
            seqSeen.insert(key);
        }
        if (auto f = importFile(p)) r.push_back(f);
    }
    if (!r.empty()) {
        selItems.clear();
        for (auto& f : r) selItems.insert(f->id);
        notify(strf("Imported %d item(s)", (int)r.size()));
    }
    return r;
}

void App::importDialog() {
    if (!project) return;
    auto files = dlgOpenFiles(hwnd,
                              L"All supported\0*.mp4;*.mov;*.mkv;*.avi;*.webm;*.m4v;*.wmv;*.mts;*.m2ts;*.mxf;*.mpg;*.gif;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp;*.heic;*.mp3;*.wav;*.aac;*.m4a;*.flac;*.ogg;*.opus;*.gsfx\0"
                              L"Video\0*.mp4;*.mov;*.mkv;*.avi;*.webm;*.m4v;*.wmv;*.mts;*.m2ts;*.mxf;*.mpg;*.gif\0"
                              L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp;*.heic\0"
                              L"Audio\0*.mp3;*.wav;*.aac;*.m4a;*.flac;*.ogg;*.opus\0ZIP archive (unpacked into the Media panel)\0*.zip\0All files\0*.*\0",
                              "Import File");
    importFiles(files);
}

void App::importFolderDialog() {
    if (!project) return;
    std::string d = dlgPickFolder(hwnd, "Import Folder");
    if (d.empty()) return;
    auto r = importFolder(d, 0);
    pProject = true;
    if (r.empty()) notify("No media files in this folder", true);
    else notify(strf("Imported %d item(s) from %s", (int)r.size(), fileName(d).c_str()));
}

void App::importZipDialog() {
    if (!project) return;
    std::string z = dlgOpenFile(hwnd, L"ZIP archive (*.zip)\0*.zip\0", "Import ZIP Archive");
    if (z.empty()) return;
    auto r = importZip(z);
    pProject = true;
    if (!r.empty()) notify(strf("Imported %d item(s) from %s", (int)r.size(), fileName(z).c_str()));
}

static void initTransform(Layer& l, ImVec2 size, const Composition& c) {
    l.anchor = Prop(2, size.x * 0.5f, size.y * 0.5f);
    l.position = Prop(2, c.w * 0.5f, c.h * 0.5f);
    l.scale = Prop(2, 100, 100);
    l.rotation = Prop(1, 0);
    l.opacity = Prop(1, 100);
}

Layer* App::addFootageLayer(Composition& c, Footage& f, double at, int index) {
    auto l = std::make_shared<Layer>();
    l->id = newId();
    l->type = LayerType::Footage;
    l->source = f.id;
    l->name = f.name;
    l->startTime = at;
    l->inPoint = at;
    double dur = f.kind == FootageKind::Image ? std::max(0.1, c.duration - at) : std::max(1.0 / c.fps, f.duration);
    l->outPoint = std::min(c.duration, at + dur);
    if (l->outPoint <= l->inPoint) l->outPoint = at + dur;
    initTransform(*l, ImVec2((float)f.w, (float)f.h), c);
    index = clampv(index, 0, (int)c.layers.size());
    c.layers.insert(c.layers.begin() + index, l);
    selLayers = {l->id};
    markChanged();
    return l.get();
}

Layer* App::addCompLayer(Composition& c, Composition& src, double at, int index) {
    if (project->compUsesComp(src.id, c.id)) { notify("Cannot nest a composition inside itself", true); return nullptr; }
    auto l = std::make_shared<Layer>();
    l->id = newId();
    l->type = LayerType::Precomp;
    l->source = src.id;
    l->name = src.name;
    l->startTime = at;
    l->inPoint = at;
    l->outPoint = at + src.duration;
    initTransform(*l, ImVec2((float)src.w, (float)src.h), c);
    index = clampv(index, 0, (int)c.layers.size());
    c.layers.insert(c.layers.begin() + index, l);
    selLayers = {l->id};
    markChanged();
    return l.get();
}

Layer* App::addSolid(Composition& c, const std::string& name, int w, int h, const float col[4], LayerType type) {
    auto l = std::make_shared<Layer>();
    l->id = newId();
    l->type = type;
    l->name = name;
    l->solidW = w;
    l->solidH = h;
    memcpy(l->solidColor, col, sizeof(l->solidColor));
    l->startTime = 0;
    l->inPoint = c.time;
    l->outPoint = c.duration;
    if (type == LayerType::Adjustment) { l->inPoint = 0; }
    initTransform(*l, type == LayerType::Solid ? ImVec2((float)w, (float)h) : project->layerSize(*l, c), c);
    c.layers.insert(c.layers.begin(), l);
    selLayers = {l->id};
    markChanged();
    return l.get();
}

Layer* App::addText(Composition& c, const std::string& text, ImVec2 pos) {
    auto l = std::make_shared<Layer>();
    l->id = newId();
    l->type = LayerType::Text;
    l->name = text;
    l->text.text = text;
    l->text.size = Prop(1, std::max(24.f, c.h / 9.f));
    l->inPoint = c.time;
    l->outPoint = c.duration;
    initTransform(*l, ImVec2((float)c.w, (float)c.h), c);
    if (pos.x >= 0) l->position = Prop(2, pos.x, pos.y);
    c.layers.insert(c.layers.begin(), l);
    selLayers = {l->id};
    pCharacter = true;
    markChanged();
    return l.get();
}

Layer* App::addNull(Composition& c) {
    auto l = std::make_shared<Layer>();
    l->id = newId();
    l->type = LayerType::Null;
    l->name = "Null " + std::to_string(c.layers.size() + 1);
    l->inPoint = 0;
    l->outPoint = c.duration;
    initTransform(*l, ImVec2(100, 100), c);
    c.layers.insert(c.layers.begin(), l);
    selLayers = {l->id};
    markChanged();
    return l.get();
}

Layer* App::addEffectLayer(Composition& c, const EffectDef* d, double at, int index) {
    auto l = std::make_shared<Layer>();
    l->id = newId();
    l->type = LayerType::Adjustment;
    l->name = d ? d->name : "Effect Layer";
    l->startTime = 0;
    l->inPoint = clampv(at, 0.0, c.duration);
    double len = std::min(5.0, c.duration - l->inPoint);
    if (len < 0.5) { l->inPoint = std::max(0.0, c.duration - 5.0); len = c.duration - l->inPoint; }
    l->outPoint = l->inPoint + len;
    initTransform(*l, ImVec2((float)c.w, (float)c.h), c);
    if (d) {
        l->effects.push_back(effects.instantiate(*d));
        selEffect = l->effects.back().id;
    }
    index = clampv(index, 0, (int)c.layers.size());
    c.layers.insert(c.layers.begin() + index, l);
    selLayers = {l->id};
    markChanged();
    return l.get();
}

void App::applyEffect(const EffectDef* d, Layer* target) {
    Composition* c = comp();
    if (!c || !d) return;
    lastEffectId = d->id;
    std::vector<Layer*> ls;
    if (target) ls.push_back(target);
    else ls = selectedLayers();
    if (ls.empty()) {
        addEffectLayer(*c, d, c->time, 0);
        notify("Effect layer created: " + d->name);
        return;
    }
    for (Layer* l : ls) {
        l->effects.push_back(effects.instantiate(*d));
        selEffect = l->effects.back().id;
        l->fxOpen = true;
    }
    pFxControls = true;
    focusPanel = "###Effect Controls";
    markChanged();
}

std::string App::presetDir() const { return joinPath(appDataDir(), "Presets"); }
std::vector<std::string> App::presetFiles() {
    std::vector<std::string> r = listFiles(resPath("Presets"), ".gspreset", true);
    auto u = listFiles(presetDir(), ".gspreset", true);
    r.insert(r.end(), u.begin(), u.end());
    return r;
}

void App::applyPreset(const std::string& path, Layer* target) {
    Composition* c = comp();
    if (!c) return;
    std::string s;
    if (!readFile(path, s)) return;
    Json j = Json::parse(s);
    std::vector<Layer*> ls;
    if (target) ls.push_back(target);
    else ls = selectedLayers();
    if (ls.empty()) {
        Layer* el = addEffectLayer(*c, nullptr, c->time, 0);
        el->name = j.str("name", fileStem(path));
        ls.push_back(el);
    }
    int added = 0;
    for (Layer* l : ls) {
        double lt = l->localTime(c->time);
        if (auto* fx = j.get("effects")) {
            for (auto& ej : fx->a) {
                EffectInst e;
                e.load(ej);
                if (!effects.find(e.defId)) continue;
                e.id = newId();
                effects.sync(e);
                // preset keyframes are relative to the preset start: shift them to the playhead
                auto shift = [&](Prop& p) { for (auto& k : p.keys) k.t += lt; };
                shift(e.intensity);
                shift(e.speed);
                for (auto& p : e.params) shift(p);
                l->effects.push_back(e);
                added++;
            }
        }
        l->fxOpen = true;
    }
    lastEffectId.clear();
    markChanged();
    notify(strf("Preset applied: %s (%d effects)", j.str("name", fileStem(path)).c_str(), added));
}

bool App::savePreset(const std::string& name, Layer& l) {
    Json j = Json::object();
    j["name"] = name;
    j["app"] = GESE_FULLNAME;
    Json fx = Json::array();
    double first = 1e9;
    for (auto& e : l.effects) {
        for (auto* p : {&e.intensity, &e.speed}) for (auto& k : p->keys) first = std::min(first, k.t);
        for (auto& p : e.params) for (auto& k : p.keys) first = std::min(first, k.t);
    }
    if (first > 1e8) first = 0;
    for (auto& e : l.effects) {
        EffectInst c = e;
        auto shift = [&](Prop& p) { for (auto& k : p.keys) k.t -= first; };
        shift(c.intensity);
        shift(c.speed);
        for (auto& p : c.params) shift(p);
        fx.push(c.save());
    }
    j["effects"] = fx;
    std::string fn = name;
    for (auto& ch : fn) if (strchr("\\/:*?\"<>|", ch)) ch = '_';
    bool ok = writeFile(joinPath(presetDir(), fn + ".gspreset"), j.dump());
    notify(ok ? "Animation preset saved: " + name : "Could not save preset", !ok);
    return ok;
}

// ---------------------------------------------------------------------------
// layer editing
// ---------------------------------------------------------------------------
void App::selectLayer(uint64_t id, bool add, bool range) {
    Composition* c = comp();
    if (!c) return;
    if (range && !selLayers.empty()) {
        int a = c->indexOf(*selLayers.begin()), b = c->indexOf(id);
        if (a >= 0 && b >= 0) {
            if (a > b) std::swap(a, b);
            for (int i = a; i <= b; i++) selLayers.insert(c->layers[i]->id);
            return;
        }
    }
    if (add) {
        if (selLayers.count(id)) selLayers.erase(id);
        else selLayers.insert(id);
    } else {
        selLayers = {id};
    }
}

void App::deleteSelectedLayers(bool ripple) {
    Composition* c = comp();
    if (!c || selLayers.empty()) return;
    for (auto it = c->layers.begin(); it != c->layers.end();) {
        Layer& l = **it;
        if (selLayers.count(l.id) && !l.locked) {
            double gapStart = l.inPoint, gap = l.outPoint - l.inPoint;
            uint64_t id = l.id;
            it = c->layers.erase(it);
            for (auto& o : c->layers) if (o->parent == id) o->parent = 0;
            if (ripple) {
                for (auto& o : c->layers)
                    if (o->inPoint >= gapStart - 1e-9) {
                        o->inPoint -= gap;
                        o->outPoint -= gap;
                        o->startTime -= gap;
                    }
            }
        } else ++it;
    }
    selLayers.clear();
    markChanged();
}

void App::duplicateSelectedLayers() {
    Composition* c = comp();
    if (!c) return;
    std::set<uint64_t> ns;
    for (int i = (int)c->layers.size() - 1; i >= 0; i--) {
        if (!selLayers.count(c->layers[i]->id)) continue;
        auto d = cloneLayer(*c->layers[i], true);
        c->layers.insert(c->layers.begin() + i, d);
        ns.insert(d->id);
    }
    if (!ns.empty()) { selLayers = ns; markChanged(); }
}

void App::splitSelectedLayers(double t) {
    Composition* c = comp();
    if (!c) return;
    t = frameSnap(t, c->fps);
    std::set<uint64_t> ns;
    std::vector<Layer*> targets = selectedLayers();
    if (targets.empty())  // blade everything under the playhead
        for (auto& l : c->layers) if (!l->locked) targets.push_back(l.get());
    for (Layer* l : targets) {
        if (l->locked || t <= l->inPoint + 1e-6 || t >= l->outPoint - 1e-6) continue;
        int idx = c->indexOf(l->id);
        auto d = cloneLayer(*l, true);
        l->outPoint = t;
        d->inPoint = t;
        c->layers.insert(c->layers.begin() + idx, d);
        ns.insert(d->id);
    }
    if (!ns.empty()) { selLayers = ns; markChanged(); }
}

void App::freezeFrame(double t) {
    Composition* c = comp();
    if (!c) return;
    const EffectDef* fz = effects.find("gese.time.freeze");
    if (!fz) { notify("Freeze Frame effect is missing (resources\\Effects\\Time.gsfx)", true); return; }
    t = frameSnap(t, c->fps);
    std::vector<Layer*> targets;
    for (Layer* l : selectedLayers())
        if (l->type == LayerType::Footage || l->type == LayerType::Precomp) targets.push_back(l);
    if (targets.empty())  // the clip under the playhead
        for (auto& l : c->layers)
            if (!l->locked && l->visible && l->activeAt(t) && (l->type == LayerType::Footage || l->type == LayerType::Precomp)) { targets.push_back(l.get()); break; }
    if (targets.empty()) { notify("Put the playhead over a video clip to freeze a frame", true); return; }
    double dur = std::max(1.0 / c->fps, settings.freezeDuration);
    // everything that starts after the freeze point moves right by the freeze length
    std::set<uint64_t> targetIds;
    for (Layer* l : targets) targetIds.insert(l->id);
    for (auto& o : c->layers)
        if (!targetIds.count(o->id) && !o->locked && o->inPoint >= t - 1e-6) {
            o->startTime += dur;
            o->inPoint += dur;
            o->outPoint += dur;
        }
    std::set<uint64_t> ns;
    for (Layer* l : targets) {
        if (l->locked || !l->activeAt(t)) continue;
        int idx = c->indexOf(l->id);
        double lt = l->localTime(t);
        LayerPtr frz = cloneLayer(*l, true);
        frz->inPoint = t;
        frz->outPoint = t + dur;
        frz->name = l->name + " (Freeze)";
        frz->audioOn = false;
        EffectInst e = effects.instantiate(*fz);
        if (!e.params.empty()) e.params[0] = Prop(1, (float)lt);
        frz->effects.insert(frz->effects.begin(), e);
        // the rest of the clip moves to after the frozen frame
        Layer* rest = l;
        if (t > l->inPoint + 1e-6) {
            LayerPtr tail = cloneLayer(*l, true);
            l->outPoint = t;
            tail->inPoint = t;
            c->layers.insert(c->layers.begin() + idx, tail);
            rest = tail.get();
        }
        rest->startTime += dur;
        rest->inPoint += dur;
        rest->outPoint += dur;
        c->layers.insert(c->layers.begin() + c->indexOf(rest->id), frz);
        ns.insert(frz->id);
    }
    if (ns.empty()) return;
    bool workAtEnd = c->workEnd >= c->duration - 1e-6;
    for (auto& l : c->layers) c->duration = std::max(c->duration, l->outPoint);
    if (workAtEnd) c->workEnd = c->duration;
    selLayers = ns;
    markChanged();
    notify(strf("Freeze frame: %.1f s", dur));
}

// Puts the layers of a compound clip back on this timeline, at the same time and place.
void App::unpackPrecomp(Layer& pl) {
    Composition* c = comp();
    Composition* inner = pl.type == LayerType::Precomp ? project->findComp(pl.source) : nullptr;
    if (!c || !inner) return;
    double sp = std::max(0.01, pl.speed);
    int at = c->indexOf(pl.id);
    if (at < 0) return;
    // the clip's own transform is kept by a null the unpacked layers are parented to (only when it isn't the identity)
    ImVec2 ls = project->layerSize(pl, *c);
    bool moved = pl.position.animated || pl.scale.animated || pl.rotation.animated || pl.anchor.animated ||
                 fabsf(pl.position.v[0] - c->w * 0.5f) > 0.01f || fabsf(pl.position.v[1] - c->h * 0.5f) > 0.01f ||
                 fabsf(pl.anchor.v[0] - ls.x * 0.5f) > 0.01f || fabsf(pl.anchor.v[1] - ls.y * 0.5f) > 0.01f ||
                 fabsf(pl.scale.v[0] - 100) > 0.01f || fabsf(pl.scale.v[1] - 100) > 0.01f || fabsf(pl.rotation.v[0]) > 0.01f || pl.parent != 0;
    LayerPtr holder;
    if (moved) {
        holder = std::make_shared<Layer>();
        holder->id = newId();
        holder->type = LayerType::Null;
        holder->name = pl.name + " (transform)";
        holder->startTime = pl.startTime;
        holder->inPoint = pl.inPoint;
        holder->outPoint = pl.outPoint;
        holder->anchor = pl.anchor;
        holder->position = pl.position;
        holder->scale = pl.scale;
        holder->rotation = pl.rotation;
        holder->parent = pl.parent;
        holder->visible = false;
    }
    std::vector<LayerPtr> out;
    std::map<uint64_t, uint64_t> ids;
    for (auto& src : inner->layers) {
        LayerPtr nl = cloneLayer(*src, true);
        ids[src->id] = nl->id;
        // inner time -> this timeline: t = start + innerTime / speed
        nl->startTime = pl.startTime + src->startTime / sp;
        nl->inPoint = std::max(pl.startTime + src->inPoint / sp, pl.inPoint);
        nl->outPoint = std::min(pl.startTime + src->outPoint / sp, pl.outPoint);
        if (nl->outPoint <= nl->inPoint + 1e-6) continue;
        nl->speed = src->speed * sp;
        for (auto& pr : layerProps(*nl))
            for (auto& k : pr.second->keys) k.t /= sp;
        // the clip's opacity, effects and sound settings carry over to every layer
        if (pl.opacity.animated || pl.opacity.v[0] < 99.99f) {
            float k = pl.opacity.f(0) / 100.f;
            nl->opacity.v[0] *= k;
            for (auto& kf : nl->opacity.keys) kf.v[0] *= k;
        }
        for (auto& e : pl.effects) {
            EffectInst ce = e;
            ce.id = newId();
            nl->effects.push_back(ce);
        }
        if (!pl.audioOn) nl->audioOn = false;
        if (holder && !src->parent) nl->parent = holder->id;
        out.push_back(nl);
    }
    for (auto& nl : out)
        if (nl->parent && ids.count(nl->parent)) nl->parent = ids[nl->parent];
    if (out.empty()) { notify("The compound clip is empty here", true); return; }
    uint64_t oldId = pl.id;
    c->layers.erase(c->layers.begin() + at);
    for (auto& o : c->layers) if (o->parent == oldId) o->parent = holder ? holder->id : 0;
    c->layers.insert(c->layers.begin() + at, out.begin(), out.end());
    if (holder) c->layers.insert(c->layers.begin() + at + (int)out.size(), holder);
    selLayers.clear();
    for (auto& nl : out) selLayers.insert(nl->id);
    markChanged();
    notify(strf("Compound clip unpacked: %d layer(s)", (int)out.size()));
}

void App::precomposeSelected(const std::string& name) {
    Composition* c = comp();
    if (!c || selLayers.empty()) return;
    auto nc = std::make_shared<Composition>();
    nc->id = newId();
    nc->name = name.empty() ? project->uniqueCompName("Compound Clip") : name;
    nc->w = c->w;
    nc->h = c->h;
    nc->fps = c->fps;
    nc->duration = c->duration;
    nc->workEnd = c->duration;
    memcpy(nc->bg, c->bg, sizeof(nc->bg));
    nc->bg[3] = 0;
    int topIndex = -1;
    double in = 1e9, out = -1e9;
    for (int i = 0; i < (int)c->layers.size(); i++) {
        auto& l = c->layers[i];
        if (!selLayers.count(l->id)) continue;
        if (topIndex < 0) topIndex = i;
        in = std::min(in, l->inPoint);
        out = std::max(out, l->outPoint);
        nc->layers.push_back(l);
    }
    if (nc->layers.empty()) return;
    c->layers.erase(std::remove_if(c->layers.begin(), c->layers.end(), [&](const LayerPtr& l) { return selLayers.count(l->id) > 0; }),
                    c->layers.end());
    project->comps.push_back(nc);
    auto l = std::make_shared<Layer>();
    l->id = newId();
    l->type = LayerType::Precomp;
    l->source = nc->id;
    l->name = nc->name;
    l->startTime = 0;
    l->inPoint = in;
    l->outPoint = out;
    initTransform(*l, ImVec2((float)nc->w, (float)nc->h), *c);
    c->layers.insert(c->layers.begin() + clampv(topIndex, 0, (int)c->layers.size()), l);
    selLayers = {l->id};
    markChanged();
    notify("Compound clip created: " + nc->name);
}

void App::moveSelectedLayers(int dir, bool toEnd) {
    Composition* c = comp();
    if (!c || selLayers.empty()) return;
    auto& L = c->layers;
    if (toEnd) {
        std::vector<LayerPtr> sel, rest;
        for (auto& l : L) (selLayers.count(l->id) ? sel : rest).push_back(l);
        L.clear();
        if (dir < 0) { L.insert(L.end(), sel.begin(), sel.end()); L.insert(L.end(), rest.begin(), rest.end()); }
        else { L.insert(L.end(), rest.begin(), rest.end()); L.insert(L.end(), sel.begin(), sel.end()); }
    } else if (dir < 0) {
        for (size_t i = 1; i < L.size(); i++)
            if (selLayers.count(L[i]->id) && !selLayers.count(L[i - 1]->id)) std::swap(L[i], L[i - 1]);
    } else {
        for (int i = (int)L.size() - 2; i >= 0; i--)
            if (selLayers.count(L[i]->id) && !selLayers.count(L[i + 1]->id)) std::swap(L[i], L[i + 1]);
    }
    markChanged();
}

void App::copyLayers() {
    Composition* c = comp();
    if (!c) return;
    clipLayers = Json::array();
    for (auto& l : c->layers) if (selLayers.count(l->id)) clipLayers.push(l->save());
}
void App::pasteLayers() {
    Composition* c = comp();
    if (!c || !clipLayers.isArr() || clipLayers.a.empty()) return;
    std::set<uint64_t> ns;
    int at = 0;
    for (auto& lj : clipLayers.a) {
        auto l = std::make_shared<Layer>();
        l->load(lj);
        auto d = cloneLayer(*l, true);
        if (d->type == LayerType::Footage && !project->findFootage(d->source)) continue;
        if (d->type == LayerType::Precomp && (!project->findComp(d->source) || project->compUsesComp(d->source, c->id))) continue;
        for (auto& e : d->effects) effects.sync(e);
        c->layers.insert(c->layers.begin() + at++, d);
        ns.insert(d->id);
    }
    if (!ns.empty()) { selLayers = ns; markChanged(); }
}

void App::fitLayer(Layer& l, bool fill) {
    Composition* c = comp();
    if (!c) return;
    ImVec2 s = project->layerSize(l, *c);
    if (s.x <= 0 || s.y <= 0) return;
    float kx = c->w / s.x, ky = c->h / s.y;
    float k = fill ? std::max(kx, ky) : std::min(kx, ky);
    double lt = l.localTime(c->time);
    l.anchor.set2(lt, s.x * 0.5f, s.y * 0.5f);
    l.position.set2(lt, c->w * 0.5f, c->h * 0.5f);
    l.scale.set2(lt, k * 100.f, k * 100.f);
    markChanged();
}

std::vector<std::pair<PropRef, Prop*>> App::layerProps(Layer& l) {
    std::vector<std::pair<PropRef, Prop*>> r;
    Prop* xf[5] = {&l.anchor, &l.position, &l.scale, &l.rotation, &l.opacity};
    for (int i = 0; i < 5; i++) r.push_back({{l.id, 0, 0, i}, xf[i]});
    for (auto& e : l.effects) {
        r.push_back({{l.id, 1, e.id, -1}, &e.intensity});
        r.push_back({{l.id, 1, e.id, -2}, &e.speed});
        for (int i = 0; i < (int)e.params.size(); i++) r.push_back({{l.id, 1, e.id, i}, &e.params[i]});
    }
    for (auto& m : l.masks) {
        Prop* mp[6] = {&m.center, &m.size, &m.feather, &m.roundness, &m.opacity, &m.expansion};
        for (int i = 0; i < 6; i++) r.push_back({{l.id, 2, m.id, i}, mp[i]});
    }
    if (l.type == LayerType::Text) {
        Prop* tp[6] = {&l.text.size, &l.text.fill, &l.text.stroke, &l.text.strokeWidth, &l.text.tracking, &l.text.leading};
        for (int i = 0; i < 6; i++) r.push_back({{l.id, 3, 0, i}, tp[i]});
    }
    r.push_back({{l.id, 4, 0, 0}, &l.volume});
    // 3D: kind 0 index 5.. (depth, X / Y rotation), 5 = camera, 6 = light, 7 = material
    if (l.threeD || l.type == LayerType::Camera || l.type == LayerType::Light) {
        r.push_back({{l.id, 0, 0, 5}, &l.posZ});
        r.push_back({{l.id, 0, 0, 6}, &l.rotX});
        r.push_back({{l.id, 0, 0, 7}, &l.rotY});
    }
    if (l.threeD) {
        Prop* mp[4] = {&l.matAmbient, &l.matDiffuse, &l.matSpecular, &l.matShininess};
        for (int i = 0; i < 4; i++) r.push_back({{l.id, 7, 0, i}, mp[i]});
    }
    if (l.type == LayerType::Camera) {
        Prop* cp[6] = {&l.target, &l.targetZ, &l.camZoom, &l.focusDist, &l.aperture, &l.blurLevel};
        for (int i = 0; i < 6; i++) r.push_back({{l.id, 5, 0, i}, cp[i]});
    }
    if (l.type == LayerType::Light) {
        Prop* lp[8] = {&l.target, &l.targetZ, &l.lightColor, &l.lightIntensity, &l.coneAngle, &l.coneFeather, &l.lightRadius, &l.lightFalloff};
        for (int i = 0; i < 8; i++) r.push_back({{l.id, 6, 0, i}, lp[i]});
    }
    return r;
}

// 3D camera and lights
Layer* App::addCamera(Composition& c) {
    auto l = std::make_shared<Layer>();
    l->id = newId();
    l->type = LayerType::Camera;
    int n = 1;
    for (auto& o : c.layers) if (o->type == LayerType::Camera) n++;
    l->name = strf("Camera %d", n);
    l->inPoint = 0;
    l->outPoint = c.duration;
    float zoom = c.w * 50.f / 36.f;  // 50 mm: the z = 0 plane stays at 100%
    l->camZoom = Prop(1, zoom);
    l->focusDist = Prop(1, zoom);
    l->position = Prop(2, c.w * 0.5f, c.h * 0.5f);
    l->posZ = Prop(1, -zoom);
    l->target = Prop(2, c.w * 0.5f, c.h * 0.5f);
    l->targetZ = Prop(1, 0);
    l->expanded = true;
    c.layers.insert(c.layers.begin(), l);
    selLayers = {l->id};
    // a camera only matters for 3D layers: switch the visual layers on when none is 3D yet
    bool any3D = false;
    for (auto& o : c.layers) if (o->threeD) any3D = true;
    if (!any3D)
        for (auto& o : c.layers)
            if (o->type == LayerType::Footage || o->type == LayerType::Solid || o->type == LayerType::Text || o->type == LayerType::Precomp) o->threeD = true;
    markChanged();
    notify(any3D ? "Camera added" : "Camera added, layers switched to 3D");
    return l.get();
}

Layer* App::addLight(Composition& c, int kind) {
    auto l = std::make_shared<Layer>();
    l->id = newId();
    l->type = LayerType::Light;
    l->lightKind = clampv(kind, 0, 3);
    int n = 1;
    for (auto& o : c.layers) if (o->type == LayerType::Light) n++;
    l->name = strf("%s Light %d", kLightKindNames[l->lightKind], n);
    l->inPoint = 0;
    l->outPoint = c.duration;
    l->position = Prop(2, c.w * 0.32f, c.h * 0.28f);
    l->posZ = Prop(1, -c.w * 0.35f);
    l->target = Prop(2, c.w * 0.5f, c.h * 0.5f);
    l->targetZ = Prop(1, 0);
    l->lightFalloff = Prop(1, 0);  // 0 = no falloff (the default)
    c.layers.insert(c.layers.begin(), l);
    selLayers = {l->id};
    bool any3D = false;
    for (auto& o : c.layers) if (o->threeD) any3D = true;
    if (!any3D) notify("Light added. Lights only shine on 3D layers: turn on the 3D switch of a layer", false);
    markChanged();
    return l.get();
}

Prop* App::resolve(const PropRef& r) {
    Composition* c = comp();
    if (!c) return nullptr;
    Layer* l = c->find(r.layer);
    if (!l) return nullptr;
    for (auto& pr : layerProps(*l)) if (pr.first == r) return pr.second;
    return nullptr;
}

bool App::anyKeySelected() {
    Composition* c = comp();
    if (!c) return false;
    for (auto& l : c->layers)
        for (auto& pr : layerProps(*l)) if (pr.second->hasSelectedKeys()) return true;
    return false;
}
void App::clearKeySelection() {
    Composition* c = comp();
    if (!c) return;
    for (auto& l : c->layers)
        for (auto& pr : layerProps(*l)) for (auto& k : pr.second->keys) k.sel = false;
}
void App::deleteSelectedKeys() {
    Composition* c = comp();
    if (!c) return;
    for (auto& l : c->layers)
        for (auto& pr : layerProps(*l)) {
            Prop* p = pr.second;
            if (!p->hasSelectedKeys()) continue;
            float cur[4];
            p->eval(l->localTime(c->time), cur);
            p->keys.erase(std::remove_if(p->keys.begin(), p->keys.end(), [](const Keyframe& k) { return k.sel; }), p->keys.end());
            if (p->keys.empty()) { p->animated = false; memcpy(p->v, cur, sizeof(p->v)); }
        }
    markChanged();
}
void App::setSelectedKeysInterp(int mode) {
    Composition* c = comp();
    if (!c) return;
    for (auto& l : c->layers)
        for (auto& pr : layerProps(*l))
            for (auto& k : pr.second->keys) {
                if (!k.sel) continue;
                if (mode <= 3) k.curve = Curve::Classic;  // menu interpolation replaces a graph-editor curve
                switch (mode) {
                case 0: k.in = k.out = Interp::Linear; break;
                case 1: k.in = k.out = Interp::Ease; k.infIn = k.infOut = 33.333f; break;
                case 2: k.in = Interp::Ease; k.infIn = 33.333f; break;
                case 3: k.out = Interp::Ease; k.infOut = 33.333f; break;
                case 4: k.out = k.out == Interp::Hold ? Interp::Linear : Interp::Hold; break;
                }
            }
    markChanged();
}

// ---------------------------------------------------------------------------
// playback
// ---------------------------------------------------------------------------
double App::time() {
    Composition* c = comp();
    return c ? c->time : 0;
}
void App::setTime(double t) {
    Composition* c = comp();
    if (!c) return;
    t = clampv(frameSnap(t, c->fps), 0.0, std::max(0.0, c->duration - 1.0 / c->fps));
    if (t != c->time) { c->time = t; needRender = true; }
    if (playing) audio.play(t, c->workStart, c->workEnd, settings.loop);
}
void App::stepFrames(int n) {
    Composition* c = comp();
    if (!c) return;
    if (playing) togglePlay();
    setTime(c->time + n / c->fps + 1e-6);
}
void App::buildAudio() {
    Composition* c = comp();
    if (!project || !c) { audio.setClips({}); return; }
    audio.setClips(collectAudioClips(*project, *c));
}
void App::togglePlay() {
    Composition* c = comp();
    if (!c) return;
    playing = !playing;
    if (playing) {
        double t = c->time;
        if (t >= c->workEnd - 1.0 / c->fps || t < c->workStart) t = c->workStart;
        c->time = t;
        buildAudio();
        audio.play(t, c->workStart, c->workEnd, settings.loop);
        playFrom = t;
    } else {
        audio.stop();
        c->time = frameSnap(c->time, c->fps);
    }
    needRender = true;
}
void App::updatePlayback() {
    Composition* c = comp();
    if (!playing || !c) return;
    double t = audio.position();
    if (t >= c->workEnd) {
        if (settings.loop) { t = c->workStart; audio.play(t, c->workStart, c->workEnd, true); }
        else { t = c->workEnd - 1.0 / c->fps; playing = false; audio.stop(); }
    }
    double ft = frameSnap(t, c->fps);
    if (ft != c->time) { c->time = ft; needRender = true; }
}

// ---------------------------------------------------------------------------
// render queue
// ---------------------------------------------------------------------------
void App::queueRender(uint64_t compId) {
    Composition* c = project ? project->findComp(compId) : nullptr;
    if (!c) return;
    RenderItem r;
    r.id = newId();
    r.comp = compId;
    const ExportPreset& pr = kExportPresets[clampv(settings.exportPreset, 0, kExportPresetCount - 1)];
    r.out.format = pr.format;
    r.out.encoder = pr.encoder;
    r.out.quality = pr.quality;
    r.out.bitrate = pr.bitrate;
    r.out.scalePct = pr.scalePct;
    r.out.audio = pr.audio;
    r.out.audioKbps = pr.audioKbps;
    std::string dir = settings.exportDir;
    if (dir.empty()) dir = project->path.empty() ? joinPath(documentsDir(), "GeSe Exports") : parentDir(project->path);
    std::string name = c->name;
    for (auto& ch : name) if (strchr("\\/:*?\"<>|", ch)) ch = '_';
    r.out.path = joinPath(dir, name + formatExtension(r.out.format));
    project->queue.push_back(r);
    pRender = true;
    focusPanel = "Render Queue";
    markChanged();
}
void App::startRenderQueue() {
    if (exporter.active() || !project) return;
    if (playing) togglePlay();
    for (auto& it : project->queue) {
        if (!it.enabled || it.status == 2 || it.status == 1) continue;
        std::string err;
        it.status = 1;
        it.progress = 0;
        if (!exporter.start(*project, it, renderer, err)) {
            it.status = 3;
            it.message = err;
            notify("Render failed: " + err, true);
            continue;
        }
        return;
    }
}
void App::tickRender() {
    if (!exporter.active()) return;
    uint64_t id = exporter.itemId();
    bool more = exporter.step(0.040);
    for (auto& it : project->queue) if (it.id == id && it.status == 1) it.progress = exporter.progress();
    if (!more) {
        if (exporter.lastOk) notify("Render finished — " + exporter.lastMessage);
        else notify("Render failed: " + exporter.lastMessage, true);
        startRenderQueue();
    }
}

// ---------------------------------------------------------------------------
// AI background removal
// ---------------------------------------------------------------------------
void App::startAi(int provider, bool allFrames) {
    Composition* c = comp();
    Layer* l = selectedLayer();
    if (!c || !l || l->type != LayerType::Footage) { notify("Select a footage layer first", true); return; }
    Footage* f = project->findFootage(l->source);
    if (!f || !f->hasVideo) { notify("The selected layer has no picture", true); return; }
    AiJobDesc d;
    d.provider = provider;
    d.gpu = settings.aiGpu;
    d.apiKey = provider == 1 ? settings.removeBgKey : (provider == 2 ? settings.photoroomKey : settings.customKey);
    d.endpoint = provider == 3 ? settings.rembgUrl : settings.customUrl;
    for (auto& fp : project->footage) if (fp->id == f->id) d.footage = fp;
    if (f->kind == FootageKind::Image || !allFrames) {
        d.frames.push_back(f->media ? f->media->indexAt(l->sourceTime(c->time)) : 0);
    } else {
        double a = std::max(l->inPoint, c->workStart), b = std::min(l->outPoint, c->workEnd);
        for (double t = a; t < b - 1e-6; t += 1.0 / c->fps) d.frames.push_back(f->media->indexAt(l->sourceTime(t)));
    }
    std::string base = project->path.empty() ? joinPath(documentsDir(), "GeSe AI Cutouts") : joinPath(parentDir(project->path), "AI Cutouts");
    d.baseName = fileStem(f->name);
    for (auto& ch : d.baseName) if (strchr("\\/:*?\"<>|[]#", ch)) ch = '_';
    d.outDir = joinPath(base, d.baseName + strf("_%llu", (unsigned long long)(newId() % 100000)));
    d.layerId = l->id;
    d.compId = c->id;
    std::string err;
    if (provider == 0) {
        // give the network the video memory held by cached frames / render targets
        renderer.clearCaches();
        gfx::trimPool(0);
    }
    if (!ai.start(d, err)) notify(err, true);
    else notify(strf("AI background removal started (%d frame%s)", (int)d.frames.size(), d.frames.size() > 1 ? "s" : ""));
}

void App::tickAi() {
    AiJobDesc d;
    std::vector<std::string> files;
    std::string err;
    if (!ai.takeResult(d, files, err)) return;
    if (!err.empty() && files.empty()) { notify("Background removal failed: " + err, true); return; }
    if (!project || files.empty()) return;
    bool savedSeq = settings.importSequences;
    settings.importSequences = files.size() > 1;
    FootagePtr f = importFile(files.front());
    settings.importSequences = savedSeq;
    if (!f) return;
    if (files.size() > 1) { f->fps = d.footage ? d.footage->fps : 30; f->duration = f->seqCount / f->fps; f->media = MediaSource::open(*f); }
    Composition* c = project->findComp(d.compId);
    if (!c) return;
    Layer* src = c->find(d.layerId);
    int idx = src ? c->indexOf(src->id) : 0;
    Layer* nl = addFootageLayer(*c, *f, src ? (files.size() > 1 ? std::max(src->inPoint, c->workStart) : src->inPoint) : c->time, idx);
    if (src && nl) {
        if (files.size() == 1) nl->outPoint = src->outPoint;
        nl->name = src->name + " (Cutout)";
        // keep the same framing as the original layer
        ImVec2 ss = project->layerSize(*src, *c);
        float k = ss.x > 0 ? (float)f->w / ss.x : 1.f;
        nl->anchor = src->anchor;
        for (int i = 0; i < 2; i++) nl->anchor.v[i] *= k;
        for (auto& kf : nl->anchor.keys) for (int i = 0; i < 2; i++) kf.v[i] *= k;
        nl->position = src->position;
        nl->rotation = src->rotation;
        nl->scale = src->scale;
        if (k > 0) {
            for (int i = 0; i < 2; i++) nl->scale.v[i] /= k;
            for (auto& kf : nl->scale.keys) for (int i = 0; i < 2; i++) kf.v[i] /= k;
        }
        nl->startTime = files.size() > 1 ? nl->inPoint : src->startTime;
    }
    notify(err.empty() ? "Background removed — new layer added" : "Background partly removed: " + err, !err.empty());
}

// ---------------------------------------------------------------------------
// clip audio: mute / extract / save, voice & speed processing
// ---------------------------------------------------------------------------
static std::vector<float> mixRange(const std::vector<AudioClip>& clips, double a, double b) {
    int64_t frames = std::max<int64_t>(0, (int64_t)llround((b - a) * AudioData::kRate));
    std::vector<float> buf((size_t)frames * 2);
    const int64_t chunk = AudioData::kRate * 4;
    for (int64_t pos = 0; pos < frames; pos += chunk) {
        int n = (int)std::min<int64_t>(chunk, frames - pos);
        mixAudio(clips, a + (double)pos / AudioData::kRate, n, AudioData::kRate, buf.data() + pos * 2, 1.f);
    }
    return buf;
}

static std::string audioSaveDialog(HWND hwnd, const std::string& name, const char* title) {
    std::string base = name;
    for (auto& ch : base) if (strchr("\\/:*?\"<>|", ch)) ch = '_';
    static const wchar_t filt[] = L"WAV audio (*.wav)\0*.wav\0MP3 audio (*.mp3)\0*.mp3\0M4A / AAC (*.m4a)\0*.m4a\0FLAC lossless (*.flac)\0*.flac\0";
    return dlgSaveFile(hwnd, filt, "wav", base, title);
}

void App::extractAudio(Layer& l) {
    Composition* c = comp();
    Footage* f = l.type == LayerType::Footage ? project->findFootage(l.source) : nullptr;
    if (!c || !f || !f->hasAudio) { notify("This clip has no sound", true); return; }
    if (!f->audio || !f->audio->ready) { notify("The sound is still loading, try again in a moment", true); return; }
    std::string dir = project->path.empty() ? joinPath(documentsDir(), "GeSe Extracted Audio") : joinPath(parentDir(project->path), "Extracted Audio");
    makeDirs(dir);
    std::string base = fileStem(f->name);
    for (auto& ch : base) if (strchr("\\/:*?\"<>|[]#", ch)) ch = '_';
    std::string out = joinPath(dir, base + ".wav");
    for (int i = 2; fileExists(out); i++) out = joinPath(dir, strf("%s_%d.wav", base.c_str(), i));
    std::string err;
    if (!saveAudioFile(out, f->audio->samples.data(), (int64_t)f->audio->samples.size() / 2, err)) { notify("Could not extract audio: " + err, true); return; }
    FootagePtr af = importFile(out);
    if (!af) return;
    uint64_t srcId = l.id;
    Layer* nl = addFootageLayer(*c, *af, l.startTime, c->indexOf(srcId) + 1);  // right below the video
    Layer* src = c->find(srcId);
    if (!nl || !src) return;
    // same timing and sound settings as the clip it came from
    nl->name = src->name + " (Audio)";
    nl->startTime = src->startTime;
    nl->inPoint = src->inPoint;
    nl->outPoint = src->outPoint;
    nl->speed = src->speed;
    nl->reversed = src->reversed;
    nl->volume = src->volume;
    nl->keepPitch = src->keepPitch;
    nl->pitch = src->pitch;
    nl->voice = src->voice;
    nl->audioFadeIn = src->audioFadeIn;
    nl->audioFadeOut = src->audioFadeOut;
    src->audioOn = false;
    selLayers = {nl->id};
    markChanged();
    notify("Audio extracted to its own layer, the video clip is muted. File: " + fileName(out));
}

void App::saveLayerAudio(Layer& l) {
    Composition* c = comp();
    if (!c || !project) return;
    std::string p = audioSaveDialog(hwnd, l.name, "Save Clip Audio");
    if (p.empty()) return;
    // exactly what the clip plays: trims, speed, voice effects, volume keyframes, fades
    auto clips = collectAudioClips(*project, *c, true, l.id);
    std::vector<float> buf = mixRange(clips, l.inPoint, l.outPoint);
    std::string err;
    if (saveAudioFile(p, buf.data(), (int64_t)buf.size() / 2, err)) notify("Audio saved: " + fileName(p));
    else notify("Could not save audio: " + err, true);
}

void App::saveCompAudio(Composition& c) {
    if (!project) return;
    std::string p = audioSaveDialog(hwnd, c.name, "Export Audio (work area)");
    if (p.empty()) return;
    auto clips = collectAudioClips(*project, c, true);
    std::vector<float> buf = mixRange(clips, c.workStart, c.workEnd);
    std::string err;
    if (saveAudioFile(p, buf.data(), (int64_t)buf.size() / 2, err)) notify("Audio exported: " + fileName(p));
    else notify("Could not export audio: " + err, true);
}

// quick video fades: a Fade transition with its Animate mode (In / Out / In & Out)
static int fadeParam(const EffectRegistry& reg, const EffectInst& e, const char* name) {
    const EffectDef* d = reg.find(e.defId);
    if (!d) return -1;
    for (int i = 0; i < (int)d->params.size() && i < (int)e.params.size(); i++) if (d->params[i].name == name) return i;
    return -1;
}
Prop* App::effectParam(EffectInst& e, const char* name) {
    int i = fadeParam(effects, e, name);
    return i >= 0 ? &e.params[i] : nullptr;
}
EffectInst* App::findFade(Layer& l) {
    for (auto& e : l.effects) if (e.defId == "gese.tr.fade") return &e;
    return nullptr;
}
int App::fadeMode(Layer& l) {
    for (auto& e : l.effects) {
        if (e.defId != "gese.tr.fade") continue;
        int m = fadeParam(effects, e, "tmode");
        return m >= 0 ? (int)lrintf(e.params[m].v[0]) : 0;
    }
    return 0;
}
void App::toggleFade(Layer& l, bool out) {
    const EffectDef* d = effects.find("gese.tr.fade");
    if (!d) return;
    int mode = fadeMode(l);
    bool fin = mode == 1 || mode == 3, fout = mode == 2 || mode == 3;
    if (out) fout = !fout; else fin = !fin;
    int nm = fin && fout ? 3 : (fin ? 1 : (fout ? 2 : 0));
    auto it = std::find_if(l.effects.begin(), l.effects.end(), [](const EffectInst& e) { return e.defId == "gese.tr.fade"; });
    if (nm == 0) {
        if (it != l.effects.end()) l.effects.erase(it);
    } else {
        if (it == l.effects.end()) {
            l.effects.push_back(effects.instantiate(*d));
            it = l.effects.end() - 1;
            int du = fadeParam(effects, *it, "tdur");
            if (du >= 0) it->params[du].v[0] = (float)std::min(0.5, (l.outPoint - l.inPoint) / 3);
        }
        int m = fadeParam(effects, *it, "tmode");
        if (m >= 0) it->params[m].v[0] = (float)nm;
    }
    markChanged();
}

void App::setLayerSpeed(Layer& l, double ns) {
    ns = clampv(ns, 0.01, 100.0);
    double srcIn = (l.inPoint - l.startTime) * l.speed;
    double dur = (l.outPoint - l.inPoint) * l.speed / ns;
    l.speed = ns;
    l.startTime = l.inPoint - srcIn / ns;
    l.outPoint = l.inPoint + dur;
}

void App::pollAudioFx() {
    // re-collect the playback clips when a voice / speed job finished or a clip's sound finished loading
    static int readyCount = -1;
    int ready = 0;
    if (project)
        for (auto& f : project->footage) if (f->audio && f->audio->ready) ready++;
    uint64_t g = audioFxGeneration();
    if (g != audioFxGen || ready != readyCount) {
        audioFxGen = g;
        readyCount = ready;
        buildAudio();
    }
}

// ---------------------------------------------------------------------------
// view
// ---------------------------------------------------------------------------
void App::setFullPreview(bool on) {
    if (on == fullPreview || !hwnd) return;
    fullPreview = on;
    if (on) {
        fpStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
        fpPlacement.length = sizeof(fpPlacement);
        GetWindowPlacement(hwnd, &fpPlacement);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(hwnd, GWL_STYLE, fpStyle & ~(LONG_PTR)(WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZE));
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, fpStyle);
        SetWindowPlacement(hwnd, &fpPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    needRender = true;
}

void App::jumpToEdit(int dir) {
    Composition* c = comp();
    if (!c) return;
    std::vector<double> pts = {0, c->duration - 1.0 / c->fps, c->workStart, c->workEnd};
    for (auto& m : c->markers) pts.push_back(m.t);
    for (auto& l : c->layers) {
        pts.push_back(l->inPoint);
        pts.push_back(l->outPoint);
        for (auto& pr : layerProps(*l))
            if (pr.second->animated) for (auto& k : pr.second->keys) pts.push_back(l->startTime + k.t);
    }
    double eps = 0.5 / c->fps, best = dir > 0 ? 1e18 : -1e18;
    for (double t : pts) {
        if (dir > 0 && t > c->time + eps && t < best) best = t;
        if (dir < 0 && t < c->time - eps && t > best) best = t;
    }
    if (fabs(best) < 1e17) {
        if (playing) togglePlay();
        setTime(best + 1e-6);
    }
}

void App::autosave() {
    if (!project || !dirty || settings.autosaveMin <= 0) return;
    double now = nowSec();
    if (lastAutosave == 0) { lastAutosave = now; return; }
    if (now - lastAutosave < settings.autosaveMin * 60.0) return;
    lastAutosave = now;
    std::string name = project->path.empty() ? "Untitled" : fileStem(project->path);
    std::string p = joinPath(joinPath(appDataDir(), "Autosave"), name + " autosave.gese");
    writeFile(p, project->save().dump());
    logf("autosaved %s", p.c_str());
}

// ---------------------------------------------------------------------------
// shell dialogs
// ---------------------------------------------------------------------------
static std::vector<std::string> runOpen(HWND owner, const wchar_t* filter, const char* title, bool multi, bool folder) {
    std::vector<std::string> out;
    IFileOpenDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return out;
    DWORD opts = 0;
    d->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM;
    if (multi) opts |= FOS_ALLOWMULTISELECT;
    if (folder) opts |= FOS_PICKFOLDERS;
    d->SetOptions(opts);
    d->SetTitle(widen(title).c_str());
    std::vector<COMDLG_FILTERSPEC> specs;
    std::vector<std::wstring> parts;
    if (filter && !folder) {
        const wchar_t* p = filter;
        while (*p) { parts.push_back(p); p += wcslen(p) + 1; }
        for (size_t i = 0; i + 1 < parts.size(); i += 2) specs.push_back({parts[i].c_str(), parts[i + 1].c_str()});
        if (!specs.empty()) d->SetFileTypes((UINT)specs.size(), specs.data());
    }
    if (SUCCEEDED(d->Show(owner))) {
        IShellItemArray* arr = nullptr;
        if (SUCCEEDED(d->GetResults(&arr))) {
            DWORD n = 0;
            arr->GetCount(&n);
            for (DWORD i = 0; i < n; i++) {
                IShellItem* it = nullptr;
                if (SUCCEEDED(arr->GetItemAt(i, &it))) {
                    PWSTR s = nullptr;
                    if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &s))) { out.push_back(narrow(s)); CoTaskMemFree(s); }
                    it->Release();
                }
            }
            arr->Release();
        }
    }
    d->Release();
    return out;
}
std::string dlgOpenFile(HWND owner, const wchar_t* filter, const char* title) {
    auto r = runOpen(owner, filter, title, false, false);
    return r.empty() ? std::string() : r[0];
}
std::vector<std::string> dlgOpenFiles(HWND owner, const wchar_t* filter, const char* title) { return runOpen(owner, filter, title, true, false); }
std::string dlgPickFolder(HWND owner, const char* title) {
    auto r = runOpen(owner, nullptr, title, false, true);
    return r.empty() ? std::string() : r[0];
}
std::string dlgSaveFile(HWND owner, const wchar_t* filter, const char* defExt, const std::string& defName, const char* title) {
    std::string out;
    IFileSaveDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return out;
    d->SetTitle(widen(title).c_str());
    std::vector<std::wstring> parts;
    std::vector<COMDLG_FILTERSPEC> specs;
    const wchar_t* p = filter;
    while (p && *p) { parts.push_back(p); p += wcslen(p) + 1; }
    for (size_t i = 0; i + 1 < parts.size(); i += 2) specs.push_back({parts[i].c_str(), parts[i + 1].c_str()});
    if (!specs.empty()) d->SetFileTypes((UINT)specs.size(), specs.data());
    d->SetDefaultExtension(widen(defExt).c_str());
    d->SetFileName(widen(defName).c_str());
    if (SUCCEEDED(d->Show(owner))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(d->GetResult(&it))) {
            PWSTR s = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &s))) { out = narrow(s); CoTaskMemFree(s); }
            it->Release();
        }
    }
    d->Release();
    return out;
}
void shellOpen(const std::string& path) { ShellExecuteW(nullptr, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
