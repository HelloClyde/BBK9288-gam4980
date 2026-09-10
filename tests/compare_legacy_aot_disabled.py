"""Compare legacy-enabled and disabled host binaries with real state evidence."""
import argparse
import os
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument("rom8")
    parser.add_argument("rome")
    parser.add_argument("games", nargs="+")
    args = parser.parse_args()
    for game in args.games:
        for frames in (100, 1000, 5000):
            states = []
            for exe in (args.baseline, args.candidate):
                env = dict(os.environ, GAM4980_SMOKE_FRAMES=str(frames),
                           GAM4980_SMOKE_STORY="1")
                result = subprocess.run([exe, args.rom8, args.rome, game],
                                        env=env, capture_output=True, text=True,
                                        check=True)
                state = [line for line in result.stdout.splitlines()
                         if line.startswith(("state hash=", "state cpu="))]
                if not state:
                    raise RuntimeError("missing state evidence")
                states.append(state)
            if states[0] != states[1]:
                raise RuntimeError((game, frames, states))
            print(f"{game}: {frames} frames equivalent", flush=True)


if __name__ == "__main__":
    main()
