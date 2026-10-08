# Agent 2, region E (0x5e000-0x80000): Watcom CRT, HMI SOS driver layer, OPL2/GUS MIDI drivers, Watcom graph.lib console

All 108 unnamed functions in todo2_E.txt and the 12 UNCERTAIN names were read (decompiled body, callers, and
capstone disassembly where the C was garbled). Nothing in this range is a tail fragment in the "no prologue,
falls into neighbour" sense; the two asm ISR tails (0x667fe, 0x62c2c) are real jump targets and are named as
such. Confidence: `?` suffix = low.

## CSV

```csv
0x0005e4b8,hmi_midi_send_event_5e4b8,"sosMIDISendMIDIData: calls MIDI driver function 0 (far ptr at DAT_0009f91a + handle*0x24) with (handle, event ptr, word, dword); game callers set a 3-byte MIDI message at DAT_000acb60 first"
0x00063f76,hmi_digi_lock_code_63f76,"sosDIGI lock list: 16 dpmi_lock_region_66e29 calls over the digital/timer code (0x632b8, 0x6cee2, 0x6290d, 0x63f65+0xdb1, 0x6cf99, 0x64e6b+0xa0c, 0x5cf8a+0x6ef, 0x635cc+0x988, ISR 0x66608+0x238, 0x66e28+0x4af) and data (DAT_00131964, DAT_000a2a50+0x1030); digital twin of snd_hmi_lock_code_5f04d"
0x0006413b,hmi_digi_unlock_code_6413b,Exact mirror of 63f76 with dpmi_unlock_region_66e56 on the same 16 regions (sosDIGIUnInitSystem path)
0x00064c3f,hmi_digi_alloc_dma_buffer_64c3f,"Allocates (size>>4)+4 paragraphs of locked DOS memory via dpmi_alloc_locked_dos_mem_66f24 and re-allocates while (linear & 0xffff) > 0xffff-size, i.e. until the block does not cross a 64K DMA page; then dpmi_alloc_selector_66ede for it. Called by snd_digi_init_driver_64386 when the driver needs its own DMA buffer"
0x0006594f,crt_strtol_core_6594f,"Watcom __strtol/__strtoul core: skips ctype-space (table 0x937b4 bit 2), sign, auto base (0x -> 16, 0 -> 8, else 10), base must be 2..36 else errno; accumulates via crt_radix_digit_65ab6 with overflow flag; endptr store; signed mode (arg4 = 1) clamps to 0x7fffffff/0x80000000, unsigned to 0xffffffff with errno ERANGE"
0x00065ab6,crt_radix_digit_65ab6,"Digit value for strtol: '0'-'9' -> 0..9, a-z/A-Z (via crt_tolower) -> 10..35, anything else -> 0x25 (37, > any base)"
0x000667fe,hmi_timer_isr_chain_old_667fe,"asm tail of the HMI IRQ0 ISR at 0x66609: if chain flag DAT_000a3a7c set, clears in-handler flag DAT_000a3a7a, reloads SS:ESP from DAT_000a2a72 (saved by the ISR), xchg's the saved old int 8 vector (DAT_000a2a54 off / DAT_000a2a58 sel) into the frame at [ebp+0x28/0x2c], pops DS/GS/FS/ES/regs and retf's into the previous handler. Not a C function"
0x00066852,hmi_make_far_ptr_66852,"Returns EDX:EAX = (selector word arg2, offset arg1): HMI MK_FP helper used by snd_midi_init_song_5e53a / snd_midi_song_setup_5ec41"
0x00066de2,hmi_midi_slot_is_free_66de2,Returns DAT_0009ea46[handle] == 0 (MIDI driver slot not loaded); used by snd_midi_init_driver_5fa77
0x00066e83,hmi_digi_init_channel_ptrs_66e83,"Fills 32 six-byte far pointers (out[i] = base + i*0x6c, selector from arg) = the 32 digital channel records of 0x6c bytes inside the driver data; called from snd_digi_init_driver_64386"
0x00066fb8,hmi_det_drv_get_caps_66fb8,"hmidet.386 driver function 2: calls entry(2), copies the returned 0x6a-byte capabilities struct (EDX) to the caller buffer and patches the selector word of four far pointers at +0x34 +0x3c +0x44 +0x4c; used by snd_digi_detect_find_6379a"
0x0006700c,hmi_digi_drv_get_info_6700c,Digital driver function 8: stores returned far pointer (EDX offset + selector) into *arg; wrapped by hmi_digi_get_driver_info_6cef3
0x00067041,hmi_digi_drv_stop_67041,Digital driver function 5 (called by snd_digi_uninit_driver_64ab8 before unloading)
0x0006706c,hmi_det_drv_find_hardware_6706c,hmidet.386 function 0 (detect hardware); called by snd_digi_detect_find_6379a and snd_digi_detect_settings_63d88
0x00067097,hmi_det_drv_get_settings_67097,"hmidet.386 function 1: AX -> DAT_000a3f94 (port), CL -> DAT_000a3f98 (IRQ), CH -> DAT_000a3f9c (DMA); results copied into the caller's _SOS_HARDWARE by snd_digi_detect_settings_63d88"
0x00067114,hmi_digi_drv_uninit_67114,Digital driver function 1 (uninit) called by snd_digi_uninit_driver_64ab8
0x0006713f,hmi_digi_drv_get_entry_points_6713f,"Digital driver function 10: entry(10, a, b, c) returns three offsets (stack, ECX, ESI) that are converted to far pointers driver+off with CS, DS, DS and stored in a 3 x 6-byte out array (mixer/timer callbacks of the loaded driver)"
0x000671a1,hmi_digi_drv_init_hw_671a1,"Digital driver function 0 with (port, IRQ|DMA packed byte, ...) = hardware init; snd_digi_init_driver_64386"
0x000671da,hmi_digi_drv_start_671da,"Digital driver function 4 (after dos_vds_lock_region / DMA buffer setup) = start output; snd_digi_init_driver_64386"
0x0006720b,hmi_digi_drv_call2_6720b,"Digital driver function 2 with (handle, word arg) during init (DMA buffer / rate setup); exact meaning not recovered"
0x0006723a,hmi_digi_drv_call3_6723a,"Digital driver function 3 with constant 8 during init (sample format / bits?); exact meaning not recovered"
0x000672e9,hmi_midi_get_callback_driver_table_672e9,"Returns DS:0x1313a2 = 6-entry far-pointer table of the internal pass-through MIDI driver (device 0xa003) whose fn0 thunk 0x6730a forwards each event to user callback DAT_0009fae6 and fn4 (0x67385) sets that callback; snd_midi_init_driver_5fa77 copies it into DAT_0009f91a + handle*0x24"
0x000673d1,hmi_midi_get_wavemidi_driver_table_673d1,"Returns DS:0x131384 = far-pointer table of the internal digital-sample MIDI driver (device 0xa005; thunks 0x673f2/0x676f9/0x67878/0x678f6/0x6790f, which pop the 32-entry note queue 67c27 and call snd_digi_stop_sample_65485)"
0x00067adc,hmi_wavemidi_queue_init_67adc,"Per-handle 32-entry ring at 0x9faec (entry 0xc bytes: +0 key, +4 event ptr, +8 word), stride 0x180: clears entries, head DAT_000a026c = 0x1f, tail DAT_000a0280 = 0, capacity DAT_000a0294 = arg2 (count at DAT_000a02a8)"
0x00067b63,hmi_wavemidi_queue_push_67b63,"If count < capacity: head = (head+1) mod 32, entry.key = *(short*)(event+0x12), entry.ptr = event, entry.word = arg3, count++; returns slot or -1"
0x00067c27,hmi_wavemidi_queue_pop_67c27,"Removes the oldest entry at tail DAT_000a0280 (clears it, tail = (tail+1) mod 32, count--); returns the slot index or -1 if empty"
0x00067cc1,hmi_wavemidi_queue_remove_at_67cc1,"Deletes entry at index arg2: shifts entries from tail forward (i <- i-1 mod 32) until the tail slot, clears tail slot, advances tail, count--; returns 0 or -1 if empty"
0x00067ded,hmi_wavemidi_queue_remove_key_67ded,"Finds the entry whose +0 key == arg2 (linear scan of 32 slots), then same shift-delete as 67cc1; returns index or -1"
0x00067f7e,hmi_midi_load_driver_67f7e,"Loads an external MIDI driver from the hmidrv.386 image: device id must be 0xa000..0xa200 (else 6) and slot DAT_0009ea46[h] free (else 9, h >= 6 -> 10); strcpy/strcat path, open (fail 0xf), read 0x2c-byte file header to DAT_0009fa8a, scan 0x2c-byte device records (DAT_0009fa8b) until id matches, malloc (fail 5) + dpmi_alloc_selector + read image + dpmi_lock_region; returns far ptr in out struct, marks slot loaded"
0x0006816f,hmi_midi_free_driver_slot_6816f,"Clears DAT_0009ea46[handle] (loaded flag); 10 if handle >= 6 or not loaded; snd_midi_uninit_driver_5ff24"
0x0006834f,hmi_midi_get_gus_driver_table_6834f,"Returns DS:0x131348 = far-pointer table of the internal Gravis UltraSound MIDI driver (device 0xa008; thunks 0x68370/0x68536/0x685a7/0x685c5/0x685de dispatch status bytes 0x80..0xe0 to snd_gus_note_on/off etc.)"
0x00068619,snd_opl_pitch_bend_68619,"OPL2 driver: for channel < 16 (not 9) store bend MSB in DAT_000a40e4[ch], flag DAT_000a4124[ch] = 1, then for each of 9 voices on that channel (DAT_000a401c[v] != 0 && DAT_000a40c0[v] == ch) recompute F-number (686db) and write it (699e1)"
0x000686db,snd_opl_calc_bent_freq_686db,"Pitch-bend frequency: note = arg2-12 reduced mod 12, base F-num from table DAT_000a4320[note]; bend < 0x40 interpolates down, >= 0x40 up, by bend-range semitones DAT_000a4164[ch] (octave wrap via block bits 0x1c00 and DAT_000a44b8/44bc); returns F-num|block word"
0x00068893,snd_opl_controller_68893,"OPL2 MIDI CC dispatch: 7 -> snd_opl_set_channel_volume_704cd; 0x40 sustain -> DAT_000a424c[ch], on release replays queued note-offs (DAT_000a42ac, count DAT_000a42a8) through snd_opl_note_off_68eca; 0x66 (102) -> bend range DAT_000a4164[ch]; 0x79 -> snd_opl_reset_controllers_69072; 0x7b -> snd_opl_all_notes_off_68fc9"
0x000689da,snd_opl_note_on_689da,"OPL2 note on: channel 9 = percussion bank (DAT_000a4074), else melodic; snd_opl_alloc_voice_70697, key off, 10 register writes to silence operators, snd_opl_load_timbre_69d61 for program DAT_000a4080[ch] (0x1e-byte timbre at DAT_000a4058 + prog*0x1e), velocity -> DAT_000a41e8[v], key on (69b31), voice note DAT_000a401c[v] = note, applies pending bend"
0x00068eca,snd_opl_note_off_68eca,"OPL2 note off: if sustain DAT_000a424c[ch] on and queue < 16 -> push 3-byte event to DAT_000a42ac[DAT_000a42a8++]; else key off (69bf3) every voice whose note == event note and channel == ch, clearing DAT_000a401c[v]"
0x00068fc9,snd_opl_all_notes_off_68fc9,"CC 0x7b: volume DAT_000a41a8/41e8 = 0x7f, DAT_000a420c = 0, sustain off, key off all voices on channel (ch < 16, or 9 if drum bank loaded)"
0x00069072,snd_opl_reset_controllers_69072,"CC 0x79: volume 0x7f, expression 0x7f, DAT_000a420c = 0, sustain 0, bend DAT_000a40e4 = 0x40 (centre), bend range DAT_000a4164 = 2"
0x0006911c,snd_opl_midi_event_6911c,"OPL2 driver fn0 (via thunk 0x69849): dispatch on status & 0xf0: 0x80 / 0x90 vel 0 -> note off (DAT_000a4296..98 scratch), 0x90 -> note on, 0xb0 -> controller, 0xc0 -> program change (7075e), 0xe0 -> pitch bend"
0x00069319,snd_opl_reset_state_69319,"OPL2 driver fn3 (thunk 0x698d1): snd_opl_silence_all_69c73, DAT_000a41a4 = 0 (bank state), voices: note 0 / channel 0 / vel 0x7f; 16 channels: bend 0x40, program 0, bend flag 0, volume 0x7f, 420c 0"
0x00069401,snd_opl_set_timbre_bank_69401,"OPL2 driver fn4 = sosMIDISetInsData (thunk 0x698ef): packs the AdLib .BNK (6955a) then first call stores melodic bank (far ptr DAT_000a404c, count from bank+8, name table bank+0xc, timbre data DAT_000a4058 = bank + *(bank+0x10)) and sends program 0 to all 9 voices; second call stores percussion bank (DAT_000a4062.., DAT_000a4074 = 1)"
0x0006955a,snd_opl_pack_timbre_bank_6955a,"In-place conversion of each 30-byte AdLib BNK instrument record (count = *(short*)(bank+8) - 2 at bank + *(bank+0x10)) into OPL register bytes: +0xb = AM|VIB|EG|KSR|MULT, +2 = KSL|TL, +5 = AR|DR, +6 = SL|RR, +0xe = FB|CNT (x2), second operator likewise at +0x18/+0xf/+0x12/+0x13"
0x0006977c,snd_opl_driver_uninit_6977c,"OPL2 driver fn2 (thunk 0x698b3): DAT_000a3fa0 = port DAT_000a3fa8, re-init chip (69940), silence all voices (69c73), clear initialised flag (69f6a: DAT_000a402b = 0), DAT_000a4040 = 0"
0x000697e1,snd_opl_driver_init_697e1,"OPL2 driver fn1 (thunk 0x69878): DAT_000a3fa4 = port from caller's hardware struct, snd_opl_init_chip_69940, snd_opl_clear_voices_69a77, DAT_000a4040 = 1"
0x00069940,snd_opl_init_chip_69940,"Accepts port 0x388 or 0x380 only (else returns 1): DAT_000a4027/DAT_000a3fa8 = port, one register write, rhythm reg shadow DAT_000a4010 = 0, snd_opl_clear_voices_69a77, DAT_000a402b = 1 (chip ready)"
0x000699e1,snd_opl_voice_set_freq_699e1,"Writes F-number low byte to shadow DAT_000a3fec[v] (reg 0xA0+v) and block|0x20 key-on to DAT_000a3ff5[v] (reg 0xB0+v); two snd_opl_write_69f9c calls"
0x00069a77,snd_opl_clear_voices_69a77,"Key off all 9 voices (0xB0+v = 0), timbre-loaded flags DAT_000a4011[0..10] = 0, DAT_000a402c = 0, rhythm reg DAT_000a4010 &= 0xc0 and rewrite (reg 0xBD)"
0x00069b31,snd_opl_voice_key_on_69b31,Same two frequency writes as 699e1 plus a third register write; marks voice active DAT_000a401c[v] = 1
0x00069bf3,snd_opl_voice_key_off_69bf3,"If voice inactive returns 6; else clears bit 0x20 in DAT_000a3ff5[v], writes reg 0xB0+v, DAT_000a401c[v] = 0, returns 0"
0x00069c73,snd_opl_silence_all_69c73,"Returns 2 if chip not initialised (DAT_000a402b == 0); else DAT_000a4010 = 0 (0xBD), 9 writes key-off + 9 writes level, timbre flags cleared"
0x00069d61,snd_opl_load_timbre_69d61,"Programs voice v with a packed 0x1e-byte timbre: operator offsets from table DAT_000a402d/402e[v*2] (modulator/carrier slots), level shadows DAT_000a3fdc[op], registers 0x20/0x40/0x60/0x80/0xE0 for both operators and 0xC0 feedback (DAT_000a3fbc[op] shadow); DAT_000a4011[v] = 1"
0x00069fd6,hmi_strcpy_far_69fd6,"Copies a NUL-terminated string two bytes per iteration and returns the destination as EDX:EAX far pointer (arg2 = selector); used by sosMIDI/DIGI InitSystem to store the driver directory string (DAT_000a3d8a)"
0x0006cef3,hmi_digi_get_driver_info_6cef3,"Returns 1 if driver slot DAT_000a3a84[h] (6-byte far ptr) is empty, 2 if the out pointer is null, else driver function 8 (6700c) and 0"
0x0006d3b2,hmi_digi_check_far_ptr_6d3b2,Returns 2 if the far pointer (off=0 and sel=0) is null else 0; parameter check in snd_digi_uninit_driver_64ab8
0x0006e2b8,snd_gus_note_in_range_6e2b8,"GUS patch layer key-range test: arg1 packs low byte = low key, high byte = high key; returns 1 if low <= note <= high; snd_gus_patch_lookup_6e351"
0x0006e2e6,snd_gus_patch_adjust_param_6e2e6,"Patch record (stride 0x7c at DAT_000a55ec): idx < 4 -> dword[idx] += val; idx == 4 -> word at +0x10 += val; else word at +8 + idx*2 = val"
0x0006ee44,snd_gus_cc_bank_select_6ee44,"Stores CC value in channel word +0x12 (DAT_000a5400[ch*0x1e]) and, if it was 0xffff (unset), also in +0x10 (DAT_000a53fe); channel record stride 0x1e from DAT_000a53ee"
0x0006ee78,snd_gus_cc_scaled_a_6ee78?,"Channel byte +1 (DAT_000a53ef) = ((val*45)/100)*2, i.e. 0..90*2 scaling; one of the modulation-depth style controllers (exact CC not recovered)"
0x0006eea1,snd_gus_cc_scaled_b_6eea1?,"Channel byte +2 (DAT_000a53f0) = ((val*45)/100)*2; sibling of 6ee78"
0x0006eeca,snd_gus_cc_pan_6eeca,"Channel byte +3 (DAT_000a53f1) = -2*val-2 (CC 10 pan: 63 -> 0x80 centre, which is the default set by 6f2a5)"
0x0006eeea,snd_gus_cc_volume_6eeea,"Channel byte +5 (DAT_000a53f3, default 100 = GM CC7 default) = val; then for each of 30 voices (DAT_000a516c, 10-byte entries, high byte of word 0 = channel) peek/poke GUS volume registers"
0x0006eff1,snd_gus_cc_expression_6eff1,"Channel byte +4 (DAT_000a53f2, default 0x7f = CC11 default) = val, then re-applies volume via 6eeea"
0x0006f01a,snd_gus_cc_sustain_6f01a,"CC 0x40: val < 0x40 -> sustain off (channel byte +0 = 0) and releases held voices (word0 low byte 0xff = released-while-sustained marker) by setting entry 0xffff and poking voice stop; else sustain flag = 1"
0x0006f0bc,snd_gus_pitch_bend_apply_6f0bc,"Channel word +6 (DAT_000a53f4) = bend/30; for every active voice on the channel re-reads and rewrites the GUS frequency register"
0x0006f166,snd_gus_cc_nrpn_lsb_6f166,"Channel word +0x14 (DAT_000a5402) = 0x100 (parameter-select marker), low byte of +0x16 (DAT_000a5404) = val (CC 98/100 low)"
0x0006f192,snd_gus_cc_nrpn_msb_6f192,"Same marker, high byte of +0x16 = val (CC 99/101 high)"
0x0006f1b4,snd_gus_cc_data_entry_msb_6f1b4,"If parameter select == 0x100 and +0x16 == 0 (RPN 0 = pitch bend range): high byte of +0xa (DAT_000a53f8, default 0x200 = 2 semitones) = val, returns 0; else 1"
0x0006f233,snd_gus_cc_data_entry_lsb_6f233,Same guard; low byte of bend range word +0xa = val (cents)
0x0006f2a5,snd_gus_reset_controllers_6f2a5,"CC 0x79 defaults for channel (ch & 0xf): +1/+2 = 0, pan +3 = 0x80, expression +4 = 0x7f, volume +5 = 100, bend +6 = 0, sustain +0 = 0, +0xc = 0, bend range +0xa = 0x200, +0x16/+0x18 = 0xffff, bank +0x10 = 0, +0x12 = 0xffff"
0x0006f33b,snd_gus_all_notes_off_6f33b,"CC 0x7b: every voice entry (30 x 10 bytes at DAT_000a516c) whose channel nibble matches -> entry = 0xffff and poke voice stop"
0x0006f6ee,snd_gus_set_bend_6f6ee,"Channel word +8 (DAT_000a53f6) = val/30, then snd_gus_pitch_bend_apply_6f0bc; no callers (driver-internal entry)"
0x0006f801,snd_gus_setup_regs_a_6f801?,"If arg >= 8 returns 1; else 3 byte pokes + 4 word pokes to the GUS (voice/DMA setup for index 0..7); no callers"
0x0006f895,snd_gus_setup_regs_b_6f895?,"If arg >= 8 returns 1; else 0x1c consecutive byte pokes (register block init for index 0..7); no callers"
0x0007075e,snd_opl_program_change_7075e,DAT_000a4080[channel] = program (from the 3-byte event); used by 6911c and by 69401 to reset voices to program 0
0x00071161,crt_init_std_streams_71161,"Watcom __InitFiles: sets stderr (third FILE, flag byte 0xa287d) to _IONBF (bits 8-10 = 4), walks the FILE table 0xa283c (0x1a stride) while _flag (+0xc) != 0 and links each into the __OpenStreams list DAT_001319f4 with 8-byte {next, FILE*} nodes (nmalloc, fatal error on failure); DAT_00131460 = terminator flag"
0x0007136e,crt_fdfs_7136e,"Watcom __FDFS double->float: exponent 0 -> 0; shifts mantissa left, rounds (+0x20000000), inf (0x7f800000|sign) if biased exponent >= 0x8fe00000 after shift, 0 if below 0x70200000, else rebias by 0x70000000 and shift 2; used by the scanf float path 6c8c1 to store %f"
0x000713ba,crt_setenvp_713ba,"Watcom __setenvp: counts strings in the DOS environment (FS = DAT_000a46b5 selector, offset DAT_000a46b1), nmallocs the pointer array (_environ = DAT_000a46c0) and a copy of all strings (DAT_000a46c4), fills pointers, zero-terminates"
0x00071a4f,crt_fatal_runtime_error_71a4f,"Watcom __fatal_runtime_error(msg, code): calls crt_debugger_trap_71d84(DS, msg); if the debugger did not take it, 71a42 -> crt_fatal_error_6275c (writes msg, exits). Callers: crt_no_fp_support_70860, heap allocator 70997, stream init 71161"
0x00071d84,crt_debugger_trap_71d84,"If DAT_000ac138 (debugger-present byte set by the Watcom debugger) != 0: pushes (selector, msg) and executes int 3 followed by the 'WVIDEO' signature bytes, returns 1; else returns 0"
0x0007998d,con_calc_page_size_7998d,"graph.lib text init: page bytes = rows DAT_001319aa * cols DAT_001319a8 * 2 rounded to 256 -> BIOS data area 0x44c (DAT_000a4655 = 0x400 base); pages DAT_001319b0 = (16K if DAT_001319b8 == 0x40 else 32K) / page, max 8"
0x00079b20,con_reinit_video_state_79b20,"If DAT_000a45b8 (re-init needed): con_init_video_segments_7a6ef, con_init_799fc, text window = whole screen (DAT_001319ee/e2/e8/ec/ea/e6), cursor row/col DAT_001319a0/a2 from BIOS 0x450 cursor word"
0x00079c48,con_hide_cursor_79c48,"If cursor shown (DAT_000a45ca): text mode (DAT_000a45b9 == 0) -> int 10h AH=1 with bit 5 set via 79cb2(0); graphics mode -> if BIOS cursor pos (0x450 + 2*page DAT_000a45cc) equals our row/col, XOR the cell back (79cd9); DAT_000a45ca = 0. Starts at 0x79c4a"
0x00079cd9,con_xor_cursor_cell_79cd9,"Graphics-mode text cursor: _setplotaction(4 = _GXOR), colour = (ncolours DAT_001319ac - 1) & 0xf, computes the cell rectangle at cursor (pixel size = DAT_001319a4/a8 x DAT_001319a6/aa), draws it via con_draw_glyph_clipped_7a819, restores plot action and colour"
0x00079d7c,con_require_graphics_mode_79d7c,"_GrStatus DAT_000a45ba = 0; if not in graphics mode (DAT_000a45b9 == 0) status = -3 (_GRNOTINPROPERMODE); returns the graphics flag. Second entry at 0x79da0 also hides the cursor"
0x00079dc4,con_show_cursor_79dc4,"If cursor enabled (DAT_000a45c8 == 1) jumps to 0x79c1a: if not already shown, text mode -> cursor shape visible (79cb2(1)), graphics -> XOR cell (79cd9); DAT_000a45ca = 1. Bytes 0x79dd6-0x79e01 are four device-dispatch thunks calling [DAT_001319ba]+0/4/8/0xc (79df0 = begin-draw, 79df9 = end-draw)"
0x00079e02,con_scroll_text_window_79e02,"graph.lib _scrolltextwindow(n): text mode -> con_move_text_rows_7a116 up or down (arg2 == 2 = down) inside window DAT_001319ea/ec..e6/e8 then con_clear_text_rows_7abfb for the vacated rows; graphics mode (0x79ee3, dropped by the decompiler) -> con_scroll_window_gfx_79fd8"
0x00079fd8,con_scroll_window_gfx_79fd8,"Graphics-mode scroll: row image bytes = con_image_row_bytes_7ad40 rounded to 4; if stack (DAT_000a4690 low) - size < 0x101 -> status -5 (_GRINSUFFICIENTMEMORY); else alloca buffer, plot action _GPSET, for each row get-image via device fn [tbl+0x10] into buffer and put-image at row+/-delta via device table pcRam00000044/40 entries, restores action"
0x0007a116,con_move_text_rows_7a116,"Text mode row move: src/dst = video base (DAT_000a4659 B000 for mode 7 else DAT_000a465d B800) + (row*cols + left)*2, copies (right-left+1)*2 bytes per row for n rows stepping by DAT_001319a8*2*dir"
0x0007a1ec,con_put_char_7a1ec,"graph.lib character output: text mode writes (attr DAT_000a45c5 << 8 | char) at page base (BIOS 0x44c * page DAT_000a45cc) + (row*cols+col)*2; graphics mode (blocks dropped by decompiler) uses built-in 8x8 font 0xaa660 or 8x14 font 0xab300, clears the cell with con_fill_cell_bg_gfx_7ab7e, _GXOR when attr bit 7 and 256 colours, then blits the glyph via con_draw_glyph_clipped_7a819"
0x0007a6b1,con_probe_crtc_7a6b1,"Writes CRTC index 0x0f at port, saves data, writes 0x5a, reads back, restores; returns true if 0x5a read back (CRTC present at 3B4h/3D4h). Used by vga_detect_mono_7a611 and 0x7a699 (colour probe at 3D4h)"
0x0007a6ef,con_init_video_segments_7a6ef,"For extender type DAT_000a46ae 1 (DOS4GW) or 9: BIOS data base DAT_000a4655 = 0x400, mono text DAT_000a4659 = 0xb0000, colour text DAT_000a465d = 0xb8000, graphics 0xa0000 -> DAT_000a4661, ROM DAT_000a4665 = 0xc0000, all selector words DAT_000a464b..4653 = DS (flat); DAT_001319e0 = DS"
0x0007a794,con_get_plot_action_7a794,"Maps internal mode DAT_000a45f3 to _getplotaction values: 0 -> _GOR(3), 1 -> _GXOR(4), 2 -> _GPRESET(1), 3 -> _GPSET(0)"
0x0007a7c0,con_set_plot_action_7a7c0,"_setplotaction(BX): returns previous via 7a794, then DAT_000a45f3 = 3 for _GPSET(0), 2 for _GPRESET(1), 1 for _GXOR(4), 0 for _GAND/_GOR; bracketed around every text draw"
0x0007a819,con_draw_glyph_clipped_7a819,"Cohen-Sutherland clip of the cell rectangle (7b1d5); if any part visible calls con_draw_glyph_gfx_7a922"
0x0007a922,con_draw_glyph_gfx_7a922,"begin-draw thunk 79df0, device fn [0x10] (set address), con_gen_glyph_blitter_7b408, end-draw thunk 79df9"
0x0007a9e3,con_set_color_7a9e3,"_setcolor: if not in graphics mode (79d7c sets status -3) returns -1 else con_set_color_raw_7aa06"
0x0007aa06,con_set_color_raw_7aa06,DAT_000a45d4 = colour & (ncolours DAT_001319ac - 1); returns previous colour
0x0007ab7e,con_fill_cell_bg_gfx_7ab7e,"Fills a text cell rectangle in graphics mode: saves plot action, fill mask (7b5bd) and colour (7a9dc), solid mask, _setcolor(bg), con_fill_rows_7b665, restores colour, mask and plot action"
0x0007abfb,con_clear_text_rows_7abfb,"Text mode: fills rows top..bottom, cols left..right of the B800/B000 buffer with 0x0720 (space, attr 7)"
0x0007ad40,con_image_row_bytes_7ad40,"Bytes per image row: planar (DAT_001319dc & 1) -> ((w+7)/8) * planes DAT_001319ae; packed -> (w*bpp+7)/8"
0x0007b123,con_clip_outcode_7b123,"Cohen-Sutherland outcode of (dx, dy) relative to the clip box: x<0 -> 8, x>0 -> 4, y<0 -> 1, y>0 -> 2; any nonzero sets _GrStatus = 2 (_GRCLIPPED)"
0x0007b16d,con_clip_line_step_7b16d,"Moves endpoint (x,y) along the segment to the clip edge value: y += (|ex-x| * (ey-y) * 2 +/- |cx-x|) / (2*|cx-x|), x = cx (rounded intercept)"
0x0007b1d5,con_clip_rect_cs_7b1d5,"Cohen-Sutherland loop over two endpoints using 7b123/7b16d; returns code1 & code2 (nonzero = completely outside, caller skips drawing)"
0x0007b408,con_gen_glyph_blitter_7b408,"Generates an x86 loop on the stack from three device code fragments (length byte before each): 66 D1 C3 73 (shl bx,1 / jnc skip) + plot fragment, 4A 7C (dec edx / jl) , 81 EE imm (sub esi) 7F (jg) 81 C6 imm (add esi) + next-row fragment, EB back, C3; needs >= 0x101 bytes stack headroom (DAT_000a4690) else _GrStatus = -5; then calls it with (x, height>>1, y, bits)"
0x0007b5bd,con_get_fill_mask_7b5bd,"_getfillmask: copies the 8-byte mask DAT_000a45d8 to the caller buffer; returns NULL if no mask set (DAT_000a45e8 == 0)"
0x0007b5ec,con_set_fill_mask_7b5ec,"_setfillmask: NULL -> restore default mask DAT_000a45e0 and DAT_000a45e8 = 0; else copy 8 bytes and set DAT_000a45e8 = (mask != default)"
0x0007b665,con_fill_rows_7b665,"begin-draw, for y = y1..max(y1,y2) con_fill_scanline_7b8dc, end-draw (filled rectangle core used for cell backgrounds)"
0x0007b8dc,con_fill_scanline_7b8dc,"If width > 0: device fn [0x10] set address, then device fn [EBX+0x38] (solid) or [EBX+0x3c] (masked, mask byte DAT_000a45d8[|y| & 7]) with (colour DAT_000a45d4, 0, mask<<8|.., width)"
0x000604c0,vga_draw_rect_outline_640_604c0,"CONFIRMED (correcting both comments): four gfx_fill_rect_clipped_6acd4 edges; 6acd4 writes at pitch 0x280 with unscaled coords = 640x480 (hi-res) variant. 603f0 (via 6abbc, coords >>1 onto pitch 0x140) is the 320x200 variant, so its comment 'hi-res variant' is reversed"
0x00060688,ui_draw_sprite_60688,"CORRECTED: not a rect fill. (x, y, sprite record*) -> vga_draw_sprite_spans_6070d(record->data, w = rec[4], h = rec[5], x, y, 0, 0) into DAT_0012ed74; byte-identical duplicate of 606c0 (which is also misnamed vga_fill_rect)"
0x00062c2c,hmi_isr_chain_to_vector_62c2c?,"asm, no prologue: takes (old offset, old selector) from the stack, ESP = EBP (ISR frame), xchg them into [ebp+0x28]/[ebp+0x2c] (saved CS:EIP), pops GS FS ES DS EDI ESI EBP EBX EBX EDX, retf -> continues in the previous interrupt handler. Same idiom as the timer ISR tail 667fe; the ISR that jumps here was not located (no C xref)"
0x000631ca,crt_getenv_631ca,"CONFIRMED real getenv: walks _environ DAT_000a46c0, crt_strnicmp_62bc8 on the name length and requires '=' right after; returns pointer past '='. The old 'returns 0' note was a decompiler artefact"
0x0006a093,hmi_midi_bind_driver_table_6a093,"RENAMED (was crt_init_fpe_table): for an externally loaded MIDI driver calls its entry (1) then fills 5 far pointers (driver base + FS:[off] from the driver header, 8-byte stride, selector CS) into the handle's function table DAT_0009f91a + h*0x24 (+6); the hmidrv.386 analogue of the internal tables 672e9/673d1/6834f"
0x0006a5f1,crt_prtf_fixed_point_6a5f1,"CONFIRMED: 16.16 fixed-point %f replacement: sign, default precision 4 (spec+8 == -1), itoa of integer part, fraction digits by repeated *10 of the low word, round up when bit 15 of the remainder is set (carry propagation through '9's and '.')"
0x0006cc04,crt_scnf_integer_6cc04,"RENAMED (names were swapped with 6c8c1): integer conversion for %d/%i/%o/%u/%x: width handling via spec+0xc, optional sign, radix auto-detect (0x -> 16, 0 -> 8), crt_hexdigit_value_6ce2d accumulation, ':' allowed for %i with flag 0x80, stores int/short/far by flags 1/4/8/0x10 of spec+0x10. 6c8c1 (collects digits, '.', exponent and calls crt_fdfs_7136e) is the float path"
0x000704cd,snd_opl_set_channel_volume_704cd,"CORRECTED meaning: CC 7 handler: DAT_000a41a8[ch] = volume, DAT_000a420c[ch] = 1, then for every active voice on the channel rewrites the operator level registers (two snd_opl_write_69f9c; carrier only if timbre byte +0xe says additive)"
0x00070697,snd_opl_alloc_voice_70697,"CONFIRMED: first free voice (DAT_000a401c[v] == 0); else steal a voice belonging to a channel without pending bend (DAT_000a4124 == 0); else voice = note % 9 fallback (arg - 9 if > 8)"
0x000717ff,crt_splitpath_pcopy_717ff,"RENAMED: Watcom _splitpath pcopy(char **field, char *dst, begin, end): if field NULL returns dst; *field = dst, copies min(end-begin, 0x92 = _MAX_PATH2) bytes, NUL, returns dst+len+1. Not environment related"
0x000719fe,crt_spawnl_719fe,"RENAMED: spawnl(mode, path, arg0, ...) = crt_spawn_6d999(mode, path, &arg0 (stack varargs), _environ DAT_000a46c0); called from the spawn core for .bat via COMSPEC"
0x00071a1f,crt_spawnvp_71a1f,"RENAMED: spawnvp(mode, path, argv) = crt_spawn_path_search_71c25(mode, path, argv, _environ) (that function is spawnvpe); wrapper 0x6dcf2 is spawnlp. The real system() is crt_system_658d5"
```


## ENGINE.md section: HMI SOS internals, OPL2/GUS MIDI drivers, Watcom graph.lib console, CRT corrections

### HMI Sound Operating System: driver function tables

- A MIDI driver handle h (0..5) owns a 0x24-byte table at `DAT_0009f91a + h*0x24` of six 6-byte far pointers:
  fn0 = send MIDI event (`hmi_midi_send_event_5e4b8`), fn1 = init (`UNK_0009f920`), fn2 = uninit,
  fn3 = reset, fn4 = set instrument data (`snd_midi_driver_call_6330e` indexes 0x9f932 = +0x18, i.e. this is
  sosMIDISetInsData), fn5 unused/callback. `DAT_0009ea6e[h]` = device id, `DAT_0009ea46[h]` = external driver loaded.
- Four MIDI drivers are linked into carpet.exe and are selected by device id in `snd_midi_init_driver_5fa77`
  (tables are filled by `snd_midi_init_system_5f8b1`, all `retf` thunks, so Ghidra has no functions for them):
  | id | table | thunks | implementation |
  |---|---|---|---|
  | 0xa002 | 0x131366 | 0x69849/0x69878/0x698b3/0x698d1/0x698ef | internal OPL2 (Adlib, port 0x388/0x380): `snd_opl_*` 0x68619-0x6a0e6 + 0x704cd/0x70697/0x7075e |
  | 0xa003 | 0x1313a2 | 0x6730a/0x6733a/0x67353/0x6736c/0x67385 | pass-through to user callback `DAT_0009fae6` (fn4 sets it) |
  | 0xa005 | 0x131384 | 0x673f2/0x676f9/0x67878/0x678f6/0x6790f | digital-sample MIDI (plays notes through sosDIGI; note queue `hmi_wavemidi_queue_*` 0x67adc-0x67ded, calls snd_digi_stop_sample_65485) |
  | 0xa008 | 0x131348 | 0x68370/0x68536/0x685a7/0x685c5/0x685de | internal Gravis UltraSound MIDI (`snd_gus_*` 0x6de66-0x70480) |
  Any other 0xa000..0xa200 id loads a driver image from hmidrv.386 (`hmi_midi_load_driver_67f7e`: 0x2c-byte file
  header + 0x2c-byte device records, malloc + selector + lock) and binds its 5 entry points with
  `hmi_midi_bind_driver_table_6a093`. The game's sound setup switches on 0xa001/0xa002/0xa004/0xa008 (carpet_all.c
  ~line 69484), so 0xa002 (OPL2) and 0xa008 (GUS) are the built-in paths and 0xa001/0xa004 need hmidrv.386.
- Digital drivers (hmidrv.386 / hmidet.386) are called through a C-callable entry with a function number:
  detect driver fn0 = find hardware, fn1 = get settings (AX port, CL IRQ, CH DMA -> DAT_000a3f94/98/9c), fn2 = get
  0x6a-byte capabilities; digital driver fn0 = init(port, irq/dma), fn1 = uninit, fn2/fn3 = buffer/format setup,
  fn4 = start, fn5 = stop, fn8 = get info far ptr, fn10 = get 3 entry points (CS:off, DS:off, DS:off).
  Per-handle digital state: `DAT_000a3a84 + h*6` driver far ptr, `DAT_000a3bf0[h]` device id (< 0xe106 = needs own
  DMA buffer -> `hmi_digi_alloc_dma_buffer_64c3f`, which retries DOS allocations until the block does not cross a 64K
  page), 32 channel records of 0x6c bytes inside the driver (`hmi_digi_init_channel_ptrs_66e83`).
- `hmi_digi_lock_code_63f76` / `hmi_digi_unlock_code_6413b` are the sosDIGI lock lists (16 regions including the
  IRQ0 ISR 0x66608+0x238 and DAT_000a2a50+0x1030, the timer state block). The IRQ0 ISR at 0x66609 saves SS:ESP to
  DAT_000a2a72/76, switches to the stack at DAT_000a2a6a, calls the C tick handler `[DAT_000a2a60]`, EOIs port 0x20;
  `hmi_timer_isr_chain_old_667fe` is its alternate exit that retf's into the saved old int 8 vector
  (DAT_000a2a54/58) when DAT_000a3a7c is set. 0x62c2c is the same idiom with the vector passed on the stack.

### Internal OPL2 driver state (device 0xa002)

- Voice tables (9 voices): `DAT_000a401c[v]` note playing (0 = free), `DAT_000a40c0[v]` MIDI channel,
  `DAT_000a41e8[v]` velocity, `DAT_000a4011[v]` timbre loaded, register shadows `DAT_000a3fec[v]` (0xA0+v),
  `DAT_000a3ff5[v]` (0xB0+v, bit 0x20 = key on), `DAT_000a3fdc[op]` levels, `DAT_000a3fbc[op]`, operator slot table
  `DAT_000a402d/402e[v*2]`, rhythm register shadow `DAT_000a4010` (0xBD).
- Channel tables (16): `DAT_000a4080` program, `DAT_000a40e4` bend MSB (0x40 centre), `DAT_000a4124` bend pending,
  `DAT_000a4164` bend range (default 2), `DAT_000a41a8` volume, `DAT_000a420c`, `DAT_000a424c` sustain; sustained
  note-off queue `DAT_000a42ac` (3-byte MIDI events, count `DAT_000a42a8`, max 16).
- Frequency table `DAT_000a4320` (F-numbers, 12 per octave) with `DAT_000a44b8/44bc` octave helpers
  (`snd_opl_calc_bent_freq_686db`).
- Instrument data is the AdLib .BNK format: header +8 count, +0xc name-table offset, +0x10 data offset, 30-byte
  records; `snd_opl_pack_timbre_bank_6955a` converts them in place to OPL register bytes and
  `snd_opl_set_timbre_bank_69401` stores the melodic bank first (`DAT_000a404c`, data `DAT_000a4058`) and the
  percussion bank on the second call (`DAT_000a4062`, flag `DAT_000a4074`; channel 9 uses it). Port in
  `DAT_000a3fa8` (0x388 or 0x380 only), chip-ready flag `DAT_000a402b`, driver-open flag `DAT_000a4040`.

### Internal GUS MIDI driver state (device 0xa008)

- 30 voice entries of 10 bytes at `DAT_000a516c`: word 0 = (channel << 8) | note, 0xffff = free, low byte 0xff =
  released while sustained.
- 16 channel records of 0x1e bytes from `DAT_000a53ee`: +0 sustain, +1/+2 scaled (val*45/100*2) controllers,
  +3 pan (-2*val-2, 0x80 centre), +4 expression (default 0x7f), +5 volume (default 100), +6 bend/30, +8 second bend
  value, +0xa bend range (0x200 = 2 semitones), +0x10/+0x12 bank select, +0x14 RPN marker 0x100, +0x16/+0x18 RPN number.
- Patch layer records stride 0x7c at `DAT_000a55ec` (`snd_gus_patch_adjust_param_6e2e6`, key range test
  `snd_gus_note_in_range_6e2b8`).

### Watcom graph.lib text/graphics console (0x79000-0x7b9ee)

This is Watcom's `graph.lib` (`_outtext`, `_settextposition`, `_scrolltextwindow`, `_setcolor`, `_setplotaction`,
`_setfillmask`, `_displaycursor`), linked in because the game prints its DOS messages through it.
- Globals: `DAT_000a45b9` graphics-mode flag (0 = text; the decompiler treats it as constant 0 so every graphics
  path shows up as "unreachable block"), `DAT_000a45ba` `_GrStatus` (0 ok, 2 `_GRCLIPPED`, -3 `_GRNOTINPROPERMODE`,
  -5 `_GRINSUFFICIENTMEMORY`), `DAT_000a45c5` text attribute, `DAT_000a45c8` cursor enabled, `DAT_000a45ca` cursor
  currently drawn, `DAT_000a45cc` active page, `DAT_000a45d4` current colour, `DAT_000a45d8[8]` fill mask
  (`DAT_000a45e0` default, `DAT_000a45e8` mask-set flag), `DAT_000a45f3` plot action (internal 0..3), `DAT_000a4655`
  BIOS data area base (0x400), `DAT_000a4659/465d/4661/4665` B000/B800/A000/C000 bases with selector words at
  `DAT_000a464b..4653`, `DAT_001319a0/a2` cursor row/col, `DAT_001319a4..aa` pixel/char width/height and cols/rows,
  `DAT_001319ac` colours, `DAT_001319ae` planes/bpp, `DAT_001319b0` pages, `DAT_001319b2` BIOS mode (7 = mono),
  `DAT_001319ba` device-driver function table (+0/+4/+8/+0xc thunks at 0x79dd6..0x79e01; +0x10 set address,
  +0x38 solid span, +0x3c masked span, 0x40/0x44 get/put image), `DAT_001319e2..ee` text window,
  `DAT_001319e4` cursor shape word, `DAT_001319dc` planar flag, `DAT_000a4690` stack low bound.
- Built-in fonts for graphics-mode text: 8x8 at 0xaa660 (modes with char height < 14) and 8x14 at 0xab300.
- `con_gen_glyph_blitter_7b408` is self-modifying: it assembles a per-device blit loop on the stack from three code
  fragments stored with a length byte in front, then calls it. Harmless but worth knowing when tracing.
- Existing name `con_set_video_mode_79cb2` is wrong: it is int 10h AH=01 set cursor shape (CX = DAT_001319e4, CH |=
  0x20 hides when arg == 0). 0x7a699 (not in the list) = colour CRTC probe at 3D4h returning 2.

### CRT corrections and additions

- scanf: `crt_scnf_number_6c8c1` is the float collector (digits, '.', exponent, stores via `crt_fdfs_7136e` for %f),
  `crt_scnf_float_6cc04` is the integer converter; names swapped above.
- spawn family: 6d999 = spawnve core, 719fe = spawnl, 71c25 = spawnvpe (PATH search), 71a1f = spawnvp, 6dcf2 = spawnlp,
  658d5 = system. `_environ` = DAT_000a46c0 (built by `crt_setenvp_713ba` from the DOS env block selector
  DAT_000a46b5 : offset DAT_000a46b1).
- `crt_fatal_runtime_error_71a4f` -> `crt_debugger_trap_71d84` (int 3 + "WVIDEO" when DAT_000ac138 says a Watcom
  debugger is attached) -> `crt_fatal_error_6275c`.
- Streams: `crt_init_std_streams_71161` sets stderr unbuffered and links stdin/stdout/stderr into the
  `__OpenStreams` list `DAT_001319f4` (8-byte nodes). FILE layout confirmed: +0 _ptr, +4 _cnt, +8 _base, +0xc _flag,
  +0x10 _handle, +0x14 _bufsize, +0x18 _ungotten, +0x19 _tmpfchar (0x1a bytes).
- `_splitpath` pieces are limited to _MAX_PATH2 = 0x92 (`crt_splitpath_pcopy_717ff`).
- `crt_prtf_fixed_point_6a5f1` confirms Bullfrog built with a no-FPU printf: %f prints a 16.16 fixed-point value.

### Renderer/UI names in this range that were wrong

- `vga_fill_rect_60688` and `vga_fill_rect_606c0` are byte-identical sprite draws (`ui_draw_sprite_*`): record
  {+0 data ptr, +4 w, +5 h} drawn by `vga_draw_sprite_spans_6070d`.
- Rect outline variants: 604c0 (via 6acd4, pitch 0x280) is 640x480; 603f0 (via 6abbc, coords halved onto pitch
  0x140) is 320x200. Both existing comments say the opposite.

### Open questions

- Which ISR jumps to `hmi_isr_chain_to_vector_62c2c`? No C xref; probably the HMI keyboard or secondary timer
  handler. Needs a byte scan for `jmp 0x62c2c`.
- The exact HMI SOS device-id names (sos.h `_MIDI_*` constants) for 0xa002/0xa003/0xa005/0xa008 were not verified
  against a header; the hardware identification above is from code behaviour (OPL port 0x388, snd_gus_* calls,
  snd_digi_stop_sample calls).
- `hmi_digi_drv_call2_6720b` / `_call3_6723a` (driver fns 2 and 3) parameters are not understood.
- `snd_gus_cc_scaled_a/b` (6ee78/6eea1) and `snd_gus_setup_regs_a/b` (6f801/6f895) are named by shape only.
- The internal driver bodies 0x6730a-0x673d1, 0x673f2-0x67adc, 0x68370-0x68619 and 0x69849-0x69940 are not
  functions in Ghidra (reached only via far thunks + retf); they should be created so the OPL/GUS/wave drivers get
  callers.
- `DAT_00131460` written by `crt_init_std_streams_71161` (value = terminator FILE's _flag) is unexplained; may be a
  decompiler artefact of `__NFiles`.
