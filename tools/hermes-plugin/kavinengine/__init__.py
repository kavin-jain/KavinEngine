"""Hermes plugin: KavinEngine's chess-bot commands, listed in Telegram's / menu (Hermes quick_commands are not).
Each command runs tools/bot_ops.py directly, no LLM. On lynxS: ~/.hermes/plugins/kavinengine is a symlink to this
folder, enabled with `hermes plugins enable kavinengine`; restart the gateway after changes here."""
import asyncio, os, pathlib

OPS = pathlib.Path(__file__).resolve().parents[2] / "bot_ops.py"
PY = pathlib.Path.home() / "Projects/lichess-bot/venv/bin/python"  # has python-chess (report, analyze)
ENV = {k: v for k, v in os.environ.items() if k in ("HOME", "USER", "LANG", "PATH", "XDG_RUNTIME_DIR")}  # no API keys
COMMANDS = {  # Telegram shows the hyphen as an underscore: /chessbot_on
    "chessbot-on": ("on", "Turn the chess bot ON"),
    "chessbot-off": ("off", "Turn the chess bot OFF (plays out its game first)"),
    "chess": ("status", "Chess bot status: on/off, game, ratings"),
    "chessreport": ("report", "Chess bot: last 24 hours"),
    "lastgame": ("last", "Chess bot: last finished game"),
    "analyze": ("analyze", "Stockfish review of the last game (or /analyze ID)"),
}


async def run(cmd, raw):
    args = [cmd] + (raw.split()[:1] if cmd == "analyze" else [])
    proc = await asyncio.create_subprocess_exec(str(PY), str(OPS), *args, env=ENV, stdout=asyncio.subprocess.PIPE,
                                                stderr=asyncio.subprocess.STDOUT)
    try:
        out, _ = await asyncio.wait_for(proc.communicate(), timeout=45)
    except asyncio.TimeoutError:
        proc.kill()
        return f"⚠️ That took over 45 s and was stopped. Try /chess for the bot's state."
    return out.decode().strip() or "(no output)"


def register(ctx):
    for name, (cmd, desc) in COMMANDS.items():
        ctx.register_command(name, lambda raw, cmd=cmd: run(cmd, raw), description=desc)
