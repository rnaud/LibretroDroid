package com.swordfish.libretrodroid;

/**
 * One entry of the core's memory map, as published through
 * {@code RETRO_ENVIRONMENT_SET_MEMORY_MAPS}.
 *
 * <p>Exposed because a RetroAchievements address is in a <em>console</em> address
 * space rather than a core one, and this map is the only thing that can translate
 * between them. A core that publishes no map leaves
 * {@code RETRO_MEMORY_SYSTEM_RAM} as the only thing to read.
 *
 * <p>Fields are {@code long} rather than {@code int} because {@code flags} and the
 * address fields are 64-bit on the native side and a 32-bit truncation here would
 * be silent.
 */
public class MemoryDescriptor {

    public final long flags;
    public final long offset;
    public final long start;
    public final long select;
    public final long disconnect;
    public final long length;
    public final String addressSpace;

    public MemoryDescriptor(
        long flags,
        long offset,
        long start,
        long select,
        long disconnect,
        long length,
        String addressSpace
    ) {
        this.flags = flags;
        this.offset = offset;
        this.start = start;
        this.select = select;
        this.disconnect = disconnect;
        this.length = length;
        this.addressSpace = addressSpace;
    }
}
