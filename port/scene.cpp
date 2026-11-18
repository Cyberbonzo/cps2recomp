// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.

#include "scene.h"

#include "../fight/alternative.h"
#include "../fight/battle.h"
#include "../fight/guest_stages.h"
#include "../machine/machine.h"
#include "../model/image_files.h"
#include "../runtime/game.h"
#include "../video/native.h"
#include "hud.h"
#include "port.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>

namespace port {

using cps2recomp::Machine;
using cps2recomp::NativeVideo;
using cps2recomp::Video;
namespace image_files = cps2recomp::image_files;
namespace battle = cps2recomp::battle;

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

    // Where scroll screen `which` (0-2) is in its map on a line of the frame: the map's column at the picture's left edge
    // and its line at the picture's line (as Raster::DrawScroll works them out, its row scroll and the game's sources'
    // moves counted; not wrapped to the map)
    void ScrollPlace(const NativeVideo::FrameData &frame, int which, int line, int &x, int &y) {
        const cps2recomp::native_video::Layers &layers = frame.layers[line];
        const int rasterLine = line + Video::kFirstLine;
        int scrollX = layers.scroll[which][0];
        const bool rowScroll = which == 1 && (layers.videoControl & 1);
        int moved = frame.options.side + frame.options.scroll[which];
        if (rowScroll) {
            const uint32_t rows = (static_cast<uint32_t>(layers.rows) * 256) & ~0x7FFu & 0x3FFFF;
            const uint32_t at = rows + (((rasterLine + layers.rowOffset) & 0x3FF) * 2);
            scrollX += (frame.video[at] << 8) | frame.video[at + 1];
            if (frame.options.rows) {
                moved += frame.options.rows[(rasterLine + layers.rowOffset) & 0x3FF];
            }
        }
        x = Video::kFirstColumn - moved + scrollX;
        y = rasterLine + layers.scroll[which][1];
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
            if (layer) {
                one.mapX.resize(Video::kHeight);
                one.mapY.resize(Video::kHeight);
            }
            for (int line = 0; line < Video::kHeight; line++) {
                const int from = std::clamp(line - shift, 0, Video::kHeight - 1);
                if (layer) {
                    ScrollPlace(frame, layer - 1, from, one.mapX[line], one.mapY[line]);
                    one.mapY[line] += line - from;
                }
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

    // A fighting game's picture by what is in it (battle::PartOf): each part the frame drawn again with only its objects and
    // its scroll screens on, so that a part is whole where another covers it. The stage's pieces are behind the fighters
    // or in front of them by where they are among the layers (Order) against the fighters' place, and objects at the
    // fighters' own level by their entry (later entries in front). On another game's stage (`stage`, moved down `shift`
    // lines) the stage's parts are that board's picture, split by its marks of what is in front of the fighters.
    enum PartIndex { kStageBehind, kOther, kPlayerBack, kPlayerFront, kStageFront, kPanel, kParts };
    void BoardParts(const NativeVideo::FrameData &frame, int width, const NativeVideo::FrameData *stage, int shift,
                    std::vector<SceneExport::Layer> &out) {
        using battle::Part;
        if (!cps2recomp::modes::RoundFought()) {
            return; // (only in a round: the menus and the character choice have no players or stage to tell apart)
        }
        const auto &tiles = frame.raster->Tiles();
        std::vector<Part> part(tiles.size(), battle::kOther);
        int playerLevels[8] = {}, firstEntry[2] = {1 << 20, 1 << 20}, lastEntry[2] = {-1, -1};
        for (size_t n = 0; n < tiles.size(); n++) {
            part[n] = battle::PartOf(tiles[n].entryTile, tiles[n].attributes);
            if (part[n] == battle::kUnknown) {
                return; // (a game that does not say what its objects are)
            }
            const int player = part[n] == battle::kPlayer1 ? 0 : part[n] == battle::kPlayer2 ? 1 : -1;
            if (player >= 0 && !tiles[n].hidden) {
                playerLevels[tiles[n].level & 7]++;
                firstEntry[player] = std::min(firstEntry[player], static_cast<int>(tiles[n].entry));
                lastEntry[player] = std::max(lastEntry[player], static_cast<int>(tiles[n].entry));
            }
        }
        // (the player in front: the one whose entries come later)
        const int front = lastEntry[1] > lastEntry[0] ? 1 : 0;
        const int lastPlayer = std::max(lastEntry[0], lastEntry[1]);
        // The fighters' place among the layers: that of the level most of their tiles have (none: all the stage behind)
        const std::vector<Place> places = Order(frame);
        const int level = static_cast<int>(std::max_element(playerLevels, playerLevels + 8) - playerLevels);
        size_t fighters = places.size();
        if (lastPlayer >= 0) {
            for (size_t n = 0; n < places.size(); n++) {
                if (places[n].which == 0 && ((places[n].levels >> level) & 1)) {
                    fighters = n;
                }
            }
        }
        auto placeOf = [&](const cps2recomp::native_video::ObjectTile &t) {
            for (size_t n = 0; n < places.size(); n++) {
                if (places[n].which == 0 && ((places[n].levels >> t.level) & 1)) {
                    return n;
                }
            }
            return places.size();
        };
        std::vector<uint8_t> index(tiles.size());
        for (size_t n = 0; n < tiles.size(); n++) {
            switch (part[n]) {
            case battle::kPanel: index[n] = kPanel; break;
            case battle::kPlayer1: index[n] = front == 0 ? kPlayerFront : kPlayerBack; break;
            case battle::kPlayer2: index[n] = front == 1 ? kPlayerFront : kPlayerBack; break;
            case battle::kStage: {
                const size_t at = placeOf(tiles[n]);
                const bool inFront = at > fighters || (at == fighters && static_cast<int>(tiles[n].entry) > lastPlayer);
                index[n] = inFront ? kStageFront : kStageBehind;
                break;
            }
            default: index[n] = kOther; break;
            }
        }
        // The scroll screens: the panel's, else the stage's, behind or in front of the fighters
        const unsigned panelLayers = battle::PanelLayers();
        unsigned scrolls[kParts] = {};
        for (size_t n = 0; n < places.size(); n++) {
            const int which = places[n].which;
            if (which && (frame.options.layerMask & (1u << which))) {
                scrolls[(panelLayers >> which) & 1 ? kPanel : n < fighters ? kStageBehind : kStageFront] |= 1u << which;
            }
        }
        static const char *const kNames[kParts] = {"stage-behind", "other", "player", "player", "stage-front", "panel"};
        std::vector<uint32_t> drawn(static_cast<size_t>(width) * Video::kHeight);
        for (int p = 0; p < kParts; p++) {
            SceneExport::Layer one;
            one.name = std::string("part-") + kNames[p];
            if (p == kPlayerBack || p == kPlayerFront) {
                one.name += (p == kPlayerFront) == (front == 0) ? "1" : "2";
            }
            one.board = "part";
            one.pixels.assign(drawn.size(), 0);
            auto keep = [&](const uint32_t *picture, int from, int line, uint32_t want, uint32_t mask) {
                const uint32_t *row = &picture[static_cast<size_t>(from) * width];
                uint32_t *to = &one.pixels[static_cast<size_t>(line) * width];
                for (int x = 0; x < width; x++) {
                    if ((row[x] & mask) == want && !to[x]) {
                        to[x] = (row[x] & 0xFFFFFFu) | 0xFF000000u;
                        one.empty = false;
                    }
                }
            };
            // (another game's stage: its pixels, those in front of the fighters or the rest)
            if (stage && (p == kStageBehind || p == kStageFront)) {
                NativeVideo::FrameData all = *stage;
                all.options.markObjects = true;
                all.options.markLayers = 0xE;
                all.options.panelObject = nullptr;
                // (behind: drawn again without its objects marked to be in front, so that it is whole where they were)
                cps2recomp::native_video::Raster behind = *stage->raster;
                if (p == kStageBehind) {
                    behind.KeepTiles([](const cps2recomp::native_video::ObjectTile &t) { return !(t.marks & Video::kFrontPixel); });
                    all.raster = &behind;
                    all.options.markFront = false;
                }
                NativeVideo::DrawFrame(all, drawn.data(), width);
                const uint32_t want = p == kStageFront ? 0xC0000000u : 0x80000000u;
                for (int line = 0; line < Video::kHeight; line++) {
                    keep(drawn.data(), std::clamp(line - shift, 0, Video::kHeight - 1), line, want, 0xC0000000u);
                }
            }
            const bool objects = std::any_of(index.begin(), index.end(), [p](uint8_t i) { return i == p; });
            if (objects || scrolls[p]) {
                NativeVideo::FrameData alone = frame;
                alone.options.layerMask = scrolls[p] | (objects ? 1u : 0u);
                alone.options.markObjects = true;
                alone.options.markLayers = scrolls[p];
                alone.options.markFront = false;
                alone.options.panelObject = nullptr;
                cps2recomp::native_video::Raster only = *frame.raster;
                size_t at = 0;
                // (KeepTiles goes through the tiles in order: each by its own place in `index`)
                only.KeepTiles([&](const cps2recomp::native_video::ObjectTile &) { return index[at++] == p; });
                alone.raster = &only;
                NativeVideo::DrawFrame(alone, drawn.data(), width);
                for (int line = 0; line < Video::kHeight; line++) {
                    keep(drawn.data(), line, line, 0x80000000u, 0x80000000u);
                }
            }
            out.push_back(std::move(one));
        }
    }

    // What the port draws over the picture (issue #398), the parts of it asked for (port/hud.h), by itself, as Port::Shown
    // draws it (a vertical game turned upright: the boxes on the board's picture, the text upright). DrawHud blends, so
    // it is drawn over black and over white: how much the white comes through is how much the pixel lets through, and
    // over black is its colour times how solid it is.
    void Overlay(int width, unsigned parts, const char *name, std::vector<SceneExport::Layer> &out) {
        const size_t size = static_cast<size_t>(width) * Video::kHeight;
        std::vector<uint32_t> over[2] = {std::vector<uint32_t>(size, 0xFF000000u), std::vector<uint32_t>(size, 0xFFFFFFFFu)};
        for (std::vector<uint32_t> &pixels : over) {
            DrawHud(pixels.data(), width, Video::kHeight, nullptr, parts & ~kHudText);
            if (!(parts & kHudText)) {
                continue;
            }
            if (!Turned()) {
                DrawHud(pixels.data(), width, Video::kHeight, nullptr, kHudText);
                continue;
            }
            std::vector<uint32_t> upright;
            TurnPicture(pixels.data(), width, upright);
            DrawHud(upright.data(), Video::kHeight, width, nullptr, kHudText);
            for (int y = 0; y < Video::kHeight; y++) {
                for (int x = 0; x < width; x++) {
                    pixels[static_cast<size_t>(y) * width + x] = upright[static_cast<size_t>(width - 1 - x) * Video::kHeight + y];
                }
            }
        }
        SceneExport::Layer one;
        one.name = name;
        one.board = "overlay";
        one.pixels.assign(size, 0);
        for (size_t n = 0; n < size; n++) {
            const uint32_t black = over[0][n], white = over[1][n];
            int through = 0;
            for (int shift = 0; shift < 24; shift += 8) {
                through = std::max(through, static_cast<int>((white >> shift) & 0xFF) - static_cast<int>((black >> shift) & 0xFF));
            }
            const int solid = 255 - std::clamp(through, 0, 255);
            if (solid == 0) {
                continue;
            }
            uint32_t pixel = static_cast<uint32_t>(solid) << 24;
            for (int shift = 0; shift < 24; shift += 8) {
                const int colour = (static_cast<int>((black >> shift) & 0xFF) * 255 + solid / 2) / solid;
                pixel |= static_cast<uint32_t>(std::min(colour, 255)) << shift;
            }
            one.pixels[n] = pixel;
            one.empty = false;
        }
        out.push_back(std::move(one));
    }

    // The overlays and the hit boxes' list (issue #398). A box whose player the game's sources do not know (something
    // thrown, in most games) goes with the player whose picture it covers more of (the parts', when there are any).
    void Overlays(SceneExport::Scene &scene) {
        const std::vector<cps2recomp::HudBox> &boxes = cps2recomp::Boxes();
        if (boxes.empty() && cps2recomp::Hud().empty()) {
            return;
        }
        Overlay(scene.width, kHudHitBoxes, "overlay-boxes", scene.overlays);
        Overlay(scene.width, kHudText | kHudDrawnBoxes, "overlay-hud", scene.overlays);
        const SceneExport::Layer *player[2] = {};
        for (const SceneExport::Layer &part : scene.parts) {
            if (part.name == "part-player1" || part.name == "part-player2") {
                player[part.name.back() - '1'] = &part;
            }
        }
        for (const cps2recomp::HudBox &box : boxes) {
            if (box.kind == cps2recomp::kHudDrawn || box.width <= 0 || box.height <= 0 || box.x >= scene.width ||
                box.y >= scene.height || box.x + box.width <= 0 || box.y + box.height <= 0) {
                continue;
            }
            SceneExport::Scene::Box one;
            one.player = box.player;
            one.kind = box.kind;
            one.thrown = box.thrown;
            one.x = box.x, one.y = box.y, one.width = box.width, one.height = box.height;
            one.colour = box.r | (box.g << 8) | (box.b << 16);
            one.fill = box.fill;
            if (one.player < 0 && player[0] && player[1]) {
                int covered[2] = {};
                for (int y = std::max(box.y, 0); y < std::min(box.y + box.height, scene.height); y++) {
                    for (int x = std::max(box.x, 0); x < std::min(box.x + box.width, scene.width); x++) {
                        for (int p = 0; p < 2; p++) {
                            covered[p] += player[p]->pixels[static_cast<size_t>(y) * scene.width + x] != 0;
                        }
                    }
                }
                one.player = covered[0] == covered[1] ? -1 : covered[1] > covered[0] ? 1 : 0;
            }
            scene.boxes.push_back(one);
        }
    }

    std::string BoxesJson(const SceneExport::Scene &scene) {
        static const char *const kKinds[] = {"", "hurt", "attack", "push", "throw", "throw-range", "body", "shot", "ship"};
        std::string json;
        for (const SceneExport::Scene::Box &box : scene.boxes) {
            json += std::string(json.empty() ? "" : ", ") + "{\"player\": " + (box.player < 0 ? "null" : std::to_string(box.player + 1)) +
                    ", \"kind\": \"" + (box.kind < std::size(kKinds) ? kKinds[box.kind] : "") + "\"" + (box.thrown ? ", \"thrown\": true" : "") +
                    ", \"x\": " + std::to_string(box.x) + ", \"y\": " + std::to_string(box.y) + ", \"width\": " + std::to_string(box.width) +
                    ", \"height\": " + std::to_string(box.height) + ", \"colour\": " + Quoted(Hex(box.colour)) +
                    ", \"fill\": " + std::to_string(box.fill) + "}";
        }
        return "[" + json + "]";
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
        for (const auto *list : {&scene.layers, &scene.parts, &scene.overlays}) {
            for (const SceneExport::Layer &layer : *list) {
                if (!layer.empty) {
                    written = image_files::WritePng(path(layer.name), AsPicture(layer.pixels, scene.width, scene.height)) && written;
                }
            }
        }
        return written;
    }

    std::string LayerJson(const SceneExport::Layer &layer, bool still) {
        if (layer.board == "part" || layer.board == "overlay") { // (a part of the picture by what is in it, or what is drawn
                                                                 // over it: its name, and its file or folder)
            return "{\"name\": " + Quoted(layer.name) + ", " +
                   (still ? "\"file\": " + (layer.empty ? std::string("null") : Quoted(layer.name + ".png")) : "\"folder\": " + Quoted(layer.name)) +
                   "}";
        }
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
    if (!machine.nativeVideo.ShownFrame(frame)) {
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
    scene.parts.clear();
    // (another game's stage under the game's: its layers first, behind the game's)
    NativeVideo::FrameData stage;
    bool guest = false;
    if (machine.guestBehind) {
        if (Machine *board = cps2recomp::guest_stages::ShownBoard()) {
            if (board->nativeVideo.LastFrame(stage) && Video::kWidth + 2 * stage.options.side == width) {
                scene.backdrop = stage.raster->Colours()[0xBFF] & 0xFFFFFF;
                BoardLayers(stage, width, "stage-", "stage", machine.guestShift, scene.layers);
                guest = true;
            }
        }
    }
    // (the endless battle's stage, which a machine of its own draws under the fighters: as another game's stage is)
    if (!guest && battle::StageBehind(stage) && Video::kWidth + 2 * stage.options.side == width) {
        scene.backdrop = stage.raster->Colours()[0xBFF] & 0xFFFFFF;
        BoardLayers(stage, width, "stage-", "stage", 0, scene.layers);
        guest = true;
    }
    BoardLayers(frame, width, "", "game", 0, scene.layers);
    BoardParts(frame, width, guest ? &stage : nullptr, guest && machine.guestBehind ? machine.guestShift : 0, scene.parts);
    scene.overlays.clear();
    scene.boxes.clear();
    Overlays(scene);
    return true;
}

SceneExport::~SceneExport() {
    if (m_stageWriter.joinable()) {
        m_stageWriter.join();
    }
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
    json += "  ]";
    if (!scene.parts.empty()) {
        json += ",\n  \"parts\": [\n";
        for (size_t n = 0; n < scene.parts.size(); n++) {
            json += "    " + LayerJson(scene.parts[n], true) + (n + 1 < scene.parts.size() ? ",\n" : "\n");
        }
        json += "  ]";
    }
    if (!scene.overlays.empty()) {
        json += ",\n  \"overlays\": [\n";
        for (size_t n = 0; n < scene.overlays.size(); n++) {
            json += "    " + LayerJson(scene.overlays[n], true) + (n + 1 < scene.overlays.size() ? ",\n" : "\n");
        }
        json += "  ],\n  \"boxes\": " + BoxesJson(scene);
    }
    json += "\n}\n";
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
    m_clip.missed = 0;
    m_clip.names.clear();
    m_clip.json.clear();
    m_clip.partNames.clear();
    m_clip.partsJson.clear();
    m_clip.overlayNames.clear();
    m_clip.overlaysJson.clear();
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
        // (no records this frame: a frame of the clip fewer; none for as long as the clip was to be, and it ends)
        if (++m_clip.missed >= m_clip.wanted) {
            StopClip();
        }
        return;
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
    std::string parts; // (the parts by what is in the picture, issue #367: those this frame has)
    for (const Layer &part : scene.parts) {
        if (std::find(m_clip.partNames.begin(), m_clip.partNames.end(), part.name) == m_clip.partNames.end()) {
            m_clip.partNames.push_back(part.name);
            m_clip.partsJson += std::string(m_clip.partsJson.empty() ? "" : "\x1F") + LayerJson(part, false);
        }
        if (!part.empty) {
            parts += std::string(parts.empty() ? "" : ", ") + Quoted(part.name);
        }
    }
    std::string overlays; // (what is drawn over the picture, issue #398: those this frame has)
    for (const Layer &overlay : scene.overlays) {
        if (std::find(m_clip.overlayNames.begin(), m_clip.overlayNames.end(), overlay.name) == m_clip.overlayNames.end()) {
            m_clip.overlayNames.push_back(overlay.name);
            m_clip.overlaysJson += std::string(m_clip.overlaysJson.empty() ? "" : "\x1F") + LayerJson(overlay, false);
        }
        if (!overlay.empty) {
            overlays += std::string(overlays.empty() ? "" : ", ") + Quoted(overlay.name);
        }
    }
    m_clip.taken++;
    job.folder = m_clip.folder;
    job.number = m_clip.taken;
    m_frameJson += std::string(m_frameJson.empty() ? "" : ",\n") + "    {\"number\": " + std::to_string(job.number) +
                   ", \"frame\": " + std::to_string(scene.frame) + ", \"backdrop\": " + Quoted(Hex(scene.backdrop)) +
                   ", \"layers\": [" + order + "]" + (m_clip.partNames.empty() ? "" : ", \"parts\": [" + parts + "]") +
                   (scene.overlays.empty() ? "" : ", \"overlays\": [" + overlays + "], \"boxes\": " + BoxesJson(scene)) + "}";
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
    auto list = [](const std::string &joined) {
        std::string out;
        for (size_t at = 0; at <= joined.size();) {
            const size_t end = std::min(joined.find('\x1F', at), joined.size());
            out += "    " + joined.substr(at, end - at) + (end < joined.size() ? ",\n" : "\n");
            at = end + 1;
        }
        return out;
    };
    const std::string layers = list(m_clip.json);
    const std::string parts = (m_clip.partsJson.empty() ? "" : ",\n  \"parts\": [\n" + list(m_clip.partsJson) + "  ]") +
                              (m_clip.overlaysJson.empty() ? "" : ",\n  \"overlays\": [\n" + list(m_clip.overlaysJson) + "  ]");
    std::string json = "{\n  \"kind\": \"clip\",\n  \"width\": " + std::to_string(m_width) + ",\n  \"height\": " +
                       std::to_string(m_height) + ",\n  \"fps\": 59.637405,\n  \"frames\": " + std::to_string(m_clip.taken) +
                       ",\n  \"picture\": \"picture\",\n  \"layers\": [\n" + (m_clip.taken ? layers : "") +
                       "  ]" + parts + ",\n  \"frameList\": [\n" + m_frameJson + "\n  ]\n}\n";
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

// --- the whole stage (StageStitch) ---

void StageStitch::Begin(int camera) {
    (void)camera;
    m_layers.clear();
    m_begun = true;
    m_frames = 0;
}

void StageStitch::Add(Machine &machine, int camera) {
    if (!m_begun) {
        return;
    }
    SceneExport::Scene scene;
    if (!SceneExport::Take(machine, scene)) {
        return;
    }
    m_width = scene.width;
    m_backdrop = scene.backdrop;
    m_frames++;
    const int width = scene.width, height = scene.height, side = (width - Video::kWidth) / 2;
    int behindX = 0, behindY = 0; // (how far the scroll screen behind an objects layer moved this frame, at the middle line)
    for (const SceneExport::Layer &layer : scene.layers) {
        auto found = std::find_if(m_layers.begin(), m_layers.end(), [&](const Canvas &c) { return c.name == layer.name; });
        if (found == m_layers.end()) {
            Canvas canvas;
            canvas.name = layer.name;
            canvas.board = layer.board;
            canvas.which = layer.which;
            canvas.levels = layer.levels;
            canvas.pixels.assign(static_cast<size_t>(kWidth) * kHeight, 0);
            canvas.seen.assign(canvas.pixels.size(), 0);
            m_layers.push_back(std::move(canvas));
            found = m_layers.end() - 1;
        }
        Canvas &c = *found;
        // How far it moved since the frame before: a scroll screen by its place in its map (on each line, wrapped as its
        // map is); objects by their picture against the frame before's
        std::vector<int> dx(height, 0), dy(height, 0);
        if (layer.which) {
            const int mask = (64 << (2 + layer.which)) - 1;
            auto unwrap = [mask](int now, int before) {
                int d = (now - before) & mask;
                return d > mask / 2 ? d - mask - 1 : d;
            };
            if (!c.started) {
                c.lastX = layer.mapX;
                c.lastY = layer.mapY;
                c.movedX.assign(height, 0);
                c.movedY.assign(height, 0);
            }
            for (int line = 0; line < height; line++) {
                dx[line] = unwrap(layer.mapX[line], c.lastX[line]);
                dy[line] = unwrap(layer.mapY[line] - line, c.lastY[line] - line);
                c.movedX[line] += dx[line];
                c.movedY[line] += dy[line];
            }
            c.lastX = layer.mapX;
            c.lastY = layer.mapY;
            behindX = dx[height / 2];
            behindY = dy[height / 2];
        } else if (c.started && !layer.empty) {
            // (the move that matches its pixels best with the frame before's, on every other line; too little alike: the
            // scroll screen's behind it)
            constexpr int kReach = 16;
            int best = behindX, bestScore = 0;
            for (int d = -kReach; d <= kReach; d++) {
                int same = 0, other = 0;
                for (int line = 0; line < height; line += 2) {
                    const uint32_t *now = &layer.pixels[static_cast<size_t>(line) * width];
                    const uint32_t *before = &c.last[static_cast<size_t>(line) * width];
                    for (int x = std::max(0, -d); x < width && x + d < width; x++) {
                        if (now[x] && before[x + d]) {
                            (now[x] == before[x + d] ? same : other)++;
                        }
                    }
                }
                if (same > 64 && same - other > bestScore) {
                    bestScore = same - other;
                    best = d;
                }
            }
            c.offX += best;
            c.offY += behindY;
        } else if (c.started) {
            c.offX += behindX;
            c.offY += behindY;
        }
        if (!layer.which) {
            c.last = layer.pixels;
        }
        if (!c.started) {
            c.started = true;
            c.firstCamera = camera;
        }
        c.lastCamera = camera;
        // Its pixels put in, as first seen (the board's own columns before the added ones)
        for (int line = 0; line < height; line++) {
            const int atY = kOrigin + line + (layer.which ? c.movedY[line] : c.offY);
            if (atY < 0 || atY >= kHeight) {
                continue;
            }
            const int moved = kOrigin + (layer.which ? c.movedX[line] : c.offX);
            for (int x = 0; x < width; x++) {
                const int atX = moved + x;
                if (atX < 0 || atX >= kWidth) {
                    continue;
                }
                const size_t at = static_cast<size_t>(atY) * kWidth + atX;
                const uint8_t quality = x >= side && x < side + Video::kWidth ? 2 : 1;
                if (quality > c.seen[at]) {
                    c.seen[at] = quality;
                    c.pixels[at] = layer.pixels[static_cast<size_t>(line) * width + x];
                }
            }
        }
    }
}

std::string StageStitch::Write(const std::string &under, const std::string &name, const std::string &stage) {
    m_begun = false;
    if (m_frames == 0 || m_layers.empty()) {
        m_layers.clear();
        return "WHOLE STAGE NOT SAVED";
    }
    // Each layer's pace, and where it goes in the picture put together (the camera in the middle of its way)
    struct Placed {
        int left = kWidth, top = kHeight, right = -1, bottom = -1; // (its pixels in its canvas)
        double pace = 1;
        int shift = 0;
    };
    std::vector<Placed> placed(m_layers.size());
    int left = 1 << 20, top = 1 << 20, right = -(1 << 20), bottom = -(1 << 20);
    for (size_t n = 0; n < m_layers.size(); n++) {
        const Canvas &c = m_layers[n];
        Placed &p = placed[n];
        for (int y = 0; y < kHeight; y++) {
            for (int x = 0; x < kWidth; x++) {
                if (c.pixels[static_cast<size_t>(y) * kWidth + x]) {
                    p.left = std::min(p.left, x);
                    p.right = std::max(p.right, x);
                    p.top = std::min(p.top, y);
                    p.bottom = std::max(p.bottom, y);
                }
            }
        }
        const int camera = c.lastCamera - c.firstCamera;
        const int moved = c.which ? c.movedX[Video::kHeight / 2] : c.offX;
        p.pace = camera ? static_cast<double>(moved) / camera : 1.0;
        p.shift = static_cast<int>(std::lround((1.0 - p.pace) * camera / 2.0));
        if (p.right >= 0) {
            left = std::min(left, p.left + p.shift);
            right = std::max(right, p.right + p.shift);
            top = std::min(top, p.top);
            bottom = std::max(bottom, p.bottom);
        }
    }
    if (right < left) {
        m_layers.clear();
        return "WHOLE STAGE NOT SAVED";
    }
    const std::string folder = NewFolder(under, name);
    if (folder.empty()) {
        m_layers.clear();
        return "WHOLE STAGE NOT SAVED";
    }
    const int across = right - left + 1, down = bottom - top + 1;
    image_files::Picture whole;
    whole.width = across;
    whole.height = down;
    whole.pixels.assign(static_cast<size_t>(across) * down, m_backdrop | 0xFF000000u);
    bool written = true;
    std::string layers;
    for (size_t n = 0; n < m_layers.size(); n++) {
        const Canvas &c = m_layers[n];
        const Placed &p = placed[n];
        SceneExport::Layer described;
        described.name = c.name;
        described.board = c.board;
        described.which = c.which;
        described.levels = c.levels;
        described.empty = p.right < 0;
        std::string json = LayerJson(described, true);
        json.pop_back();
        if (p.right >= 0) {
            image_files::Picture one;
            one.width = p.right - p.left + 1;
            one.height = p.bottom - p.top + 1;
            one.pixels.resize(static_cast<size_t>(one.width) * one.height);
            for (int y = 0; y < one.height; y++) {
                for (int x = 0; x < one.width; x++) {
                    const uint32_t pixel = c.pixels[static_cast<size_t>(p.top + y) * kWidth + p.left + x];
                    one.pixels[static_cast<size_t>(y) * one.width + x] = pixel;
                    if (pixel) {
                        whole.pixels[static_cast<size_t>(p.top + y - top) * across + (p.left + p.shift + x - left)] = pixel;
                    }
                }
            }
            written = image_files::WritePng(folder + c.name + ".png", one) && written;
            char place[160];
            snprintf(place, sizeof(place), ", \"x\": %d, \"y\": %d, \"width\": %d, \"height\": %d, \"pace\": %.3f",
                     p.left + p.shift - left, p.top - top, one.width, one.height, p.pace);
            json += place;
        }
        layers += "    " + json + "}" + (n + 1 < m_layers.size() ? ",\n" : "\n");
    }
    written = image_files::WritePng(folder + "stage.png", whole) && written;
    const std::string json = "{\n  \"kind\": \"stage\",\n  \"stage\": " + Quoted(stage) + ",\n  \"width\": " + std::to_string(across) +
                             ",\n  \"height\": " + std::to_string(down) + ",\n  \"backdrop\": " + Quoted(Hex(m_backdrop)) +
                             ",\n  \"picture\": \"stage.png\",\n  \"frames\": " + std::to_string(m_frames) + ",\n  \"layers\": [\n" +
                             layers + "  ]\n}\n";
    written = image_files::WriteAll(folder + "stage.json", std::vector<uint8_t>(json.begin(), json.end())) && written;
    m_layers.clear();
    fprintf(stderr, "stage: %s (%d frames)\n", folder.c_str(), m_frames);
    if (!written) {
        return "WHOLE STAGE NOT SAVED";
    }
    const size_t at = folder.find("exports/");
    return "WHOLE STAGE SAVED  " + (at == std::string::npos ? folder : folder.substr(at));
}

bool SceneExport::WriteStage(StageStitch &&stitch, const std::string &under, const std::string &name, const std::string &stage) {
    if (m_stageWriting) {
        return false;
    }
    if (m_stageWriter.joinable()) {
        m_stageWriter.join();
    }
    m_stageWriting = true;
    m_stageWriter = std::thread([this, stitch = std::move(stitch), under, name, stage]() mutable {
        const std::string said = stitch.Write(under, name, stage);
        std::lock_guard<std::mutex> lock(m_lock);
        m_said = said;
        m_saying = true;
        m_stageWriting = false;
    });
    return true;
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
