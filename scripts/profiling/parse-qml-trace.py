#!/usr/bin/env python3
"""Summarize a Qt QML profiler 1.02 XML trace."""

from __future__ import annotations

import argparse
import json
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path


def parse_trace(path: Path) -> dict:
    root = ET.parse(path).getroot()
    event_data = root.find("eventData")
    model = root.find("profilerDataModel")
    if event_data is None or model is None:
        raise ValueError("trace has no eventData or profilerDataModel")

    events = []
    for element in event_data.findall("event"):
        event = dict(element.attrib)
        event.update({child.tag: child.text or "" for child in element})
        event["index"] = int(event["index"])
        events.append(event)

    ranges_by_event = defaultdict(list)
    for element in model.findall("range"):
        if "eventIndex" in element.attrib:
            ranges_by_event[int(element.attrib["eventIndex"])].append(dict(element.attrib))

    event_summary = defaultdict(lambda: {"count": 0, "total_ms": 0.0})
    hotspots = []
    for event in events:
        kind = event.get("type", "Unknown")
        ranges = ranges_by_event[event["index"]]
        duration_ms = sum(float(item.get("duration", 0)) for item in ranges) / 1_000_000
        event_summary[kind]["count"] += 1
        event_summary[kind]["total_ms"] += duration_ms
        if duration_ms > 0:
            hotspots.append({
                "total_ms": duration_ms,
                "avg_ms": duration_ms / max(len(ranges), 1),
                "count": len(ranges),
                "type": kind,
                "displayname": event.get("displayname", ""),
                "filename": event.get("filename", ""),
                "line": int(event.get("line", 0)),
                "column": int(event.get("column", 0)),
                "details": event.get("details", ""),
            })

    for item in event_summary.values():
        item["total_ms"] = round(item["total_ms"], 3)
        item["avg_ms"] = round(item["total_ms"] / max(item["count"], 1), 3)

    animation_segments = []
    for item in model.findall("range"):
        attributes = item.attrib
        if "framerate" in attributes:
            animation_segments.append({
                "start_ns": int(attributes["startTime"]),
                "framerate": float(attributes["framerate"]),
                "animation_count": int(attributes.get("animationcount", 0)),
                "thread": int(attributes.get("thread", 0)),
            })

    memory_events = Counter(
        event.get("memoryEventType", "unknown")
        for event in events
        if event.get("type") == "MemoryAllocation"
    )
    pixmap_events = Counter(
        event.get("cacheEventType", "unknown")
        for event in events
        if event.get("type") == "PixmapCache"
    )

    wall_ms = (int(root.attrib["traceEnd"]) - int(root.attrib["traceStart"])) / 1_000_000
    return {
        "trace": {
            "path": str(path),
            "version": root.attrib.get("version", ""),
            "trace_start": int(root.attrib["traceStart"]),
            "trace_end": int(root.attrib["traceEnd"]),
            "wall_ms": round(wall_ms, 3),
            "event_data_total_ms": round(float(event_data.attrib.get("totalTime", 0)) / 1_000_000, 3),
            "event_count": len(events),
        },
        "event_summary": dict(sorted(event_summary.items(), key=lambda item: item[1]["total_ms"], reverse=True)),
        "hotspots": sorted(hotspots, key=lambda item: item["total_ms"], reverse=True),
        "animation_segments": animation_segments,
        "memory_event_counts": dict(memory_events),
        "pixmap_event_counts": dict(pixmap_events),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = parse_trace(args.trace)
    text = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
        print(f"parse-qml-trace: wrote {args.output}")
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
