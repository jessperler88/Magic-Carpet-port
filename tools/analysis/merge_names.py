"""Merge agent deliverables (CSV blocks inside ```...``` fences in agent_*.md) into
ghidra/names/carpet_names.csv.  Rules: existing curated names are kept unless the
agent's line says the old one is wrong (handled by an explicit override map);
'?' suffix -> stripped, comment prefixed with 'UNCERTAIN:'; names must be valid
Ghidra symbols; table-generated names (carpet_names_tables.csv) are left alone -
ApplyNames applies the curated file second, so curated wins on conflict."""
import pathlib as _pl; _REPO = _pl.Path(__file__).resolve().parents[2].as_posix()   # the repository root
import csv, re, sys, glob, io
NAMES = (_REPO + "/ghidra/names/carpet_names.csv")
FUNCS = (_REPO + "/ghidra/export/carpet_functions.csv")
OVERRIDES = {0x5afd0: "mem_set_5afd0", 0x61510: "vga_palette_fade_61510",
    # agent conflicts resolved towards the agent that read the function in depth
    0x156b0: "player_function_keys_156b0", 0x3c540: "demo_record_playback_step_3c540",
    0x3c200: "demo_load_state_3c200", 0x49720: "sound_request_49720", 0x4f530: "net_exchange_frame_4f530",
    0x5b7f8: "mouse_cursor_hide_for_blit_5b7f8", 0x5b850: "mouse_cursor_unhide_5b850",
    0x3db20: "game_check_level_won_3db20", 0x1fab0: "render_frame_1fab0", 0x43610: "ui_draw_radar_43610",
    0x42a20: "ui_draw_radar_blips_42a20", 0x62c2c: "isr_chain_old_vector_62c2c",
    # round 4 (agent4_C read the bodies; the round-2 "not read" rows had already cleared the UNCERTAIN marker)
    0x43db0: "scenery_standing_stone_update_43db0", 0x46960: "creature_proximity_wake_timer_46960", 0x468e0: "creature_wake_tick_468e0"}   # old name was wrong (agent evidence)
funcs = {int(r['address'], 16) for r in csv.DictReader(open(FUNCS))}
cur = {}; header = []
for line in open(NAMES, encoding='utf-8'):
    if line.startswith('#') or line.startswith('address,'): header.append(line.rstrip('\n')); continue
    r = next(csv.reader([line]))
    if len(r) >= 2 and r[0].startswith('0x'): cur[int(r[0], 16)] = (r[1], r[2] if len(r) > 2 else '')
added = updated = skipped = 0; bad = []
moved = {}
for line in open((_REPO + "/ghidra/names/moved_entries.txt")):
    if line.startswith('0x'): a_, b_ = line.split(','); moved[int(a_, 16)] = int(b_.strip(), 16)
conflicts = []
deleted = set()
for dpath in glob.glob((_REPO + "/ghidra/names/delete_stubs*.txt")):
    for line in open(dpath):
        line = line.split('#')[0].strip()
        if line: deleted.add(int(line, 16))
for path in sorted(glob.glob((_REPO + "/docs/analysis/agent*_*.md"))):
    text = open(path, encoding='utf-8').read()
    blocks = re.findall(r"```(?:csv)?\n(.*?)```", text, re.S)
    for b in blocks:
        for r in csv.reader(io.StringIO(b)):
            if len(r) < 2 or not re.match(r'^0x[0-9a-fA-F]+$', r[0].strip()): continue
            a = int(r[0], 16); name = r[1].strip(); cm = r[2].strip() if len(r) > 2 else ''
            if name.upper().startswith('FRAGMENT') or name.upper().startswith('DATA'): continue   # not functions (deleted separately)
            if a in deleted: continue   # function was removed as a stub/fragment
            if a in moved:   # function entry was moved off padding bytes: rename suffix
                name = re.sub(r'_[0-9a-f]{5,6}(\??)$', lambda m: f'_{moved[a]:x}' + m.group(1), name); a = moved[a]
            if name.endswith('?'): name = name[:-1]; cm = 'UNCERTAIN: ' + cm
            if not re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', name): bad.append((path, r)); continue
            if a not in funcs:
                alt = next((a+k for k in range(1,6) if a+k in funcs), None)
                if alt is None: bad.append((path, 'no function at', hex(a), name)); continue
                cm = f"(agent gave 0x{a:x}; function starts at 0x{alt:x}) " + cm
                name = re.sub(r'_[0-9a-f]{5,6}$', f'_{alt:x}', name); a = alt
            if a in cur:
                if cur[a][0] != name and a not in OVERRIDES:
                    if cur[a][1].startswith('UNCERTAIN'):   # agent re-read an uncertain name: agent wins
                        conflicts.append(('REPLACED-UNCERTAIN', hex(a), cur[a][0], name))
                    else:
                        conflicts.append(('KEPT-OLD', hex(a), cur[a][0], name, cm[:80])); skipped += 1; continue
            if a in OVERRIDES:
                if name != OVERRIDES[a] and a in cur: continue   # keep the comment of the line that supplied the override name
                name = OVERRIDES[a]
            # same name again: keep the curated comment, unless it was UNCERTAIN and the agent now confirms it
            if a in cur and cur[a][0] == name and a not in OVERRIDES and not (cur[a][1].startswith('UNCERTAIN') and cm and not cm.startswith('UNCERTAIN')):
                cm = cur[a][1] or cm
            if a in cur: updated += 1
            else: added += 1
            cur[a] = (name, cm)
for a, n in OVERRIDES.items():
    if a in cur and cur[a][0] != n: cur[a] = (n, cur[a][1]); updated += 1
# detect duplicate names
seen = {}
for a, (n, c) in cur.items():
    if n in seen: bad.append(('duplicate name', n, hex(a), hex(seen[n])))
    seen[n] = a
with open(NAMES, 'w', encoding='utf-8', newline='') as f:
    f.write('\n'.join(header) + '\n')
    w = csv.writer(f, lineterminator='\n')
    for a in sorted(cur):
        if a in deleted: continue
        w.writerow([f"0x{a:08x}", cur[a][0], cur[a][1]])
print(f"added={added} updated={updated} skipped(conflict)={skipped} total={len(cur)}")
for b in bad: print("PROBLEM:", b)
for c in conflicts: print("CONFLICT:", c)
