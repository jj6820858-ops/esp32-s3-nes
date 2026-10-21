#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rg_input.h"
#include "szpi_touch.h"

static void point(uint8_t *packet, int n, int x, int y, int event)
{
    int raw_x = 240 - y, raw_y = x;
    uint8_t *p = packet + 1 + n * 6;
    p[0] = (event << 6) | (raw_x >> 8); p[1] = raw_x;
    p[2] = (n << 4) | (raw_y >> 8); p[3] = raw_y;
}

int main(void)
{
    uint8_t packet[13] = {2};
    point(packet, 0, 90, 190, 2);  /* Hold right. */
    point(packet, 1, 290, 182, 0); /* Press A while moving. */
    assert(szpi_touch_decode(packet, sizeof(packet)) == (RG_KEY_RIGHT | RG_KEY_A));
    point(packet, 1, 290, 182, 1); /* Released points must not stick. */
    assert(szpi_touch_decode(packet, sizeof(packet)) == RG_KEY_RIGHT);
    packet[0] = 0;
    assert(szpi_touch_decode(packet, sizeof(packet)) == 0);
    packet[0] = 3;
    assert(szpi_touch_decode(packet, sizeof(packet)) == 0);
    assert(szpi_touch_decode(packet, 7) == 0);
    assert(szpi_touch_decode(NULL, 0) == 0);
    memset(packet, 0, sizeof(packet)); packet[0] = 2;
    point(packet, 0, 16, 190, 0); point(packet, 1, 90, 190, 0);
    assert(szpi_touch_decode(packet, sizeof(packet)) == 0); /* Opposite directions. */
    assert(szpi_touch_keys_at(10, 150) == (RG_KEY_UP | RG_KEY_LEFT));
    assert(szpi_touch_keys_at(55, 190) == 0); /* D-pad dead zone. */
    assert(szpi_touch_keys_at(128, 228) == RG_KEY_SELECT);
    assert(szpi_touch_keys_at(180, 228) == RG_KEY_START);
    assert(szpi_touch_keys_at(320, 180) == 0);
    assert(szpi_touch_keys_at(-1, 180) == 0);
    memset(packet, 0, sizeof(packet)); packet[0] = 1;
    point(packet, 0, 290, 182, 0); packet[1] = 0x0f; packet[2] = 0xff;
    assert(szpi_touch_decode(packet, sizeof(packet)) == 0); /* Bad coordinates. */
    uint16_t outside[8], before[8];
    for (int i = 0; i < 8; i++) outside[i] = before[i] = 0x1234;
    szpi_touch_overlay_row(outside, 120, 100, 8);
    assert(memcmp(outside, before, sizeof(outside)) == 0);
    uint16_t guarded[10] = {0}; guarded[0] = 0x1234; guarded[9] = 0x5678;
    for (int y = 0; y < 240; ++y)
        szpi_touch_overlay_row(guarded + 1, 8, y, 8);
    assert(guarded[0] == 0x1234 && guarded[9] == 0x5678);
    puts("SZPI touch: dual touch, release, transform, bounds and overlay tests passed");
    return 0;
}
