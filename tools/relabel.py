#!/usr/bin/env python3
"""Knowledge distillation for training data: keep our own positions, replace each label with a stronger teacher
engine's search score. Records are 32-byte bulletformat (side to move shown as White); the stored position carries no
castling or en-passant rights, so the teacher searches it without them. Mate scores become +-2500 cp.
Usage: relabel.py IN.bin OUT.bin "TEACHER CMD"|none NODES START STRIDE MAX   (records START, START+STRIDE, ... < MAX)"""
import struct, subprocess, sys

PIECES = "PNBRQK"


def fen(occ, pcs):
    board, idx = [None] * 64, 0
    while occ:
        sq = (occ & -occ).bit_length() - 1
        occ &= occ - 1
        nib = (pcs[idx // 2] >> (4 * (idx & 1))) & 15
        idx += 1
        board[sq] = PIECES[nib & 7].lower() if nib & 8 else PIECES[nib & 7]
    rows = []
    for r in range(7, -1, -1):
        row, empty = "", 0
        for f in range(8):
            p = board[r * 8 + f]
            if p is None:
                empty += 1
            else:
                row += (str(empty) if empty else "") + p
                empty = 0
        rows.append(row + (str(empty) if empty else ""))
    return "/".join(rows) + " w - - 0 1"


def main():
    src, dst, cmd, nodes, start, stride, limit = sys.argv[1:8]
    start, stride, limit = int(start), int(stride), int(limit)
    if cmd == "none":  # baseline arm: the same records with their original labels
        data = open(src, "rb").read()
        with open(dst, "wb") as out:
            for i in range(start, min(len(data) // 32, limit), stride): out.write(data[32 * i: 32 * i + 32])
        return
    eng = subprocess.Popen(cmd.split(), stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    send = lambda s: eng.stdin.write(s + "\n")
    send("uci"); send("setoption name Hash value 16"); send("isready")
    while eng.stdout.readline().strip() != "readyok": pass
    data = open(src, "rb").read()
    n = min(len(data) // 32, limit)
    with open(dst, "wb") as out:
        for i in range(start, n, stride):
            rec = bytearray(data[32 * i: 32 * i + 32])
            occ, = struct.unpack_from("<Q", rec, 0)
            send(f"position fen {fen(occ, rec[8:24])}"); send(f"go nodes {nodes}")
            score = None
            for line in eng.stdout:
                if line.startswith("bestmove"): break
                if " score cp " in line: score = int(line.split(" score cp ")[1].split()[0])
                elif " score mate " in line: score = 2500 if int(line.split(" score mate ")[1].split()[0]) > 0 else -2500
            if score is None: continue
            struct.pack_into("<h", rec, 24, max(-32000, min(32000, score)))
            out.write(rec)
    send("quit")


if __name__ == "__main__":
    main()
