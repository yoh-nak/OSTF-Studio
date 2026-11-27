"""Validate the proposed public alpha against the published release inventory."""
import json
import re
import sys


def next_tag(proposed, releases):
    match = re.fullmatch(r"(v[0-9]+\.[0-9]+\.[0-9]+-public-alpha\.)[1-9][0-9]*", proposed)
    if not match:
        raise ValueError(f"Invalid public alpha tag: {proposed}")
    prefix = match[1]
    numbers = []
    for release in releases:
        if not release["draft"]:
            number = re.fullmatch(re.escape(prefix) + r"([1-9][0-9]*)", release["tag_name"])
            if number:
                numbers.append(int(number[1]))
    return prefix + str(max(numbers, default=0) + 1)


if __name__ == "__main__":
    proposed = sys.argv[1]
    expected = next_tag(proposed, json.load(sys.stdin))
    if proposed != expected:
        sys.exit(f"Expected the next published alpha to be {expected}, received {proposed}")
