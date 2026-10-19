"""Summarize stage JSONL without treating world-only timing as game FPS."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import statistics

PARTITION = ("buffers", "lighting", "horizon_prepare", "shadow_validate", "scene_record", "gpu_begin", "gpu_shadow", "gpu_end", "gpu_publish", "atmosphere", "sampling_resolve")


def stats(values):
    ordered = sorted(values)
    return {"median": statistics.median(ordered), "min": min(ordered), "max": max(ordered), "p95Exploratory": ordered[min(len(ordered)-1, int(len(ordered)*0.95))]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    args = parser.parse_args()
    rows = [json.loads(line) for line in args.input.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    cases = {}
    for name in dict.fromkeys(row["case"] for row in rows):
        samples = [row for row in rows if row["case"] == name and not row["coldOrWarmup"]]
        first = samples[0]
        stage_names = first["stagesMs"].keys()
        cases[name] = {
            "samples": len(samples), "allHardwareGpu": all(row["usedGpu"] and not row["warp"] and row["cpuTriangles"] == 0 for row in samples),
            "viewport": [first["width"], first["height"]], "sampledViewport": [first["sampleWidth"], first["sampleHeight"]],
            "sampleFactor": first["sampleFactor"], "limited": first["sampleLimited"], "adapter": first["adapter"],
            "triangles": first["triangles"], "cpuTriangles": first["cpuTriangles"], "drawCalls": first["drawCalls"],
            "fullWorldMs": stats([row["fullWorldMs"] for row in samples]),
            "stagesMs": {stage: stats([row["stagesMs"][stage] for row in samples]) for stage in stage_names},
            "unscopedWorldMs": stats([row["fullWorldMs"] - sum(row["stagesMs"][stage] for stage in PARTITION) for row in samples]),
            "failure": first["failure"], "frameHash": first["frameHash"],
        }
        valid = [row for row in samples if row.get("gpuTimestampValid")]
        cases[name]["gpuTimestampValidSamples"] = len(valid)
        if valid:
            cases[name]["gpuDrawTimestampMs"] = stats([row["gpuDrawTimestampMs"] for row in valid])
            cases[name]["gpuCopyTimestampMs"] = stats([row["gpuCopyTimestampMs"] for row in valid])
    output = args.input.with_name("summary.json")
    output.write_text(json.dumps({"scope": "Offscreen world only, excludes game loop/UI/SDL and does not establish sustained FPS; stage intervals nest; GPU timestamps can include queue bubbles during CPU batch submission", "cases": cases, "coldEvents": [row for row in rows if row["coldOrWarmup"]]}, indent=2), encoding="utf-8")
    for name, case in cases.items():
        top = sorted(((stage, case["stagesMs"][stage]["median"]) for stage in PARTITION), key=lambda x: x[1], reverse=True)[:4]
        print(name, "GPU" if case["allHardwareGpu"] else "CPU FALLBACK", round(case["fullWorldMs"]["median"], 3), "ms", "; ".join(f"{key}={value:.3f}" for key, value in top))
    print(output)


if __name__ == "__main__":
    main()
