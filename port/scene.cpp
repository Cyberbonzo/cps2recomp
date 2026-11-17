// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.

#include "scene.h"

#include "../fight/guest_stages.h"
#include "../machine/machine.h"
#include "../model/image_files.h"
#include "../video/native.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>

namespace port {

using cps2recomp::Machine;
using cps2recomp::NativeVideo;
using cps2recomp::Video;
namespace image_files = cps2recomp::image_files;

namespace {
    constexpr int kMostWaiting = 120; // (a clip's frames waiting to be written: past that it is cut short)
    constexpr const char *kLayerNames[4] = {"objects", "scroll1", "scroll2", "scroll3"};
    constexpr int kTileSizes[4] = {16, 8, 16, 32};

    std::filesystem::path PathOf(const std::string &text) {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }
    // A folder of its own under `under` ("name", or "name 2" and on), made; "" if it could not be
    std::string NewFolder(const std::string &under, const std::string &name) {
        std::error_code error;
        std::string folder = under + name;
        for (int n = 2; std::filesystem::exists(PathOf(folder), error); n++) {
            folder = under + name + " " + std::to_string(n);
        }
        std::filesystem::create_directories(PathOf(folder), error);
        return error ? "" : folder + "/";
    }
    std::string Hex(uint32_t colour) {
        char text[8];
        snprintf(text, sizeof(text), "#%02x%02x%02x", colour & 0xFF, (colour >> 8) & 0xFF, (colour >> 16) & 0xFF);
        return text;
    }
    std::string Quoted(const std::string &text) {
        std::string out = "\"";
        for (const char c : text) {
            if (c == '"' || c == '\\') {
                out += '\\';
            }
            out += c;
        }
        return out + "\"";
    }

    // A board's layers back to front: its scroll screens as its layer control orders them in the middle of the picture,
    // and its objects among them. A CPS-1's objects are where the layer control puts them. A CPS-2's are put by their
    // level (the priority control: an object is behind a screen whose priority is its level or more), so that objects
    // of different levels can be at different places (a stage's objects behind a scroll screen, the fighters in front):
    // each place its own layer, with the levels drawn there (bit n: level n).
    struct Place {
        int which = 0;        // (0 the objects, 1-3 the scroll screens)
        unsigned levels = 0xFF;
    };
    std::vector<Place> Order(const NativeVideo::FrameData &frame) {
        const uint16_t control = frame.layers[Video::kHeight / 2].layerControl;
        std::vector<int> order;
        for (int shift = 6; shift <= 12; shift += 2) {
            const int layer = (control >> shift) & 3;
            if (std::find(order.begin(), order.end(), layer) == order.end()) {
                order.push_back(layer);
            }
        }
        for (int layer = 0; layer < 4; layer++) {
            if (std::find(order.begin(), order.end(), layer) == order.end()) {
                order.push_back(layer);
            }
        }
        std::vector<Place> places;
        if (frame.raster->Cps1()) {
            for (const int layer : order) {
                places.push_back({layer, 0xFF});
            }
            return places;
        }
        order.erase(std::find(order.begin(), order.end(), 0));
        // (for each level its objects have: in front of every screen behind it)
        unsigned at[4] = {}; // (the levels in front of the first n screens)
        unsigned present = 0;
        for (const auto &tile : frame.raster->Tiles()) {
            present |= tile.hidden ? 0u : 1u << (tile.level & 7);
        }
        const uint16_t priority = frame.raster->Priority();
        for (int level = 0; level < 8; level++) {
            if (!((present >> level) & 1)) {
                continue;
            }
            size_t in = 0;
            for (size_t n = 0; n < order.size(); n++) {
                if (level > ((priority >> (4 * order[n])) & 0xF)) {
                    in = n + 1;
                }
            }
            at[in] |= 1u << level;
        }
        for (size_t n = 0; n <= order.size(); n++) {
            if (at[n] || (n == order.size() && !present)) {
                places.push_back({0, at[n] ? at[n] : 0xFF});
            }
            if (n < order.size()) {
                places.push_back({order[n], 0});
            }
        }
        return places;
    }

    // One board's layers, each the frame drawn again from its records with that layer alone on, its pixels marked; moved
    // down `shift` lines (another game's stage under the game's: Machine::guestShift)
    void BoardLayers(const NativeVideo::FrameData &frame, int width, const std::string &prefix, const std::string &board, int shift,
                     std::vector<SceneExport::Layer> &out) {
        std::vector<uint32_t> drawn(static_cast<size_t>(width) * Video::kHeight);
        const std::vector<Place> places = Order(frame);
        const int objectPlaces = static_cast<int>(std::count_if(places.begin(), places.end(), [](const Place &p) { return p.which == 0; }));
        int screensBehind = 0; // (an objects layer's name, when there is more than one: how many scroll screens are behind it)
        for (const Place &place : places) {
            const int layer = place.which;
            if (!(frame.options.layerMask & (1u << layer))) {
                screensBehind += layer != 0;
                continue; // (the game's sources have it off: not part of what is shown)
            }
            NativeVideo::FrameData alone = frame;
            alone.options.layerMask = 1u << layer;
            alone.options.markObjects = true;
            alone.options.markLayers = layer ? 1u << layer : 0;
            alone.options.markFront = false;
            alone.options.panelObject = nullptr;
            cps2recomp::native_video::Raster levels;
            if (layer == 0 && place.levels != 0xFF) {
                levels = *frame.raster;
                levels.KeepLevels(place.levels);
                alone.raster = &levels;
            }
            NativeVideo::DrawFrame(alone, drawn.data(), width);
            SceneExport::Layer one;
            one.which = layer;
            one.levels = layer == 0 ? place.levels : 0;
            one.name = prefix + kLayerNames[layer];
            if (layer == 0 && objectPlaces > 1) {
                one.name += "-" + std::to_string(screensBehind);
            }
            one.board = board;
            one.pixels.assign(drawn.size(), 0);
            for (int line = 0; line < Video::kHeight; line++) {
                const int from = std::clamp(line - shift, 0, Video::kHeight - 1);
                const uint32_t *row = &drawn[static_cast<size_t>(from) * width];
                uint32_t *to = &one.pixels[static_cast<size_t>(line) * width];
                for (int x = 0; x < width; x++) {
                    if (row[x] & 0x80000000u) {
                        to[x] = (row[x] & 0xFFFFFFu) | 0xFF000000u;
                        one.empty = false;
                    }
                }
            }
            out.push_back(std::move(one));
            screensBehind += layer != 0;
        }
    }

    image_files::Picture AsPicture(const std::vector<uint32_t> &pixels, int width, int height) {
        image_files::Picture picture;
        picture.width = width;
        picture.height = height;
        picture.pixels = pixels;
        return picture;
    }

    // A scene's files: `picture` and its layers' (the empty ones not written), named `file` (a still's: "<layer>.png";
    // a clip's "<layer>/0001.png")
    bool WriteScene(const SceneExport::Scene &scene, const std::string &folder, int number) {
        auto path = [&](const std::string &name) {
            if (number <= 0) {
                return folder + name + ".png";
            }
            char file[16];
            snprintf(file, sizeof(file), "%04d.png", number);
            std::error_code error;
            std::filesystem::create_directories(PathOf(folder + name), error);
            return folder + name + "/" + file;
        };
        bool written = image_files::WritePng(path("picture"), AsPicture(scene.picture, scene.width, scene.height));
        for (const SceneExport::Layer &layer : scene.layers) {
            if (!layer.empty) {
                written = image_files::WritePng(path(layer.name), AsPicture(layer.pixels, scene.width, scene.height)) && written;
            }
        }
        return written;
    }

    std::string LayerJson(const SceneExport::Layer &layer, bool still) {
        std::string json = "{\"name\": " + Quoted(layer.name) + ", \"board\": " + Quoted(layer.board) + ", \"kind\": " +
                           (layer.which ? "\"scroll\"" : "\"objects\"") + ", \"tile\": " + std::to_string(kTileSizes[layer.which]);
        if (layer.which == 0 && layer.levels != 0xFF) {
            std::string levels;
            for (int n = 0; n < 8; n++) {
                if ((layer.levels >> n) & 1) {
                    levels += (levels.empty() ? "" : ", ") + std::to_string(n);
                }
            }
            json += ", \"levels\": [" + levels + "]";
        }
        if (still) {
            json += ", \"file\": " + (layer.empty ? std::string("null") : Quoted(layer.name + ".png"));
        } else {
            json += ", \"folder\": " + Quoted(layer.name);
        }
        return json + "}";
    }
} // namespace

bool SceneExport::Take(Machine &machine, Scene &scene) {
    NativeVideo::FrameData frame;
    if (!machine.nativeVideo.LastFrame(frame)) {
        return false;
    }
    const int width = Video::kWidth + 2 * frame.options.side;
    if (width != machine.PictureWidth()) {
        return false;
    }
    scene.width = width;
    scene.height = Video::kHeight;
    scene.frame = machine.frame;
    scene.backdrop = frame.raster->Colours()[0xBFF] & 0xFFFFFF;
    scene.picture.assign(machine.Picture(), machine.Picture() + static_cast<size_t>(width) * Video::kHeight);
    for (uint32_t &pixel : scene.picture) {
        pixel = (pixel & 0xFFFFFFu) | 0xFF000000u;
    }
    scene.layers.clear();
    // (another game's stage under the game's: its layers first, behind the game's)
    if (machine.guestBehind) {
        if (Machine *board = cps2recomp::guest_stages::ShownBoard()) {
            NativeVideo::FrameData stage;
            if (board->nativeVideo.LastFrame(stage) && Video::kWidth + 2 * stage.options.side == width) {
                scene.backdrop = stage.raster->Colours()[0xBFF] & 0xFFFFFF;
                BoardLayers(stage, width, "stage-", "stage", machine.guestShift, scene.layers);
            }
        }
    }
    BoardLayers(frame, width, "", "game", 0, scene.layers);
    return true;
}

SceneExport::~SceneExport() {
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_stopping = true;
    }
    m_wake.notify_all();
    for (std::thread &writer : m_writers) {
        writer.join();
    }
}

std::string SceneExport::Save(Machine &machine, const std::string &under, const std::string &name) {
    Scene scene;
    if (!Take(machine, scene)) {
        return "NO SCENE TO SAVE YET";
    }
    const std::string folder = NewFolder(under, name);
    if (folder.empty() || !WriteScene(scene, folder, 0)) {
        return "SCENE NOT SAVED";
    }
    std::string json = "{\n  \"kind\": \"scene\",\n  \"frame\": " + std::to_string(scene.frame) + ",\n  \"width\": " +
                       std::to_string(scene.width) + ",\n  \"height\": " + std::to_string(scene.height) + ",\n  \"backdrop\": " +
                       Quoted(Hex(scene.backdrop)) + ",\n  \"picture\": \"picture.png\",\n  \"layers\": [\n";
    for (size_t n = 0; n < scene.layers.size(); n++) {
        json += "    " + LayerJson(scene.layers[n], true) + (n + 1 < scene.layers.size() ? ",\n" : "\n");
    }
    json += "  ]\n}\n";
    if (!image_files::WriteAll(folder + "scene.json", std::vector<uint8_t>(json.begin(), json.end()))) {
        return "SCENE NOT SAVED";
    }
    fprintf(stderr, "scene: %s\n", folder.c_str());
    const size_t at = folder.find("exports/");
    return "SCENE SAVED  " + (at == std::string::npos ? folder : folder.substr(at));
}

std::string SceneExport::StartClip(const std::string &under, const std::string &name, int frames) {
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_clip.on || m_finishing || !m_jobs.empty() || m_writing) {
            return "A CLIP IS STILL BEING SAVED";
        }
        m_failed = false;
    }
    m_clip.folder = NewFolder(under, name);
    if (m_clip.folder.empty()) {
        return "CLIP NOT SAVED";
    }
    m_clip.wanted = frames;
    m_clip.taken = 0;
    m_clip.names.clear();
    m_clip.json.clear();
    m_clip.on = true;
    if (m_writers.empty()) {
        const unsigned threads = std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u);
        for (unsigned n = 0; n < threads; n++) {
            m_writers.emplace_back([this] { Writer(); });
        }
    }
    return "RECORDING SCENE CLIP";
}

void SceneExport::Frame(Machine &machine) {
    if (!m_clip.on) {
        return;
    }
    Job job;
    if (!Take(machine, job.scene)) {
        return; // (no records this frame: a frame of the clip fewer)
    }
    const Scene &scene = job.scene;
    std::string order, empty;
    for (const Layer &layer : scene.layers) {
        if (std::find(m_clip.names.begin(), m_clip.names.end(), layer.name) == m_clip.names.end()) {
            m_clip.names.push_back(layer.name);
            m_clip.json += std::string(m_clip.json.empty() ? "" : "\x1F") + LayerJson(layer, false);
        }
        (layer.empty ? empty : order) += std::string((layer.empty ? empty : order).empty() ? "" : ", ") + Quoted(layer.name);
    }
    m_clip.taken++;
    job.folder = m_clip.folder;
    job.number = m_clip.taken;
    m_frameJson += std::string(m_frameJson.empty() ? "" : ",\n") + "    {\"number\": " + std::to_string(job.number) +
                   ", \"frame\": " + std::to_string(scene.frame) + ", \"backdrop\": " + Quoted(Hex(scene.backdrop)) +
                   ", \"layers\": [" + order + "]}";
    m_width = scene.width;
    m_height = scene.height;
    bool full;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_jobs.push_back(std::move(job));
        full = static_cast<int>(m_jobs.size()) >= kMostWaiting;
    }
    m_wake.notify_one();
    if (m_clip.taken >= m_clip.wanted || full) {
        StopClip(full && m_clip.taken < m_clip.wanted);
    }
}

void SceneExport::StopClip(bool cutShort) {
    if (!m_clip.on) {
        return;
    }
    m_clip.on = false;
    // scene.json, written when the last frame is
    std::string layers;
    for (size_t at = 0; at <= m_clip.json.size();) {
        const size_t end = std::min(m_clip.json.find('\x1F', at), m_clip.json.size());
        layers += "    " + m_clip.json.substr(at, end - at) + (end < m_clip.json.size() ? ",\n" : "\n");
        at = end + 1;
    }
    std::string json = "{\n  \"kind\": \"clip\",\n  \"width\": " + std::to_string(m_width) + ",\n  \"height\": " +
                       std::to_string(m_height) + ",\n  \"fps\": 59.637405,\n  \"frames\": " + std::to_string(m_clip.taken) +
                       ",\n  \"picture\": \"picture\",\n  \"layers\": [\n" + (m_clip.taken ? layers : "") +
                       "  ],\n  \"frameList\": [\n" + m_frameJson + "\n  ]\n}\n";
    m_frameJson.clear();
    std::lock_guard<std::mutex> lock(m_lock);
    m_finalJson = std::move(json);
    m_finalFolder = m_clip.folder;
    m_finalFrames = m_clip.taken;
    m_cutShort = cutShort;
    m_finishing = true;
    if (m_jobs.empty() && m_writing == 0) {
        Finish();
    }
}

// (with the lock held: the clip's frames all written)
void SceneExport::Finish() {
    m_finishing = false;
    const bool written = image_files::WriteAll(m_finalFolder + "scene.json", std::vector<uint8_t>(m_finalJson.begin(), m_finalJson.end()));
    std::string folder = m_finalFolder;
    const size_t at = folder.find("exports/");
    if (at != std::string::npos) {
        folder = folder.substr(at);
    }
    if (m_failed || !written) {
        m_said = "CLIP NOT SAVED";
    } else if (m_finalFrames == 0) {
        m_said = "NO SCENE TO SAVE YET";
    } else {
        m_said = std::string(m_cutShort ? "CLIP CUT SHORT  " : "CLIP SAVED  ") + folder;
    }
    m_saying = true;
    fprintf(stderr, "scene clip: %s (%d frames)\n", m_finalFolder.c_str(), m_finalFrames);
}

void SceneExport::Writer() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_wake.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });
            if (m_jobs.empty()) {
                return;
            }
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
            m_writing++;
        }
        const bool written = WriteScene(job.scene, job.folder, job.number);
        std::lock_guard<std::mutex> lock(m_lock);
        m_writing--;
        m_failed = m_failed || !written;
        if (m_finishing && m_jobs.empty() && m_writing == 0) {
            Finish();
        }
    }
}

bool SceneExport::TakeSaid(std::string &said) {
    std::lock_guard<std::mutex> lock(m_lock);
    if (!m_saying) {
        return false;
    }
    m_saying = false;
    said = m_said;
    return true;
}

} // namespace port
