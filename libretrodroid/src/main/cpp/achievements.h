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

#ifndef LIBRETRODROID_ACHIEVEMENTS_H
#define LIBRETRODROID_ACHIEVEMENTS_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "rcheevos/include/rc_runtime.h"
#include "rcheevos/src/rc_libretro.h"

namespace libretrodroid {

/**
 * RetroAchievements condition evaluation, using rcheevos.
 *
 * ### Why this is here and not in the app
 *
 * The condition language is not a small thing to reimplement. Surveyed across 453
 * achievements and eight consoles: `AddAddress` — pointer indirection — is 40% of
 * all conditions, `AndNext` 23%, and 35% of achievements use alternate groups,
 * with hit counts, deltas, `PauseIf`, `ResetIf` and `Measured` making up most of
 * the rest. That is the whole language, and a subtly wrong reimplementation either
 * withholds achievements somebody earned or awards ones they did not — on their
 * real account. So the reference implementation does the work.
 *
 * It lives in this library rather than in the app because this is where the
 * emulation thread and the core's memory pointers already are. Evaluation happens
 * once per frame, between `retro_run` calls, in C: no JNI on the hot path, and the
 * app needs no native build of its own.
 *
 * ### Addresses are console addresses
 *
 * A RetroAchievements address is in the *console's* space, not the core's. Mega
 * Drive counts from 0 while the core's work RAM sits at 0xFF0000; Game Boy and
 * SNES have several regions and no single base at all. `rc_libretro_memory_init`
 * does that translation, given the core's memory map when it publishes one and a
 * per-console fallback layout when it does not — and cores really do omit it:
 * genesis_plus_gx publishes no map for Game Gear, so the fallback is not a rare
 * path.
 *
 * ### What this deliberately does not do
 *
 * No networking, no accounts, no hardcore. The app already talks to the service,
 * and this reports triggered ids for it to submit. Nothing here writes to memory
 * either — reading is an achievement, writing is a cheat engine.
 */
class Achievements {
public:
    static Achievements& getInstance();

    Achievements(Achievements const&) = delete;
    void operator=(Achievements const&) = delete;

    /**
     * Activates a set for the running game.
     *
     * `consoleId` is RetroAchievements' own console numbering, which the app gets
     * from the same request as the definitions. Returns the number of
     * achievements that were accepted: a definition rcheevos cannot parse is
     * skipped rather than failing the set, because one unparseable achievement in
     * a set of forty should cost that one achievement.
     */
    int start(uint32_t consoleId, const std::vector<std::pair<uint32_t, std::string>>& definitions);

    /** Drops the set and its memory mapping. Safe to call when nothing is loaded. */
    void stop();

    /**
     * One frame of evaluation, called from the emulation thread after the core
     * has run. Does nothing at all when no set is loaded, which is the common
     * case and must stay free.
     */
    void doFrame();

    /**
     * Ids triggered since the last call, and clears them.
     *
     * Drained rather than pushed, because the alternative is calling into Java
     * from the emulation thread — which would mean attaching the thread to the VM
     * and blocking emulation on whatever the callback does.
     */
    std::vector<uint32_t> takeTriggered();

    /** For the app to report what actually loaded. */
    bool isActive() const { return active; }
    uint32_t regionCount() const { return regions.count; }
    size_t mappedBytes() const { return regions.total_size; }

    void recordTriggered(uint32_t id);
    uint32_t peek(uint32_t address, uint32_t numBytes);

private:
    Achievements() = default;

    /** [stop] with the lock already held, so start can reuse it. */
    void stopLocked();

    bool active = false;
    bool runtimeInitialised = false;
    rc_runtime_t runtime {};
    rc_libretro_memory_regions_t regions {};

    // Guards the triggered list, which the emulation thread appends to and a
    // Java-side thread drains. The runtime itself is only ever touched from the
    // emulation thread, apart from start/stop, which take it too.
    std::mutex lock;
    std::vector<uint32_t> triggered;
};

} // namespace libretrodroid

#endif //LIBRETRODROID_ACHIEVEMENTS_H
