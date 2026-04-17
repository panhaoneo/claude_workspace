#!/usr/bin/env python3
"""Quick self-test using the sample log lines from the spec."""

import sys
sys.path.insert(0, '.')
from extract_user_ip import extract

SAMPLE_LINES = [
    # Step 1 – reg_mgr_TBT supplies sessionID 16200000
    "[08:28:10.862.007][3201871][INFO][XTP:0]reg_mgr_TBT: [sub][udp][SH] count[1] sessionID[16200000]\n",

    # Step 2 – OnLogin with that sessionID reveals user "alice"
    "[08:28:10.861.285][3201871][INFO][XTP:0]OnLogin: trading_day[20260416] user[alice] clientID[98] session[0x7fdc8c000b90] sessionID[16200000] PushQueueID[1].\n",

    # Step 3 – alice logs in from a client; the 3 preceding lines carry ip + success
    "[08:28:13.924.991][79360][INFO][XTP:0]mac: ip:192.168.1.11 client_id:1 user_id:123 user_name:alice\n",
    "[08:28:13.924.986][79360][INFO][XTP:0]User(alice) login success.\n",
    "[08:28:13.924.924][79360][INFO][XTP:0]OnLogin: trading_day[20260416] user[alice] clientID[97] session[0x7f46ec031dc0] sessionID[46100003] PushQueueID[4].\n",

    # Same user, second IP (different session entirely)
    "[08:29:00.000.000][99999][INFO][XTP:0]mac: ip:10.0.0.5 client_id:2 user_id:123 user_name:alice\n",
    "[08:29:00.000.001][99999][INFO][XTP:0]User(alice) login success.\n",
    "[08:29:00.000.002][99999][INFO][XTP:0]OnLogin: trading_day[20260416] user[alice] clientID[97] session[0xdeadbeef] sessionID[AABB1122] PushQueueID[5].\n",

    # A different user "bob" NOT in reg_mgr_TBT – should be ignored
    "[08:30:00.000.000][11111][INFO][XTP:0]mac: ip:172.16.0.1 client_id:3 user_id:456 user_name:bob\n",
    "[08:30:00.000.001][11111][INFO][XTP:0]User(bob) login success.\n",
    "[08:30:00.000.002][11111][INFO][XTP:0]OnLogin: trading_day[20260416] user[bob] clientID[99] session[0xcafe] sessionID[DEADC0DE] PushQueueID[6].\n",

    # alice again, duplicate ip – should not appear twice in output
    "[08:31:00.000.000][22222][INFO][XTP:0]mac: ip:192.168.1.11 client_id:1 user_id:123 user_name:alice\n",
    "[08:31:00.000.001][22222][INFO][XTP:0]User(alice) login success.\n",
    "[08:31:00.000.002][22222][INFO][XTP:0]OnLogin: trading_day[20260416] user[alice] clientID[97] session[0xfeed] sessionID[FFFF0001] PushQueueID[7].\n",
]

results = extract(SAMPLE_LINES)

print("Results:", results)

assert ("alice", "192.168.1.11") in results, "Missing alice/192.168.1.11"
assert ("alice", "10.0.0.5")    in results, "Missing alice/10.0.0.5"
assert ("bob",  "172.16.0.1")  not in results, "bob should be excluded (not in reg_mgr_TBT)"
# duplicate should collapse
assert len([r for r in results if r == ("alice", "192.168.1.11")]) == 1, "Duplicate not collapsed"
assert len(results) == 2, f"Expected 2 unique pairs, got {len(results)}: {results}"

print("\nAll assertions passed.")
