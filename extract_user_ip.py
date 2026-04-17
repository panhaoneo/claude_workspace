#!/usr/bin/env python3
"""
Extract unique user+IP pairs from XTP trading log files.

Steps:
  1. Collect sessionIDs from "reg_mgr_TBT" lines (deduplicated)
  2. Map sessionID -> username via matching OnLogin lines
  3. For each username, find all OnLogin lines and inspect the 3
     preceding lines for "mac: ip:<IP>" and "login success"
  4. Output unique (user, ip) pairs
"""

import re
import sys
import os
import glob
from datetime import datetime, timedelta

SESSION_PAT      = re.compile(r'reg_mgr_TBT:.*?sessionID\[([A-Za-z0-9]+)\]')
ONLOGIN_PAT      = re.compile(r'OnLogin:.*?user\[([^\]]+)\].*?sessionID\[([A-Za-z0-9]+)\]')
IP_PAT           = re.compile(r'\bmac:\s+ip:(\S+)')
LOGIN_SUCCESS_PAT = re.compile(r'login success', re.IGNORECASE)


def is_recent(path, days=30):
    try:
        mtime = datetime.fromtimestamp(os.path.getmtime(path))
        return mtime >= datetime.now() - timedelta(days=days)
    except OSError:
        return False


def collect_lines(paths):
    """Read all lines from the given files, preserving order."""
    lines = []
    for path in paths:
        try:
            with open(path, 'r', errors='replace') as fh:
                lines.extend(fh.readlines())
        except OSError as exc:
            print(f"[WARN] Cannot read {path}: {exc}", file=sys.stderr)
    return lines


def extract(lines):
    # --- Step 1: sessionIDs from reg_mgr_TBT ---
    session_ids = set()
    for line in lines:
        m = SESSION_PAT.search(line)
        if m:
            session_ids.add(m.group(1))

    print(f"[INFO] reg_mgr_TBT sessionIDs found: {len(session_ids)}", file=sys.stderr)
    if not session_ids:
        return set()

    # --- Step 2: username per sessionID via OnLogin ---
    usernames = set()
    for line in lines:
        m = ONLOGIN_PAT.search(line)
        if m and m.group(2) in session_ids:
            usernames.add(m.group(1))

    print(f"[INFO] Usernames resolved: {len(usernames)}  {sorted(usernames)}", file=sys.stderr)
    if not usernames:
        return set()

    # --- Step 3: find all OnLogin lines for these users,
    #             check 3 preceding lines for IP + login success ---
    user_ips = set()
    for idx, line in enumerate(lines):
        m = ONLOGIN_PAT.search(line)
        if not m or m.group(1) not in usernames:
            continue

        user = m.group(1)
        preceding = lines[max(0, idx - 3): idx]

        found_ip = None
        found_success = False
        for prev in preceding:
            ip_m = IP_PAT.search(prev)
            if ip_m:
                found_ip = ip_m.group(1)
            if LOGIN_SUCCESS_PAT.search(prev):
                found_success = True

        if found_ip and found_success:
            user_ips.add((user, found_ip))

    return user_ips


def resolve_paths(args):
    """Expand globs, recurse into directories, filter to recent files."""
    paths = []
    for arg in args:
        expanded = glob.glob(arg, recursive=True)
        targets = expanded if expanded else [arg]
        for t in targets:
            if os.path.isdir(t):
                for root, _, files in os.walk(t):
                    for fname in files:
                        paths.append(os.path.join(root, fname))
            else:
                paths.append(t)

    recent, skipped = [], []
    for p in paths:
        if is_recent(p):
            recent.append(p)
        else:
            skipped.append(p)

    if skipped:
        print(f"[INFO] Skipped {len(skipped)} file(s) older than 30 days", file=sys.stderr)
    return recent


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 extract_user_ip.py <logfile|dir|glob> [...]")
        print("       Processes files modified within the last 30 days.")
        sys.exit(1)

    paths = resolve_paths(sys.argv[1:])
    if not paths:
        print("[ERROR] No log files found within the last 30 days.", file=sys.stderr)
        sys.exit(1)

    print(f"[INFO] Processing {len(paths)} file(s)...", file=sys.stderr)
    lines = collect_lines(paths)
    print(f"[INFO] Total lines loaded: {len(lines)}", file=sys.stderr)

    results = extract(lines)

    print("\nuser\tip")
    print("-" * 40)
    for user, ip in sorted(results):
        print(f"{user}\t{ip}")
    print(f"\nTotal unique user+IP pairs: {len(results)}")


if __name__ == "__main__":
    main()
