#!/usr/bin/env python3
"""Sign KavinEngine up for arenas that allow bots, in the Lichess teams it belongs to. lichess-bot then plays the games
(a tournament pairing arrives as a normal gameStart). Run hourly on the bot server (systemd timer).

  tournament_join.py                 join suitable arenas (running or starting within 24 h)
  tournament_join.py --dry-run       list what would be joined
  tournament_join.py --survey T...   for each team id: size, open/closed, recent and upcoming bot arenas
  tournament_join.py --join-team T   join a team (needs team:write)

The token lives in ~/.lichess-token-tournament (scopes tournament:write, team:read, team:write), separate from the bot's
bot:play token. Suitable = standard chess, rated, bots allowed, and an estimated game length (base + 40 x increment) of
3 to 30 minutes: no bullet (home Wi-Fi drops and the 30 s clock reserve) and nothing that ties the bot up for hours.
"""
import json, os, sys, time, urllib.error, urllib.parse, urllib.request

API = "https://lichess.org"
TOKEN = open(os.path.expanduser("~/.lichess-token-tournament")).read().strip()
STATE = os.path.expanduser("~/.cache/kavinengine-bot/joined-arenas.json")
MIN_SECONDS, MAX_SECONDS, HORIZON_MS = 180, 1800, 24 * 3600 * 1000


def call(path, data=None, ndjson=False):
    req = urllib.request.Request(API + path, data=urllib.parse.urlencode(data).encode() if data is not None else None,
                                 headers={"Authorization": "Bearer " + TOKEN,
                                          "Accept": "application/x-ndjson" if ndjson else "application/json"})
    for attempt in range(4):  # the home connection drops now and then
        try:
            body = urllib.request.urlopen(req, timeout=30).read().decode()
            break
        except urllib.error.HTTPError as e:
            return {"error": f"HTTP {e.code}: {e.read().decode()[:200]}"}
        except (urllib.error.URLError, OSError) as e:
            if attempt == 3:
                return {"error": f"network: {e}"} if not ndjson else []
            time.sleep(2 ** attempt)
    if ndjson:
        return [json.loads(line) for line in body.splitlines() if line.strip()]
    return json.loads(body) if body.strip() else {}


def candidate(a, now):  # from the team list, which has no botsAllowed flag
    seconds = a["clock"]["limit"] + 40 * a["clock"]["increment"]
    return (a.get("rated", True) and a["variant"]["key"] == "standard" and MIN_SECONDS <= seconds <= MAX_SECONDS
            and a["finishesAt"] > now + 5 * 60_000 and a["startsAt"] < now + HORIZON_MS)


def bots_allowed(arena_id):  # only the tournament's own page says whether bots may join
    return bool(call(f"/api/tournament/{arena_id}").get("botsAllowed"))


def survey(teams):
    now = time.time() * 1000
    for t in teams:
        info, arenas = call(f"/api/team/{t}"), call(f"/api/team/{t}/arena?max=60", ndjson=True)
        arenas = [a for a in arenas if isinstance(a, dict)] if isinstance(arenas, list) else []
        month = [a for a in arenas if a["startsAt"] > now - 30 * 86_400_000]
        up = [a for a in arenas if a["finishesAt"] > now]
        bots = [a for a in up if bots_allowed(a["id"])]
        ok = [a for a in bots if candidate(a, now)]
        tcs = sorted({f"{a['clock']['limit'] // 60}+{a['clock']['increment']}" for a in bots})
        print(f"{t:28s} members {info.get('nbMembers', '?'):>6} open {str(info.get('open', '?')):5s} | arenas: "
              f"{len(month):2d} in 30 days | upcoming/running {len(up):2d}, bots allowed {len(bots):2d}, "
              f"suitable {len(ok):2d} | {tcs}")
        time.sleep(0.5)


def main():
    args = sys.argv[1:]
    if args[:1] == ["--survey"]:
        return survey(args[1:])
    if args[:1] == ["--join-team"]:
        for t in args[1:]:
            print(t, call(f"/team/{t}/join", {}))  # Lichess serves this one without /api
            time.sleep(1)
        return
    dry = "--dry-run" in args
    account = call("/api/account")
    if "id" not in account:  # network outage or API error (a KeyError here killed the 2026-10-09 19:34 and 19:52 runs)
        sys.exit(f"{time.strftime('%F %T')} account lookup failed: {account.get('error')}")
    me = account["id"]
    team_list = call(f"/api/team/of/{me}")
    if not isinstance(team_list, list):
        sys.exit(f"{time.strftime('%F %T')} team lookup failed: {team_list.get('error')}")
    teams = [t["id"] for t in team_list]
    joined = set(json.load(open(STATE))) if os.path.exists(STATE) else set()
    now = time.time() * 1000
    seen = set()
    for t in teams:
        arenas = call(f"/api/team/{t}/arena?max=40", ndjson=True)
        for a in arenas if isinstance(arenas, list) else []:
            if a["id"] in seen or a["id"] in joined or not candidate(a, now) or not bots_allowed(a["id"]):
                continue
            seen.add(a["id"])
            battle = a.get("teamBattle")
            team = None
            if battle:  # a team battle needs one of our teams in it
                team = next((x for x in teams if x in battle.get("teams", [])), None)
                if team is None:
                    continue
            label = (f"{a['id']} {time.strftime('%m-%d %H:%M', time.localtime(a['startsAt'] / 1000))} "
                     f"{a['clock']['limit'] // 60}+{a['clock']['increment']} {a['fullName']}")
            if dry:
                print("would join", label, f"(team {team})" if team else "")
                continue
            r = call(f"/api/tournament/{a['id']}/join", {"team": team} if team else {})
            print("joined" if r.get("ok") else f"not joined ({r.get('error')})", label)
            if r.get("ok"):
                joined.add(a["id"])
            time.sleep(1)
        time.sleep(0.5)
    if not dry:
        os.makedirs(os.path.dirname(STATE), exist_ok=True)
        json.dump(sorted(joined)[-500:], open(STATE, "w"))
    print(time.strftime("%F %T"), f"{me}: teams {teams}")


if __name__ == "__main__":
    main()
