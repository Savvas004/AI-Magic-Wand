#!/usr/bin/env python3
"""Conservatively stage V4 actions, preserving a verified SAFE baseline.
No flashing, receiver changes, or changes to RF model/header.
Run from the AI_Magic_Wand_Final project root.
"""
import argparse
from datetime import datetime
from pathlib import Path
import re
import shutil
import sys

p = argparse.ArgumentParser()
p.add_argument('--stage', choices=('safe','cursor','scroll','tabs','click'), required=True)
p.add_argument('--confirm-clicks', action='store_true', help='Required to permit LEFT_CLICK')
args = p.parse_args()
if args.stage == 'click' and not args.confirm_clicks:
    p.error('click stage requires --confirm-clicks')
if args.confirm_clicks and args.stage != 'click':
    p.error('--confirm-clicks applies only to click stage')

root = Path.cwd()
active = root / 'src/main.cpp'
header = root / 'include/rf_model_5gestures.h'
verified = root / 'firmware_baselines/export_verification.json'
base_dir = root / 'backups/v4_staged_live'
baseline = root / 'firmware_baselines/main_v4_4_safe.cpp'
if not all(x.is_file() for x in (active,header,verified)):
    sys.exit('STOP: missing active main.cpp, V4 RF header, or V4 export verification')
import json
report = json.loads(verified.read_text())
if report.get('class_mismatches') != 0 or report.get('udp_enabled') is not False or report.get('trees') != 300:
    sys.exit('STOP: unexpected V4 model verification')
if 'static const uint16_t RF_NUM_TREES = 300;' not in header.read_text():
    sys.exit('STOP: RF header is not the expected 300-tree model')

safe_needle = 'return false; // V4 SAFE TEST: NEVER SEND UDP (including MOVE)'
flag_needle = 'constexpr bool ENABLE_GESTURE_ACTIONS=false; // V4 SAFE TEST: gestures disabled'
func_needle = 'bool sendCommand(const char* cmd, int dx=0,int dy=0){'
start_needle = 'V4 SAFE DRY-RUN (NO UDP); BOOT toggles CURSOR/GESTURE'

if not baseline.exists():
    current = active.read_text()
    if not all(current.count(x) == 1 for x in (safe_needle,flag_needle,func_needle,start_needle)):
        sys.exit('STOP: first run requires currently installed V4 Safe Test main.cpp')
    base_dir.mkdir(parents=True,exist_ok=True)
    shutil.copy2(active,baseline)
    print('Stored immutable SAFE baseline at:', baseline)

base = baseline.read_text()
if not all(base.count(x)==1 for x in (safe_needle,flag_needle,func_needle,start_needle)):
    sys.exit('STOP: saved safe baseline is invalid')

# Always regenerate from the safe baseline, never stack patches.
if args.stage == 'safe':
    replacement = base
else:
    allowed = {'cursor':['MOVE'],
               'scroll':['MOVE','SCROLL_UP','SCROLL_DOWN'],
               'tabs':['MOVE','SCROLL_UP','SCROLL_DOWN','PREV_TAB'],
               'click':['MOVE','SCROLL_UP','SCROLL_DOWN','PREV_TAB','LEFT_CLICK']}[args.stage]
    all_commands=('MOVE','SCROLL_UP','SCROLL_DOWN','PREV_TAB','LEFT_CLICK')
    deny = [c for c in all_commands if c not in allowed]
    # Fail closed for any protocol command not explicitly enumerated.
    checks = ['  // V4 STAGED LIVE: explicit command allowlist.']
    checks.append('  const bool known = '+ ' || '.join(f'strcmp(cmd,"{c}")==0' for c in all_commands) + ';')
    checks.append('  if (!known) return false;')
    for c in deny:
        checks.append(f'  if (strcmp(cmd,"{c}")==0) return false;')
    replacement = base.replace(safe_needle, '\n'.join(checks),1)
    replacement = replacement.replace(flag_needle,
        'constexpr bool ENABLE_GESTURE_ACTIONS=true; // V4 STAGED LIVE: sendCommand applies allowlist',1)
    replacement = replacement.replace(start_needle,
        f'V4 LIVE STAGE {args.stage.upper()}; BOOT toggles CURSOR/GESTURE',1)

# Before mutating active source, make backup and avoid source expected mutations.
current = active.read_text()
if current == replacement:
    print('Already at stage:',args.stage,'; no changes')
    sys.exit(0)
base_dir.mkdir(parents=True,exist_ok=True)
backup = base_dir / ('main_before_stage_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.cpp')
shutil.copy2(active,backup)
active.write_text(replacement)
print('SUCCESS: staged V4 mode:',args.stage)
print('Command allowlist:', 'none' if args.stage=='safe' else ', '.join(allowed))
print('Previous main backup:', backup)
print('RF model/header unchanged. No build or upload performed.')
print('To return to no-UDP firmware: python tools/set_v4_stage.py --stage safe')
