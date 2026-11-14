#!/usr/bin/env python3
"""Verify Android touch hit testing on the connected device using the probe APK."""
import argparse
import json
from pathlib import Path
import subprocess
import time

PACKAGE = "io.github.djoshi.bitedj.probe"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", help="ADB serial; omit if exactly one device is connected")
    parser.add_argument("--package", default=PACKAGE, help="Probe package ID")
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "build/probe/touch-report.json")
    args = parser.parse_args()
    package = args.package
    adb = ["adb"] + (["-s", args.serial] if args.serial else [])

    def command(*words, allow_failure=False):
        result = subprocess.run(adb + list(words), capture_output=True, text=True, timeout=30)
        if result.returncode and not allow_failure:
            raise RuntimeError(result.stderr or result.stdout)
        return result.stdout

    def state():
        try:
            return json.loads(command("shell", "run-as", package, "cat",
                                      "files/input-coordinate-test.json", allow_failure=True))
        except json.JSONDecodeError:
            return {}

    def wait(predicate):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            report = state()
            if predicate(report):
                return report
            time.sleep(0.1)
        raise RuntimeError("Timed out waiting for probe state: " + json.dumps(state()))

    command("shell", "am", "force-stop", package)
    command("shell", "run-as", package, "rm", "-f", "files/input-coordinate-test.json")
    command("shell", "am", "start", "-n", package + "/io.github.djoshi.bitedj.BiteDJActivity",
            "--ez", "inputCoordinateTest", "true")
    wait(lambda report: "main0" in report.get("targets", {}))
    results = []

    def tap(key):
        before = wait(lambda report: key in report.get("targets", {}))
        target = before["targets"][key]
        previous = before.get("clicks", {}).get(key, 0)
        command("shell", "input", "tap", str(target["x"]), str(target["y"]))
        after = wait(lambda report: report.get("clicks", {}).get(key, 0) == previous + 1)
        press = after["lastPress"]
        expected_receiver = "positionedMenu" if key.startswith("menu") else key
        error = max(abs(press["globalX"] - target["qtX"]),
                    abs(press["globalY"] - target["qtY"]))
        if press["target"] != expected_receiver or error > 1.1:
            raise RuntimeError("Touch coordinate mismatch: " + json.dumps({
                "key": key, "expected": target, "received": press, "error": error}))
        result = {"target": key, "physical": [target["x"], target["y"]],
                  "expectedQt": [target["qtX"], target["qtY"]], "received": press,
                  "maxErrorLogicalPixels": error, "passed": True}
        results.append(result)
        print(key, "PASS", "error=" + str(round(error, 3)), flush=True)
        return after

    for i in range(4):
        tap("main" + str(i))
    tap("openDialog")
    for i in range(4):
        tap("dialog" + str(i))
    tap("closeDialog")
    for i in range(3):
        tap("openMenu")
        tap("menu" + str(i))

    before = state()
    context = before["targets"]["contextTarget"]
    old_menu = before.get("clicks", {}).get("contextMenu", 0)
    old_tap = before.get("clicks", {}).get("contextTap", 0)
    command("shell", "input", "swipe", str(context["x"]), str(context["y"]),
            str(context["x"]), str(context["y"]), "850")
    after = wait(lambda report: report.get("clicks", {}).get("contextMenu", 0) == old_menu + 1)
    if after.get("clicks", {}).get("contextTap", 0) != old_tap or \
            after.get("lastPress", {}).get("button") != 2:
        raise RuntimeError("Long press also generated a left click: " + json.dumps(after))
    results.append({"target": "contextTarget", "gesture": "longPress",
                    "rightButton": True, "noLeftClick": True, "passed": True})
    print("contextTarget PASS right click without left click", flush=True)

    command("shell", "input", "tap", str(context["x"]), str(context["y"]))
    wait(lambda report: report.get("clicks", {}).get("contextTap", 0) == old_tap + 1)
    before = state()
    inner = before["targets"]["innerScrollArea"]
    old_inner = before.get("innerScrollValue", 0)
    old_outer = before.get("scrollValue", 0)
    command("shell", "input", "swipe", str(inner["x"]), str(inner["y"] + 25),
            str(inner["x"]), str(inner["y"] - 25), "450")
    after = wait(lambda report: report.get("innerScrollValue", 0) > old_inner)
    if after.get("scrollValue", 0) != old_outer:
        raise RuntimeError("Nested scroll also moved parent: " + json.dumps(after))
    results.append({"target": "innerScrollArea", "gesture": "drag",
                    "before": old_inner, "after": after["innerScrollValue"],
                    "parentUnchanged": True, "passed": True})
    print("innerScrollArea PASS", old_inner, "->", after["innerScrollValue"], flush=True)

    before = state()
    scroll = before["targets"]["scrollArea"]
    old_value = before.get("scrollValue", 0)
    command("shell", "input", "swipe", str(scroll["x"]), str(scroll["y"] + 70),
            str(scroll["x"]), str(scroll["y"] - 70), "450")
    after = wait(lambda report: report.get("scrollValue", 0) > old_value)
    results.append({"target": "scrollArea", "gesture": "drag",
                    "before": old_value, "after": after["scrollValue"], "passed": True})
    print("scrollArea PASS", old_value, "->", after["scrollValue"], flush=True)

    before = state()
    edge = before["targets"]["scrollAreaEdge"]
    old_value = before.get("scrollValue", 0)
    command("shell", "input", "swipe", str(edge["x"]), str(edge["y"] + 70),
            str(edge["x"]), str(edge["y"] - 70), "450")
    after = wait(lambda report: report.get("scrollValue", 0) > old_value)
    results.append({"target": "scrollAreaEdge", "gesture": "drag",
                    "before": old_value, "after": after["scrollValue"], "passed": True})
    print("scrollAreaEdge PASS", old_value, "->", after["scrollValue"], flush=True)

    final = state()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"qt": final["qt"], "contentOrigin": final["contentOrigin"],
        "passed": True, "cases": results}, indent=2) + "\n")
    print("All", len(results), "touch cases passed;", args.output, flush=True)


if __name__ == "__main__":
    main()
