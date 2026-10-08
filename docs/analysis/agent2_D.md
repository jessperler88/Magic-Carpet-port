# Agent 2 / region D (0x4c000-0x5e000): sound setup, NetBIOS, VFX1, front end, FLI, memory, joystick, HMI

Every function in todo2_D.txt was read (decompiled body; disassembly where the C was garbled). 86 unnamed
entries + 9 UNCERTAIN re-reads. Three entries are not code (DATA) and one is a FRAGMENT.

```csv
0x0004c0a0,texture_mark_resident_4c0a0,"Clears the 0x211-byte flag table DAT_000b9580 then, if DAT_000987e8, walks a 14-byte record list (ends when words +6/+8 are both 0) setting flag[rec.id]=0xff for records whose byte +0xb == -1; called by 4bbf0 (tmap init) just before 4c0f0"
0x0004c0f0,texture_load_resident_4c0f0,"For every texture id 0..0x210 flagged in DAT_000b9580 calls sprite_group_load_4b8b0 (preload of always-resident textures at tmap init)"
0x0004c280,tmap_cache_create_4c280,"Creates the tmap cache struct (0x1a bytes: +0 size +4 free +8 dir (n x 0xe) +0xc index (n x 4) +0x10 data +0x14 count +0x16 capacity +0x18 mode) with n=0x211; pool ptr NULL -> four mem_alloc (mode 1) else carved in place from cfg+0xa8/+0xac pool (mode 2); crt_exit on failure"
0x0004c3d0,tmap_dir_find_free_4c3d0,"Returns the first directory slot (0xe-byte entries at cache+8) whose +4 size is 0, or -1"
0x0004c3fb,tmap_dir_entry_data_4c3fb,"Returns +0 (data ptr) of directory entry n; no callers"
0x0004c41c,tmap_dir_entry_valid_4c41c,"1 if n < capacity (+0x16) and entry size != 0 and cache size != 0; no callers"
0x0004c460,tmap_cache_add_4c460,"Appends a chunk if size <= free: takes a free dir slot, copies data to data+size-free, entry = {+0 ptr +4 size +8 order +0xa slot +0xc id}, index[count++] = entry, free -= size; returns entry ptr or 0"
0x0004c6ee,tmap_cache_clear_4c6ee,"Frees every directory entry (free += size; zero ptr/size/order) and sets count (+0x14) = 0"
0x0004c750,tmap_dir_find_by_id_4c750,"Returns the directory entry whose +0xc id == arg, else 0"
0x0004c790,tmap_cache_destroy_4c790,"If *pcache: for mode 1 (+0x18 == 1) frees the four blocks; *pcache = 0; called from 4bc80 (tmap shutdown)"
0x0004c7f0,texture_anim_table_create_4c7f0,"Allocates {u16 count; ptr to count x 0x1c records} under mem_set_owner_tag and zeroes +0/+4 of each record; these 0x1c records are the texture-anim records of texture_anim_next_frame_4c9f0 (the sprite_cache_* names 4c880/4cb10/4ca50/4c610 operate on the same table)"
0x0004c9be,texture_anim_update_all_4c9be,"For each 0x1c record with +0 (active) nonzero calls texture_anim_next_frame_4c9f0; per-tick texture animation driver"
0x0004ca66,texture_anim_table_clear_4ca66,"Zeroes +0 (active) and +4 (sprite ptr) of every anim record"
0x0004cab0,texture_anim_table_deactivate_4cab0,"Zeroes +0 (active) of every anim record, keeps the sprite pointers"
0x0004cb50,texture_anim_table_destroy_4cb50,"Frees the record array and the header; *ptable = 0; called from 4bc80"
0x0004cb88,palette_build_tint_table_4cb88,"Builds a 256-entry remap table: target = pal[i] + ((rgb - pal[i]) * factor >> 8) per channel (args r g b and three factors) then nearest palette index by 2dr^2+2dg^2+db^2; called from tables_load_or_generate_3eaa0"
0x0004cd39,math_isqrt16_4cd39,"16-bit integer sqrt: seed from the u16 table at 0x4cdb0 indexed by the highest set bit, then Newton x=(x+n/x)/2 until x <= n/x; no callers (32-bit sibling is math_isqrt_4cd7a)"
0x0004cdb0,DATA,"Not code: 16 u16 sqrt seeds 1 2 2 4 5 8 0xb 0x10 0x16 0x20 0x2d 0x40 0x5a 0x80 0xb5 0x100 (0x4cdb0-0x4cdcf) used by math_isqrt_4cd7a/4cd39, followed by padding"
0x0004cdf0,vga_set_dac_entry_4cdf0,"out 3C8h index then 3C9h r g b (register args); called from 18e90"
0x0004ce13,mem_fill_bytes_4ce13,"memset(dest byte n): dword stores then remainder"
0x0004ce51,mem_copy_bytes_4ce51,"memcpy(dest src n): dword copies then remainder"
0x0004cef0,dbg_draw_text_bitmap_4cef0,"Draws a string with a 6-row bitmap font at 0x140000 (8 bytes per glyph, index char-0x20): TAB advances x to the next multiple of 8, LF adds 6*pitch; each font byte is dispatched through a 256-entry jump table at 0x4cf67 to unrolled pixel plotters in colour arg; no callers (debug printer)"
0x0004d096,ui_blit_icon_32w_4d096,"Copies a 32-pixel-wide n-row 8-bit image to dest (pitch DAT_0012ed70): 0 -> 0, 0xFE skipped (transparent), else (b & 0xF) + colour base; no callers"
0x0004d2b0,music_init_from_sndsetup_4d2b0,"Reads c:%s/sndsetup.inf (path from the \carpet.cd env var), creates it with 'SOUNDFX = none 0 0 0' / 'MUSIC = none 388 0 0' if missing, sscanf '%s = %s %x %d %d', BF_MUSIC env override '%s %x'; 'none' -> DAT_0009e30c/30d = 0; card name (ADLIB SBLAST SBPRO ADLIBG SB16FM GRAVIS PASFM COMPATIBLE ROLAND SBAWE32 GENERAL WBLAST) -> music_init_hmi_4d550 with the HMI device id in EBX; from sound_initialise_34140"
0x0004d550,music_init_hmi_4d550,"HMI MIDI init: zero 0xa2495.. state, snd_timer_init, sosMIDIInitSystem (0x92c98), init driver ('\nError : %s' -> music off). Device id switch: 0xA001 -> DAT_0012e06e=2; 0xA002 (OPL FM) loads data\inst.bnk + data\drum.bnk via 4d880 and sosMIDI driver calls, DAT_0012e06f=1 DAT_0012e06e=0; 0xA004 -> 1; 0xA008 -> 2; then music_load_bank_5c870 ('\nError opening music files')"
0x0004d880,file_load_rnc_alloc_far_4d880,"file_get_unpacked_size -> mem_alloc -> file_load_rnc_3cbe0, frees on size mismatch; returns far pointer (offset in EAX, DS in EDX) for the HMI bank loader"
0x0004d8f0,sound_init_from_sndsetup_4d8f0,"Same sndsetup.inf parse for the SOUNDFX line (BF_SOUND env override); 'none' -> DAT_0009e320/321 = 0, else sound_digital_init_4da10; from sound_initialise_34140"
0x0004e0f0,sound_request_fade_in_4e0f0,"Stores target volume DAT_0009e3c0 = arg and if the mode arg == 3 calls sound_play_fade_in_4e120; from sound_request_49720"
0x0004e120,sound_play_fade_in_4e120,"If (owner sample) already playing: fade-in flag DAT_0012e270[ch]=1 (fade-out cleared) target DAT_0012e2b0[ch]=DAT_0009e3c0; else first free HMI channel starts the sample (flags 0x4300 volume 0 DAT_0012e0f0[ch]=0) and is flagged fade-in"
0x0004e400,sound_start_fade_out_4e400,"Finds the channel playing (owner sample): clears fade-in, DAT_0012e2f0[ch]=arg (fade parameter), DAT_0012e350[ch]=current volume DAT_0012e0f0[ch], sets fade-out flag DAT_0012e330[ch]; from 49c40"
0x0004ebb0,net_receive_chunked_4ebb0,"Receives len bytes as len>>11 NCB RECEIVE blocks (net_receive_4eb30, 0x800 each) plus remainder; returns len or the short count; called by 4f4d0"
0x0004ec4c,net_receive_player_wrap_4ec4c,"Calls net_receive_player_4ec10 and returns 1; no callers"
0x0004ecf0,net_send_chunked_4ecf0,"Sends len bytes as 0x800-byte NCB SEND blocks (net_send_4ec80) plus remainder; returns the first nonzero error; called by 4f470"
0x0004ed70,net_player_status_4ed70,"2 if p == local DAT_0009e3ca; 1 if NCB DAT_0009e400[p] byte +2 nonzero and +0x31 (cmd_cplt) == 0 (session up); else 0; used by net_build_player_status_4ed50"
0x0004f61e,net_exchange_block_4f61e,"If network: for each player p - local: send own slot (buf + local*size) to every player via 4f470; remote with status 1: receive into buf + p*size via 4f4d0; alternative to net_exchange_frame_4f530, no callers"
0x0004f6c1,FRAGMENT,"Mid-body of net_exchange_block_4f61e (function spans 0x4f61e-0x4f6e0)"
0x0004fba0,vfx1_find_driver_fn_base_4fba0,"Probes the VFX1 int 33h driver: real-mode int 33h (50250) with AX = 0x60xx|0x7f .. 0x70xx step 0x100 until the returned AX == 0x7f00|xx; returns the function base (AH) or 0 -> DAT_0012e5bc; from vfx1_init_4fdc0"
0x0004fc20,vfx1_driver_query_4fc20,"int 33h AX = base|3: copies DAT_0012e64c returned bytes from the DOS transfer buffer DAT_0012e628 into dest, *count = DAT_0012e64c; returns AX status"
0x0004fca0,vfx1_driver_send_block_4fca0,"Copies 256 bytes from src into the DOS transfer buffer DAT_0012e628 then int 33h AX = base|4; returns AX"
0x0004fd20,vfx1_driver_read_tracker_4fd20,"int 33h AX = base|5: copies DAT_0012e64c result bytes from the DOS buffer into dest, *count set; polled by altdev_read_50080"
0x00050220,vfx1_free_dos_buffer_50220,"If DAT_0012e628: DPMI 0101h free DOS memory (502c0) and DAT_0012e628 = 0"
0x00050250,vfx1_int33_realmode_50250,"DPMI int 31h AX=0300h: simulate real-mode int 33h with the 0x32-byte register block DAT_0012e634 (AX at DAT_0012e650, ECX DAT_0012e64c, EDX DAT_0012e648, DS DAT_0012e656)"
0x000502c0,dpmi_free_dos_mem_502c0,"int 31h AX=0101h free DOS memory block, DX = selector arg"
0x000502f0,dpmi_alloc_dos_mem_502f0,"int 31h AX=0100h with BX = (size+15)>>4 paragraphs; outputs segment and selector through the two pointer args; returns segment<<4 (linear address) or 0"
0x00050370,stereo_page_blank_50370,"out 302h 2; out 303h 10h; clears the back buffer (pitch*height) and blits it (320 or VESA); companion of stereo_page_flip_503d0 (303h 1); from stereo_mode_enter_2ff50"
0x000504f0,fli_mem_read_504f0,"Reads n bytes from the in-memory FLI stream pointer DAT_0012eab0 into dest (NULL = skip) and advances the pointer"
0x000509f0,fli_mem_decode_color256_509f0,"COLOR_256 chunk from the memory stream: packet count, then per packet skip + count bytes and 256 colour triples (always 256 entries)"
0x00050a90,fli_mem_decode_ss2_50a90,"FLI_SS2 (type 7) word-run decoder from the memory stream into DAT_0009e444: line count; per-line words with 0x8000/0x4000 flags (skip lines / last pixel); packets {skip signed count}: negative = replicate word, positive = copy words"
0x00050be0,fli_mem_decode_lc_50be0,"FLI_LC (type 12 DELTA_FLI) decoder: first line, line count; per line packet count; packets {skip signed size}: negative = memset abs(size) bytes, positive = copy"
0x00050d00,fli_mem_decode_brun_50d00,"FLI_BRUN (type 15): DAT_0012eaaa lines of DAT_0012eaa8 pixels into DAT_0009e444; per line packet count then signed counts (negative = literal copy, positive = fill); decompiler dropped the body, asm is clear"
0x00051eac,DATA,"Not code: tail rows of the scancode->ASCII table at 0x51E55 ('ASDFGHJKL:@~ |ZXCVBNM<>?') plus padding before fe_init_state_51ed0"
0x00052980,fe_check_shift_q_quit_52980,"If Shift held (key 0x2a DAT_0012ee4a or 0x36 DAT_0012ee56) and last scancode DAT_0012eea0 == 0x10 (Q): DAT_0009e504 = 1 and player[local]+0x340f = 1 (quit to DOS); returns 1 when triggered"
0x000529d0,fe_wait_ticks_or_input_529d0,"Loops until n*0x78 ticks of DAT_0012eab4 elapsed, a key/mouse event (DAT_0012ee0a/0c/0e DAT_0012eea0, cleared) or Shift+Q (52980); calls the optional callback each pass (nonzero return ends the wait); used by logo/title screens"
0x00055920,fe_lobby_slot_connecting_55920,"NetBIOS progress callback: DAT_0009e522[p*3] = 1, DAT_0012ed10++, redraw slots (55210), draw session name cfg+0x75 centred in clip rect (0xad 0x2a 0x5a 0xf) with font DAT_0009e510, blit; from net_add_name/net_call"
0x00055a60,fe_lobby_slot_clear_55a60,"Same as 55920 but sets slot state 0 (connection cleared/failed); from net_add_name"
0x00057580,fe_menu_text_dialog_57580,"Main-menu item 2 handler (table 0x5167c): saves background, FLI callback DAT_0012ea9c = 579c0, hides cursor if no VFX, fli_play (name string at 0x516cc), then two ui_text_edit_field calls (second with shift flag 0x20), DAT_0012ed2e = 2 and redraws the menu overlay. fe_dialog_scroll_text_57617 starts mid-instruction (inside push 0x516cc at 0x57615): the real function is 0x57580 (775 bytes)"
0x000578a0,fe_dialog_draw_title_a_578a0,"Dialog draw callback: restores background then centres the string pointed to by DAT_000add70 at y=10 with font DAT_0009e510 in the current text clip rect"
0x00057930,fe_dialog_draw_title_b_57930,"Identical to 578a0 but draws the string pointed to by DAT_000add74"
0x000579c0,fe_fli_composite_bg_579c0,"FLI frame callback (DAT_0012ea9c in 57580): for 64000 pixels of DAT_0012ed74 replaces colour 0 with the pixel of a background buffer (register arg)"
0x00058820,ui_str_prepend_58820,"Inserts src in front of dest: memmove(dest+strlen(src) dest strlen(dest)+1) then copies src; returns dest; text-edit helper"
0x00058880,ui_str_delete_at_58880,"Deletes the character at index pos: memmove(s+pos s+pos+1 strlen(s+pos)); text-edit helper"
0x00058f30,ui_font_relocate_glyphs_58f30,"For each 6-byte glyph entry between font+0 and font+4 (end) adds the base (font+8) to the u32 offset when offset < base (one-time pointer fix-up); from ui_font_init_589d0"
0x0005940c,gfx_fill_rect_clipped_5940c,"Fills a w x h rect at (x y) with a colour in DAT_0012ed74 clipped to the sprite clip window (DAT_0012ed80/90/98/a4/a8/88, pitch DAT_0012ed70); modes (arg & 3) 1..3 return without drawing; text cursor of ui_text_edit_field"
0x00059d8d,joy_read_raw_axes_59d8d,"joy_poll_axes_59fad then stores raw counts DAT_0012ed42/44 (x y) and DAT_0012ed56/58 (axes 3/4), buttons DAT_0012ed6a; used by analogue calibration"
0x0005a370,joy_calibrate_prompt_5a370,"Calibration prompt loop: redraw config screen (fe_config_draw_52ab0) + highlight rect (vga_fill_rect), poll digital joystick, blit; waits for button bit 0x10 of DAT_0012ed6a to be released then pressed, vsync between; from joy_init_digital/analog"
0x0005ad03,cpu_identify_5ad03,"Intel CPUID sample code: EFLAGS AC bit (0x40000) toggle -> 386 (DAT_0009e59c=3) vs 486 (4); ID bit (0x200000) -> CPUID(0) vendor at DAT_0009e5aa ('GenuineIntel' -> DAT_0009e59d=1), CPUID(1) -> DAT_0009e5a6, family DAT_0009e59c, model DAT_0009e59e, stepping DAT_0009e59f, DAT_0009e5b6 = CPUID present"
0x0005adfe,cpu_detect_fpu_5adfe,"FNINIT/FNSTCW: DAT_0009e5a4 = control word 0x37f, DAT_0009e5a0 = FPU present; on a 386 distinguishes 287 (DAT_0009e5a1=2) from 387 (=3) by the +inf == -inf test"
0x0005af62,dos_get_disk_free_5af62,"int 21h AH=36h (drive in DL): fills {sectors/cluster free clusters bytes/sector total clusters} (4 u16) or returns errno on AX=-1; from init_early_3c800 and 10010"
0x0005aff8,crt_rand_5aff8,"ANSI rand(): seed = seed*0x41C64E6D + 0x3039 (seed pointer from 5aff2); returns (seed>>16) & 0x7fff; from 14c70"
0x0005b824,mouse_cursor_restore_unlock_5b824,"If a mouse is present restores the saved background (5b740) and clears the blit lock DAT_0009e5d4; no callers"
0x0005c40b,fli_file_play_5c40b,"File-streamed FLI player (no callers): reads the 2-byte frame count DAT_0009e6e8 and 8 header bytes into DAT_000adf68, then per frame 4-byte size + 2-byte type and dispatch (5c54f); decoders 5c609/5c6e0/5c787 write to DAT_0012ed74; older sibling of fli_play_508f0"
0x0005c4ce,fli_file_read_header_5c4ce,"Reads the header into DAT_000adf68 and stores width DAT_0009e6de, height DAT_0009e6e0, depth DAT_0009e6e2"
0x0005c56d,fli_file_skip_chunk_5c56d,"Reads and discards a chunk (dest NULL) and sets DAT_0009e73a = 1"
0x0005c594,fli_file_chunk_dispatch_5c594?,"Sets DAT_0009e73a = 0; the rest (switch on chunk type calling 5c609/5c6e0/5c787) was dropped by the decompiler as unreachable - verify in asm"
0x0005c609,fli_file_decode_ss2_5c609,"FLI_SS2 word-run delta decoder from the file stream into DAT_0012ed74 (pitch DAT_0009e6e0): line count then per-line packet words with 0x8000/0x4000 flags"
0x0005c6e0,fli_file_decode_lc_5c6e0,"FLI_LC (type 12) decoder: start line, line count; per line packet count; packets {skip signed count}: negative = fill, positive = copy"
0x0005c787,fli_file_decode_brun_5c787,"FLI_BRUN decoder for DAT_0009e6e2 lines of DAT_0009e6e0 pixels: signed packet counts (positive = replicate, negative = literal copy)"
0x0005c813,fli_file_wait_frame_5c813,"Frame pacing via int 21h AH=2Ch (DX = seconds:hundredths): spins until hundredths - DAT_0009e6f4 >= DAT_0009e704 (frame delay), wraps at 6000"
0x0005c924,music_bank_relocate_5c924,"Walks the 0x20-byte records of music.tab (DAT_0012dfe0+0x20 .. DAT_0012dfe4) adding the data base DAT_0012dfe8 to +0x12 and counts them into DAT_0009e316 (track count); decompiler emitted an empty body"
0x0005ca58,sound_bank_relocate_5ca58,"Same for the sample bank: records DAT_0012e1d4+0x20 .. DAT_0012e240, base DAT_0012e1b0 added to +0x12, count -> DAT_0012e246"
0x0005cac0,fli_stream_read_5cac0,"If a preloaded buffer DAT_0009e844 exists copies n bytes from DAT_0009e848 (advancing, bounded by end DAT_0009e84c and position DAT_0009e850) else file_read_61a40(fd dest n); shared by the file FLI decoders"
0x0005cb50,fli_stream_preload_5cb50,"crt_read(fd DAT_0009e844 n) loads the whole file into the preload buffer; DAT_0009e848 = buffer, DAT_0009e84c = bytes read, DAT_0009e850 = 0; from cue_script_step_17d80"
0x0005cd60,sound_play_sample_loud_5cd60,"Starts (owner sample) on the first free HMI channel at volume 0x7FFF (flags 0x100), records DAT_0012e070/0f0; no duplicate check; from cue_script_step_17d80"
0x0005d235,hmi_timer_set_event_rate_5d235,"sosTIMERAlterEventRate: slot < 16 and active -> mask IRQ0, DAT_000a3cb9[slot] = Hz; if 0x1234DC/Hz < divisor DAT_000a3c55 raise the PIT rate (5d57b); recompute DAT_000a3cf9[i] = (Hz<<16)/(0x1234DC/divisor) for all active slots (0xff00 = full rate), zero accumulators DAT_000a3d39; unmask; returns 0 or error 10"
0x0005d558,hmi_timer_get_event_rate_5d558,"Returns DAT_000a3cb9[slot] (event rate in Hz)"
0x0005d69b,hmi_midi_send_event_5d69b,"sosMIDI channel-steal sender for a 3-byte MIDI event (GS:ptr) on driver idx (fn table DAT_0009f91a + idx*0x24). DAT_000a2481 == 0: pass-through scaling controller 7 by DAT_000a2565>>7. Else maps logical channel to physical via DAT_000a1847, allocates a free physical channel (DAT_000a2515 enabled / DAT_000a1b17 owner) or steals the lowest priority DAT_000a1ac7, re-sends program/bend/volume/pan from DAT_000a1bba/b8/b9/bb, channel 9 (drums) fixed; returns 0 ok / -1 no channel / 1 passthrough"
0x0004f360,net_player_disconnect_4f360,"Player p leaving: if p is local cancels and hangs up every other NCB, sprintf name + net_delete_name, DAT_0009e3c9 = 0; else cancel + hang up p; rebuilds the status table and DAT_0009e3fa (host index) = first player with nonzero status; from player_commands_process_3a8b0"
0x00050080,vfx1_read_tracker_50080,"int 33h base|5 (4fd20) into DAT_0012e4a0; on error frees the DOS buffer and returns 0; DAT_0009e43c (VFX1 head tracker) -> DAT_0012e62c/2e/30 = three axis words; DAT_0009e43d (CyberPuck) -> DAT_0012e66a/6c/6e + button byte DAT_0012e670; returns 1"
0x00050120,vfx1_driver_cmd_6008_50120,"int 33h AX=6008h with BX = bytes +2/+3 of the driver info block DAT_0012e5f4, only when the head tracker is present; VFX1 driver control call (exact function unknown)"
0x00050180,vfx1_vip_set_palette_50180,"Programs the VFX1 VIP board DAC through index port DAT_0012e624 / data port DAT_0012e61c (set in vfx1_init from the VFX env var, default 0x300): reg 7 = 0xFF, reg 6 mode bits (arg 0 -> |3, 1 -> |1, else |4), reg 9 = 0, reg 10 then 768 palette bytes; from vga_set_palette_302f0"
0x000503d0,stereo_page_flip_503d0?,"out 302h 2; out 303h 1 then clear + blit; pairs with stereo_page_blank_50370 (303h 10h); from mapmode_palette_save_30350. Ports 302h/303h are constants, not the VIP port variable - device unidentified"
0x000588f0,ui_get_clip_rect_588f0,"Copies the six text clip-rect dwords DAT_0012ead0..eae4 (x x2 y y2 w h) into the caller's struct; callers use it for centring"
0x00059a60,mem_free_coalesce_59a60,"Finds the block node whose ptr == arg in the list at 0x130120, clears the owner tag (+0x10), calls mem_split_block_59980, then merges every free node with its successor (mem_merge_next_59b90) and updates stats (622a8); returns 1"
0x0005ac80,cpu_detect_5ac80,"Confirmed: 5ace8 -> cpu_identify_5ad03 + cpu_detect_fpu_5adfe; cfg+8 = (CPU family DAT_0009e59c == 5), i.e. Pentium"
0x0005be68,mouse_set_range_5be68,"int 33h fn 7/8 (set horizontal/vertical range) by DAT_0012edae: 8 -> 0..0x1400 x 0..0xF00 (8x 640x480), 2 -> 640x480, 4 and 1 -> 640x400; from input_mouse_init and the two vga_set_mode functions"
```

## ENGINE.md section: region D findings (agent 2, 2026-10-06)

### Corrections to existing names

- `net_send_to_player_4f4d0` and `net_receive_from_player_4f470` are **swapped**. 4f470 calls 4ecf0, which
  issues NCB 0x94 SEND (`net_send_4ec80`) in 0x800-byte blocks; 4f4d0 calls 4ebb0, which issues NCB 0x95
  RECEIVE (`net_receive_4eb30`). So 4f470 = send to player, 4f4d0 = receive from player. Read with that in
  mind, `net_exchange_frame_4f530` becomes: host (local index == DAT_0009e3fa) receives from every other
  player then sends to all; a client sends first then receives. Both re-listen (`net_listen_4ea10`) when the
  player's NCB cmd_cplt (+0x31) is nonzero.
- `fe_dialog_scroll_text_57617` is not a function start: 0x57617 is inside the instruction `push 0x516cc`
  at 0x57615. The real function is `fe_menu_text_dialog_57580` (0x57580-0x57888, 775 bytes), the main-menu
  item 2 handler from table 0x5167c. Delete the 57617 symbol when fixing function bounds.
- `video_mode_extra_5be68` is `mouse_set_range_5be68`: int 33h functions 7 and 8 set the mouse x/y range
  (640x400 for modes 1 and 4, 640x480 for mode 2, 5120x3840 for mode 8).
- `ui_clip_rect_width_588f0` returns the whole clip rect (6 dwords), not just the width.
- `sprite_cache_*` (4c880/4cb10/4ca50/4c610) and the new `texture_anim_table_*` names (4c7f0/4c9be/4ca66/
  4cab0/4cb50) operate on the same table: {u16 count; ptr to count x 0x1c records} created by 4c7f0 and
  stepped by `texture_anim_next_frame_4c9f0`. The 0x1c record is the texture-animation record
  (+0 active, +4 ptr to sprite ptr, +8 offset, +0xc 6, +0xe first, +0x10 count, +0x12/+0x14 w/h,
  +0x16 frame, +0x18 slot, +0x1a sprite id). Consider renaming the sprite_cache_ names to texture_anim_.
- Three todo entries are data, not code: 0x4cdb0 (sqrt seed table), 0x51eac (tail of the scancode->ASCII
  table at 0x51E55), and 0x4f6c1 is a fragment inside 4f61e.

### The "alternate device" is the Forte VFX1, driven through an int 33h driver extension (confirmed)

`vfx1_init_4fdc0` allocates a DOS transfer buffer (`dpmi_alloc_dos_mem_502f0`, int 31h 0100h ->
DAT_0012e628), reads the VIP board port from the `VFX` environment variable (default 0x300 ->
DAT_0012e624 index, DAT_0012e61c = port+1 data), then probes the VFX1 driver: `vfx1_find_driver_fn_base_4fba0`
issues real-mode int 33h (DPMI 0300h, register block DAT_0012e634, 0x32 bytes, AX at DAT_0012e650) with
AX = 0x60xx|7F .. 0x70xx|7F until the driver answers AX = 0x7F00|xx; the function base goes to DAT_0012e5bc.
Driver calls then use AX = base|3 (query: `vfx1_driver_query_4fc20`, copies DAT_0012e64c bytes from the DOS
buffer -> info block DAT_0012e5f4), base|4 (`vfx1_driver_send_block_4fca0`, 256 bytes to the driver) and
base|5 (`vfx1_driver_read_tracker_4fd20`, head-tracker report). `altdev_read_50080` (now
`vfx1_read_tracker_50080`) is polled from `input_joystick_poll_5a4e0`: DAT_0009e43c = head tracker present ->
DAT_0012e62c/2e/30 (three axis words, yaw/pitch/roll), DAT_0009e43d = CyberPuck present ->
DAT_0012e66a/6c/6e + buttons DAT_0012e670. `vfx1_driver_cmd_6008_50120` is an extra int 33h AX=6008h call
with BX taken from the info block. `vfx1_vip_set_palette_50180` mirrors the game palette into the VIP
board's own DAC (index regs 7, 6, 9, 10 + 768 bytes), called from `vga_set_palette_302f0`.
Speculative: the names "CyberPuck" and the axis meaning are from the VFX1 product, not from strings.

Ports 302h/303h (`stereo_page_blank_50370`: 303h = 10h, `stereo_page_flip_503d0`: 303h = 1, both preceded
by 302h = 2) are constants and not the VIP port variable; the device behind them is still unidentified
(3D shutter glasses or the VFX1's stereo page register). Flagged `?`.

### sndsetup.inf and HMI music initialisation

`sound_initialise_34140` calls `sound_init_from_sndsetup_4d8f0` (SOUNDFX line) and
`music_init_from_sndsetup_4d2b0` (MUSIC line). Both build the path with sprintf("c:%s/sndsetup.inf") from
the `\carpet.cd` environment value, create the file with defaults `SOUNDFX = none 0 0 0` /
`MUSIC = none 388 0 0` when missing, parse `%s = %s %x %d %d` (name, card, port, irq, dma), and accept an
environment override (`BF_SOUND` / `BF_MUSIC`, format `%s %x`). Card name `none` disables the subsystem.
Music card names: ADLIB SBLAST SBPRO ADLIBG SB16FM GRAVIS PASFM COMPATIBLE ROLAND SBAWE32 GENERAL WBLAST
(strings 0x92c2b..0x92c90). `music_init_hmi_4d550` receives the HMI device id in EBX and switches on it:
0xA001 -> DAT_0012e06e = 2, 0xA002 -> loads `data\inst.bnk` and `data\drum.bnk` (through
`file_load_rnc_alloc_far_4d880`, far pointers DAT_0012e068/6c and DAT_0012e062/66) and sets DAT_0012e06f = 1,
0xA004 -> 1, 0xA008 -> 2. From HMI SOS headers (recollection, flag): 0xA001 = MPU-401, 0xA002 = OPL2 FM,
0xA004 = internal/OPL3?, 0xA008 = AWE32. Then `music_load_bank_5c870` loads music%d-%d.dat/.tab and
`music_bank_relocate_5c924` relocates the 0x20-byte tab records (+0x12 += data base DAT_0012dfe8) and
counts them into DAT_0009e316 (track count). `sound_bank_relocate_5ca58` does the same for the sample bank
(DAT_0012e1d4 .. DAT_0012e240, base DAT_0012e1b0, count DAT_0012e246). Both were decompiled as empty bodies.

### Sound fades

`sound_request_fade_in_4e0f0` stores the target volume in DAT_0009e3c0 and, for request mode 3, calls
`sound_play_fade_in_4e120`: an already-playing (owner, sample) channel is flagged in DAT_0012e270 with the
target in DAT_0012e2b0[ch]; otherwise a new channel is started at volume 0 with flags 0x4300 and flagged.
`sound_start_fade_out_4e400` flags DAT_0012e330[ch], saves the current volume in DAT_0012e350[ch] and the
argument in DAT_0012e2f0[ch]. The per-tick steppers are the known `sound_update_fadein_4dfc0` /
`sound_update_fadeout_4e2e0`. `sound_play_sample_loud_5cd60` (cue script) starts at 0x7FFF with flags 0x100.

### Two FLI players

1. Memory-stream player (`fli_play_508f0`, in use): `fli_mem_read_504f0` reads from the pointer
   DAT_0012eab0; chunk decoders `fli_mem_decode_color256_509f0` (type 4), `fli_mem_decode_ss2_50a90`
   (type 7), `fli_mem_decode_lc_50be0` (type 12) and `fli_mem_decode_brun_50d00` (type 15) write into
   DAT_0009e444 with width DAT_0012eaa8 and height DAT_0012eaaa.
2. File-stream player (`fli_file_play_5c40b`, no callers): `fli_stream_read_5cac0` reads from a preloaded
   buffer (DAT_0009e844 buffer, DAT_0009e848 cursor, DAT_0009e84c end, DAT_0009e850 position; filled by
   `fli_stream_preload_5cb50`) or from the file; header fields DAT_0009e6de/e0/e2 (w, h, depth);
   decoders 5c609 (SS2), 5c6e0 (LC), 5c787 (BRUN) write straight into DAT_0012ed74; pacing by int 21h 2Ch
   hundredths with delay DAT_0009e704 (`fli_file_wait_frame_5c813`). `cue_script_step_17d80` still calls
   the preloader, so the buffer globals are live even though the player itself is dead code.

### Front end helpers

- `fe_wait_ticks_or_input_529d0(callback, n)` waits n*0x78 ticks (DAT_0012eab4) or any key/mouse event, and
  `fe_check_shift_q_quit_52980` makes Shift+Q (scancodes 0x2a/0x36 + 0x10) quit to DOS from the logo/title
  screens (DAT_0009e504 = 1, player record +0x340f = 1).
- `fe_lobby_slot_connecting_55920` / `fe_lobby_slot_clear_55a60` are the progress callbacks the NetBIOS
  layer (`net_add_name_4e530`, `net_call_4e600`) invokes to animate lobby slot p (DAT_0009e522[p*3] = 1 / 0)
  and redraw the session name cfg+0x75 in the rect (0xad, 0x2a, 0x5a x 0xf).
- `fe_dialog_draw_title_a_578a0` / `_b_57930` are confirm-dialog draw callbacks that centre the strings at
  DAT_000add70 / DAT_000add74 (string pointer globals; their contents were not resolved).
- `fe_fli_composite_bg_579c0` is the FLI frame callback used by the text dialog: colour 0 pixels of the
  back buffer are replaced by the background copy (64000 pixels, i.e. 320x200 only).
- Text editing: `ui_str_prepend_58820`, `ui_str_delete_at_58880`, cursor via `gfx_fill_rect_clipped_5940c`
  (clipped to the sprite window DAT_0012ed80/88/90/98/a4/a8; only fill mode 0 implemented).
- `ui_font_relocate_glyphs_58f30` adds the font base to every glyph offset smaller than the base (6-byte
  entries), making font files position-independent after loading.

### Texture cache (tmaps) container

`tmap_cache_create_4c280` builds a 0x1a-byte header: +0 total size, +4 free bytes, +8 directory
(0x211 x 0xe entries {+0 data ptr, +4 size, +8 order, +0xa slot, +0xc id}), +0xc index (0x211 x u32 entry
ptrs in insertion order), +0x10 data area, +0x14 count, +0x16 capacity, +0x18 mode (1 = four mem_alloc
blocks, 2 = carved in place from the pool cfg+0xa8 / size cfg+0xac). `tmap_cache_add_4c460` appends at
data + size - free; `tmap_dir_find_by_id_4c750`, `tmap_dir_find_free_4c3d0`, `tmap_cache_clear_4c6ee`,
`tmap_cache_destroy_4c790` complete the API (DAT_000adf58 holds the cache, DAT_000adf50 the anim table;
both created in 4bbf0 and torn down in 4bc80). DAT_000b9580[0x211] marks always-resident textures
(`texture_mark_resident_4c0a0` from a 14-byte record list when DAT_000987e8, loaded by
`texture_load_resident_4c0f0`).

### CPU detection

`cpu_detect_5ac80` -> 5ace8 -> `cpu_identify_5ad03` (Intel's CPUID sample: AC-bit toggle for 386/486,
ID-bit for CPUID; DAT_0009e59c family, DAT_0009e59d GenuineIntel, DAT_0009e59e model, DAT_0009e59f
stepping, DAT_0009e5a6 raw CPUID(1), DAT_0009e5aa vendor string, DAT_0009e5b6 CPUID available) and
`cpu_detect_fpu_5adfe` (DAT_0009e5a0 FPU present, DAT_0009e5a1 2 = 287 / 3 = 387, DAT_0009e5a4 control
word). cfg+8 = (family == 5), i.e. Pentium.

### HMI timer / MIDI internals touched

`hmi_timer_set_event_rate_5d235` (sosTIMERAlterEventRate) and `hmi_timer_get_event_rate_5d558` use the
tables already noted for 5d093 (DAT_000a3c59 callbacks, DAT_000a3cb9 rates, DAT_000a3cf9 increments,
DAT_000a3d39 accumulators, DAT_000a3c55 PIT divisor, clock 0x1234DC). `hmi_midi_send_event_5d69b` is the
sosMIDI event sender with dynamic channel allocation/stealing (logical->physical map DAT_000a1847, owner
DAT_000a1b17, priority DAT_000a1ac7, enabled mask DAT_000a2515, per-channel program/bend/volume/pan state
DAT_000a1bb7..bb, master volume DAT_000a2565, driver function table DAT_0009f91a + driver*0x24).

### Open questions

- Which device sits behind ports 302h/303h in the stereo page functions (50370/503d0)?
- Meaning of the VFX1 driver function 6008h (`vfx1_driver_cmd_6008_50120`) and the 256-byte block sent by
  `vfx1_driver_send_block_4fca0` during init (palette? configuration?).
- What main-menu item 2 (`fe_menu_text_dialog_57580`) edits with its two text fields (buffers were passed
  in registers), and the contents of DAT_000add70/74 (dialog title strings).
- The exact HMI device-id constants (0xA001/A002/A004/A008) should be checked against the HMI SOS headers.
- `fli_file_chunk_dispatch_5c594` body was dropped by the decompiler; confirm it is the type switch.
