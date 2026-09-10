"""Compare the live word templates against ordinary execution on real GAMs."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("rom8", type=Path)
    parser.add_argument("rome", type=Path)
    parser.add_argument("games", type=Path, nargs="+")
    args = parser.parse_args()
    for game in args.games:
        for frames in (100, 1000, 5000):
            states = []
            for mask in (8191, 131071):
                env = dict(os.environ, GAM4980_GAME_AOT_SEMANTIC_MASK=str(mask),
                           GAM4980_SMOKE_FRAMES=str(frames), GAM4980_SMOKE_STORY="1")
                result = subprocess.run(
                    [str(args.executable.resolve()), str(args.rom8), str(args.rome), str(game)],
                    env=env, text=True, capture_output=True, check=True)
                state = [line for line in result.stdout.splitlines()
                         if line.startswith(("state hash=", "state cpu="))]
                if not state:
                    raise RuntimeError("missing state evidence")
                states.append(state)
            if states[0] != states[1]:
                raise RuntimeError((str(game), frames, states))
            print(f"{game.name}: {frames} frames equivalent", flush=True)
            if frames == 5000:
                for line in result.stdout.splitlines():
                    if any(f"pattern={kind} " in line for kind in range(20, 24)):
                        print(line, flush=True)


if __name__ == "__main__":
    main()
