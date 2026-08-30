/*
 *     Copyright (C) 2025  Filippo Scognamiglio
 *     https://github.com/Swordfish90/LibretroDroid
 *
 *     LibretroDroid is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, either version 3 of the License, or
 *     (at your option) any later version.
 *
 *     LibretroDroid is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with LibretroDroid.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "achievements.h"

#include "libretrodroid.h"
#include "log.h"

namespace libretrodroid {

Achievements& Achievements::getInstance() {
    static Achievements instance;
    return instance;
}

/**
 * Where rcheevos gets its bytes.
 *
 * `rc_libretro_memory_find` returns a pointer into the core's own RAM for a
 * console address, or null when nothing is mapped there. Null is normal — a set
 * can reference a region this core does not expose — and rcheevos expects zero
 * rather than a refusal, so an unmapped read is a zero.
 *
 * Little-endian assembly regardless of the console: the *achievement* names the
 * byte order it wants through its operand size, so a reader that swapped based on
 * the console would be swapping twice.
 */
uint32_t Achievements::peek(uint32_t address, uint32_t numBytes) {
    uint8_t buffer[4] = { 0, 0, 0, 0 };
    uint32_t read = rc_libretro_memory_read(&regions, address, buffer, numBytes);
    if (read == 0) {
        return 0;
    }

    switch (numBytes) {
        case 1:
            return buffer[0];
        case 2:
            return buffer[0] | ((uint32_t) buffer[1] << 8);
        case 3:
            return buffer[0] | ((uint32_t) buffer[1] << 8) | ((uint32_t) buffer[2] << 16);
        case 4:
            return buffer[0] | ((uint32_t) buffer[1] << 8) | ((uint32_t) buffer[2] << 16)
                | ((uint32_t) buffer[3] << 24);
        default:
            return 0;
    }
}

void Achievements::recordTriggered(uint32_t id) {
    std::lock_guard<std::mutex> guard(lock);
    triggered.push_back(id);
    LOGI("Achievement %u triggered", id);
}

static uint32_t peekCallback(uint32_t address, uint32_t numBytes, void* ud) {
    return Achievements::getInstance().peek(address, numBytes);
}

/**
 * rcheevos' event handler takes no user data, hence the singleton.
 *
 * Only `TRIGGERED` is acted on. The rest — primed, paused, reset — are for a UI
 * that shows progress as it happens, which this does not have; ignoring them costs
 * nothing because rcheevos keeps the state itself either way.
 */
static void eventCallback(const rc_runtime_event_t* event) {
    if (event == nullptr) {
        return;
    }
    if (event->type == RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED) {
        Achievements::getInstance().recordTriggered(event->id);
    }
}

/**
 * Answers "where is RETRO_MEMORY_x", for the case where the core published no map.
 *
 * rcheevos asks this and then lays out the console's known regions across whatever
 * comes back. Routed through LibretroDroid rather than touching the core directly
 * so there is one place that knows whether a core is even loaded.
 */
static void coreMemoryInfo(uint32_t id, rc_libretro_core_memory_info_t* info) {
    auto [data, size] = LibretroDroid::getInstance().getMemoryRegion(id);
    info->data = (uint8_t*) data;
    info->size = size;
}

int Achievements::start(
    uint32_t consoleId,
    const std::vector<std::pair<uint32_t, std::string>>& definitions
) {
    std::lock_guard<std::mutex> guard(lock);

    stopLocked();

    // The core's map when it has one. An empty map is passed as null, which is
    // what tells rcheevos to fall back to the per-console layout rather than
    // trying to honour a map with no entries in it.
    auto& descriptors = LibretroDroid::getInstance().getMemoryMap();
    std::vector<struct retro_memory_descriptor> retroDescriptors;
    retroDescriptors.reserve(descriptors.size());
    for (auto& descriptor : descriptors) {
        struct retro_memory_descriptor converted {};
        converted.flags = descriptor.flags;
        converted.ptr = descriptor.pointer;
        converted.offset = descriptor.offset;
        converted.start = descriptor.start;
        converted.select = descriptor.select;
        converted.disconnect = descriptor.disconnect;
        converted.len = descriptor.length;
        converted.addrspace = nullptr;
        retroDescriptors.push_back(converted);
    }

    struct retro_memory_map map {};
    map.descriptors = retroDescriptors.data();
    map.num_descriptors = (unsigned) retroDescriptors.size();

    int mapped = rc_libretro_memory_init(
        &regions,
        retroDescriptors.empty() ? nullptr : &map,
        coreMemoryInfo,
        consoleId
    );
    if (!mapped || regions.total_size == 0) {
        LOGE("Could not map memory for console %u; achievements stay inactive", consoleId);
        rc_libretro_memory_destroy(&regions);
        regions = {};
        return 0;
    }

    rc_runtime_init(&runtime);
    runtimeInitialised = true;

    int accepted = 0;
    for (auto& definition : definitions) {
        int result = rc_runtime_activate_achievement(
            &runtime,
            definition.first,
            definition.second.c_str(),
            nullptr,
            0
        );
        if (result == RC_OK) {
            accepted++;
        } else {
            // One definition this build of rcheevos cannot parse should cost that
            // achievement, not the set.
            LOGE("Achievement %u rejected: %s", definition.first, rc_error_str(result));
        }
    }

    active = accepted > 0;
    LOGI(
        "Achievements active for console %u: %d of %zu accepted, %u regions, %zu bytes mapped",
        consoleId, accepted, definitions.size(), regions.count, regions.total_size
    );

    if (!active) {
        stopLocked();
    }

    return accepted;
}

void Achievements::stop() {
    std::lock_guard<std::mutex> guard(lock);
    stopLocked();
}

void Achievements::stopLocked() {
    if (runtimeInitialised) {
        rc_runtime_destroy(&runtime);
        runtimeInitialised = false;
    }
    rc_libretro_memory_destroy(&regions);
    regions = {};
    triggered.clear();
    active = false;
}

void Achievements::doFrame() {
    // Read without the lock: this runs every frame on the emulation thread, and
    // `active` only changes under start/stop, which are rare and idempotent from
    // this side. Taking a mutex sixty times a second to read one bool is the kind
    // of cost that does not belong in a step loop.
    if (!active) {
        return;
    }
    rc_runtime_do_frame(&runtime, eventCallback, peekCallback, this, nullptr);
}

std::vector<uint32_t> Achievements::takeTriggered() {
    std::lock_guard<std::mutex> guard(lock);
    std::vector<uint32_t> result;
    result.swap(triggered);
    return result;
}

} // namespace libretrodroid
