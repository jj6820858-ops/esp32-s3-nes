#include <assert.h>
#include <stdio.h>
#include "nes/nes.h"

int main(void)
{
    nes_t *nes=nes_init(SYS_DETECT,32000,true,NULL);
    assert(nes && nes_loadfile("sdcard/roms/nes/SZPI-Controls.nes")==0);
    static uint8_t frame[NES_SCREEN_PITCH*NES_SCREEN_HEIGHT];
    nes_setvidbuf(frame);
    for (int i=0;i<12;i++) nes_emulate(true);
    assert(nes->mem->ram[0x203]==128 && nes->mem->ram[0x200]==112);
    input_update(0,NES_PAD_RIGHT|NES_PAD_A);
    for (int i=0;i<10;i++) nes_emulate(true);
    assert(nes->mem->ram[0x203]>128);
    bool audio=false;
    for (int i=0;i<nes->apu->samples_per_frame*2;i++) audio|=nes->apu->buffer[i]!=0;
    assert(audio);
    input_update(0,NES_PAD_B); nes_emulate(true);
    assert((nes->ppu->palette[0x11]&0x3f)==0x2a);
    unsigned different=0;
    for (size_t i=1;i<sizeof(frame);i++) different+=frame[i]!=frame[0];
    assert(different>50);
    FILE *out=fopen("logs/nes-demo-frame.raw","wb");
    assert(out); fwrite(frame,1,sizeof(frame),out); fclose(out);
    puts("NES demo: ROM boot, movement, APU audio, B palette and video frame passed");
    return 0;
}
