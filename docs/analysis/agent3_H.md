# Agent 3, region H: 0x5e000-0x80000 (Watcom CRT, DOS/DPMI glue, HMI SOS, OPL2/GUS MIDI drivers, graph.lib)

90 functions from docs/analysis/todo3_H.txt. 35 of them are the 17-byte empty functions
(`push ebx/esi/edi/ebp; mov ebp,esp; sub esp,0; pop...; ret`) that HMI compiled into every module as
lock-region delimiters (see section 2). 1 FRAGMENT (0x65a98), 0 DATA, 4 names with `?`.

## 1. Names

```csv
0x0005e518,hmi_lock_marker_5e518,"empty HMI marker function; DPMI lock region 0x5e529..0x5f02b (= marker+0x11 .. next marker, 0xb02 bytes: snd_midi_init_song_5e53a .. snd_midi_set_volume_5ef56) in snd_hmi_lock_code_5f04d"
0x0005f02b,hmi_lock_marker_5f02b,"empty marker; ends region 0x5e529 and starts region 0x5f03c..0x6010c (0x10d0: lock/unlock lists, snd_midi_init/uninit_system, init/uninit_driver, all_notes_off)"
0x0006010c,hmi_lock_marker_6010c,"empty marker; end of lock region 0x5f03c+0x10d0 (last sosMIDI code before crt_sprintf_603bc)"
0x00061730,crt_stricmp_wrap_61730,"stack wrapper: re-pushes (s1, s2) and calls crt_stricmp_61745; public stricmp/strcmpi entry over the internal worker"
0x00061978,mem_free_stub_61978,"HMI-style frame (push ebx/esi/edi/ebp) that frees its single stack arg via mem_free_59a10; called only by music_shutdown_61870 (releases the music/driver buffer)"
0x00061e53,crt_access_wrap_61e53,"stack wrapper: re-pushes (path, mode) and calls crt_access_61e68 (int 21h 4300h)"
0x00062893,hmi_lock_marker_62893,"empty marker; sosDIGI lock region 0x62893..0x628fc (0x69 bytes: sound_count_playing_628a4) in hmi_digi_lock_code_63f76"
0x000628fc,hmi_lock_marker_628fc,"empty marker; region 0x6290d..0x62a60 (0x153: hmi_sample_done_6291e, hmi_sample_is_playing_629ba)"
0x000629ba,hmi_sample_is_playing_629ba,"(hDriver, sample id): scans the 32 channel far ptrs DAT_001315a4 + h*0xc0 + i*6 for channel +0x34 == id; returns 1 if flags +0x30 bit 0x8000 (playing, cleared by hmi_stop_sample_65485) is set and bit 0x1000 (+0x31 & 0x10) is clear, else 0; complement of hmi_sample_done_6291e"
0x00062ebf,crt_freopen_find_stream_62ebf,"freopen/_fsopen prologue: looks the FILE up in __OpenStreams DAT_001319f4 (if still open, _flag & 3, crt_doclose_63058(fp,1) keeps the node) else in the closed-stream list DAT_00131460 (unlinks and pushes it onto the open list); unknown FILE -> errno = 4 (EBADF, Watcom numbering) and 0; returns fp. Identifies DAT_00131460 as __ClosedStreams"
0x000630ff,crt_scnf_cget_file_630ff,"scanf input callback for FILEs: specs+8 = FILE*, specs+0x10 bit 2 = end-of-input; fast path takes the buffered char directly unless the FILE has _UNGET (flag 4) or the char is CR/0x1a (text-mode handling), otherwise crt_fgetc_6c023; EOF (-1) sets the eoinp bit"
0x00063225,crt_scnf_cget_string_63225,"sscanf input callback: specs+8 = char*; returns *p++ or on NUL sets specs+0x10 |= 2 (end of input) and returns -1"
0x000632b8,hmi_lock_marker_632b8,"empty marker; sosDIGI lock region 0x632b8..0x632ec (0x34: snd_hmi_error_string_632c9)"
0x000632ec,hmi_lock_marker_632ec,"empty marker; sosMIDI lock region 0x632fd..0x63435 (0x138: snd_midi_driver_call_6330e, snd_midi_send_ins_data_paced_63344)"
0x00063344,snd_midi_send_ins_data_paced_63344,"(hDriver, ?, far ptr data, selector, count): if count: 64K spin, hmi_timer_add_event_5d093(callback 0x6341d at 0x5dc = 1500 Hz), then per byte waits for DAT_000a25b6 (set by the callback), clears it and calls driver fn4 (+0x18, DAT_0009f932, sosMIDISetInsData) with (data+i, selector, 1, h); hmi_timer_remove_event_5d3a9. 0.67 ms per byte = MPU-401/MT-32 SysEx pacing; returns timer error or 0"
0x000635cc,hmi_lock_marker_635cc,"empty marker; sosDIGI lock region 0x635cc..0x63f54 (0x988: snd_digi_detect_init_635dd .. snd_digi_detect_verify_settings_63e70)"
0x00063947,snd_digi_detect_get_caps_63947,"sosDIGIDetectGetCaps(device id, far caps ptr, selector): id outside 0xe000..0xe200 -> 6 (_ERR_INVALID_DRIVER_ID), null ptr -> 2 (_ERR_INVALID_POINTER); re-reads the hmidet.386 header (0x2c bytes at DAT_00131408, record count DAT_00131428) and walks the 0x30-byte device records (DAT_001313c0: +0x24 image offset DAT_001313e4, +0x28 id DAT_001313e8, +0x2d bit 0x80 flag) on file handle DAT_001313f4; loads the matching image into DAT_001313f8 and calls hmi_det_drv_get_caps_66fb8; no match -> 7 (_ERR_NO_DRIVER_FOUND)"
0x00063e70,snd_digi_detect_verify_settings_63e70,"sosDIGIDetectVerifySettings(far ptr {+0 port, +4 irq, +8 dma}, selector): null -> 2; re-reads the current device record and image (DAT_001313f0 offset) from hmidet.386, hmi_det_drv_find_hardware_6706c, then hmi_det_drv_verify_settings_670dc(entry DAT_00131404, port, dma, irq, DAT_00131434:DAT_00131438); returns the driver result"
0x00064ce8,hmi_seg_off_to_linear_64ce8,"(offset, segment) -> (segment & 0xffff) * 16 + offset; real-mode address helper in the DMA-buffer lock region 0x64d27..0x64e5a (no callers)"
0x00064e5a,hmi_lock_marker_64e5a,"empty marker; sosDIGI lock region 0x64e6b..0x65877 (0xa0c: hmi_start_sample_64e7c, hmi_stop_sample_65485, hmi_digi_continue_sample_65511)"
0x00065511,hmi_digi_continue_sample_65511,"sosDIGIContinueSample? (hDriver, channel, far _SOS_START_SAMPLE): no-op (returns 0) if device id DAT_000a3b7e[h] >= 0xe106; else refills channel record DAT_001315a4[h*0xc0+ch*6] from the sample struct: +0/+8 = ptr(+0)/sel(+4), +0x10 = ptr + length(+0x24), +0x50 = +8, loop fields +0x18/+0x1c/+0x20/+0x2c from +0x24/+0x28/+0x20 when flags(+0x1c) bit 0x40 else +0x18/+0x1c = +8, +0x34 = id(+0x12), +0x30 = flags | 0x8800, +0x3c/+0x40 = callback +0x14/+0x18, +0x38 = +0xc, +0x4a = +0x1a. Same mapping as hmi_start_sample_64e7c but without slot allocation; no callers"
0x00065947,crt_inp_65947,"inp(port): edx = port, eax = 0, in al,dx; called by input_keyboard_isr_4fa28"
0x00065a98,FRAGMENT,"shared body of strtol/strtoul: the real entries are 0x65a95 (push ebx; push 0 = strtol) and 0x65ab1 (push ebx; push 1; jmp 0x65a98 = strtoul, currently FUN_00065ab1); the body pushes (s, endptr, base) and calls crt_strtol_core_6594f with the unsigned flag as 4th stack arg, add esp,0x10. Create functions at 0x65a95 = crt_strtol_65a95 and 0x65ab1 = crt_strtoul_65ab1"
0x0006608b,crt_set_edom_6608b,"__set_EDOM: crt_set_errno_660aa(13) (Watcom errno numbering: EDOM = 13); 0x66096 (push 14; jmp) is __set_ERANGE sharing the tail and crt_errno_fail_6609a sets EINVAL (9) and returns -1"
0x000660bd,crt_call_hook_a46b9_660bd?,"pushes EAX and calls the statically initialised function pointer DAT_000a46b9 (only reference in the image is this call; never written by code); sits between crt_set_doserrno_660b6 and crt_memset_660d0, probably an exit/signal hook stub"
0x00066173,crt_prtf_putc_file_66173,"fprintf output callback (specs, c): crt_flsbuf_6dd18(c, FILE at specs+0) then specs->_output_count (+0x10)++; used by crt_vfprintf_core_66193 which follows it"
0x00066472,crt_set_binary_mode_66472,"__set_binary(handle): iomode |= _BINARY (0x40) via crt_get_iomode_6de82/crt_set_iomode_6ded8; for TTY handles (_ISTTY 0x2000) int 21h AX=4400h get device info then AX=4401h with DL |= 0x20 (raw mode); DOS error -> crt_set_errno_dos_6b108; returns 0"
0x00066876,hmi_lock_marker_66876,"empty marker; sosMIDI lock region 0x66887..0x66dc0 (0x539): starts with another empty marker at 0x66887 followed by a 1.2 KB unrecovered HMI function at 0x66898 (song index DAT_000a3d89, tables 0x9f8fa/0x9f9ce/0x9f9ee) - not a function in Ghidra"
0x00066e15,hmi_lock_marker_66e15,"empty marker; end of sosMIDI lock region 0x66dd1..0x66e15 (0x44: hmi_midi_slot_is_free_66de2); dpmi_lock_region_66e29 follows"
0x00066f71,hmi_drv_call_ax_stub_66f71?,"orphan 10-byte asm between dpmi_alloc_locked_dos_mem_66f24 and the fn8 get-info caller at 0x66f7b: push es; ax = [ebp+0xc]; call [ebp+8]; pop es; ret - uses the caller's frame (no prologue), no callers; unused driver-call helper"
0x000670dc,hmi_det_drv_verify_settings_670dc,"hmidet.386 driver function 3 (verify settings): (entry, port -> EBX, dma -> CH, irq -> CL, far ptr -> FS:EDI), EAX = 3, pushes entry and calls it, saves/restores fs/gs/es; called by snd_digi_detect_verify_settings_63e70"
0x0006730a,hmi_midi_cbdrv_send_event_6730a,"device 0xa003 (user-callback MIDI driver) fn0 = send event, far entry ending in retf: calls [DAT_0009fae6](handle, event ptr, word, dword) and returns 0; fn1 0x6733a init, fn2 0x67353 uninit, fn3 0x6736c reset just return 0; fn4 0x67385 (SetInsData) stores DAT_0009fae6/DAT_0009faea = callback offset/selector"
0x000673f2,hmi_wavemidi_send_event_673f2,"device 0xa005 (digital-sample MIDI) fn0 = send event, far/retf: 0xBx 0x7B all-notes-off -> pop every queue entry (hmi_wavemidi_queue_pop_67c27) and hmi_stop_sample_65485(DAT_000a11da[h] digital handle, slot+1); 0x9x note on: note table DAT_000a02bc + h*0x300 + note*6 (far ptr to sample record, selector at +4) must be set; vel 0 or re-trigger (record+0x1a != 0x8000) -> hmi_wavemidi_queue_remove_key_67ded + stop; queue full (DAT_000a02a8 >= DAT_000a0294) -> pop oldest + stop; queue_push_67b63, record+0x12 = slot+1 then note; velocity*DAT_000a2565>>7 <<8 into record+0x10 when DAT_000a122a[h]; hmi_start_sample_64e7c(digital handle, record, sel); other statuses ignored, returns 0"
0x00067b52,hmi_wavemidi_queue_uninit_67b52,"empty function (same shape as the lock markers) called with the handle by the wave-MIDI driver fn2 uninit at 0x67878; pair of hmi_wavemidi_queue_init_67adc"
0x00067f5c,hmi_lock_marker_67f5c,"empty marker; sosMIDI lock region 0x67f6d..0x681d9 (0x26c: hmi_midi_load_driver_67f7e, hmi_midi_free_driver_slot_6816f)"
0x000681d9,hmi_lock_marker_681d9,"empty marker; sosMIDI lock region 0x681ea..0x68251 (0x67 bytes, unnamed helper before hmi_midi_get_gus_driver_table_6834f)"
0x00068370,snd_gus_midi_send_event_68370,"device 0xa008 (internal GUS MIDI) fn0 = send event, far/retf: dispatch on status & 0xf0 with (data2, data1, channel) pushed: 0x80 snd_gus_note_off_6ecf5; 0x90 snd_gus_note_event_6ee0d (vel 0 -> note_off); 0xA0 snd_gus_set_bend_6f6ee(data1, ch); 0xB0 snd_gus_controller_6f395; 0xC0 snd_gus_program_change_6f586; 0xD0 snd_gus_channel_pressure_noop_6f6e6; 0xE0 snd_gus_pitch_bend_6f72e; callees clean the stack (ret 0xc/ret 8); returns 0. fn1 0x68536 init = snd_gus_detect_6f986 + snd_gus_init_703e2 + 7 table ptrs DAT_000a57dc..f4 + snd_gus_channels_init_6def4; fn2 0x685a7 = snd_gus_shutdown_70480; fn3 0x685c5 / fn4 0x685de return 0"
0x00068871,hmi_lock_marker_68871,"empty marker; OPL lock region 0x68882..0x689b8 (snd_opl_controller_68893)"
0x000689b8,hmi_lock_marker_689b8,"empty marker; OPL lock region 0x689c9..0x68ea8 (snd_opl_note_on_689da)"
0x00068ea8,hmi_lock_marker_68ea8,"empty marker; OPL lock region 0x68eb9..0x690fa (snd_opl_note_off_68eca, snd_opl_all_notes_off_68fc9, snd_opl_reset_controllers_69072)"
0x000690fa,hmi_lock_marker_690fa,"empty marker; OPL lock region 0x6910b..0x692f7 (snd_opl_midi_event_6911c)"
0x000692f7,hmi_lock_marker_692f7,"empty marker; OPL lock region 0x69308..0x693df (snd_opl_reset_state_69319)"
0x000693df,hmi_lock_marker_693df,"empty marker; OPL lock region 0x693f0..0x6975a (snd_opl_set_timbre_bank_69401, snd_opl_pack_timbre_bank_6955a)"
0x0006975a,hmi_lock_marker_6975a,"empty marker; OPL lock region 0x6976b..0x697bf (snd_opl_driver_uninit_6977c)"
0x000697bf,hmi_lock_marker_697bf,"empty marker; OPL lock region 0x697d0..0x69827 (snd_opl_driver_init_697e1)"
0x00069827,hmi_lock_marker_69827,"empty marker; OPL lock region 0x69838..0x6991e = the five far entries of the 0xa002 OPL driver (0x69849 fn0 send -> snd_opl_midi_event_6911c, 0x69878 fn1 init -> snd_opl_driver_init_697e1 with DAT_0013196c = handle, 0x698b3 fn2 -> snd_opl_driver_uninit_6977c, 0x698d1 fn3 -> snd_opl_reset_state_69319, 0x698ef fn4 -> snd_opl_set_timbre_bank_69401)"
0x000699bf,hmi_lock_marker_699bf,"empty marker; OPL lock region 0x699d0..0x69a55 (snd_opl_voice_set_freq_699e1)"
0x00069a55,hmi_lock_marker_69a55,"empty marker; OPL lock region 0x69a66..0x69b0f (snd_opl_clear_voices_69a77)"
0x00069b0f,hmi_lock_marker_69b0f,"empty marker; OPL lock region 0x69b20..0x69bd1 (snd_opl_voice_key_on_69b31)"
0x00069bd1,hmi_lock_marker_69bd1,"empty marker; OPL lock region 0x69be2..0x69c51 (snd_opl_voice_key_off_69bf3)"
0x00069c51,hmi_lock_marker_69c51,"empty marker; OPL lock region 0x69c62..0x69d3f (snd_opl_silence_all_69c73)"
0x00069d3f,hmi_lock_marker_69d3f,"empty marker; OPL lock region 0x69d50..0x69f48 (snd_opl_load_timbre_69d61)"
0x00069f48,hmi_lock_marker_69f48,"empty marker; OPL lock region 0x69f59..0x69f89 (snd_opl_clear_chip_ready_69f6a)"
0x00069f6a,snd_opl_clear_chip_ready_69f6a,"DAT_000a402b (OPL chip-ready flag) = 0, returns 0; called by snd_opl_driver_uninit_6977c"
0x00069f89,hmi_lock_marker_69f89,"empty marker; end of OPL lock region 0x69f59+0x30; snd_opl_write_69f9c follows"
0x0006add5,crt_creat_6add5,"creat(path, pmode) = crt_open_66274(path, 0x62 = O_RDWR|O_CREAT|O_TRUNC, pmode); used by file_open_619a0 when creating a file"
0x0006ae9e,crt_lseek_wrap_6ae9e,"stack wrapper: re-pushes (handle, offset, whence) and calls crt_lseek_6aeb8 (int 21h 42h)"
0x0006bbfc,crt_tolower_wrap_6bbfc,"stack wrapper: re-pushes (c) and calls crt_tolower_6bc0a"
0x0006cee2,hmi_lock_marker_6cee2,"empty marker; sosDIGI lock region 0x6cee2..0x6cf88 (0xa6: hmi_digi_get_driver_info_6cef3)"
0x0006cf88,hmi_lock_marker_6cf88,"empty marker; sosDIGI lock region 0x6cf99..0x6d40b (0x472: snd_hmi_load_digital_driver_6cfaa, snd_hmi_unload_driver_6d1a6, snd_hmi_load_midi_driver_6d210, hmi_digi_check_far_ptr_6d3b2, hmi_stub_return_zero_6d3f0)"
0x0006d3f0,hmi_stub_return_zero_6d3f0,"HMI frame, local = 0, returns 0: compiled-out sosDIGI stub between hmi_digi_check_far_ptr_6d3b2 and the lock marker at 0x6d40b; no callers"
0x0006d59d,dos_int_call_allregs_6d59d,"software-interrupt call with a full register block: AL = int number, EDX -> 0x26-byte block {+0 eax, +4 ebx, +8 ecx, +0xc edx, +0x10 ebp, +0x14 esi, +0x18 edi, +0x1c ds, +0x1e es, +0x20 fs, +0x22 gs, +0x24 flags}; pushal/segments, 0x6d5ea (not a function) loads every register from the block and returns into the `int n; ret` table at 0x6d61e + n*3, then all registers and flags are stored back; sibling of dos_int386x_core_6d540 (which uses the 0x1c-byte union REGS)"
0x0006dcf2,crt_spawnlp_6dcf2,"spawnlp(mode, path, arg0, ...): crt_spawnvp_71a1f(mode, path, &arg0); called by crt_system_658d5 (P_WAIT, COMSPEC, /c cmd)"
0x0006f6e6,snd_gus_channel_pressure_noop_6f6e6,"push ebp; mov ebp,esp; jmp 0x6f4b7 = shared tail `xor eax,eax; pop ebp; ret 0xc`: 3-arg no-op returning 0; the 0xDx channel-pressure handler of snd_gus_midi_send_event_68370"
0x0006f911,snd_gus_match_sysex_6f911,"(len, ptr): matches an incoming SysEx against the two 0x16-byte templates at DAT_000a4c84 (0xfe = wildcard, 0xff = end, +0x10 = index of the value byte, +0x12 = handler): F0 41 xx 42 12 40 01 30 vv cs F7 (Roland GS DT1 reverb macro) -> snd_gus_setup_regs_b_6f895(value) and 40 01 38 (GS chorus macro) -> snd_gus_setup_regs_a_6f801(value); returns the handler result or 1 when nothing matched"
0x00070675,hmi_lock_marker_70675,"empty marker (no lock-list entry) between snd_opl_set_channel_volume_704cd and snd_opl_alloc_voice_70697; same compiled-in delimiter as the OPL markers"
0x0007073c,hmi_lock_marker_7073c,"empty marker (no lock-list entry) between snd_opl_alloc_voice_70697 and snd_opl_program_change_7075e"
0x00070846,crt_itoa_wrap_70846,"stack wrapper: re-pushes (value, buf, radix) and calls crt_itoa_70811"
0x0007091a,crt_ltoa_wrap_7091a,"stack wrapper: re-pushes (value, buf, radix) and calls crt_ltoa_708e5"
0x0007095e,crt_record_ss_7095e,"DAT_000a9c24 = SS; the saved selector is compared by crt_alloca_check_70979 so the stack check is skipped when running on another stack (ISR)"
0x00070bf6,crt_heap_realloc_dpmi_block_70bf6,"Watcom __ReAllocDPMIBlock(block, new size): finds the heap segment in DAT_000a4670 whose first block (+0x2c) is the pointer, crt_heap_unlink_70b57, requires seg[-1] == 0, DPMI AX=0503h resize (handle SI:DI from the segment header-8, BX:CX = new size rounded up to 4K), stores the new linear address, crt_heap_link_70d10, seg+0x18 = 1; if the remainder >= 0xc splits it and crt_nfree_6b900 frees the tail; returns the block or 0"
0x000712e6,crt_flushall_712e6,"flushall(): crt_flush_streams_712f1(-1)"
0x00071328,crt_getche_71328,"returns and clears the pushed-back char DAT_000a46a0 (ungetch buffer), else int 21h AH=01h read keyboard with echo; used by crt_fill_buffer_6c0dd for unbuffered TTY stdin"
0x000714c5,dos_findnext_714c5,"_dos_findnext(dta): int 21h AH=1Ah set DTA, dos_dta_copy_in_7150b, AH=4Fh find next, crt_dos_result_71c12 (CF -> errno), dos_dta_copy_714e8"
0x0007150b,dos_dta_copy_in_7150b,"extender type 9 only (DAT_000a46ae == 9): AH=2Fh get DTA then copies the 0x2b-byte find record from the caller buffer (EDX) into the real DTA (ES:EBX); reverse direction of dos_dta_copy_714e8 (which copies DTA -> caller)"
0x00071a42,crt_fatal_error_stub_71a42,"loads (msg, code) from the stack into EAX/EDX and jumps to crt_fatal_error_6275c; called by crt_fatal_runtime_error_71a4f"
0x00071a7d,crt_heapenable_71a7d,"_heapenable(flag): returns the old __heap_enabled DAT_000ac130 and stores the new one; crt_heap_grow_70ea1 / crt_heap_expand_70f70 return 0 when it is 0 (or DAT_000a4680 == -2)"
0x00072217,castle_mark_footprint_circle_72217,"(x 16.16, y 16.16, radius in cells): ORs bit 0x80 (built / castle footprint) into the cell flag map DAT_000fdfb0[(y>>16)<<8 | (x>>16)] over a filled circle: centre row +-(r-1), then rows +-k with half-width g_circle_profile[(k*0x10000/r)>>8]*r>>8 (same table as ui_draw_radar_43610); setter counterpart of castle_footprint_clear_11980; no callers"
0x00078e23,render_draw_line_clipped_78e23,"(p1 {int x, int y}, p2): 16-bit line clipped to the software viewport 0..DAT_0009b600-1 x 0..DAT_0009b604-1 (render_set_viewport_78dd5) with parametric edge intersections, drawn in g_fill_colour at DAT_0009b5f4 + y*DAT_0009b5fc: horizontal -> rep stosb, vertical -> pitch loop, else Bresenham with straight/diagonal step pair; game code placed late by the linker (between render_set_viewport_78dd5 and vesa_present_anaglyph16_79246)"
0x00079888,con_outmem_79888,"graph.lib _outmem(text, length): con_reinit_video_state_79b20, con_hide_cursor_79c48, con_write_chars_7968a(text, length, 0), con_show_cursor_79dc4"
0x00079ba7,con_set_gfx_mode_params_79ba7,"records the selected graphics mode: DAT_000a45b9 = 1 (graphics), con_init_799fc, DAT_001319a4/a6 = x/y resolution, 1319ac colours, 1319ae planes/bpp, 1319b0 pages, 1319d4/1319d6/1319da device words, 1319dc |= flags, copies to 1319ce/1319d0 (graph.lib _setvideomode helper)"
0x00079da0,con_require_graphics_hide_cursor_79da0,"_GrStatus DAT_000a45ba = 0; text mode (DAT_000a45b9 == 0) -> -3 _GRNOTINPROPERMODE, else con_hide_cursor_79c48; jumps into the tail of con_require_graphics_mode_79d7c (0x79d97) to return the graphics flag"
0x00079df0,con_dev_begin_draw_79df0,"calls device driver table entry [DAT_001319ba + 8]; paired with con_dev_end_draw_79df9 around every graphics primitive (scroll 79fd8, put_char 7a1ec, line 7a922, fill_rows 7b665)"
0x00079df9,con_dev_end_draw_79df9,"calls device driver table entry [DAT_001319ba + 0xc] (restore adapter state after a primitive)"
0x0007a699,con_detect_colour_crtc_7a699,"con_probe_crtc_7a6b1(0x3d4) -> 2 (colour CRTC present) else 0; called by ega_detect_7a52f"
0x0007a864,con_lineto_clipped_7a864,"graph.lib _lineto worker (x1, y1, x2, y2): con_clip_rect_cs_7b1d5 -> 0 if fully outside; if the line style DAT_000a45ef != 0xffff and the start point moved, rotates the 16-bit style mask by max(|dx|,|dy|) & 15 so the pattern stays aligned; calls 0x7a922(x1, y1, x2, y2, colour DAT_000a45d4, style) and returns 1. Shows that 7a922/7b408 are the styled line primitive (7b408 emits shl bx,1/jnc = style bit test), not glyph code"
0x0007aa27,con_clearscreen_gfx_7aa27,"graph.lib _clearscreen(area) in graphics mode: 1 = _GVIEWPORT -> clip rect DAT_001319be..c4; 2 = _GWINDOW -> text window DAT_001319e6..ec converted to pixels with cell size (1319a4/1319a8, 1319a6/1319aa); else whole screen 0,0..w-1,h-1; con_fill_cell_bg_gfx_7ab7e then con_set_cursor_795e6(1,1) for areas 0 and 2"
0x0007acac,con_imagesize_7acac,"graph.lib _imagesize(x1, y1, x2, y2): con_require_graphics_mode_79d7c else 0; adds the logical origin DAT_000a45c0/c2 and calls 0x7acf7 (not a function: (|dx|+1 -> con_image_row_bytes_7ad40) * (|dy|+1) + 6-byte header)"
0x0007b331,con_clip_snap_to_edge_7b331,"(&x, &y, outcode): bit 1 -> y = clip top DAT_001319c2, bit 2 -> y = clip bottom 1319c4, bit 4 -> x = clip right 1319c0, bit 8 -> x = clip left 1319be; Cohen-Sutherland helper next to con_clip_rect_cs_7b1d5"
0x0007b6bc,con_remapallpalette_vga_7b6bc,"(dword palette 0x00BBGGRR, count): dos_alloc_realmode_mem_7b75f(3*count), unpacks to R,G,B bytes, int 10h AX=1012h set DAC block via dos_realmode_int_7b828, dos_free_realmode_mem_7b7fa; returns 1, or 0 if the real-mode allocation failed (graph.lib _remapallpalette VGA path)"
```

## 2. Notes for docs/ENGINE.md (region H, round 3)

### HMI lock-region markers (the 17-byte empty functions)

Every HMI source module was compiled with empty delimiter functions (`push ebx/esi/edi/ebp; mov ebp,esp;
sub esp,0; ...; ret`, 17 bytes). The two lock lists use them as region bounds: a region in
`snd_hmi_lock_code_5f04d` starts at marker+0x11 (the first byte after the empty function) and its length
runs exactly to the next marker (e.g. 0x5e529+0xb02 = 0x5f02b, 0x67f6d+0x26c = 0x681d9, 0x69f59+0x30 =
0x69f89); the `hmi_digi_lock_code_63f76` list instead starts at the marker itself (0x632b8+0x34 = 0x632ec,
0x6cee2+0xa6 = 0x6cf88). 35 of them were in this list and are named `hmi_lock_marker_<addr>`; more exist that
are not functions yet (0x66887, 0x66dc0, 0x67aba, 0x67acb, 0x68251, 0x68262, 0x6832d, 0x6833e, 0x685f7,
0x68608, 0x6991e, 0x6992f, 0x6d40b, 0x6d4b3, 0x6d52f, 0x672c7, 0x673af, 0x673c0). One of them,
`hmi_wavemidi_queue_uninit_67b52`, is also a real (compiled-out) function called by the wave-MIDI uninit entry.

### Internal MIDI driver far entries -> implementation (device table `DAT_0009f91a + h*0x24`)

The "thunks" listed in Region E are the driver bodies themselves (far functions ending in `retf`,
all args on the stack: [ebp+0x14] first arg, handle last):

| device | fn0 send event | fn1 init | fn2 uninit | fn3 reset | fn4 SetInsData |
|---|---|---|---|---|---|
| 0xa002 OPL2 | 0x69849 -> `snd_opl_midi_event_6911c` | 0x69878 -> `snd_opl_driver_init_697e1` (DAT_0013196c = handle) | 0x698b3 -> `snd_opl_driver_uninit_6977c` | 0x698d1 -> `snd_opl_reset_state_69319` | 0x698ef -> `snd_opl_set_timbre_bank_69401` |
| 0xa003 callback | `hmi_midi_cbdrv_send_event_6730a` -> `[DAT_0009fae6]` | 0x6733a return 0 | 0x67353 return 0 | 0x6736c return 0 | 0x67385: DAT_0009fae6/ea = callback far ptr |
| 0xa005 wave MIDI | `hmi_wavemidi_send_event_673f2` | 0x676f9 (see below) | 0x67878 | 0x678f6 return 0 | 0x6790f (bank loader, see below) |
| 0xa008 GUS | `snd_gus_midi_send_event_68370` | 0x68536: `snd_gus_detect_6f986`, `snd_gus_init_703e2`, 7 table ptrs DAT_000a57dc..f4 = 0xa5800/0xa5a08/0xa5b74/0xa60e7/0xa624d/0xa6817/0xa9295, `snd_gus_channels_init_6def4` | 0x685a7 -> `snd_gus_shutdown_70480` | 0x685c5 return 0 | 0x685de return 0 |

The fn1/fn2/fn3/fn4 bodies, 0x67a9b and 0x6d5ea are not functions in Ghidra yet.

Wave-MIDI driver (0xa005) internals, from 0x676f9/0x67878/0x6790f:
- init(far init struct, handle h): searches the 5 digital driver slots `0x131478 + i*0x40` for one whose
  +0 equals struct+0 (device id) and stores its index in `DAT_000a11da[h]`; if none, `DAT_000a1216[h] = 1`,
  initialises its own digital driver with `snd_digi_init_driver_64386(struct+0, struct+0x1c, +0x20, +0x14,
  +0x18, ...)`, registers a timer event (`hmi_timer_add_event_5d093`, handle kept in `DAT_000a1202[h]`) and
  sets `DAT_000a11ee[h] = 1`; `DAT_000a122a[h] = struct+0x10` (velocity scaling on/off, master volume byte
  `DAT_000a2565`); `hmi_wavemidi_queue_init_67adc(h, struct+0xc)`.
- uninit: if it owns the digital driver: `snd_digi_uninit_driver_64ab8(DAT_000a11da[h], 1, 1)`,
  `hmi_timer_remove_event_5d3a9(DAT_000a1202[h])`; then `hmi_wavemidi_queue_uninit_67b52(h)`.
- SetInsData(bank far ptr, selector, h): `DAT_000a11bc[h*6]` = bank; clears the 128-note table
  `DAT_000a02bc + h*0x300` (6 bytes per note: far ptr to sample record); the bank starts with the
  signature string at `DAT_000a123e` (mismatch -> error 0xe), +0x24 = total size, records from +0x28:
  a 0x54-byte sample header (+8 data length, +0x12 MIDI note, +0x14/+0x18 = done-callback far ptr which
  the loader sets to CS:0x67a9b, +0x1a/+0x1c flags used by `hmi_start_sample_64e7c`) followed by the PCM
  data; the per-note table gets the record pointer and record+0x54 becomes the sample data pointer.
  0x67a9b (far, retf) is the sample-done callback: `hmi_wavemidi_queue_remove_at_67cc1(h, slot-1)`.
- send event: see the CSV line; the queue holds the playing notes (slot+1 is stored in record+0x12 and
  used as the sosDIGI sample id).

### sosDIGI detection and channel records
- `snd_digi_detect_get_caps_63947` / `snd_digi_detect_verify_settings_63e70` complete the sosDIGIDetect*
  set (init 635dd, uninit 63746, find 6379a, settings 63d88). hmidet.386 layout: 0x2c-byte header (record
  count at +0x20 -> `DAT_00131428`), 0x30-byte device records (+0x24 image offset, +0x28 device id, +0x2d
  flags bit 0x80), file handle `DAT_001313f4`, current image `DAT_001313f8`, entry `DAT_00131404`,
  data selector/offset `DAT_00131434`/`DAT_00131438`. hmidet.386 function 3 = verify settings
  (`hmi_det_drv_verify_settings_670dc`: EBX port, CH dma, CL irq).
- HMI error codes seen: 2 _ERR_INVALID_POINTER, 6 _ERR_INVALID_DRIVER_ID, 7 _ERR_NO_DRIVER_FOUND,
  10 _ERR_INVALID_HANDLE (hmi_stop_sample_65485), 0xe invalid bank signature.
- Digital channel record (0x6c bytes, far ptr table `DAT_001315a4 + h*0xc0 + ch*6`): +0 current ptr,
  +8 start ptr, +0x10 end ptr, +0x18/+0x1c loop start, +0x20 loop length, +0x2c remaining, +0x30 flags
  (0x8000 playing, 0x800 set at start, 0x1000 tested by `hmi_sample_is_playing_629ba`), +0x34 sample id,
  +0x38 volume?, +0x3c/+0x40 done callback, +0x4a pan/word from _SOS_START_SAMPLE+0x1a, +0x50 copy of
  struct+8. `hmi_digi_continue_sample_65511` (probably sosDIGIContinueSample) refills a record in place.
- `snd_midi_send_ins_data_paced_63344` sends instrument/SysEx data one byte per 1500 Hz tick through
  driver fn4 (callback 0x6341d sets `DAT_000a25b6`).

### GUS MIDI SysEx
`snd_gus_match_sysex_6f911` recognises Roland GS DT1 messages `F0 41 dev 42 12 40 01 30 vv` (reverb macro)
and `40 01 38 vv` (chorus macro) from templates at `DAT_000a4c84` (0x16 bytes each, 0xfe wildcard, 0xff end,
+0x10 value index, +0x12 handler). This resolves the Region E open question: `snd_gus_setup_regs_b_6f895`
is the reverb-macro handler and `snd_gus_setup_regs_a_6f801` the chorus-macro handler.

### Watcom CRT details
- Watcom errno numbering starts at ENOENT = 1: `crt_set_edom_6608b` sets 13 (EDOM), 0x66096 sets 14
  (ERANGE), `crt_errno_fail_6609a` sets 9 (EINVAL) and `crt_freopen_find_stream_62ebf` sets 4 (EBADF).
- `DAT_00131460` (Region E open question) is the `__ClosedStreams` list head: freopen moves a FILE from it
  back to `__OpenStreams` `DAT_001319f4`.
- Heap: `crt_heapenable_71a7d` is `_heapenable()`, `DAT_000ac130` = `__heap_enabled` (heap growth refused
  when 0 or when `DAT_000a4680 == -2`); `crt_heap_realloc_dpmi_block_70bf6` is `__ReAllocDPMIBlock`
  (DPMI 0503h grow-in-place of a whole heap segment).
- Several public entries are 3-arg stack wrappers over internal workers (`crt_*_wrap_*`): stricmp,
  access, lseek, tolower, itoa, ltoa; strtol/strtoul share one body (true entries 0x65a95 / 0x65ab1, see
  FRAGMENT 0x65a98).
- `crt_record_ss_7095e` stores SS in `DAT_000a9c24` for `crt_alloca_check_70979`; `crt_creat_6add5` uses
  O_RDWR|O_CREAT|O_TRUNC (0x62); `crt_getche_71328` with ungetch buffer `DAT_000a46a0` feeds
  `crt_fill_buffer_6c0dd` for unbuffered tty stdin; `dos_findnext_714c5` + `dos_dta_copy_in_7150b`
  (caller buffer -> DTA) complete the findfirst/findnext pair with `dos_dta_copy_714e8` (DTA -> caller).
- `dos_int_call_allregs_6d59d` is a second int386-style caller using a 0x26-byte block with segment
  registers and flags (layout in the CSV), distinct from `dos_int386x_core_6d540`.

### graph.lib corrections
- `con_draw_glyph_gfx_7a922` and `con_gen_glyph_blitter_7b408` are the styled **line** primitive: 7a864
  (`con_lineto_clipped_7a864`) passes (x1, y1, x2, y2, colour `DAT_000a45d4`, 16-bit line style
  `DAT_000a45ef`) and the generated loop tests the style with `shl bx,1 / jnc`. `con_draw_glyph_clipped_7a819`
  is probably the other _lineto path. Suggested renames: con_line_gfx_7a922, con_gen_line_loop_7b408.
- 0x7acf7 (not a function) is the `_imagesize` arithmetic used by `con_imagesize_7acac`.

### Late game code
- `castle_mark_footprint_circle_72217` (0x72217, right before the triangle rasteriser) sets bit 0x80 of the
  cell flag map 0xFDFB0 in a circle using `g_circle_profile` (0xCDCB0); no callers (dead or pointer-called).
- `render_draw_line_clipped_78e23` is the software line drawer over the viewport globals set by
  `render_set_viewport_78dd5` (`DAT_0009b600/9b604` size, `DAT_0009b5f4` dest, `DAT_0009b5fc` pitch,
  `g_fill_colour`).

### Open questions
- 0x66898-0x66dbb (1.2 KB, HMI frame, song index `DAT_000a3d89`, tables 0x9f8fa/0x9f9ce/0x9f9ee) is a
  large sosMIDI function with no Ghidra function (probably the timer-tick sequencer); likewise the driver
  entries 0x6733a..0x67385, 0x676f9/0x67878/0x678f6/0x6790f/0x67a9b, 0x68536..0x685de, 0x69849..0x698ef
  and the int-table loader 0x6d5ea should be created so their callees get callers.
- `DAT_000a46b9` (called by 0x660bd with EAX) is never written in code; its static initial value was not
  checked.
- Bit 0x1000 of the digital channel flags (tested only by `hmi_sample_is_playing_629ba`) has no setter in
  the decompiled code; its meaning (paused?) is unverified.
- `hmi_digi_continue_sample_65511` = sosDIGIContinueSample is inferred from behaviour, not from a header.
