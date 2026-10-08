"""Generate ghidra/names/carpet_names_tables.csv from the Thing class/model
dispatch tables at 0x943da.  Table B (constructors) is indexed by type (= level
Model); Table A (per-tick update handlers) is indexed by Thing state (+0x46).
State -> type mapping per class comes from the handler analysis (docs/ENGINE.md)."""
import pathlib as _pl; _REPO = _pl.Path(__file__).resolve().parents[2].as_posix()   # the repository root
import sys, struct, csv, re, collections
sys.path.insert(0, (_REPO + "/tools"))
from mctools.lefile import LEFile
from mctools.level import CLASS_NAMES, MODEL_NAMES
le = LEFile.parse((_REPO + "/MagicCarpet/magic/carpet.exe"))
base, img = le.flat_image(True)
u32 = lambda a: struct.unpack_from('<I', img, a-base)[0]
u16 = lambda a: struct.unpack_from('<H', img, a-base)[0]
funcs = {int(r['address'],16): r for r in csv.DictReader(open((_REPO + "/ghidra/export/carpet_functions.csv")))}
CLS = {1:'class1', 2:'scenery', 3:'player', 5:'creature', 6:'class6', 7:'weather', 8:'class8', 9:'projectile', 10:'effect', 11:'switch', 12:'spell', 13:'class13'}
# Creature states are type * 6 + {0 idle / type-specific, 1 main, 2 attack, 3 follow, 4 dying, 5 dead}
# (port_creatures.md); 102..119 are disabled records, 120 the body segments.  Before round 5 this table
# started each type one state late, so every type's first state carried the previous type's name.
CREATURE_ROLES = ['s', 'main', 'attack', 'follow', 'dying', 'dead']
# class 3 states (port_castle.md, port_player.md)
PLAYER_STATES = {0: 'flyer', 1: 'ai_wizard', 2: 'dying', 3: 'dead', 4: 'castle_active', 5: 'castle_build',
                 6: 'castle_destroyed', 7: 'balloon_wait', 8: 'balloon_wait', 9: 'balloon', 10: 'unused'}
def effect_state_type(s):
    """class 10 Table A index -> type (port_constructors.md / port_effects.md / port_features.md)"""
    if s <= 0x1a: return s
    if s <= 0x1d: return 0x1b                 # wall piece, one state per direction
    if s <= 0x2e: return s - 2                # 0x1e..0x2e -> types 0x1c..0x2c
    if s in (0x30, 0x33, 0x34, 0x35): return 0x2d   # wizard castle: build / living / collapse
    if s <= 0x38: return s - 4                # 0x36..0x38 -> 0x32..0x34
    if s == 0x39: return 0x34                 # hatching crab egg
    return s - 5                              # 0x3a..0x3d -> 0x35..0x38
def slug(s): return re.sub(r'[^a-z0-9]+','_',s.lower()).strip('_')
def model_label(c, t):
    mn = MODEL_NAMES.get(c,{}).get(t); return slug(mn) if mn else f"type{t}"
def state_label(c, s):
    """label for an update handler of class c in state s"""
    if c == 5:
        if s == 120: return 'segment'
        if s >= 102: return 'disabled'
        return model_label(5, s // 6)
    if c == 12: return model_label(12, s // 3) + (f"_p{s % 3}" if s % 3 else "")
    if c == 2: return model_label(2, s // 3)
    if c == 10: return model_label(10, effect_state_type(s))
    if c == 9:                                 # type == state up to 13; state 14 = lightning segment
        if s == 14: return 'lightning_segment'
        return model_label(9, s - 1 if s > 14 else s)
    if c == 3: return PLAYER_STATES.get(s, f"type{s}")
    if c in (11, 7): return model_label(c, s)
    return f"type{s}"
def walk(t):
    m=0; rows=[]
    while u32(t+m*14)==0x943cc and m<=200:
        rows.append((m, u16(t+m*14+4), u32(t+m*14+6), u32(t+m*14+10))); m+=1
    return rows
uses = collections.defaultdict(list)
for c in range(1,14):
    r=0x943da+c*0x12; ta,tb=u32(r),u32(r+4)
    for m,idx,h,en in walk(ta):
        if h: uses[h].append(('update',c,m,en))
    for m,idx,h,en in walk(tb):
        if h: uses[h].append(('create',c,m,en))
out=[]
for h,us in sorted(uses.items()):
    if h not in funcs: print("missing fn", hex(h), us); continue
    kinds={u[0] for u in us}; classes={u[1] for u in us}
    kind=us[0][0]; c=us[0][1]; cn=CLS[c]; cname=CLASS_NAMES.get(c,'?')
    if len(kinds)==1 and len(classes)==1:
        if len(us)==1:
            m=us[0][2]
            if kind=='create':
                name=f"{cn}_create_{model_label(c,m)}_{h:x}"
                cm=f"Thing constructor (Table B): class {c} {cname}, type/model {m}. Table B for a class is *(int*)(0x943de+class*0x12), 14-byte records."
            else:
                name=f"{cn}_{state_label(c,m)}_s{m}_update_{h:x}"
                cm=f"Thing update handler (Table A): class {c} {cname}, state (+0x46) {m}. Table A is *(int*)(0x943da+class*0x12)."
            if not us[0][3]: name += "_disabled"; cm += " enabled=0 in table."
        else:
            ms=sorted(u[2] for u in us)
            if kind=='update' and c==12 and len(ms)>=20:
                name=f"spell_phase{ms[0]%3}_common_{h:x}"; cm=f"Shared spell update handler for phase {ms[0]%3} of every spell (states {ms[0]}..{ms[-1]})."
            else:
                name=f"{cn}_{kind}_shared_{h:x}"; cm=f"Shared {kind} handler for class {c} ({cname}) {'states' if kind=='update' else 'types'} {ms}."
    else:
        name=f"thing_{'_'.join(sorted(kinds))}_shared_{h:x}"; cm=f"Shared handler used by {sorted(set((u[0],u[1],u[2]) for u in us))}."
    out.append((h,name,cm))
w=open((_REPO + "/ghidra/names/carpet_names_tables.csv"),"w",newline='',encoding='utf-8')
w.write("address,name,comment\n# Generated by gennames.py from the class/model handler tables at 0x943da (see docs/ENGINE.md). Curated names in carpet_names.csv are applied after this file and win on conflict.\n")
cw=csv.writer(w, lineterminator='\n')
for h,n,c in out: cw.writerow([f"0x{h:08x}", n, c])
w.close(); print("table names:", len(out))
