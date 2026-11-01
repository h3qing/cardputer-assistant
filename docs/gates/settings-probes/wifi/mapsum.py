import re, sys, collections
def load(path):
    txt = open(path, errors="replace").read()
    out = collections.Counter()
    cur = None
    # Track output section, then input sections with sizes and archive names
    for line in txt.splitlines():
        m = re.match(r"^(\.\S+)\s", line) or re.match(r"^(\.\S+)$", line)
        if m and not line.startswith(" "):
            cur = m.group(1)
        if cur not in (".dram0.data", ".dram0.bss", ".iram0.text"):
            continue
        m = re.match(r"^\s+(?:\S+)?\s*0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S+)$", line)
        if m:
            size = int(m.group(1), 16)
            src = m.group(2)
            a = re.search(r"lib([A-Za-z0-9_]+)\.a", src)
            key = a.group(1) if a else src.split("/")[-1]
            out[(cur, key)] += size
    return out
b, w = load(sys.argv[1]), load(sys.argv[2])
diff = collections.Counter()
for k in set(b) | set(w):
    d = w[k] - b[k]
    if d: diff[k] = d
for (sec, key), d in sorted(diff.items(), key=lambda x: -x[1])[:25]:
    print(f"{sec:12s} {key:28s} {d:+7d}")
