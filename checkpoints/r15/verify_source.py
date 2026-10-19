from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parent
m=json.loads((root/"source-manifest.json").read_text())
assert len(m["files"])==841
withheld={x["relative"] for x in m["withheldEmbeddedData"]}
for x in m["files"]:
    if x["relative"] in withheld: continue
    p=root/x["relative"];d=p.read_bytes()
    assert len(d)==x["bytes"] and hashlib.sha256(d).hexdigest()==x["sha256"],x["relative"]
actual={"Source/"+p.relative_to(root/"Source").as_posix() for p in (root/"Source").rglob("*") if p.is_file()}
assert actual-withheld=={x["relative"] for x in m["files"]}-withheld
h=hashlib.sha256("\n".join(x["relative"]+":"+x["sha256"] for x in m["files"]).encode()).hexdigest()
assert h==m["sourceClosureSha256"]
print("PASS: 839 published R15 Source files byte-exact; full 841-file manifest closure "+h+"; 2 data headers withheld")
