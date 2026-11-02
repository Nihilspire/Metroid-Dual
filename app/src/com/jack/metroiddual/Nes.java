package com.jack.metroiddual;

/** NES constants: master palette, libretro joypad bits, Metroid RAM/ROM addresses. */
final class Nes {
    private Nes() {}
    static final int[] PALETTE = {
        0x747474,0x24188C,0x0000A8,0x44009C,0x8C0074,0xA80010,0xA40000,0x7C0800,0x402C00,0x004400,0x005000,0x003C14,0x183C5C,0x000000,0x000000,0x000000,
        0xBCBCBC,0x0070EC,0x2038EC,0x8000F0,0xBC00BC,0xE40058,0xD82800,0xC84C0C,0x887000,0x009400,0x00A800,0x009038,0x008088,0x000000,0x000000,0x000000,
        0xFCFCFC,0x3CBCFC,0x5C94FC,0xCC88FC,0xF478FC,0xFC74B4,0xFC7460,0xFC9838,0xF0BC3C,0x80D010,0x4CDC48,0x58F898,0x00E8D8,0x787878,0x000000,0x000000,
        0xFCFCFC,0xA8E4FC,0xC4D4FC,0xD4C8FC,0xFCC4FC,0xFCC4D8,0xFCBCB0,0xFCD8A8,0xFCE4A0,0xE0FCA0,0xA8F0BC,0xB0FCCC,0x9CFCF0,0xC4C4C4,0x000000,0x000000 };
    static int color(int idx) { return 0xFF000000 | PALETTE[idx & 0x3F]; }

    // RETRO_DEVICE_ID_JOYPAD_* bits
    static final int B = 1, Y = 1 << 1, SELECT = 1 << 2, START = 1 << 3, UP = 1 << 4, DOWN = 1 << 5,
            LEFT = 1 << 6, RIGHT = 1 << 7, A = 1 << 8;
    static final String[] BUTTON_NAMES = { "UP", "DOWN", "LEFT", "RIGHT", "B", "A", "SELECT", "START", "TURBO B", "TURBO A", "MENU", "FAST", "SAVE", "LOAD", "3D VIEW", "3D DEPTH +", "3D DEPTH -" };
    static final int[] BUTTON_BITS = { UP, DOWN, LEFT, RIGHT, B, A, SELECT, START, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    // Metroid (USA) RAM
    static final int GAME_MODE = 0x1D, MAIN_ROUTINE = 0x1E, MAP_Y = 0x4F, MAP_X = 0x50, IN_AREA = 0x74,
            HEALTH_LO = 0x106, HEALTH_HI = 0x107, TANKS = 0x6877, GEAR = 0x6878, MISSILES = 0x6879, MAX_MISSILES = 0x687A,
            JUST_IN_BAILEY = 0x69B3;
    // ROM file offsets (iNES header included)
    static final int ROM_FONT = 0x1B4D0, ROM_SPRITES = 0x18010, ROM_WORLDMAP = 0x254E, ROM_ITEMDATA = 0x1039;
    static final String[] AREA_NAMES = { "BRINSTAR", "NORFAIR", "KRAID", "TOURIAN", "RIDLEY" };

    static boolean inGame() {
        return Native.peek(GAME_MODE) == 0 && Native.peek(MAIN_ROUTINE) == 3;
    }
}
