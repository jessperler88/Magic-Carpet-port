# Round 4, region D (0x4c000-0x80000): stereo page, CPU detect, FLI file player, Watcom CRT stubs, HMI/OPL/"GUS" driver internals

All 13 functions in todo4_D.txt were read (decompiled C, capstone disassembly, callers from carpet_calls.csv,
plus a byte scan of the flat image for rel32 call/jmp targets and dword data references where the export shows
no callers). No name keeps the UNCERTAIN marker. The main new fact: the `snd_gus_*` driver (HMI device 0xa008)
is not a Gravis UltraSound driver but the Creative AWE32 EMU8000 driver (evidence below); the four driver
functions in this list are therefore renamed `snd_awe32_*`.

## CSV

```csv
0x000503d0,vfx1_vip_stereo_leave_503d0,"VFX1 VIP card register write on leaving the 3D view in interlaced-stereo 640 mode: out 302h=2, out 303h=1 (VIP default base 300h+2/+3; opt_interlaced is set only when vfx1_init_4fdc0 succeeds in config_parse_33750 / fe_config_apply_input_device_59330), then clears the back buffer DAT_0012ed74 (DAT_0012ed70*DAT_0012ed78 bytes) and blits it (vga_copy_320x200_610f0 if DAT_0012edae&1 else vesa_copy_banked_4f974); only caller mapmode_palette_save_30350 (mode_3d && opt_interlaced && pitch DAT_0009b5fc==0x280); sibling stereo_page_blank_50370 writes 303h=10h on entry from stereo_mode_enter_2ff50"
0x0005ac80,cpu_detect_5ac80,"Runs cpu_detect_all_5ace8 (cpu_identify_5ad03 + cpu_detect_fpu_5adfe), copies family/FPU/vendor/model/stepping bytes DAT_0009e59c..a0 into an unused 5-byte local, then g_cfg->pentium (cfg+8) = (family DAT_0009e59c == 5); callers config_parse_33750 and fe_screen_intel_logo_56510"
0x0005c594,fli_file_apply_color256_5c594,"File-stream FLI player: called once per frame from the asm player loop at 0x5c264 (not a Ghidra function) at 0x5c36a when DAT_0009e73a==1 (set by fli_file_skip_chunk_5c56d after it read a type-4 COLOR_256 chunk into [DAT_000adf8c]); clears the flag and, if the player's apply-palette argument DAT_0009e6fc ([ebp+0xc] of 0x5c264) == 1, decodes the packets (word count; per packet skip byte, count byte, count RGB triples into the 768-byte palette [DAT_000adf7c] + skip*3) and calls vga_set_palette_302f0(palette) with CX=0x300; the decompiler pruned the body because the only write to DAT_0009e6fc is in non-function code, so Ghidra folded it to constant 0 - the asm switch-free body is verified"
0x000631ca,crt_getenv_631ca,"Watcom getenv(name): if _environ DAT_000a46c0 and name are non-null, strlen(name) via repne scasb, then for each env string crt_strnicmp_62bc8(env, name, len)==0 && env[len]=='=' returns env+len+1; else 0; callers music_init_from_sndsetup_4d2b0, sound_init_from_sndsetup_4d8f0, vfx1_init_4fdc0 (VFX variable), crt_system_658d5, crt_spawn_6d999, crt_spawn_path_search_71c25"
0x000660bd,crt_call_hook_a46b9_660bd,"push eax; call [DAT_000a46b9]; add esp,4; ret: register-arg to stack-call adapter for a CRT default-routine pointer that is statically initialised to the 1-byte ret stub crt_ret_stub_6ba52 (0x6ba52) and never written; byte scan finds no call/jmp or data reference to 0x660bd in the image, so it is dead Watcom CRT glue next to crt_set_errno_660aa/crt_set_doserrno_660b6"
0x00066f71,hmi_drv_call_ax_stub_66f71,"Orphan 10-byte asm helper of the HMI driver-call module: push es; ax=[ebp+0xc] (function number); call [ebp+8] (driver entry); pop es; ret, using the caller's frame; byte scan finds no call/jmp or data reference anywhere in the image, so it is unused code between dpmi_alloc_locked_dos_mem_66f24 and hmi_det_drv_get_caps_66fb8"
0x0006a5f1,crt_prtf_fixed_point_6a5f1,"printf %f replacement for 16.16 fixed point (buf, value, spec): '-' and negate if value<0, spec+8 precision defaults to 4 when -1, integer part = value>>16 via crt_itoa_70811 radix 10, '.', then precision digits by frac=(frac&0xffff)*10 and digit=frac>>16, finally rounds up if bit 15 of the remaining fraction is set with carry propagation across '9's and '.' (prepends '1' and shifts); called only from crt_prtf_convert_arg_6a6ef"
0x0006ee78,snd_awe32_cc_reverb_send_6ee78,"EMU8000 driver CC 0x5b (91, Effects 1 depth = reverb send) handler from snd_gus_controller_6f395 (val, ch): channel record byte +1 (DAT_000a53ef, stride 0x1e) = ((val*45)/100)*2 (0..114); the only other reference is the reset to 0 in snd_gus_reset_controllers_6f2a5 - snd_gus_note_on_6e712 never reads it, so the reverb send (PTRX bits 15-8) stays patch-defined"
0x0006eea1,snd_awe32_cc_chorus_send_6eea1,"EMU8000 driver CC 0x5d (93, Effects 3 depth = chorus send) handler from snd_gus_controller_6f395 (val, ch): channel record byte +2 (DAT_000a53f0) = ((val*45)/100)*2; snd_gus_note_on_6e712 adds it to the patch chorus word (DAT_000a5612 + patch) clamped to 0xff and writes it to bits 31-24 of the CSL register (poke_w code 0x70|voice = reg 7 Data0), i.e. the EMU8000 chorus send amount; reset to 0 by snd_gus_reset_controllers_6f2a5"
0x0006f801,snd_awe32_set_chorus_mode_6f801,"EMU8000 chorus preset (type 0..7, else returns 1): the exact Creative chorus sequence INIT3(9), INIT3(12), INIT4(3) (poke_b codes 0x3409/0x340c/0x3603) then dwords HWCF4, HWCF5 (0x1409/0x140a), HWCF6=0x8000, HWCF7=0 (0x140d/0x140e) from the 8 x 14-byte preset table DAT_000a4a38 (entry 0 = E600 03F6 BC2C 00000000 0000006D = Chorus 1); reached through the SysEx table DAT_000a4c84 (+0x12 handler pointer at 0xa4cac) from snd_gus_match_sysex_6f911 for Roland GS DT1 'F0 41 xx 42 12 40 01 38 vv' (chorus macro)"
0x0006f895,snd_awe32_set_reverb_mode_6f895,"EMU8000 reverb preset (type 0..7, else returns 1): 28 poke_b word writes whose pointer codes come from the byte table DAT_000a4aa8 (byte/32 selects INIT1 0x2400 / INIT2 0x2600 / INIT3 0x3400 / INIT4 0x3600, byte%32 = channel: INIT1(3) INIT1(5) INIT4(1f) INIT1(7) INIT2(14) INIT2(16) ...) and values from DAT_000a4ac4 + type*0x38 (8 x 28 words, type 0 = B488 A450 9550 84B5 383A 3EB5 72F4 ... = Room 1); reached through the SysEx table DAT_000a4c84 (handler pointer at 0xa4c96) from snd_gus_match_sysex_6f911 for Roland GS DT1 '40 01 30 vv' (reverb macro)"
0x000704b6,snd_awe32_get_hw_block_704b6,"Returns &DAT_000a4eb8 = the EMU8000 driver's hardware block {+0 dword 0x5678 marker, +4 0, +8 word 0x1e = 30 voices, +0xa word DAT_000a4ec2 = EMU8000 base port (0x620 default; pointer reg at +0x802, Data0/1/2/3 at +0/+0x400/+0x402/+0x800), +0xc dword DAT_000a4ec4 = sample DRAM size found by the 0x1234 write/read probe minus 0x200000}; no call, jmp or data reference anywhere in the image (dead driver export)"
0x00070697,snd_opl_alloc_voice_70697,"OPL2 voice allocation for snd_opl_note_on_689da(channel): returns the first of 9 voices with DAT_000a401c[v]==0 (no note); otherwise steals the first voice whose channel DAT_000a40c0[v] has no pending pitch bend (DAT_000a4124[ch]==0, scanning channels 0..15); otherwise returns channel (minus 9 if >= 9) as the voice; returned byte is used as the voice index"
```

## Notes for ENGINE.md

### HMI device 0xa008 is the Creative AWE32 (EMU8000), not a Gravis UltraSound

Everything named `snd_gus_*` (0x6de66-0x70480) talks to an EMU8000:

- `snd_gus_poke_b_6df9d` / `snd_gus_poke_w_6e069` / `snd_gus_peek_b_6e001` write the register code to
  `DAT_000a4ec2 + 0x802` and the data to `base | ((code>>8)&2) + ((code>>8)&0xc)<<8`. With the default base
  0x620 (initial value at 0xa4ec2) that is the EMU8000 layout: Pointer register 0xE22 (base+0x802), Data0 0x620
  (base+0, 32-bit), Data1 0xA20 (base+0x400), Data2 0xA22 (base+0x402), Data3 0xE20 (base+0x800). A GUS uses
  base+0x102..0x107 and base+0x506 and has no +0x802 register.
- Register code encoding (16-bit): bits 0-4 channel, bits 12-14 register number (shifted into pointer bits 5-7),
  bits 10-11 data-port group (0x4 = +0x400 Data1, 0x8 = +0x800 Data3), bit 9 = +2 (Data2). So 0x60|v = PSST,
  0x70|v = CSL (Data0 dwords), 0x04|v = CCCA, 0x54|v = DCYSUSV (Data1), 0x24xx/0x26xx/0x34xx/0x36xx =
  INIT1/INIT2/INIT3/INIT4, 0x1409..0x140e = HWCF4..HWCF7, 0x807f = Data3 reg 0 channel 31.
- `snd_awe32_set_chorus_mode_6f801` and `snd_awe32_set_reverb_mode_6f895` issue exactly Creative's published
  chorus/reverb sequences, and the preset tables are Creative's values byte for byte: chorus table
  `DAT_000a4a38` (8 x {feedback, delay_offset, lfo_depth, delay, lfo_freq}), entry 0 = E600 03F6 BC2C 00000000
  0000006D (Chorus 1), entry 1 = E608 031A BC6E 00000000 0000017C (Chorus 2); reverb command bytes
  `DAT_000a4aa8` = 03 05 7f 07 34 36 0f 17 1f 27 2f 37 3d 3f 41 43 09 0b 11 13 19 1b 21 23 29 2b 31 33 and
  reverb values `DAT_000a4ac4` (8 x 28 words), preset 0 = B488 A450 9550 84B5 383A 3EB5 72F4 72A4 7254 7204
  7204 7204 4416 4516 A490 A590 842A 852A ... (Room 1).
- The memory probe in `snd_gus_init_703e2` writes/reads the test word 0x1234 and stores
  `DAT_000a4ec4 = end - 0x200000`: EMU8000 sample DRAM starts at sample address 0x200000.
- 30 voices (`DAT_000a516c`, +8 of the hardware block) = EMU8000's 32 channels minus the two reserved for effects.
- The SysEx matcher `snd_gus_match_sysex_6f911` maps Roland GS DT1 reverb/chorus macros onto the EMU8000
  presets; the GS reverb/chorus type numbers 0..7 index the tables directly.

This also settles the open question about the HMI SOS device ids: 0xa008 = AWE32, as the earlier recollection
in ENGINE.md said. Recommendation: rename the whole `snd_gus_*` family to `snd_awe32_*` (about 60 functions),
fix the region-E table row for 0xa008, and change "GUS register access through port base DAT_000a4ec2 + 0x802"
to "EMU8000 pointer register at base+0x802". `snd_gus_detect_6f986` ("probe at base and base+0x400") is an
EMU8000 presence probe.

### EMU8000 driver channel record (stride 0x1e from DAT_000a53ee), corrections

+1 = CC91 reverb send, +2 = CC93 chorus send, both scaled `(val*45/100)*2`. The chorus byte is applied at note
on (CSL bits 31-24, added to the patch chorus word at patch+0x26 / `DAT_000a5612`); the reverb byte is written
but never read anywhere in the image (operand scan for 0xa53ef: only 0x6ee98 and the reset at 0x6f2b9). Pan
(+3) goes to PSST bits 31-24 (`or ah,0x60`) in the same code.

### The 302h/303h "stereo page" ports are the VFX1 VIP card

`g_state->opt_interlaced` is set to 1 in exactly two places (config_parse_33750 line ~27123 and
fe_config_apply_input_device_59330), both immediately after `vfx1_init_4fdc0` returns non-zero. So the
interlaced-stereo 640x480 path exists only for the Forte VFX1, and the fixed ports 302h/303h are the VIP card's
default base 300h + 2/+3 (vfx1_init reads the base from the `VFX` variable, default 0x300). 303h = 10h is written
when the 3D view is entered (`stereo_page_blank_50370`, from stereo_mode_enter_2ff50) and 303h = 1 when it is
left (`vfx1_vip_stereo_leave_503d0`, from mapmode_palette_save_30350), each preceded by 302h = 2 and followed by a
clear-and-blit of the back buffer. The meaning of the values 1 and 0x10 (line-interleaved stereo on/off is the
obvious reading) is not confirmed by any string; speculation. `stereo_page_blank_50370` should be renamed
`vfx1_vip_stereo_enter_50370` for symmetry.

### File-stream FLI player: the real player is un-functioned asm at 0x5c264

- 0x5c264-0x5c40a is a hand-written player that Ghidra has no function for (prologue `push ebp; mov ebp,esp;
  push eax..esi`; args [ebp+8] = loop flag source, [ebp+0xc] = apply-palette flag -> `DAT_0009e6fc`, [ebp+0x10] =
  cue script for `cue_script_step_17d80`). It opens `DAT_0009e708` (path buffer) with crt open 66296 -> handle
  `DAT_0009e700`, zeroes `DAT_0009e6ec` (frame counter) and `DAT_0009e6f6`, seeds the DOS clock pacing word
  `DAT_0009e6f4` (int 21h AH=2Ch, seconds*100+hundredths), then per chunk reads 4-byte size `DAT_0009e6e4` and
  2-byte magic `DAT_0009e6dc`: 0xAF12 -> `fli_file_read_header_5c4ce`; 0xF1FA -> frame: `DAT_0009e6de`--
  (frames left), `DAT_0009e6ec`++, cue step, `fli_file_play_5c40b` (which is really the per-frame sub-chunk
  loop: count `DAT_0009e6e8`, 8 header bytes to [DAT_000adf68], type 7 -> 5c609, 12 -> 5c6e0, 4 -> 5c56d,
  15 -> 5c787, 16 (FLI_COPY) -> 5c531 straight into the frame buffer DAT_0012ed74, other -> 5c54f discard into
  [DAT_000adf68]), then if `DAT_0009e704` (delay) != 0: palette apply (5c594), `rep movsd` 0x3e80 dwords from
  DAT_0012ed74 to A0000h (320x200 only), `fli_file_wait_frame_5c813`; when frames reach 0: int 10h AX=1200h
  BL=36h (VGA refresh on), loop control through `DAT_0009e6fa` (looping flag, cleared when [ebp+8] and the
  key/mouse words DAT_0012ee0e / DAT_0012eea0 are set), close (664de) and a 500-iteration re-open loop while
  `DAT_0009e6fa` counts down. The function needs to be created in Ghidra (`fli_file_play_5c264`), and 5c40b should
  become `fli_file_decode_frame_chunks_5c40b`.
- `fli_file_skip_chunk_5c56d` is misnamed: it reads the COLOR_256 chunk body (size-6 bytes) into the buffer at
  [DAT_000adf8c] and raises `DAT_0009e73a`; the palette itself lives at [DAT_000adf7c] (both pointers are also
  listed in the data table at 0x96f6c / 0x96f98, probably the allocation table that fills them).
- `DAT_0009e6fc` is only ever written by the asm player, which is why the decompiler pruned the palette path of
  5c594 as unreachable; the path is real.

### Watcom CRT / HMI glue

- `crt_getenv_631ca` is Watcom's case-insensitive DOS getenv (strnicmp + '=' test over `_environ`).
- `crt_prtf_fixed_point_6a5f1` confirms the 16.16 fixed-point %f: integer part = high word, four fraction digits
  by default, round-half-up on bit 15 of the residue.
- `crt_call_hook_a46b9_660bd`, `hmi_drv_call_ax_stub_66f71` and `snd_awe32_get_hw_block_704b6` have no call,
  jump or data reference anywhere in the flat image (scanned for rel32 E8/E9 targets and little-endian dword
  values); all three are dead code. Which Watcom default-routine pointer `DAT_000a46b9` is (it sits between the
  environment selector/offset DAT_000a46b5/b1 and `_environ` DAT_000a46c0 in the startup data) could not be
  established without the CRT source; speculation: a `__null_*_rtn`-style hook such as `__FPE_handler`.
- `cpu_detect_5ac80` copies the five detection bytes into a stack buffer it never uses (compiler kept the
  dead stores); only the family==5 test matters.

### Open questions

- Exact meaning of the VIP register values written to 303h (1 vs 0x10) and of 302h = 2.
- Whether the data table at 0x96f6c/0x96f98 (holding the addresses 0xadf8c / 0xadf7c) is the game's
  startup buffer-allocation list; not read.
- The 0x5678 marker at the start of the EMU8000 hardware block (0xa4eb8) has no reader in the image.
